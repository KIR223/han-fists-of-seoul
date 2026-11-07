// han/ai/life_profile.h — профиль жизни города из данных (этап 06 плана города): бюджет, радиусы и политика появления трафика и толпы,
// плотность трафика по часу суток. Файл — data/city/life_profile.json; числа живут в данных, не в коде.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "han/ai/city_crowd.h"
#include "han/ai/traffic.h"

namespace han::ai {

struct LifeProfile {
    TrafficSettings traffic;
    CityCrowdSettings crowd;
    std::vector<std::pair<float, float>> trafficHours;  // (час 0..24, доля maxCars), по возрастанию часа

    // Доля maxCars в этот час: линейная интерполяция по кривой, сутки замкнуты (до первой точки и после последней — крайние значения).
    float trafficDensityAt(float hour) const;
};

// Читает и проверяет профиль. Ошибки не меняют out.
bool loadLifeProfile(const std::string& file, LifeProfile& out, std::string& error);

}  // namespace han::ai
