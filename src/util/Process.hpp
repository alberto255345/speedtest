#pragma once

#include <string>

int runCmdCapture(const std::string& cmd, std::string& output);
bool hasCmd(const std::string& name);
