cmake_minimum_required(VERSION 3.24)
project(google-calendar-kit VERSION 0.2.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(nlohmann_json 3.10 REQUIRED)
find_package(Boost 1.74 CONFIG REQUIRED)
find_package(Threads REQUIRED)

option(GCAL_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
if(GCAL_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(google_calendar
    src/types.cpp
    src/curl_transport.cpp
    src/oauth_session.cpp
    src/loopback.cpp
    src/client.cpp
)
add_library(gcal::calendar ALIAS google_calendar)
set_target_properties(google_calendar PROPERTIES
    EXPORT_NAME calendar
    POSITION_INDEPENDENT_CODE ON
)
target_compile_features(google_calendar PUBLIC cxx_std_20)
target_include_directories(google_calendar PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(google_calendar
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE CURL::libcurl OpenSSL::Crypto Boost::headers Threads::Threads
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(google_calendar PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(GCAL_BUILD_EXAMPLES "Build the calendar-list example" ON)
if(GCAL_BUILD_EXAMPLES)
    add_executable(calendar-list examples/calendar_list.cpp)
    target_link_libraries(calendar-list PRIVATE gcal::calendar)
endif()

if(BUILD_TESTING)
    add_executable(calendar-tests tests/calendar_tests.cpp)
    target_link_libraries(calendar-tests PRIVATE gcal::calendar Threads::Threads)
    add_test(NAME calendar-tests COMMAND calendar-tests)
    add_executable(oauth-loopback-tests tests/oauth_loopback_tests.cpp)
    target_link_libraries(oauth-loopback-tests PRIVATE gcal::calendar Boost::headers CURL::libcurl OpenSSL::Crypto Threads::Threads)
    add_test(NAME oauth-loopback-tests COMMAND oauth-loopback-tests)
    set_tests_properties(calendar-tests oauth-loopback-tests PROPERTIES TIMEOUT 20)
endif()

install(TARGETS google_calendar EXPORT GoogleCalendarKitTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/google-calendar-kit)
install(EXPORT GoogleCalendarKitTargets NAMESPACE gcal::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GoogleCalendarKit)
configure_package_config_file(cmake/GoogleCalendarKitConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/GoogleCalendarKitConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GoogleCalendarKit)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/GoogleCalendarKitConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/GoogleCalendarKitConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/GoogleCalendarKitConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/GoogleCalendarKit)
