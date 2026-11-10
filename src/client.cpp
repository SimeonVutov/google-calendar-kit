// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include "detail.hpp"
#include <gcal/client.hpp>
#include <set>
#include <stdexcept>

namespace gcal {
namespace {

constexpr auto api = "https://www.googleapis.com/calendar/v3";

std::string event_url(const std::string &calendar, const std::string &event = {}) {
    auto url = std::string(api) + "/calendars/" + detail::escape(calendar) + "/events";
    if (!event.empty())
        url += '/' + detail::escape(event);
    return url;
}

std::string guest_updates(GuestUpdates policy) {
    switch (policy) {
    case GuestUpdates::All:
        return "all";
    case GuestUpdates::ExternalOnly:
        return "externalOnly";
    case GuestUpdates::None:
        return "none";
    }
    return "all";
}

} // namespace

Client::Client(std::shared_ptr<OAuthSession> session, std::shared_ptr<HttpTransport> transport)
    : session_(std::move(session)), transport_(std::move(transport)) {
    if (!session_ || !transport_)
        throw std::invalid_argument("Client requires a session and HTTP transport");
}

Result<nlohmann::json> Client::request(HttpRequest request) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto token = session_->access_token(attempt != 0);
        if (!token)
            return token.error();

        request.headers["Authorization"] = "Bearer " + token.value();
        request.headers["Accept"] = "application/json";
        if (!request.body.empty())
            request.headers["Content-Type"] = "application/json; charset=utf-8";
        auto response = transport_->send(request);
        if (!response)
            return response.error();
        if (response.value().status == 401 && attempt == 0)
            continue;
        return detail::json_response(response.value());
    }
    return Error{ErrorCode::AuthenticationRequired, "Sign in again"};
}

Result<std::vector<nlohmann::json>> Client::pages(const std::string &url) {
    std::vector<nlohmann::json> items;
    std::set<std::string> seen;
    std::string next = url;
    for (;;) {
        auto result = request({"GET", next, {}, {}});
        if (!result)
            return result.error();
        try {
            const auto &json = result.value();
            if (!json.is_object())
                return Error{ErrorCode::InvalidResponse, "Expected a collection response"};
            if (json.contains("items")) {
                if (!json.at("items").is_array())
                    return Error{ErrorCode::InvalidResponse, "Expected an items array"};
                for (const auto &item : json.at("items"))
                    items.push_back(item);
            }
            const auto token = json.value("nextPageToken", "");
            if (token.empty())
                return items;
            if (!seen.insert(token).second || seen.size() > 1000)
                return Error{ErrorCode::InvalidResponse, "Invalid pagination sequence"};
            next = url + (url.find('?') == std::string::npos ? '?' : '&') +
                   "pageToken=" + detail::escape(token);
        } catch (const nlohmann::json::exception &) {
            return Error{ErrorCode::InvalidResponse, "Malformed collection response"};
        }
    }
}

Result<std::vector<Calendar>> Client::calendars() {
    auto result = pages(std::string(api) + "/users/me/calendarList?maxResults=250");
    if (!result)
        return result.error();

    std::vector<Calendar> calendars;
    for (const auto &json : result.value()) {
        auto calendar = parse_calendar(json);
        if (!calendar)
            return calendar.error();
        calendars.push_back(std::move(calendar.value()));
    }
    return calendars;
}

Result<std::vector<Event>> Client::events(const std::string &calendar_id, const EventQuery &query) {
    if (calendar_id.empty())
        return Error{ErrorCode::InvalidArgument, "Calendar ID is required"};
    if (auto valid = validate(TimedSpan{query.start, query.end, query.time_zone}); !valid)
        return valid.error();

    std::map<std::string, std::string> parameters{{"timeMin", query.start},
                                                  {"timeMax", query.end},
                                                  {"singleEvents", "true"},
                                                  {"orderBy", "startTime"},
                                                  {"maxResults", "2500"}};
    if (!query.time_zone.empty())
        parameters["timeZone"] = query.time_zone;
    auto result = pages(event_url(calendar_id) + '?' + detail::form(parameters));
    if (!result)
        return result.error();

    std::vector<Event> events;
    for (const auto &json : result.value()) {
        auto event = parse_event(calendar_id, json);
        if (!event)
            return event.error();
        if (!event.value().cancelled)
            events.push_back(std::move(event.value()));
    }
    return events;
}

Result<Event> Client::event(const std::string &calendar_id, const std::string &event_id) {
    if (calendar_id.empty() || event_id.empty())
        return Error{ErrorCode::InvalidArgument, "Calendar and event IDs are required"};
    auto result = request({"GET", event_url(calendar_id, event_id), {}, {}});
    if (!result)
        return result.error();
    return parse_event(calendar_id, result.value());
}

Result<std::map<std::string, std::string>> Client::event_colors() {
    auto result = request({"GET", std::string(api) + "/colors", {}, {}});
    if (!result)
        return result.error();

    std::map<std::string, std::string> colors;
    try {
        const auto &palette = result.value().at("event");
        if (!palette.is_object())
            return Error{ErrorCode::InvalidResponse, "Expected an event color palette"};
        for (const auto &[id, color] : palette.items())
            colors[id] = color.at("background").get<std::string>();
    } catch (const nlohmann::json::exception &) {
        return Error{ErrorCode::InvalidResponse, "Malformed event color palette"};
    }
    return colors;
}

Result<Event> Client::create(const std::string &calendar_id, const EventDraft &draft,
                             GuestUpdates guests) {
    if (calendar_id.empty())
        return Error{ErrorCode::InvalidArgument, "Calendar ID is required"};
    auto json = encode(draft);
    if (!json)
        return json.error();

    const auto url = event_url(calendar_id) + "?sendUpdates=" + guest_updates(guests);
    auto result = request({"POST", url, {}, json.value().dump()});
    if (!result)
        return result.error();
    return parse_event(calendar_id, result.value());
}

Status Client::writable_reference(const Event &event) const {
    if (event.calendar_id.empty() || event.id.empty() || event.etag.empty())
        return Error{ErrorCode::InvalidArgument, "A fetched event with an ETag is required"};
    if (event.cancelled)
        return Error{ErrorCode::InvalidArgument, "Cannot modify a cancelled event"};
    if (event.is_recurring_series)
        return Error{ErrorCode::UnsupportedOperation,
                     "Modify a specific occurrence; series edits are not supported"};
    return Done{};
}

Result<Event> Client::update(const Event &event, const EventPatch &patch, GuestUpdates guests) {
    if (auto valid = writable_reference(event); !valid)
        return valid.error();
    if (patch.time && event.time.index() != patch.time->index())
        return Error{ErrorCode::UnsupportedOperation,
                     "Changing between all-day and timed events is not supported"};
    auto json = encode(patch);
    if (!json)
        return json.error();

    const auto url =
        event_url(event.calendar_id, event.id) + "?sendUpdates=" + guest_updates(guests);
    auto result = request({"PATCH", url, {{"If-Match", event.etag}}, json.value().dump()});
    if (!result)
        return result.error();
    return parse_event(event.calendar_id, result.value());
}

Result<Event> Client::move(const Event &event, const EventSpan &time, GuestUpdates guests) {
    EventPatch patch;
    patch.time = time;
    return update(event, patch, guests);
}

Status Client::remove(const Event &event, GuestUpdates guests) {
    if (auto valid = writable_reference(event); !valid)
        return valid.error();
    const auto url =
        event_url(event.calendar_id, event.id) + "?sendUpdates=" + guest_updates(guests);
    auto result = request({"DELETE", url, {{"If-Match", event.etag}}, {}});
    if (!result)
        return result.error();
    return Done{};
}

} // namespace gcal
