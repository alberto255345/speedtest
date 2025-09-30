cmake_minimum_required(VERSION 3.16)
project(monitor_ddd_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(JSON REQUIRED nlohmann_json)
pkg_check_modules(WIRINGPI REQUIRED wiringPi)

include_directories(
    ${CURL_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
    ${WIRINGPI_INCLUDE_DIRS}
    src
)

file(GLOB SRC
    src/main.cpp
    src/app/*.cpp
    src/infra/*.cpp
    src/util/*.cpp
)

add_executable(monitor ${SRC})

target_link_libraries(monitor
    ${CURL_LIBRARIES}
    ${WIRINGPI_LIBRARIES}
)
