# Design and integration

## Boundaries

`Client` implements calendar operations; `OAuthSession` owns one account's in-memory
tokens. `HttpTransport` is the single replaceable boundary for network I/O, allowing
deterministic tests without exposing configurable Google endpoints in the public API.
The production transport uses libcurl with HTTPS verification, bounded response size,
timeouts, connection reuse, and no automatic redirects.

Event conversion and validation live in `types.cpp`. OAuth protocol steps live in
`oauth_session.cpp`; the temporary HTTP callback listener is isolated in `loopback.cpp`.
The listener uses Boost.Beast's HTTP parser rather than handwritten HTTP parsing.
Resource ownership uses values, smart pointers, and RAII.

The core intentionally has no UI, event loop requirement, scheduler, or persistent
cache. Dependencies are libcurl (HTTP), OpenSSL (PKCE/randomness), Boost (loopback HTTP),
and nlohmann/json (Google's wire format). Qt is only relevant to a future adapter.

## Execution and ownership

Calls block until completion or timeout. The default HTTP deadline is 30 seconds;
interactive sign-in has a separate three-minute deadline. The browser opener must
return promptly: waiting for the callback inside it would prevent the listener from
processing the redirect.

Applications that need clean worker shutdown can pass a C++ `std::stop_token`
to `make_curl_transport()` and `sign_in()`. Cancellation stops the OAuth listener
or aborts the transfer cooperatively and returns `ErrorCode::Cancelled`.

Applications own workers and scheduling. Keep a shared session and transport alive
for the client. The provided transport serializes requests and reuses its connection;
the session protects token refresh with a mutex. Do not sign in, restore, or clear
accounts concurrently with calendar operations. An injected transport must provide
equivalent thread safety if shared between workers.

Normal API failures use `Result<T>` and typed `ErrorCode`s. Invalid construction and
allocation failures may throw standard exceptions. Results own their data and remain
valid independently of subsequent requests. No raw owning pointers cross the API.

## Refresh and writes

The application controls refresh frequency and keeps the last successful schedule.
The library follows collection pagination automatically. Date-range reads expand
recurring events into occurrences; all-day events keep date values and exclusive ends.
Timed values require RFC3339 offsets; time-zone names are passed through to Google.
Leap-second timestamps are not accepted, and timed dates must fit `system_clock`.

Before a request, expired access tokens refresh automatically. An HTTP 401 causes one
refresh and one retry. Transport failures and ambiguous write outcomes are not retried
automatically: retrying an uncertain create could duplicate an event. Rate-limit and
service failures remain visible to the caller.

Updates and deletes require a fetched event and send its ETag with `If-Match`. A 412
becomes `Conflict`; the application must reload and decide whether to reapply the
change. Updating uses PATCH so omitted fields, such as attendees or conference details,
are preserved. PATCH consumes more quota units than a single GET; this tradeoff keeps
partial writes explicit and avoids overwriting fields this library does not model.

Recurring series edits and converting between timed and all-day types return
`UnsupportedOperation`. Supporting those later requires deliberate public operations,
not a flag that silently changes the meaning of an ordinary edit.

## Token persistence

The core never writes credentials to disk. The host app restores a `Tokens` snapshot
and stores the latest snapshot using its platform credential store after operations.
Tokens and authorization URLs must not be logged. `clear_tokens()` removes this
session's in-memory credentials; it does not revoke Google's grant or erase the host's
credential-store entry. Network errors contain status information, not raw server bodies.

## Quickshell adapter

Build and install the core, then implement a separate Qt/QML module linked to
`gcal::calendar`. That module should expose account status, a date-range event model,
create/update/move/remove methods, and completion/error signals. Calls run on a worker;
model mutations happen on the QML thread. The plugin must match the host's Qt/toolchain.

The existing agenda model becomes a presentation adapter: convert returned events,
retain ETags outside editable display fields, and submit changes through the plugin.
Keep a pending preview while a write runs; apply the server's returned event on success,
or restore the previous event on failure. Refresh cached event/calendar colors after
reconnection. Preserve all-day dates as dates, rather than coercing them to UTC midnight.

Separate Quickshell processes may instantiate separate clients. Sharing one live
connection would require a process bridge, which is unnecessary for this first version.

## References

- [Google desktop OAuth](https://developers.google.com/identity/protocols/oauth2/native-app)
- [Event listing](https://developers.google.com/workspace/calendar/api/v3/reference/events/list)
- [Partial event updates](https://developers.google.com/workspace/calendar/api/v3/reference/events/patch)
- [Conditional writes](https://developers.google.com/workspace/calendar/api/guides/version-resources)
- [Recurring events](https://developers.google.com/workspace/calendar/api/guides/recurringevents)
