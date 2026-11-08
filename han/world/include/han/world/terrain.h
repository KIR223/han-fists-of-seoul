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
    // Площадка статического района с плавным переходом к исходному рельефу; физика и рендер читают одну сетку.
    bool flattenArea(float minX, float minZ, float maxX, float maxZ, float height, float blend);
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

// Раскраска дальней земли по высоте и крутизне (этап 08f): равнина, лесные склоны гор, голая скала на обрывах.
// Цвета умножаются на текстуру материала дальней земли, поэтому подбираются под неё (data/city/far_terrain.json).
struct FarTerrainPalette {
    float lowland[3] = {0.75f, 0.75f, 0.75f};
    float forest[3] = {0.40f, 0.55f, 0.32f};
    float rock[3] = {0.62f, 0.60f, 0.57f};
    float forestMinHeight = 45.f;    // выше этой отметки (м над уровнем карты) начинается лес
    float forestFullHeight = 90.f;   // с этой отметки лес в полную силу
    float rockNormalY = 0.72f;       // нормаль круче (ny меньше) — скала; между rockNormalY и rockNormalY + rockBlend — плавный переход
    float rockBlend = 0.12f;
};
bool loadFarTerrainPalette(const std::string& file, FarTerrainPalette& out, std::string& error);
// Перекрашивает вершины меша по высоте (position.y + sink) и нормали. Меш — результат buildFarTerrain.
void colorFarTerrain(GeoMesh& mesh, const FarTerrainPalette& palette, float sink = 6.f);

}  // namespace han::world
// Пример: Heightmap h; h.load("assets_bin/terrain/seoul.hht", err); float y = h.heightAt(x, z); GeoMesh m = buildTerrainCell(h, {2, 1});
