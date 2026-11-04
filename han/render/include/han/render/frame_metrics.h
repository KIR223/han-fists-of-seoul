#pragma once
// Измерения кадра (этап 01 плана города): процентили и запись JSON. Только CPU-логика, без Vulkan.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace han::render {

struct Percentiles {
    std::size_t count = 0;
    double mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
};

// Метод «ближайший ранг»: p-й процентиль — элемент с номером ceil(p * n) (с единицы). Пустой ряд даёт нули.
inline Percentiles summarize(std::vector<double> values) {
    Percentiles r;
    values.erase(std::remove_if(values.begin(), values.end(), [](double v) { return !std::isfinite(v); }), values.end());
    if (values.empty()) return r;
    std::sort(values.begin(), values.end());
    r.count = values.size();
    double total = 0;
    for (const double v : values) total += v;
    r.mean = total / static_cast<double>(values.size());
    const auto at = [&](double p) {
        const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size())));
        return values[std::min(values.size() - 1, rank == 0 ? 0 : rank - 1)];
    };
    r.p50 = at(0.50);
    r.p95 = at(0.95);
    r.p99 = at(0.99);
    r.max = values.back();
    return r;
}

// Один кадр измерений. Поля -1 — «не измерено».
struct FrameSample {
    double wallMs = -1;      // весь кадр от начала итерации до конца drawFrame (CPU + ожидание GPU)
    double cpuPrepMs = -1;   // подготовка кадра до drawFrame: стриминг, симуляция, сбор списка рисования
    double drawMs = -1;      // сам вызов drawFrame: запись команд, отправка и ожидание завершения предыдущего кадра
    double gpuMs = -1;       // GPU-время всего кадра с постобработкой (при FIFO может включать ожидание swapchain)
    double gpuShadowMs = -1; // GPU-время каскадов теней
    double gpuSceneMs = -1;  // GPU-время основного прохода сцены (без теней и постобработки)
    double drawCalls = -1, shadowDrawCalls = -1, triangles = -1, shadowTriangles = -1, items = -1;
    double cellsLoaded = -1, cellsLoading = -1, cellRequests = -1, cellUnloads = -1;
    double cars = -1, people = -1;
    double carsWithin150 = -1, carsWithin400 = -1;  // машины в радиусе от камеры (видимость по пирамиде не считается)
    double cameraX = 0, cameraZ = 0;  // для привязки худших кадров к месту (координаты могут быть отрицательными)
    long frame = -1;                  // номер кадра с начала запуска (включая прогрев)
    double workingSetMb = -1;
};

class MetricsRecorder {
public:
    explicit MetricsRecorder(std::size_t warmupFrames) : warmup_(warmupFrames) {}
    void add(const FrameSample& sample) {
        if (seen_++ >= warmup_) samples_.push_back(sample);
    }
    // Самые долгие кадры (пики стриминга и пересборки толпы): индекс кадра, время, запросы ячеек и место камеры.
    std::vector<FrameSample> worst(std::size_t count) const {
        std::vector<FrameSample> sorted = samples_;
        std::sort(sorted.begin(), sorted.end(), [](const FrameSample& a, const FrameSample& b) { return a.wallMs > b.wallMs; });
        if (sorted.size() > count) sorted.resize(count);
        return sorted;
    }
    const std::vector<FrameSample>& samples() const { return samples_; }
    std::size_t warmup() const { return warmup_; }

    template <class Field>
    Percentiles of(Field field) const {
        std::vector<double> values;
        for (const auto& s : samples_) {
            const double v = s.*field;
            if (v >= 0) values.push_back(v);
        }
        return summarize(std::move(values));
    }

private:
    std::size_t warmup_ = 0, seen_ = 0;
    std::vector<FrameSample> samples_;
};

inline std::string jsonEscape(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) out += ' ';
        else out += c;
    }
    return out;
}

inline std::string toJson(const Percentiles& p) {
    char buffer[200];
    std::snprintf(buffer, sizeof(buffer), "{\"count\":%zu,\"mean\":%.4f,\"p50\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f}", p.count, p.mean, p.p50, p.p95, p.p99, p.max);
    return buffer;
}

// Запись сводки: метаданные сцены (пары ключ-значение, значения — строки) и процентили по каждому ряду.
inline std::string metricsToJson(const MetricsRecorder& recorder, const std::vector<std::pair<std::string, std::string>>& meta) {
    std::string out = "{\n  \"meta\": {";
    for (std::size_t i = 0; i < meta.size(); ++i)
        out += (i ? ", " : "") + std::string("\"") + jsonEscape(meta[i].first) + "\": \"" + jsonEscape(meta[i].second) + "\"";
    out += "},\n  \"warmup_frames\": " + std::to_string(recorder.warmup()) + ",\n  \"measured_frames\": " + std::to_string(recorder.samples().size());
    const auto row = [&](const char* name, double FrameSample::*field) {
        out += std::string(",\n  \"") + name + "\": " + toJson(recorder.of(field));
    };
    row("wall_ms", &FrameSample::wallMs);
    row("cpu_prep_ms", &FrameSample::cpuPrepMs);
    row("draw_ms", &FrameSample::drawMs);
    row("gpu_shadow_ms", &FrameSample::gpuShadowMs);
    row("gpu_scene_ms", &FrameSample::gpuSceneMs);
    row("gpu_total_ms", &FrameSample::gpuMs);
    row("draw_calls", &FrameSample::drawCalls);
    row("shadow_draw_calls", &FrameSample::shadowDrawCalls);
    row("triangles", &FrameSample::triangles);
    row("shadow_triangles", &FrameSample::shadowTriangles);
    row("items", &FrameSample::items);
    row("cells_loaded", &FrameSample::cellsLoaded);
    row("cells_loading", &FrameSample::cellsLoading);
    row("cell_requests_per_frame", &FrameSample::cellRequests);
    row("cell_unloads_per_frame", &FrameSample::cellUnloads);
    row("cars_simulated", &FrameSample::cars);
    row("people_drawn", &FrameSample::people);
    row("cars_within_150m", &FrameSample::carsWithin150);
    row("cars_within_400m", &FrameSample::carsWithin400);
    row("working_set_mb", &FrameSample::workingSetMb);
    out += ",\n  \"worst_frames\": [";
    const auto worst = recorder.worst(5);
    for (std::size_t i = 0; i < worst.size(); ++i) {
        char buffer[300];
        std::snprintf(buffer, sizeof(buffer), "%s{\"frame\":%ld,\"wall_ms\":%.3f,\"cpu_prep_ms\":%.3f,\"cell_requests\":%.0f,\"cells_loading\":%.0f,\"x\":%.1f,\"z\":%.1f}",
                      i ? "," : "", worst[i].frame, worst[i].wallMs, worst[i].cpuPrepMs, worst[i].cellRequests, worst[i].cellsLoading, worst[i].cameraX, worst[i].cameraZ);
        out += buffer;
    }
    out += "]\n}\n";
    return out;
}

}  // namespace han::render
