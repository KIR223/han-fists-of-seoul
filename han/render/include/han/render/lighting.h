#pragma once
// Освещение сцены (E4): солнце и аналитическое небо. Числа здесь — стартовая модель; таблица «время суток → солнце, цвет,
// экспозиция» из части 34.1 дизайна переедет в data/render/*.json, когда появится разбор данных (интерфейс останется прежним).
#include "han/core/math.h"

namespace han::render {

struct SunLight {
    core::Vec3 direction{0.0f, 1.0f, 0.0f};  // единичный вектор К солнцу
    core::Vec3 color{1.0f, 1.0f, 1.0f};       // линейный RGB, наибольший канал 1
    float illuminance = 0.0f;                 // сила света; освещённая белая (альбедо 1) поверхность даёт радиацию illuminance / π
};

struct SkyColors {
    core::Vec3 zenith, horizon, ground;  // яркость неба над головой, у горизонта и «земли» под горизонтом (линейный свет)
};

// Цвет чёрного тела по температуре (Кельвины, 1000–40000) в линейном RGB, нормирован на наибольший канал.
core::Vec3 colorFromTemperature(float kelvin);

// Солнце: elevation — высота над горизонтом, градусы (отрицательная — под горизонтом); azimuth — от севера (−Z) по часовой
// стрелке, градусы (90 — восток, +X). Освещённость падает до нуля ниже −4° (сумерки светит только небо).
SunLight makeSun(float elevationDegrees, float azimuthDegrees, float kelvin, float noonIlluminance = 7.5f);

// Небо под высоту солнца: день, золотой час, синий час, ночь.
SkyColors skyFromSun(const SunLight& sun);

}  // namespace han::render
// Пример: auto sun = makeSun(35.f, 160.f, 5200.f); auto sky = skyFromSun(sun); params.sun = sun; params.sky = sky;
