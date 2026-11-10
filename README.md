# google-calendar-kit

A small, framework-independent C++20 library for Google Calendar. Sign in with
OAuth, list calendars and events, and create, edit, move, or delete events.

By [Simeon Vutov](https://github.com/SimeonVutov). [Apache 2.0](LICENSE).

## Install on Linux

Install a C++20 compiler, CMake 3.24+, and development packages for libcurl,
OpenSSL, Boost 1.74+, and nlohmann/json 3.10+.

On Debian/Ubuntu, for example:

```sh
sudo apt install g++ cmake libcurl4-openssl-dev libssl-dev libboost-dev nlohmann-json3-dev
```

Build from source and install to a prefix of your choice:

```sh
git clone https://github.com/SimeonVutov/google-calendar-kit.git
cd google-calendar-kit
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
```

Add `-DBUILD_SHARED_LIBS=ON` if you want a shared library.

**Arch Linux:** after cloning, you can use the optional package instead:

```sh
cd packaging/arch
makepkg -si
```

This builds from the checkout and installs through pacman. No AUR publication is required.

## Use the library

```cmake
find_package(GoogleCalendarKit CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE gcal::calendar)
```

For a non-system installation, point `CMAKE_PREFIX_PATH` at the chosen prefix.
Include `<gcal/client.hpp>` and use `OAuthSession` to authenticate, then `Client`
to work with calendars and events. See the [API and integration notes](docs/design.md).

To test sign-in with the CMake build:

```sh
./build/calendar-list /path/to/desktop-client.json
```

Follow the [Google account setup guide](docs/authentication.md) to create that
file. Keep the configuration and tokens outside the repository.

## Scope

The caller runs blocking operations on its own worker, chooses which calendars
to fetch, and owns token storage and refresh scheduling. The core has no Qt,
database, or background service. Individual recurring occurrences are supported;
series-wide edits are outside the current scope.

The Quickshell QML adapter is a separate consumer of this library. Its build and
installation instructions belong to the Quickshell configuration.

See [CHANGELOG.md](CHANGELOG.md) for changes and [NOTICE](NOTICE) for attribution.
