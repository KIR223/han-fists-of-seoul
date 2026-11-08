#pragma once
// Рендерер Vulkan 1.3 (E2): инстанс, устройство, swapchain, глубина, меши, dynamic rendering.
// Заголовок не тянет Vulkan и SDL: поверхность окна создаёт колбэк, который даёт платформенный слой.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/overlay.h"
#include "han/render/shadow.h"
#include "han/render/texture.h"

namespace han::render {

struct RendererDesc {
    std::vector<const char*> instanceExtensions;  // расширения для поверхности окна (от SDL)
    bool vsync = true;                            // false — IMMEDIATE/MAILBOX для замеров без привязки к частоте монитора
    bool validation = true;                       // слой VK_LAYER_KHRONOS_validation, если установлен
    bool meshStagedUpload = false;                // диагностика: не использовать окно BAR, грузить меши в память видеокарты через промежуточный буфер
    bool meshInSystemMemory = false;              // диагностика (этап 10): буферы мешей в системной памяти, как было раньше; по умолчанию — в памяти видеокарты, если она доступна процессору
    int width = 1280, height = 720;
    int shadowMapSize = 2048;  // сторона карты теней каждого из трёх каскадов (1024 / 2048 / 4096 — пресеты качества)
    float shadowBatchCellSize = 0.f;  // 0 — прежний проход; город объединяет непрозрачные тени по ячейкам
    // Создаёт поверхность окна для VkInstance (void*) и возвращает VkSurfaceKHR как uint64; 0 — ошибка.
    std::function<std::uint64_t(void* vkInstance)> createSurface;
};

using MeshId = std::uint32_t;     // 0 — «нет меша»
using TextureId = std::uint32_t;  // 0 — «нет текстуры» (рисуется белой)

struct DrawItem {
    MeshId mesh = 0;
    core::Mat4 model;
    bool castsShadow = true;
    MeshId shadowMesh = 0; // 0 — основной меш; растительность может дать более дешёвый LOD для тени
    // Палитра по столбцам, в пространстве mesh. Живёт до возврата drawFrame.
    // Один mesh можно рисовать с разными позами. Максимум 128 костей на объект.
    std::span<const core::Mat4> bones;
};

// Точечный источник (фонарь, неон): без теней, затухает до нуля на radius. В кадре учитывается до 32 источников.
struct PointLight {
    core::Vec3 position;
    core::Vec3 color;  // линейный цвет, умноженный на яркость
    float radius = 15.0f;
};

// Всё, что меняется от кадра к кадру и не относится к конкретному мешу.
struct FrameParams {
    core::Mat4 viewProjection;
    core::Vec3 cameraPosition;
    SunLight sun;
    SkyColors sky;
    float nearPlane = 0.1f, farPlane = 2000.0f;  // плоскости отсечения камеры, с которыми построена viewProjection (нужны каскадам теней)
    bool shadows = true;            // тени от солнца (без солнечного света их всё равно нет)
    ShadowSettings shadow;          // границы каскадов и дальность поиска отбрасывающих объектов; mapSize берётся из RendererDesc
    // Временное сглаживание (часть 34.4: джиттер 8 фаз, накопление 0,9). Движущиеся объекты без вектора движения сглаживаются только
    // обрезкой истории по окрестности; векторы движения — отдельный шаг.
    bool taa = true;
    float taaFeedback = 0.9f;   // доля истории в результате
    bool resetHistory = false;  // true — сбросить историю (телепорт, резкая смена камеры)
    bool debugCascades = false;     // подкрасить каскады: красный — ближний, зелёный — средний, синий — дальний
    float exposure = 1.0f;    // множитель яркости перед тонмаппингом
    float neonBoost = 1.0f;   // множитель свечения окон и вывесок ночью (настроение района)
    float windowEmission = 0.35f, signEmission = 1.6f;
    float litWindowFraction = 0.42f;
    std::vector<PointLight> pointLights;  // до 32 ближайших к камере; пусто — только солнце и небо
    float wetness = 0.0f;     // 0..1: мокрые поверхности (асфальт темнее и глянцевее; рисуется только горизонталь)
    float fogDensity = 0.0f;  // воздушная дымка: доля цвета горизонта = 1 - exp(-расстояние * плотность); 0 — выключена (0,0006: 1 км ≈ 45%)
    float fogHeightFalloff = 0.0f;  // плотность дымки убывает с высотой; 0 сохраняет прежнее поведение
    float specularAntiAliasing = 0.0f;  // фильтрация бликов по вариации нормали; 0 выключает
    float contrast = 1.2f;    // кривая AgX: степень контраста (1 — нейтрально)
    float saturation = 1.2f;  // насыщенность после AgX, который сам по себе заметно обесцвечивает яркие цвета (1 — нейтрально)
    // Bloom (часть 34.4: 6 уровней, порог 1,0, интенсивность 0,08; ночью 0,12). intensity 0 выключает свечение в итоговом кадре.
    float bloomIntensity = 0.08f;
    float bloomThreshold = 1.0f;  // яркость (в линейных единицах сцены), с которой начинается свечение
    float bloomKnee = 0.5f;       // мягкость порога
    float bloomRadius = 1.0f;     // ширина ореола
};

// Счётчики последнего drawFrame и GPU-время (измерения этапа 01 плана города).
// Основной проход считает вызовы по частям меша после отсечения; теневой — суммарно по всем каскадам.
struct FrameStats {
    std::uint32_t items = 0;
    std::uint32_t mainDrawCalls = 0, shadowDrawCalls = 0;
    std::uint64_t mainTriangles = 0, shadowTriangles = 0;
    // GPU-время кадра, чьи результаты уже готовы (кадр отстаёт на число кадров в полёте); -1 — запросы времени недоступны.
    // gpuShadowMs — каскады теней, gpuSceneMs — основной проход; gpuMs — весь кадр с постобработкой (при FIFO там может быть ожидание swapchain).
    double gpuMs = -1.0, gpuSceneMs = -1.0, gpuShadowMs = -1.0;  // gpuShadowMs — каскады теней
};

class Renderer {
public:
    // nullptr при ошибке; причина пишется в error.
    static std::unique_ptr<Renderer> create(const RendererDesc& desc, std::string& error);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Подключает наложение (nullptr — отключить). Владение остаётся у вызывающего; наложение должно жить дольше рендерера
    // или быть отключено до его уничтожения. onRendererReady вызывается сразу.
    void setOverlay(IOverlay* overlay);

    // Загружает текстуру (RGBA8 со всеми мип-уровнями) в память GPU. 0 — ошибка (в т. ч. некорректные данные).
    TextureId uploadTexture(const TextureData& texture);
    // Освобождает текстуру; ждёт простоя GPU. Меши, которые на неё ссылались, дальше рисуются белыми.
    void releaseTexture(TextureId id);

    // Загружает меш в память GPU. 0 — ошибка. textures[i] — текстура для mesh.textures[i]; нет записи или 0 — белая.
    MeshId uploadMesh(const MeshData& mesh, std::span<const TextureId> textures = {});

    // Заменяет данные существующего меша (горячая перезагрузка); MeshId остаётся прежним.
    // Старые буферы освобождаются после завершения кадров, которые могли их использовать.
    // false — id неверный или не хватило памяти; тогда прежний меш остаётся как был.
    bool replaceMesh(MeshId id, const MeshData& mesh, std::span<const TextureId> textures = {});

    // Удаляет меш из отрисовки; память освобождается после завершения использовавших её кадров.
    // Слот может сразу взять следующий uploadMesh.
    void releaseMesh(MeshId id);

    // Рисует кадр: меши с PBR при свете frame.sun и окружении frame.sky, затем небо, тонмаппинг AgX и наложение.
    // width/height — текущий размер окна в пикселях (для пересоздания swapchain).
    // false — кадр пропущен (окно свёрнуто, пересоздание swapchain) или произошла ошибка устройства.
    bool drawFrame(const FrameParams& frame, std::span<const DrawItem> items, int width, int height);

    // Снимок экрана: следующий нарисованный кадр (вместе с наложением) сохраняется в PNG (RGB, как на экране).
    // false — захват невозможен (swapchain без TRANSFER_SRC или уже есть незавершённый запрос).
    // После drawFrame() screenshotPending() == false; при ошибке записи screenshotError() непуст.
    bool requestScreenshot(const std::filesystem::path& path);
    bool screenshotPending() const;
    const std::string& screenshotError() const;

    const FrameStats& lastFrameStats() const;
    const char* presentModeName() const;  // FIFO / MAILBOX / IMMEDIATE
    const std::string& deviceName() const;
    bool validationEnabled() const;
    // (буферов в памяти видеокарты, буферов в системной памяти) среди созданных мешей.
    std::pair<std::uint32_t, std::uint32_t> meshBufferPlacement() const;
    std::uint32_t meshStagedBuffers() const;  // из них загруженных через промежуточный буфер (память видеокарты без доступа процессора)
    std::uint64_t meshVramBytes() const;
    // Память видеокарты (все её кучи) для процесса: занято и выделено системой в бюджет, МБ; valid = false, если нет VK_EXT_memory_budget.
    struct VideoMemory { bool valid = false; double usedMb = -1, budgetMb = -1; };
    VideoMemory videoMemory() const;  // сколько байт буферов мешей выделено в памяти видеокарты (накопительно)
    // Сколько сообщений уровня ERROR и WARNING выдал слой валидации (критерий E2: ноль).
    std::uint32_t validationErrors() const;
    std::uint32_t validationWarnings() const;
    // Фактический размер цели рендера (swapchain/HDR) в пикселях: сравнивается с размером окна в пикселях.
    void renderExtent(int& width, int& height) const;

private:
    Renderer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace han::render
// Пример: auto cube = r->uploadMesh(makeBox({.5f, .5f, .5f}, {1, 0, 0})); r->drawFrame(cam.viewProjection(aspect), items, w, h);
