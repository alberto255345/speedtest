#pragma once

#include "domain/MacProvider.hpp"

#include <filesystem>

class MacListProvider : public MacProvider {
public:
    MacListProvider(std::filesystem::path list, std::filesystem::path idx)
        : list_(std::move(list)), idx_(std::move(idx)) {}

    std::optional<std::string> next() override;

private:
    std::filesystem::path list_;
    std::filesystem::path idx_;
};
