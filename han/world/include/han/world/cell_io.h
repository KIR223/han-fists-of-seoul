// han/world/cell_io.h — запечённые ячейки города (E9/E16): CityChunk в компактный бинарный файл .hcell и обратно.
// Нужно, чтобы большой город не разбирался из OSM при каждом запуске: han_citycook строит ячейки заранее, игра читает по файлу на ячейку.
// Формат HCL1 (little-endian): "HCL1", int32 cx, cz, float32 cellSize; два меша (земля, здания): uint32 вершин, индексов, частей;
// вершина — 3 x uint16 позиция в сантиметрах (x, z от угла ячейки со смещением 10 м, y со смещением 100 м), 3 x int8 нормаль, 3 x uint8 цвет, 2 x float32 UV;
// индексы uint32; части: uint32 first, count, uint8 материал. Затем фонари (float32 x3) и коллайдеры стен (center x3, half x3, yaw).
// Точность позиции 1 см; нормали 1/127.
#pragma once
#include <string>

#include "han/world/city_builder.h"

namespace han::world {

bool saveCell(const std::string& file, CellCoord cell, float cellSize, const CityChunk& chunk, std::string& error);
// cell и cellSize возвращаются из файла; false — файл не найден, повреждён или другой версии.
bool loadCell(const std::string& file, CellCoord& cell, float& cellSize, CityChunk& chunk, std::string& error);
// Имя файла ячейки в папке: c_<x>_<z>.hcell.
std::string cellFileName(CellCoord cell);

}  // namespace han::world
// Пример: saveCell(dir + "/" + cellFileName(c), c, 256.f, chunk, err); CityChunk back; loadCell(path, c2, size, back, err);
