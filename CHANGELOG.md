# Changelog

## 0.1.0 — Initial prerelease

- Framework-independent C++20 library with CMake installation and package exports.
- Browser OAuth with PKCE, state validation, a loopback callback, and token refresh.
- Calendar listing and paginated date-range event reads.
- Creation, partial updates, rescheduling, and conditional deletion of individual events.
- Typed dates, time-zone-aware event spans, event colors, and explicit error results.
- Fake-API and local OAuth tests, with GCC/Clang and sanitizer CI coverage.

### Scope

Recurring occurrences are supported; series-wide changes and conversion between timed
and all-day events are not. Token persistence and refresh scheduling belong to the
application. There is no durable cache, background service, or offline write queue.

Local builds, tests, sanitizer checks, and an installed-package consumer have been
verified. Live Google authorization and calendar writes still need an account-level
smoke test before treating this as a stable release.
