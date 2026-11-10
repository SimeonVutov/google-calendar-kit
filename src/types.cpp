// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include "detail.hpp"
#include <chrono>
#include <gcal/types.hpp>
#include <regex>

namespace gcal {
namespace {

Result<std::chrono::sys_days> date(const std::string &value) {
    static const std::regex pattern(R"((\d{4})-(\d{2})-(\d{2}))");
    std::smatch parts;
    if (!std::regex_match(value, parts, pattern))
        return Error{ErrorCode::InvalidArgument, "Expected a YYYY-MM-DD date"};

    const std::chrono::year_month_day day{std::chrono::year(std::stoi(parts[1])),
                                          std::chrono::month(std::stoul(parts[2])),
                                          std::chrono::day(std::stoul(parts[3]))};
    if (!day.ok())
        return Error{ErrorCode::InvalidArgument, "Invalid calendar date"};

    return std::chrono::sys_days(day);
}

void encode_time(nlohmann::json &json, const EventSpan &span) {
    if (const auto *timed = std::get_if<TimedSpan>(&span)) {
        json["start"] = {{"dateTime", timed->start}};
        json["end"] = {{"dateTime", timed->end}};
        if (!timed->time_zone.empty()) {
            json["start"]["timeZone"] = timed->time_zone;
            json["end"]["timeZone"] = timed->time_zone;
        }
    } else {
        const auto &dates = std::get<DateSpan>(span);
        json["start"] = {{"date", dates.start}};
        json["end"] = {{"date", dates.end}};
    }
}

} // namespace

Result<std::chrono::system_clock::time_point> detail::timestamp(const std::string &value) {
    static const std::regex pattern(
        R"((\d{4}-\d{2}-\d{2})[Tt](\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?([Zz]|[+-]\d{2}:\d{2}))");
    std::smatch parts;
    if (!std::regex_match(value, parts, pattern))
        return Error{ErrorCode::InvalidArgument, "Expected an RFC3339 timestamp with an offset"};

    auto day = date(parts[1]);
    if (!day)
        return day.error();

    const int hour = std::stoi(parts[2]);
    const int minute = std::stoi(parts[3]);
    const int second = std::stoi(parts[4]);
    if (hour > 23 || minute > 59 || second > 59)
        return Error{ErrorCode::InvalidArgument, "Invalid time of day"};

    int offset = 0;
    const std::string zone = parts[6];
    if (zone != "Z" && zone != "z") {
        const int hours = std::stoi(zone.substr(1, 2));
        const int minutes = std::stoi(zone.substr(4, 2));
        if (hours > 23 || minutes > 59)
            return Error{ErrorCode::InvalidArgument, "Invalid UTC offset"};
        offset = (hours * 60 + minutes) * (zone[0] == '+' ? 1 : -1);
    }

    std::string fraction = parts[5];
    fraction.resize(9, '0');
    const auto instant = day.value() + std::chrono::hours(hour) +
                         std::chrono::minutes(minute - offset) + std::chrono::seconds(second);
    const auto minimum =
        std::chrono::ceil<std::chrono::seconds>(std::chrono::system_clock::time_point::min());
    const auto maximum =
        std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::time_point::max());
    if (instant < minimum || instant > maximum)
        return Error{ErrorCode::InvalidArgument, "Timestamp is outside the supported clock range"};

    const auto base = std::chrono::time_point_cast<std::chrono::system_clock::duration>(instant);
    const auto subsecond = std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::nanoseconds(std::stoll(fraction)));
    if (instant == maximum && subsecond > std::chrono::system_clock::time_point::max() - base)
        return Error{ErrorCode::InvalidArgument, "Timestamp is outside the supported clock range"};
    return base + subsecond;
}

Status validate(const EventSpan &span) {
    if (const auto *timed = std::get_if<TimedSpan>(&span)) {
        auto start = detail::timestamp(timed->start);
        auto end = detail::timestamp(timed->end);
        if (!start)
            return start.error();
        if (!end)
            return end.error();
        if (end.value() <= start.value())
            return Error{ErrorCode::InvalidArgument, "Event end must be after start"};
    } else {
        const auto &dates = std::get<DateSpan>(span);
        auto start = date(dates.start);
        auto end = date(dates.end);
        if (!start)
            return start.error();
        if (!end)
            return end.error();
        if (end.value() <= start.value())
            return Error{ErrorCode::InvalidArgument, "Exclusive end date must be after start"};
    }
    return Done{};
}

Result<Event> parse_event(const std::string &calendar_id, const nlohmann::json &json) {
    try {
        Event event;
        event.calendar_id = calendar_id;
        event.id = json.at("id").get<std::string>();
        event.etag = json.value("etag", "");
        event.title = json.value("summary", "");
        event.description = json.value("description", "");
        event.location = json.value("location", "");
        event.color_id = json.value("colorId", "");
        event.recurring_event_id = json.value("recurringEventId", "");
        event.is_recurring_series = json.contains("recurrence") && !json.at("recurrence").empty();
        event.cancelled = json.value("status", "") == "cancelled";
        if (event.cancelled)
            return event;

        const auto &start = json.at("start");
        const auto &end = json.at("end");
        if (start.contains("dateTime")) {
            event.time =
                TimedSpan{start.at("dateTime"), end.at("dateTime"), start.value("timeZone", "")};
        } else {
            event.time = DateSpan{start.at("date"), end.at("date")};
        }
        if (auto valid = validate(event.time); !valid)
            return Error{ErrorCode::InvalidResponse, valid.error().message};

        return event;
    } catch (const nlohmann::json::exception &) {
        return Error{ErrorCode::InvalidResponse, "Malformed event response"};
    }
}

Result<Calendar> parse_calendar(const nlohmann::json &json) {
    try {
        return Calendar{json.at("id"),
                        json.value("summaryOverride", json.value("summary", "")),
                        json.value("timeZone", ""),
                        json.value("backgroundColor", ""),
                        json.value("accessRole", ""),
                        json.value("primary", false)};
    } catch (const nlohmann::json::exception &) {
        return Error{ErrorCode::InvalidResponse, "Malformed calendar response"};
    }
}

Result<nlohmann::json> encode(const EventDraft &draft) {
    if (auto valid = validate(draft.time); !valid)
        return valid.error();

    nlohmann::json json{
        {"summary", draft.title}, {"description", draft.description}, {"location", draft.location}};
    if (!draft.color_id.empty())
        json["colorId"] = draft.color_id;
    encode_time(json, draft.time);
    return json;
}

Result<nlohmann::json> encode(const EventPatch &patch) {
    nlohmann::json json = nlohmann::json::object();
    if (patch.title)
        json["summary"] = *patch.title;
    if (patch.description)
        json["description"] = *patch.description;
    if (patch.location)
        json["location"] = *patch.location;
    if (patch.color_id)
        json["colorId"] =
            patch.color_id->empty() ? nlohmann::json(nullptr) : nlohmann::json(*patch.color_id);
    if (patch.time) {
        if (auto valid = validate(*patch.time); !valid)
            return valid.error();
        encode_time(json, *patch.time);
    }
    if (json.empty())
        return Error{ErrorCode::InvalidArgument, "No event changes supplied"};
    return json;
}

} // namespace gcal
