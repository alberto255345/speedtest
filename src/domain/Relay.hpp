#pragma once

struct Relay {
    virtual ~Relay() = default;
    virtual bool pulse(double seconds, bool active_high = true) = 0;
};
