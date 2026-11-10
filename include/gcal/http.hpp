// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#pragma once

#include "result.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace gcal {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

class HttpTransport {
  public:
    virtual ~HttpTransport() = default;
    virtual Result<HttpResponse> send(const HttpRequest &request) = 0;
};

// Reuses one connection handle. A transport serializes its own requests.
std::shared_ptr<HttpTransport>
make_curl_transport(std::chrono::seconds timeout = std::chrono::seconds(30));

} // namespace gcal
