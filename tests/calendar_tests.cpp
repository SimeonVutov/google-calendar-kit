// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include <deque>
#include <gcal/client.hpp>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

class FakeTransport final : public gcal::HttpTransport {
  public:
    std::deque<gcal::HttpResponse> responses;
    std::vector<gcal::HttpRequest> requests;

    gcal::Result<gcal::HttpResponse> send(const gcal::HttpRequest &request) override {
        requests.push_back(request);
        if (responses.empty())
            throw std::runtime_error("Unexpected HTTP request");
        auto response = std::move(responses.front());
        responses.pop_front();
        return response;
    }
};

gcal::Event sample_event() {
    gcal::Event event;
    event.calendar_id = "owner@example.com";
    event.id = "event/id";
    event.etag = "\"version-1\"";
    event.title = "Design";
    event.time =
        gcal::TimedSpan{"2026-11-10T10:00:00+02:00", "2026-11-10T11:00:00+02:00", "Europe/Sofia"};
    return event;
}

void test_time_validation() {
    require(bool(gcal::validate(gcal::DateSpan{"2028-02-29", "2028-03-01"})), "Leap day rejected");
    require(!gcal::validate(gcal::DateSpan{"2026-02-29", "2026-03-01"}),
            "Impossible date accepted");
    require(!gcal::validate(gcal::DateSpan{"2026-11-10", "2026-11-10"}),
            "Empty all-day span accepted");
    require(bool(gcal::validate(
                gcal::TimedSpan{"2026-11-10T10:00:00+02:00", "2026-11-10T09:00:00Z", ""})),
            "Offsets compared lexically");
    require(!gcal::validate(gcal::TimedSpan{"2026-11-10T10:00:00Z", "2026-11-10T10:00:00Z", ""}),
            "Zero duration accepted");
    require(!gcal::validate(gcal::TimedSpan{"2026-11-10T25:00:00Z", "2026-11-11T01:00:00Z", ""}),
            "Invalid hour accepted");
}

void test_pagination_and_errors() {
    auto http = std::make_shared<FakeTransport>();
    auto session = std::make_shared<gcal::OAuthSession>(gcal::OAuthConfig{"test-client"}, http);
    session->restore_tokens({"fixture-access", "fixture-refresh",
                             std::chrono::system_clock::now() + std::chrono::hours(1)});
    gcal::Client client(session, http);

    http->responses = {
        {200, R"({"items":[{"id":"a","summary":"First"}],"nextPageToken":"next&token"})"},
        {200, R"({"items":[{"id":"b","summary":"Second"}]})"}};
    auto calendars = client.calendars();
    require(bool(calendars) && calendars.value().size() == 2, "Pagination lost calendars");
    require(http->requests.back().url.find("pageToken=next%26token") != std::string::npos,
            "Page token was not encoded");

    http->responses = {{200, R"({"items":[],"nextPageToken":"repeat"})"},
                       {200, R"({"items":[],"nextPageToken":"repeat"})"}};
    auto loop = client.calendars();
    require(!loop && loop.error().code == gcal::ErrorCode::InvalidResponse,
            "Repeated page token accepted");

    http->responses = {{412, "{}"}};
    gcal::EventPatch patch;
    patch.description = "";
    auto conflict = client.update(sample_event(), patch);
    require(!conflict && conflict.error().code == gcal::ErrorCode::Conflict, "Conflict was hidden");
    const auto &request = http->requests.back();
    require(request.headers.at("If-Match") == "\"version-1\"", "ETag not sent");
    require(request.url.find("owner%40example.com/events/event%2Fid") != std::string::npos,
            "Path IDs not encoded");
    const auto body = nlohmann::json::parse(request.body);
    require(body == nlohmann::json{{"description", ""}}, "Patch changed unrelated fields");

    auto series = sample_event();
    series.is_recurring_series = true;
    auto unsupported = client.remove(series);
    require(!unsupported && unsupported.error().code == gcal::ErrorCode::UnsupportedOperation,
            "Series deleted implicitly");

    http->responses = {{403, R"({"error":{"errors":[{"reason":"rateLimitExceeded"}]}})"}};
    auto limited = client.calendars();
    require(!limited && limited.error().code == gcal::ErrorCode::RateLimited,
            "Rate limiting misclassified");
    http->responses = {{200, "not-json"}};
    require(!client.calendars(), "Invalid JSON accepted");
}

void test_refresh_and_unauthorized_retry() {
    auto http = std::make_shared<FakeTransport>();
    auto session = std::make_shared<gcal::OAuthSession>(gcal::OAuthConfig{"test-client"}, http);
    session->restore_tokens({"expired", "fixture-refresh", {}});
    http->responses = {
        {200, R"({"access_token":"fresh","expires_in":3600,"token_type":"Bearer"})"}};
    auto token = session->access_token();
    require(bool(token) && token.value() == "fresh", "Expired token was not refreshed");
    require(session->tokens().refresh_token == "fixture-refresh", "Refresh token was lost");
    require(http->requests.size() == 1, "Unexpected refresh requests");
    require(bool(session->access_token()) && http->requests.size() == 1,
            "Valid token refreshed unnecessarily");

    gcal::Client client(session, http);
    http->responses = {{401, "{}"},
                       {200, R"({"access_token":"renewed","expires_in":3600})"},
                       {200, R"({"items":[]})"}};
    require(bool(client.calendars()), "401 refresh/retry failed");
    require(http->requests.back().headers.at("Authorization") == "Bearer renewed",
            "Retry used stale token");

    http->responses = {
        {401, "{}"}, {200, R"({"access_token":"again","expires_in":3600})"}, {401, "{}"}};
    const auto count = http->requests.size();
    auto rejected = client.calendars();
    require(!rejected && rejected.error().code == gcal::ErrorCode::AuthenticationRequired,
            "Repeated 401 not returned");
    require(http->requests.size() == count + 3, "401 retried without a bound");
}

void test_event_mapping() {
    auto event = gcal::parse_event(
        "calendar",
        nlohmann::json::parse(
            R"({"id":"one","etag":"v1","summary":"📅 Work","recurringEventId":"series","start":{"dateTime":"2026-11-10T10:00:00+02:00","timeZone":"Europe/Sofia"},"end":{"dateTime":"2026-11-10T11:00:00+02:00"}})"));
    require(bool(event) && event.value().title == "📅 Work", "Unicode title damaged");
    require(event.value().recurring_event_id == "series" && !event.value().is_recurring_series,
            "Instance classified as series");
    auto deleted = gcal::parse_event("calendar", {{"id", "gone"}, {"status", "cancelled"}});
    require(bool(deleted) && deleted.value().cancelled, "Minimal cancellation rejected");
    require(!gcal::parse_event("calendar", {{"id", "broken"}}), "Missing event times accepted");
}

} // namespace

int main() {
    try {
        test_time_validation();
        test_pagination_and_errors();
        test_refresh_and_unauthorized_retry();
        test_event_mapping();
        auto real_transport = gcal::make_curl_transport();
        require(bool(real_transport), "HTTP transport could not be constructed");
        std::cout << "All calendar tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
