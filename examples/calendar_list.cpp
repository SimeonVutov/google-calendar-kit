// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Simeon Vutov

#include <fstream>
#include <gcal/client.hpp>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: calendar-list <desktop-client.json>\n";
        return 2;
    }

    gcal::OAuthConfig config;
    try {
        std::ifstream input(argv[1]);
        const auto credentials = nlohmann::json::parse(input).at("installed");
        config.client_id = credentials.at("client_id");
        config.client_secret = credentials.value("client_secret", "");
    } catch (const std::exception &) {
        std::cerr << "Unable to load the desktop OAuth client configuration\n";
        return 2;
    }

    auto transport = gcal::make_curl_transport();
    auto session = std::make_shared<gcal::OAuthSession>(std::move(config), transport);
    auto signed_in = session->sign_in([](const std::string &url) -> gcal::Status {
        std::cout << "Open this URL in your browser:\n" << url << '\n';
        return gcal::Done{};
    });
    if (!signed_in) {
        std::cerr << signed_in.error().message << '\n';
        return 1;
    }

    gcal::Client client(session, transport);
    auto calendars = client.calendars();
    if (!calendars) {
        std::cerr << calendars.error().message << '\n';
        return 1;
    }
    for (const auto &calendar : calendars.value())
        std::cout << calendar.name << '\t' << calendar.id << '\n';
}
