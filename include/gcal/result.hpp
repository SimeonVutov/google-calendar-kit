// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#pragma once

#include <string>
#include <utility>
#include <variant>

namespace gcal {

enum class ErrorCode {
    InvalidArgument,
    AuthenticationRequired,
    PermissionDenied,
    NotFound,
    Conflict,
    RateLimited,
    Network,
    Timeout,
    InvalidResponse,
    UnsupportedOperation,
    ServiceUnavailable,
    AuthorizationFailed
};

struct Error {
    ErrorCode code;
    std::string message;
    int http_status = 0;
};

template <typename T> class Result {
  public:
    Result(T value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}

    explicit operator bool() const {
        return std::holds_alternative<T>(storage_);
    }
    const T &value() const {
        return std::get<T>(storage_);
    }
    T &value() {
        return std::get<T>(storage_);
    }
    const Error &error() const {
        return std::get<Error>(storage_);
    }

  private:
    std::variant<T, Error> storage_;
};

using Done = std::monostate;
using Status = Result<Done>;

} // namespace gcal
