# google-calendar-kit

A small C++20 library for reading and editing Google Calendar. Framework-independent,
with browser sign-in, automatic token refresh, typed events, and explicit errors.

Author: [Simeon Vutov](https://github.com/SimeonVutov).

## Features

- List calendars and events in a date range, including recurring occurrences.
- Create, edit, reschedule, and delete individual events or occurrences.
- Preserve time zones, all-day dates, descriptions, Unicode titles, and color IDs.
- Detect stale edits with ETags instead of silently overwriting newer changes.
- Browser authorization with PKCE and an ephemeral loopback callback.

The library has no Qt dependency, database, daemon, polling loop, or offline write queue.
Calls perform blocking I/O; GUI applications should use their own worker thread.

## Build and install

Requirements: C++20, CMake 3.24+, libcurl, OpenSSL, Boost 1.74+, and nlohmann/json 3.10+.
On Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja curl openssl boost nlohmann-json
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

The default build produces a static library. Add `-DBUILD_SHARED_LIBS=ON` for a shared
library, or `-DGCAL_BUILD_EXAMPLES=OFF` to omit the example executable.

In another CMake project:

```cmake
find_package(GoogleCalendarKit CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE gcal::calendar)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix if CMake cannot locate the package.
Alternatively, use `add_subdirectory()` and link the same target.

## Connect an account

Enable the Calendar API in a Google Cloud project, configure OAuth consent, and
create a **Desktop app** OAuth client. Keep its downloaded configuration outside
this repository. The example signs in and lists calendars:

```sh
./build/calendar-list /path/to/desktop-client.json
```

Open the printed URL in your browser. The example keeps tokens only for that process.
The library does not read configuration files or start a browser itself; the example
loads configuration, while your application supplies its own browser opener.

```cpp
auto transport = gcal::make_curl_transport();
auto session = std::make_shared<gcal::OAuthSession>(config, transport);

// Run on a worker. The opener must launch the browser and return promptly.
auto login = session->sign_in(open_browser);
if (!login) {
    show_error(login.error().message);
    return;
}

gcal::Client calendar(session, transport);
auto events = calendar.events("primary", {
    "2026-11-10T00:00:00+02:00",
    "2026-11-11T00:00:00+02:00",
    "Europe/Sofia"
});
```

`config`, `open_browser`, and `show_error` above belong to your application. Store
`session->tokens()` in the OS credential store and restore them with `restore_tokens()`
on startup. Refresh can update tokens: persist the latest snapshot after operations.
Never put tokens, client configuration, or account data in Git or logs.

## Edit an event

Use an `Event` returned by `events()` or `event()`; it contains the calendar ID,
event ID, and ETag needed for a conditional write.

```cpp
gcal::EventPatch changes;
changes.description = "Updated notes";
auto updated = calendar.update(event, changes);
if (!updated) {
    show_error(updated.error().message);
    return;
}

auto moved = calendar.move(updated.value(), gcal::TimedSpan{
    "2026-11-10T14:00:00+02:00",
    "2026-11-10T15:00:00+02:00",
    "Europe/Sofia"
});
```

After success, replace the old event with the returned event and its new ETag before
editing again. An absent patch field is unchanged; an empty string clears a field.
`DateSpan` uses an exclusive end date. Event colors are Google palette IDs, available
through `event_colors()`, rather than arbitrary RGB values.

Writes default to Google's `sendUpdates=all`; pass `GuestUpdates::None` or
`ExternalOnly` when your application needs a different guest-notification policy.
On `ErrorCode::Conflict`, fetch the latest event and let the user retry deliberately.

## Application integration

Any C++ app can link the library directly. A Quickshell integration would be a
separate QML plugin that runs calls on a worker and exposes an event model and
signals. Other languages can use a dedicated binding or process adapter. Those
adapters are not part of this initial library.

Keep the visible schedule in application memory. Refresh when opening it, periodically
while active, and after writes. Submit a move once on release, not on each pointer
update. See [design and integration notes](docs/design.md).

## Development

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGCAL_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Tests use a fake HTTP transport and local loopback callbacks; no Google credentials
are needed. GitHub Actions builds with GCC/Clang and runs sanitizer checks.

Current scope: individual events and recurring occurrences. Series edits, switching
between timed/all-day event types, push notifications, and durable offline storage
are deliberately unsupported. The 0.x API may change.

## License

[Apache License 2.0](LICENSE). Commercial use is permitted. Preserve copyright,
license, and applicable attribution notices when redistributing; see [NOTICE](NOTICE).

Google Calendar is a Google product. This project is not affiliated with Google.
