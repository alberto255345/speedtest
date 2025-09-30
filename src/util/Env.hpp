#pragma once

#include <filesystem>
#include <string>

void loadDotenv(const std::filesystem::path& dotenvPath);
std::string getenvOr(const std::string& key, const std::string& defVal);
