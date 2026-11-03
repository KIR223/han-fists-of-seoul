#pragma once
#include <string>
#include <vector>

namespace han::world {
struct CityDayKey {
    float hour = 0, elevation = 0, azimuth = 0, kelvin = 0, exposure = 0, litWindows = 0;
};
class CityDayCycle {
public:
    bool load(const std::string& file, std::string& error);
    CityDayKey sample(float hour) const;
    float startHour() const { return startHour_; }
    float secondsPerDay() const { return secondsPerDay_; }
private:
    float startHour_ = 0, secondsPerDay_ = 0;
    std::vector<CityDayKey> keys_;
};
}  // namespace han::world
