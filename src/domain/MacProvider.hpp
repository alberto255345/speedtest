#pragma once

#include <optional>
#include <string>

struct MacProvider {
    virtual ~MacProvider() = default;
    virtual std::optional<std::string> next() = 0;
};
