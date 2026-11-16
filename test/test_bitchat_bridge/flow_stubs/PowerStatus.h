// Host stand-in for the firmware's power status; never compiled into firmware.
#pragma once
namespace meshtastic
{
class PowerStatus
{
  public:
    bool battery = false, charging = false;
    int millivolts = 0;
    unsigned percent = 0;
    bool getHasBattery() const { return battery; }
    bool getIsCharging() const { return charging; }
    int getBatteryVoltageMv() const { return millivolts; }
    unsigned getBatteryChargePercent() const { return percent; }
};
} // namespace meshtastic
inline meshtastic::PowerStatus fakePower;
inline meshtastic::PowerStatus *powerStatus = &fakePower;
