// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include "detail.hpp"
#include <curl/curl.h>
#include <gcal/http.hpp>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace gcal {
namespace {

struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Unable to initialize libcurl");
    }
    ~CurlGlobal() {
        curl_global_cleanup();
    }
};

using Curl = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using Headers = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

std::size_t append_body(char *data, std::size_t size, std::size_t count, void *context) noexcept {
    constexpr std::size_t maximum = 16 * 1024 * 1024;
    auto &body = *static_cast<std::string *>(context);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size)
        return 0;
    const auto length = size * count;
    if (length > maximum - body.size())
        return 0;
    try {
        body.append(data, length);
        return length;
    } catch (...) {
        // Exceptions must never cross libcurl's C callback boundary.
        return 0;
    }
}

class CurlTransport final : public HttpTransport {
  public:
    explicit CurlTransport(std::chrono::seconds timeout, std::stop_token stop)
        : handle_(nullptr, curl_easy_cleanup), timeout_(timeout), stop_(stop) {
        static const CurlGlobal global;
        handle_.reset(curl_easy_init());
        if (!handle_)
            throw std::runtime_error("Unable to create libcurl handle");
        if (timeout.count() <= 0 || timeout.count() > std::numeric_limits<long>::max())
            throw std::invalid_argument("HTTP timeout must be positive and fit in a long");
    }

    Result<HttpResponse> send(const HttpRequest &request) override {
        std::lock_guard lock(mutex_);
        if (stop_.stop_requested())
            return Error{ErrorCode::Cancelled, "Request cancelled"};
        curl_easy_reset(handle_.get());
        Headers headers(nullptr, curl_slist_free_all);
        for (const auto &[name, value] : request.headers) {
            if (name.find_first_of("\r\n:") != std::string::npos ||
                value.find_first_of("\r\n") != std::string::npos)
                return Error{ErrorCode::InvalidArgument, "Invalid HTTP header"};
            auto *next = curl_slist_append(headers.get(), (name + ": " + value).c_str());
            if (!next)
                return Error{ErrorCode::Network, "Unable to allocate HTTP headers"};
            headers.release();
            headers.reset(next);
        }

        HttpResponse response;
        auto *curl = handle_.get();
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_.count()));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(
            curl, CURLOPT_XFERINFOFUNCTION,
            +[](void *context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                return static_cast<std::stop_token *>(context)->stop_requested() ? 1 : 0;
            });
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop_);
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "google-calendar-kit/0.2.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                             static_cast<curl_off_t>(request.body.size()));
        }

        const auto status = curl_easy_perform(curl);
        if (status == CURLE_ABORTED_BY_CALLBACK && stop_.stop_requested())
            return Error{ErrorCode::Cancelled, "Request cancelled"};
        if (status != CURLE_OK)
            return Error{status == CURLE_OPERATION_TIMEDOUT ? ErrorCode::Timeout
                                                            : ErrorCode::Network,
                         curl_easy_strerror(status)};

        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        response.status = static_cast<int>(code);
        return response;
    }

  private:
    Curl handle_;
    std::chrono::seconds timeout_;
    std::mutex mutex_;
    std::stop_token stop_;
};

} // namespace

std::shared_ptr<HttpTransport> make_curl_transport(std::chrono::seconds timeout,
                                                   std::stop_token stop) {
    return std::make_shared<CurlTransport>(timeout, stop);
}

std::string detail::escape(const std::string &value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            output += static_cast<char>(character);
        } else {
            output += '%';
            output += hex[character >> 4];
            output += hex[character & 15];
        }
    }
    return output;
}

std::string detail::form(const std::map<std::string, std::string> &values) {
    std::string result;
    for (const auto &[key, value] : values) {
        if (!result.empty())
            result += '&';
        result += escape(key) + '=' + escape(value);
    }
    return result;
}

Result<nlohmann::json> detail::json_response(const HttpResponse &response) {
    if (response.status >= 200 && response.status < 300) {
        if (response.body.empty())
            return nlohmann::json::object();
        auto json = nlohmann::json::parse(response.body, nullptr, false);
        if (json.is_discarded())
            return Error{ErrorCode::InvalidResponse, "Server returned invalid JSON",
                         response.status};
        return json;
    }

    ErrorCode code = ErrorCode::ServiceUnavailable;
    switch (response.status) {
    case 400:
        code = ErrorCode::InvalidArgument;
        break;
    case 401:
        code = ErrorCode::AuthenticationRequired;
        break;
    case 403:
        code = ErrorCode::PermissionDenied;
        break;
    case 404:
        code = ErrorCode::NotFound;
        break;
    case 409:
    case 412:
        code = ErrorCode::Conflict;
        break;
    case 429:
        code = ErrorCode::RateLimited;
        break;
    }
    const auto json = nlohmann::json::parse(response.body, nullptr, false);
    if (response.status == 403 && json.is_object()) {
        const auto serialized = json.dump();
        if (serialized.find("rateLimitExceeded") != std::string::npos ||
            serialized.find("userRateLimitExceeded") != std::string::npos)
            code = ErrorCode::RateLimited;
    }
    return Error{code, "Google API request failed (HTTP " + std::to_string(response.status) + ")",
                 response.status};
}

} // namespace gcal
