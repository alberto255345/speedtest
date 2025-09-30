#include "util/Time.hpp"

#include <ctime>

std::string nowStr() {
    std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_r(&t, &tm);
    char buff[32];
    std::strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buff);
}
