// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#pragma once

#include "oauth_session.hpp"
#include "types.hpp"
#include <map>
#include <memory>
#include <vector>

namespace gcal {

// Blocking I/O, independent of any UI or event loop. Call from your application's worker.
class Client {
  public:
    Client(std::shared_ptr<OAuthSession> session, std::shared_ptr<HttpTransport> transport);

    Result<std::vector<Calendar>> calendars();
    Result<std::vector<Event>> events(const std::string &calendar_id, const EventQuery &query);
    Result<Event> event(const std::string &calendar_id, const std::string &event_id);
    Result<std::map<std::string, std::string>> event_colors();
    Result<Event> create(const std::string &calendar_id, const EventDraft &draft,
                         GuestUpdates guests = GuestUpdates::All);
    Result<Event> update(const Event &event, const EventPatch &patch,
                         GuestUpdates guests = GuestUpdates::All);
    Result<Event> move(const Event &event, const EventSpan &time,
                       GuestUpdates guests = GuestUpdates::All);
    Status remove(const Event &event, GuestUpdates guests = GuestUpdates::All);

  private:
    Result<nlohmann::json> request(HttpRequest request);
    Result<std::vector<nlohmann::json>> pages(const std::string &url);
    Status writable_reference(const Event &event) const;

    std::shared_ptr<OAuthSession> session_;
    std::shared_ptr<HttpTransport> transport_;
};

} // namespace gcal
