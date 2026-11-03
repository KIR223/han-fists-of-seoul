#pragma once
#include "han/render/mesh.h"

namespace han::render {
// Модели без внешних ассетов и реальных марок. Координата Y=0 — опора на дорогу.
// У машины текстурный слот 0 отведён для светящихся фар/фонарей.
MeshData makeStreetCar(core::Vec3 dimensions, core::Vec3 paint);
MeshData makeAdultCityPerson(float height, core::Vec3 coat, float stride = 0.f);
// Вход метро: ступени, ограждения, навес и световой указатель (текстурный слот 0).
MeshData makeCityMetroEntrance(core::Vec3 dimensions);
// heading города: 0 — север (-Z), положительный угол — восток (+X).
core::Mat4 streetModelTransform(core::Vec3 position, float heading);
}  // namespace han::render
