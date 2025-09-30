#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct EmailSender {
    virtual ~EmailSender() = default;
    virtual bool send(const std::string& subject,
                      const std::string& body,
                      const std::vector<std::filesystem::path>& attachments) = 0;
};
