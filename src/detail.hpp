// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#pragma once

#include <functional>
#include <gcal/http.hpp>
#include <map>
#include <nlohmann/json.hpp>

namespace gcal::detail {

std::string escape(const std::string &value);
std::string form(const std::map<std::string, std::string> &values);
Result<nlohmann::json> json_response(const HttpResponse &response);
Result<std::chrono::system_clock::time_point> timestamp(const std::string &value);

struct AuthorizationCode {
    std::string code;
    std::string redirect_uri;
};

Result<AuthorizationCode> receive_authorization(
    const std::function<Result<std::string>(const std::string &redirect_uri)> &authorize,
    const std::string &expected_state, std::chrono::seconds timeout);

} // namespace gcal::detail
