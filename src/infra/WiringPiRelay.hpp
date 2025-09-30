#pragma once

#include "domain/Relay.hpp"

#include <wiringPi.h>

class WiringPiRelay : public Relay {
public:
    explicit WiringPiRelay(int bcmPin) : bcmPin_(bcmPin) {
        if (wiringPiSetupGpio() == -1) {
            ok_ = false;
        } else {
            pinMode(bcmPin_, OUTPUT);
            digitalWrite(bcmPin_, LOW);
            ok_ = true;
        }
    }

    bool pulse(double seconds, bool active_high = true) override {
        if (!ok_) return false;
        int on = active_high ? HIGH : LOW;
        int off = active_high ? LOW : HIGH;
        digitalWrite(bcmPin_, on);
        delay(static_cast<unsigned int>(seconds * 1000));
        digitalWrite(bcmPin_, off);
        return true;
    }

private:
    int bcmPin_;
    bool ok_ = false;
};
