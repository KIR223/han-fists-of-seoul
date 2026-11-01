#pragma once
// Рендерер Vulkan 1.3 (E2): инстанс, устройство, swapchain, глубина, меши, dynamic rendering.
// Заголовок не тянет Vulkan и SDL: поверхность окна создаёт колбэк, который даёт платформенный слой.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "han/render/lighting.h"
#include "han/render/mesh.h"
#include "han/render/overlay.h"
#include "han/render/shadow.h"
#include "han/render/texture.h"

namespace han::render {

struct RendererDesc {
    std::vector<const char*> instanceExtensions;  // расширения для поверхности окна (от SDL)
    bool validation = true;                       // слой VK_LAYER_KHRONOS_validation, если установлен
    int width = 1280, height = 720;
    int shadowMapSize = 2048;  // сторона карты теней каждого из трёх каскадов (1024 / 2048 / 4096 — пресеты качества)
    // Создаёт поверхность окна для VkInstance (void*) и возвращает VkSurfaceKHR как uint64; 0 — ошибка.
    std::function<std::uint64_t(void* vkInstance)> createSurface;
};

using MeshId = std::uint32_t;     // 0 — «нет меша»
using TextureId = std::uint32_t;  // 0 — «нет текстуры» (рисуется белой)

struct DrawItem {
    MeshId mesh = 0;
    core::Mat4 model;
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
    float fogDensity = 0.0f;  // воздушная дымка: доля цвета горизонта = 1 - exp(-расстояние * плотность); 0 — выключена (0,0006: 1 км ≈ 45%)
    float contrast = 1.2f;    // кривая AgX: степень контраста (1 — нейтрально)
    float saturation = 1.2f;  // насыщенность после AgX, который сам по себе заметно обесцвечивает яркие цвета (1 — нейтрально)
    // Bloom (часть 34.4: 6 уровней, порог 1,0, интенсивность 0,08; ночью 0,12). intensity 0 выключает свечение в итоговом кадре.
    float bloomIntensity = 0.08f;
    float bloomThreshold = 1.0f;  // яркость (в линейных единицах сцены), с которой начинается свечение
    float bloomKnee = 0.5f;       // мягкость порога
    float bloomRadius = 1.0f;     // ширина ореола
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

    // Заменяет данные существующего меша (горячая перезагрузка); MeshId остаётся прежним. Ждёт простоя GPU.
    // false — id неверный или не хватило памяти; тогда прежний меш остаётся как был.
    bool replaceMesh(MeshId id, const MeshData& mesh, std::span<const TextureId> textures = {});

    // Освобождает память меша (стриминг ячеек). Ждёт простоя GPU; id пропускается при рисовании, а слот берёт следующий uploadMesh.
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

    const std::string& deviceName() const;
    bool validationEnabled() const;
    // Сколько сообщений уровня ERROR и WARNING выдал слой валидации (критерий E2: ноль).
    std::uint32_t validationErrors() const;
    std::uint32_t validationWarnings() const;

private:
    Renderer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace han::render
// Пример: auto cube = r->uploadMesh(makeBox({.5f, .5f, .5f}, {1, 0, 0})); r->drawFrame(cam.viewProjection(aspect), items, w, h);
