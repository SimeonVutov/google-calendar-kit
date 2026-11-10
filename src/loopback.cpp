// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include "detail.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <charconv>
#include <optional>

namespace gcal::detail {
namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

Result<std::string> decode(std::string_view input) {
    std::string output;
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] == '%') {
            if (input.size() - index < 3)
                return Error{ErrorCode::InvalidArgument, "Invalid callback encoding"};
            unsigned int byte = 0;
            const auto *begin = input.data() + index + 1;
            const auto parsed = std::from_chars(begin, begin + 2, byte, 16);
            if (parsed.ec != std::errc{} || parsed.ptr != begin + 2)
                return Error{ErrorCode::InvalidArgument, "Invalid callback encoding"};
            output += static_cast<char>(byte);
            index += 2;
        } else {
            output += input[index] == '+' ? ' ' : input[index];
        }
    }
    return output;
}

Result<std::map<std::string, std::string>> parameters(std::string_view query) {
    std::map<std::string, std::string> values;
    while (!query.empty()) {
        const auto separator = query.find('&');
        const auto pair = query.substr(0, separator);
        const auto equal = pair.find('=');
        auto key = decode(pair.substr(0, equal));
        auto value =
            decode(equal == std::string_view::npos ? std::string_view{} : pair.substr(equal + 1));
        if (!key || !value)
            return Error{ErrorCode::InvalidArgument, "Invalid callback encoding"};
        if (!values.emplace(key.value(), value.value()).second)
            return Error{ErrorCode::InvalidArgument, "Repeated callback parameter"};
        if (separator == std::string_view::npos)
            break;
        query.remove_prefix(separator + 1);
    }
    return values;
}

class CallbackConnection : public std::enable_shared_from_this<CallbackConnection> {
  public:
    using Completion = std::function<void(Result<std::string>)>;

    CallbackConnection(tcp::socket socket, std::string state, Completion complete)
        : stream_(std::move(socket)), state_(std::move(state)), complete_(std::move(complete)) {
        parser_.body_limit(0);
        parser_.header_limit(8192);
    }

    void start() {
        stream_.expires_after(std::chrono::seconds(10));
        http::async_read(stream_, buffer_, parser_,
                         [self = shared_from_this()](boost::system::error_code error, std::size_t) {
                             if (!error)
                                 self->handle();
                         });
    }

  private:
    void handle() {
        const auto &request = parser_.get();
        const std::string target(request.target());
        const auto separator = target.find('?');
        if (request.method() != http::verb::get ||
            target.substr(0, separator) != "/oauth/callback") {
            respond(http::status::not_found, "Not found.", std::nullopt);
            return;
        }

        auto query = parameters(separator == std::string::npos
                                    ? std::string_view{}
                                    : std::string_view(target).substr(separator + 1));
        if (!query || !query.value().contains("state") || query.value().at("state") != state_) {
            respond(http::status::bad_request, "Invalid authorization state.", std::nullopt);
            return;
        }
        if (query.value().contains("error")) {
            respond(http::status::bad_request,
                    "Authorization was declined. You may close this tab.",
                    Error{ErrorCode::AuthorizationFailed, "Authorization was declined"});
            return;
        }
        const auto code = query.value().find("code");
        if (code == query.value().end() || code->second.empty()) {
            respond(http::status::bad_request, "Missing authorization code.", std::nullopt);
            return;
        }
        respond(http::status::ok, "Authorization received. You may close this tab.", code->second);
    }

    void respond(http::status status, std::string message,
                 std::optional<Result<std::string>> result) {
        response_.result(status);
        response_.version(11);
        response_.set(http::field::content_type, "text/plain; charset=utf-8");
        response_.set(http::field::cache_control, "no-store");
        response_.set("Referrer-Policy", "no-referrer");
        response_.keep_alive(false);
        response_.body() = std::move(message);
        response_.prepare_payload();
        http::async_write(stream_, response_,
                          [self = shared_from_this(), result = std::move(result)](
                              boost::system::error_code, std::size_t) mutable {
                              boost::system::error_code ignored;
                              self->stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
                              if (result)
                                  self->complete_(std::move(*result));
                          });
    }

    boost::beast::tcp_stream stream_;
    boost::beast::flat_buffer buffer_;
    http::request_parser<http::empty_body> parser_;
    http::response<http::string_body> response_;
    std::string state_;
    Completion complete_;
};

} // namespace

Result<AuthorizationCode>
receive_authorization(const std::function<Result<std::string>(const std::string &)> &authorize,
                      const std::string &expected_state, std::chrono::seconds timeout) {
    net::io_context context;
    tcp::acceptor listener(context);
    boost::system::error_code error;
    listener.open(tcp::v4(), error);
    if (!error)
        listener.bind({net::ip::address_v4::loopback(), 0}, error);
    if (!error)
        listener.listen(4, error);
    if (error)
        return Error{ErrorCode::Network, "Unable to start the loopback authorization listener"};

    const auto redirect =
        "http://127.0.0.1:" + std::to_string(listener.local_endpoint().port()) + "/oauth/callback";
    std::optional<Result<std::string>> result;
    net::steady_timer deadline(context, timeout);
    auto complete = [&](Result<std::string> code) {
        if (!result) {
            result = std::move(code);
            context.stop();
        }
    };
    deadline.async_wait([&](boost::system::error_code timer_error) {
        if (!timer_error)
            complete(Error{ErrorCode::Timeout, "Browser authorization timed out"});
    });

    std::function<void()> accept;
    accept = [&] {
        listener.async_accept([&](boost::system::error_code accept_error, tcp::socket socket) {
            if (accept_error) {
                if (accept_error != net::error::operation_aborted)
                    complete(Error{ErrorCode::Network, "Loopback authorization listener failed"});
                return;
            }
            std::make_shared<CallbackConnection>(std::move(socket), expected_state, complete)
                ->start();
            if (!result)
                accept();
        });
    };
    accept();

    auto opened = authorize(redirect);
    if (!opened)
        return opened.error();
    context.run();
    if (!result)
        return Error{ErrorCode::AuthorizationFailed, "Authorization did not complete"};
    if (!*result)
        return result->error();
    return AuthorizationCode{std::move(result->value()), redirect};
}

} // namespace gcal::detail
