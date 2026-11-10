// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include "detail.hpp"
#include <array>
#include <gcal/oauth_session.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace gcal {
namespace {

std::string base64url(const unsigned char *bytes, std::size_t length) {
    std::string encoded(4 * ((length + 2) / 3), '\0');
    EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()), bytes,
                    static_cast<int>(length));
    for (auto &character : encoded) {
        if (character == '+')
            character = '-';
        if (character == '/')
            character = '_';
    }
    while (!encoded.empty() && encoded.back() == '=')
        encoded.pop_back();
    return encoded;
}

Result<std::string> random_token() {
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        return Error{ErrorCode::AuthorizationFailed, "Unable to generate authorization entropy"};
    return base64url(bytes.data(), bytes.size());
}

} // namespace

OAuthSession::OAuthSession(OAuthConfig config, std::shared_ptr<HttpTransport> transport)
    : config_(std::move(config)), transport_(std::move(transport)) {
    if (!transport_)
        throw std::invalid_argument("OAuth session requires an HTTP transport");
}

void OAuthSession::restore_tokens(Tokens tokens) {
    std::lock_guard lock(mutex_);
    tokens_ = std::move(tokens);
}

Tokens OAuthSession::tokens() const {
    std::lock_guard lock(mutex_);
    return tokens_;
}

void OAuthSession::clear_tokens() {
    std::lock_guard lock(mutex_);
    tokens_ = {};
}

Result<Tokens> OAuthSession::exchange(const std::string &form) {
    auto response = transport_->send({"POST",
                                      "https://oauth2.googleapis.com/token",
                                      {{"Content-Type", "application/x-www-form-urlencoded"}},
                                      form});
    if (!response)
        return response.error();
    auto result = detail::json_response(response.value());
    if (!result) {
        if (response.value().status == 400 || response.value().status == 401)
            return Error{ErrorCode::AuthenticationRequired,
                         "Google rejected the token request; sign in again",
                         response.value().status};
        return result.error();
    }

    try {
        const auto &json = result.value();
        Tokens tokens;
        tokens.access_token = json.at("access_token");
        tokens.refresh_token = json.value("refresh_token", "");
        const auto lifetime = json.at("expires_in").get<long long>();
        if (tokens.access_token.empty() || lifetime <= 0 || lifetime > 86400 ||
            json.value("token_type", "Bearer") != "Bearer")
            return Error{ErrorCode::InvalidResponse, "Invalid OAuth token response"};
        tokens.expires_at = std::chrono::system_clock::now() + std::chrono::seconds(lifetime);
        return tokens;
    } catch (const nlohmann::json::exception &) {
        return Error{ErrorCode::InvalidResponse, "Malformed OAuth token response"};
    }
}

Result<std::string> OAuthSession::access_token(bool force_refresh) {
    std::lock_guard lock(mutex_);
    if (!force_refresh && !tokens_.access_token.empty() &&
        tokens_.expires_at > std::chrono::system_clock::now() + std::chrono::seconds(30))
        return tokens_.access_token;
    if (tokens_.refresh_token.empty() || config_.client_id.empty())
        return Error{ErrorCode::AuthenticationRequired, "Sign in before requesting calendar data"};

    std::map<std::string, std::string> parameters{{"client_id", config_.client_id},
                                                  {"grant_type", "refresh_token"},
                                                  {"refresh_token", tokens_.refresh_token}};
    if (!config_.client_secret.empty())
        parameters["client_secret"] = config_.client_secret;
    auto refreshed = exchange(detail::form(parameters));
    if (!refreshed)
        return refreshed.error();
    if (refreshed.value().refresh_token.empty())
        refreshed.value().refresh_token = tokens_.refresh_token;
    tokens_ = std::move(refreshed.value());
    return tokens_.access_token;
}

Result<Tokens> OAuthSession::sign_in(const BrowserOpener &open_browser,
                                     std::chrono::seconds timeout, std::stop_token stop) {
    if (stop.stop_requested())
        return Error{ErrorCode::Cancelled, "Authorization cancelled"};
    if (config_.client_id.empty() || config_.scopes.empty() || !open_browser ||
        timeout.count() <= 0)
        return Error{ErrorCode::InvalidArgument,
                     "Client ID, scopes, browser opener and positive timeout are required"};
    auto state = random_token();
    auto verifier = random_token();
    if (!state)
        return state.error();
    if (!verifier)
        return verifier.error();

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_Digest(verifier.value().data(), verifier.value().size(), digest.data(), &digest_size,
                   EVP_sha256(), nullptr) != 1)
        return Error{ErrorCode::AuthorizationFailed, "Unable to generate PKCE challenge"};
    const auto challenge = base64url(digest.data(), digest_size);

    auto authorization = detail::receive_authorization(
        [&](const std::string &redirect) -> Result<std::string> {
            std::string scopes;
            for (const auto &scope : config_.scopes) {
                if (!scopes.empty())
                    scopes += ' ';
                scopes += scope;
            }
            const auto url = "https://accounts.google.com/o/oauth2/v2/auth?" +
                             detail::form({{"client_id", config_.client_id},
                                           {"redirect_uri", redirect},
                                           {"response_type", "code"},
                                           {"scope", scopes},
                                           {"state", state.value()},
                                           {"code_challenge", challenge},
                                           {"code_challenge_method", "S256"},
                                           {"access_type", "offline"},
                                           {"prompt", "consent"}});
            auto opened = open_browser(url);
            if (!opened)
                return opened.error();
            return url;
        },
        state.value(), timeout, stop);
    if (!authorization)
        return authorization.error();
    if (stop.stop_requested())
        return Error{ErrorCode::Cancelled, "Authorization cancelled"};

    std::map<std::string, std::string> parameters{
        {"client_id", config_.client_id},
        {"grant_type", "authorization_code"},
        {"code", authorization.value().code},
        {"redirect_uri", authorization.value().redirect_uri},
        {"code_verifier", verifier.value()}};
    if (!config_.client_secret.empty())
        parameters["client_secret"] = config_.client_secret;
    auto result = exchange(detail::form(parameters));
    if (result)
        restore_tokens(result.value());
    return result;
}

} // namespace gcal
