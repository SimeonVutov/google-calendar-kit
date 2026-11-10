// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include <array>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <curl/curl.h>
#include <gcal/oauth_session.hpp>
#include <iostream>
#include <openssl/evp.h>
#include <regex>
#include <stdexcept>
#include <thread>

namespace {

namespace net = boost::asio;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string parameter(const std::string &url, const std::string &key) {
    const std::regex expression("(?:[?&]|^)" + key + "=([^&]*)");
    std::smatch parts;
    if (!std::regex_search(url, parts, expression))
        throw std::runtime_error("Required OAuth parameter missing");
    const std::string encoded = parts[1];
    int length = 0;
    std::unique_ptr<char, decltype(&curl_free)> decoded(
        curl_easy_unescape(nullptr, encoded.c_str(), static_cast<int>(encoded.size()), &length),
        curl_free);
    if (!decoded)
        throw std::runtime_error("URL decoding failed");
    return {decoded.get(), static_cast<std::size_t>(length)};
}

unsigned int callback(const std::string &redirect, const std::string &query) {
    const std::regex expression(R"(http://127\.0\.0\.1:(\d+)(/oauth/callback))");
    std::smatch parts;
    require(std::regex_match(redirect, parts, expression), "Callback was not bound to loopback");

    net::io_context context;
    tcp::socket socket(context);
    socket.connect(
        {net::ip::address_v4::loopback(), static_cast<unsigned short>(std::stoul(parts[1]))});
    http::request<http::empty_body> request{http::verb::get, parts[2].str() + '?' + query, 11};
    request.set(http::field::host, "127.0.0.1");
    http::write(socket, request);
    boost::beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return response.result_int();
}

class TokenEndpoint final : public gcal::HttpTransport {
  public:
    std::vector<gcal::HttpRequest> requests;
    gcal::Result<gcal::HttpResponse> send(const gcal::HttpRequest &request) override {
        require(request.url == "https://oauth2.googleapis.com/token", "Incorrect token endpoint");
        requests.push_back(request);
        return gcal::HttpResponse{
            200,
            R"({"access_token":"fixture-access","refresh_token":"fixture-refresh","expires_in":3600,"token_type":"Bearer"})"};
    }
};

void test_success_and_state_validation() {
    auto endpoint = std::make_shared<TokenEndpoint>();
    gcal::OAuthSession session({"fixture-client"}, endpoint);
    std::jthread browser;
    std::exception_ptr browser_failure;
    std::string challenge;

    auto result = session.sign_in(
        [&](const std::string &url) -> gcal::Status {
            require(parameter(url, "code_challenge_method") == "S256", "PKCE was disabled");
            require(parameter(url, "access_type") == "offline", "Offline refresh access missing");
            challenge = parameter(url, "code_challenge");
            const auto redirect = parameter(url, "redirect_uri");
            const auto state = parameter(url, "state");
            require(state.size() >= 43, "Authorization state too short");
            browser = std::jthread([&, redirect, state] {
                try {
                    require(callback(redirect, "state=wrong&code=fixture-code") == 400,
                            "Wrong state accepted");
                    require(callback(redirect, "state=" + state + "&code=fixture-code") == 200,
                            "Valid callback failed");
                } catch (...) {
                    browser_failure = std::current_exception();
                }
            });
            return gcal::Done{};
        },
        std::chrono::seconds(5));
    if (browser.joinable())
        browser.join();
    if (browser_failure)
        std::rethrow_exception(browser_failure);
    require(bool(result), "Sign-in failed");
    require(endpoint->requests.size() == 1, "Unexpected token exchange count");
    const auto &form = endpoint->requests.front().body;
    require(parameter(form, "code") == "fixture-code", "Authorization code was not exchanged");
    const auto verifier = parameter(form, "code_verifier");
    require(verifier.size() == 43, "PKCE verifier length incorrect");

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    require(EVP_Digest(verifier.data(), verifier.size(), digest.data(), &size, EVP_sha256(),
                       nullptr) == 1,
            "Digest failed");
    std::array<unsigned char, 128> encoded{};
    EVP_EncodeBlock(encoded.data(), digest.data(), static_cast<int>(size));
    std::string expected(reinterpret_cast<char *>(encoded.data()));
    for (auto &byte : expected) {
        if (byte == '+')
            byte = '-';
        if (byte == '/')
            byte = '_';
    }
    while (expected.back() == '=')
        expected.pop_back();
    require(challenge == expected, "PKCE challenge did not match verifier");
    require(session.tokens().refresh_token == "fixture-refresh",
            "Sign-in did not retain refresh token");
}

void test_denial_timeout_and_browser_failure() {
    auto endpoint = std::make_shared<TokenEndpoint>();
    gcal::OAuthSession session({"fixture-client"}, endpoint);
    std::jthread browser;
    auto denied = session.sign_in(
        [&](const std::string &url) -> gcal::Status {
            browser = std::jthread(
                [redirect = parameter(url, "redirect_uri"), state = parameter(url, "state")] {
                    callback(redirect, "state=" + state + "&error=access_denied");
                });
            return gcal::Done{};
        },
        std::chrono::seconds(5));
    if (browser.joinable())
        browser.join();
    require(!denied && denied.error().code == gcal::ErrorCode::AuthorizationFailed,
            "Denial ignored");
    require(endpoint->requests.empty(), "Denied authorization attempted token exchange");

    auto timeout = session.sign_in([](const std::string &) -> gcal::Status { return gcal::Done{}; },
                                   std::chrono::seconds(1));
    require(!timeout && timeout.error().code == gcal::ErrorCode::Timeout,
            "Callback timeout failed");
    auto failed = session.sign_in([](const std::string &) -> gcal::Status {
        return gcal::Error{gcal::ErrorCode::AuthorizationFailed, "Browser launch failed"};
    });
    require(!failed, "Browser launch failure ignored");
}

} // namespace

int main() {
    try {
        auto initialize_curl = gcal::make_curl_transport();
        test_success_and_state_validation();
        test_denial_timeout_and_browser_failure();
        std::cout << "All loopback OAuth tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
