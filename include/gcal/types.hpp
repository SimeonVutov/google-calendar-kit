// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#pragma once

#include "result.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace gcal {

// Preserve offsets and IANA time zones; local clock strings alone are ambiguous.
struct TimedSpan {
    std::string start;
    std::string end;
    std::string time_zone;
};

// Both values are YYYY-MM-DD. The end date is exclusive, matching Google Calendar.
struct DateSpan {
    std::string start;
    std::string end;
};

using EventSpan = std::variant<TimedSpan, DateSpan>;

struct Calendar {
    std::string id;
    std::string name;
    std::string time_zone;
    std::string background_color;
    std::string access_role;
    bool primary = false;
};

struct Event {
    std::string calendar_id;
    std::string id;
    std::string etag;
    std::string title;
    std::string description;
    std::string location;
    std::string color_id;
    EventSpan time;
    std::string recurring_event_id;
    bool is_recurring_series = false;
    bool cancelled = false;
};

struct EventDraft {
    std::string title;
    EventSpan time;
    std::string description;
    std::string location;
    std::string color_id;
};

// An absent field stays unchanged; an empty string explicitly clears a field.
struct EventPatch {
    std::optional<std::string> title;
    std::optional<EventSpan> time;
    std::optional<std::string> description;
    std::optional<std::string> location;
    std::optional<std::string> color_id;
};

struct EventQuery {
    std::string start;
    std::string end;
    std::string time_zone;
};

enum class GuestUpdates { All, ExternalOnly, None };

Status validate(const EventSpan &span);
Result<Event> parse_event(const std::string &calendar_id, const nlohmann::json &json);
Result<Calendar> parse_calendar(const nlohmann::json &json);
Result<nlohmann::json> encode(const EventDraft &draft);
Result<nlohmann::json> encode(const EventPatch &patch);

} // namespace gcal
