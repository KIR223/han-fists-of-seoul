// han/world/terrain.h — рельеф карты (E17): сетка высот в метрах карты (из тайлов Terrarium, han_terrain_fetch.py) и сетки земли по ячейкам.
// Файл .hht: "HHT1", int32 ширина, высота, float32 minX, minZ, шаг, затем float32 высоты (м над уровнем моря), строки по z.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "han/world/geometry.h"
#include "han/world/streaming.h"

namespace han::world {

class Heightmap {
public:
    bool load(const std::string& file, std::string& error);
    // Сетка из готовых значений (тесты, генераторы): строки по z, шаг cell метров.
    static Heightmap fromGrid(float minX, float minZ, float cell, int width, int height, std::vector<float> heights);

    bool valid() const { return width_ > 1 && height_ > 1; }
    float minX() const { return minX_; }
    float minZ() const { return minZ_; }
    float maxX() const { return minX_ + static_cast<float>(width_ - 1) * cell_; }
    float maxZ() const { return minZ_ + static_cast<float>(height_ - 1) * cell_; }
    float cell() const { return cell_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Билинейная высота; за пределами сетки — значение у края (земля не обрывается).
    float heightAt(float x, float z) const;
    // Единичная нормаль по градиенту.
    void normalAt(float x, float z, float& nx, float& ny, float& nz) const;
    float minHeight() const { return minH_; }
    float maxHeight() const { return maxH_; }

private:
    float sample(int i, int j) const { return data_[static_cast<std::size_t>(j) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(i)]; }
    float minX_ = 0, minZ_ = 0, cell_ = 25.f, minH_ = 0, maxH_ = 0;
    int width_ = 0, height_ = 0;
    std::vector<float> data_;
};

struct TerrainSettings {
    float cellSize = 256.f;   // как у ячеек города
    float spacing = 8.f;      // шаг вершин земли, м (сетка высот 25 м, интерполяция внутри клетки гладкая)
    float sink = 0.35f;       // земля чуть ниже дорог и тротуаров, чтобы не было z-борьбы
    float uvTile = 4.f;       // метров на плитку текстуры земли
};

// Сетка земли для одной ячейки (материал Terrain). Пустая, если ячейка вне рельефа.
GeoMesh buildTerrainCell(const Heightmap& map, CellCoord cell, const TerrainSettings& settings = {});

// Дальний план (E17): вся карта одной грубой сеткой (шаг 128 м), опущенной на sink метров, чтобы не торчать сквозь ближнюю землю и город.
// Рисуется всегда (отсекается по кадру); за радиусом загрузки ячеек именно она показывает холмы и горы у горизонта.
GeoMesh buildFarTerrain(const Heightmap& map, float spacing = 128.f, float sink = 6.f, float uvTile = 64.f);

}  // namespace han::world
// Пример: Heightmap h; h.load("assets_bin/terrain/seoul.hht", err); float y = h.heightAt(x, z); GeoMesh m = buildTerrainCell(h, {2, 1});
