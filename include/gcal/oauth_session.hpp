// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#pragma once

#include "http.hpp"
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

namespace gcal {

struct OAuthConfig {
    std::string client_id;
    std::string client_secret;
    std::vector<std::string> scopes = {
        "https://www.googleapis.com/auth/calendar.calendarlist.readonly",
        "https://www.googleapis.com/auth/calendar.events"};
};

struct Tokens {
    std::string access_token;
    std::string refresh_token;
    std::chrono::system_clock::time_point expires_at{};
};

using BrowserOpener = std::function<Status(const std::string &url)>;

// One session represents one account. Tokens remain in memory; callers own persistence.
class OAuthSession {
  public:
    OAuthSession(OAuthConfig config, std::shared_ptr<HttpTransport> transport);

    Result<Tokens> sign_in(const BrowserOpener &open_browser,
                           std::chrono::seconds timeout = std::chrono::minutes(3));
    void restore_tokens(Tokens tokens);
    Tokens tokens() const;
    void clear_tokens();

    // Refreshes expired tokens. force_refresh is used once after an HTTP 401.
    Result<std::string> access_token(bool force_refresh = false);

  private:
    Result<Tokens> exchange(const std::string &form);

    OAuthConfig config_;
    std::shared_ptr<HttpTransport> transport_;
    mutable std::mutex mutex_;
    Tokens tokens_;
};

} // namespace gcal
