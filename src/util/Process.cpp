#include "util/Process.hpp"

#include <cstdio>
#include <string>
#include <sys/wait.h>

int runCmdCapture(const std::string& cmd, std::string& output) {
    std::string full = cmd + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) return -1;
    char buf[4096];
    output.clear();
    while (fgets(buf, sizeof buf, pipe)) output += buf;
    int rc = pclose(pipe);
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
}

bool hasCmd(const std::string& name) {
    std::string out;
    return runCmdCapture("which " + name, out) == 0;
}
