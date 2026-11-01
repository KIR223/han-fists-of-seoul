#include <SDL3/SDL.h>
#include <RmlUi/Core.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "han/render/ktx2_io.h"
#include "han/ui/rml_layer.h"

namespace han::ui {
namespace {

class SystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return static_cast<double>(SDL_GetTicksNS()) * 1e-9; }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT || type == Rml::Log::LT_WARNING) std::fprintf(stderr, "[rmlui] %s\n", message.c_str());
        return true;
    }
};

// Отрисовка через ImDrawList фона окна. Геометрия копируется при компиляции; текстуры — ImTextureData, зарегистрированные у ImGui.
class RenderInterface final : public Rml::RenderInterface {
public:
    struct Geometry {
        std::vector<Rml::Vertex> vertices;
        std::vector<int> indices;
    };

    ~RenderInterface() override { dropAllTextures(); }

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override {
        auto* g = new Geometry{{vertices.begin(), vertices.end()}, {indices.begin(), indices.end()}};
        return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { delete reinterpret_cast<Geometry*>(handle); }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) override {
        const auto* g = reinterpret_cast<const Geometry*>(handle);
        if (texture && std::getenv("HAN_RML_DEBUG")) std::fprintf(stderr, "[rml] draw tex id=%llu status=%d\n", static_cast<unsigned long long>(reinterpret_cast<ImTextureData*>(texture)->TexID), static_cast<int>(reinterpret_cast<ImTextureData*>(texture)->Status));
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImTextureRef ref;
        ImVec2 white(0.f, 0.f);
        if (texture) {
            ref = reinterpret_cast<ImTextureData*>(texture)->GetTexRef();
        } else {
            ref = ImGui::GetIO().Fonts->TexRef;  // без текстуры: белый пиксель атласа шрифтов ImGui
            white = ImGui::GetFontTexUvWhitePixel();
        }
        if (scissor_) dl->PushClipRect(clipMin_, clipMax_, false);
        dl->PushTexture(ref);
        dl->PrimReserve(static_cast<int>(g->indices.size()), static_cast<int>(g->vertices.size()));
        const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        for (int i : g->indices) dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
        for (const Rml::Vertex& v : g->vertices) {
            // RmlUi даёт предумноженный альфа-цвет, ImGui смешивает с прямым: делим обратно.
            const float a = static_cast<float>(v.colour.alpha);
            // Кадр — sRGB-поверхность, а ImGui кладёт цвет как есть: чтобы цвета из .rcss выглядели заданными, переводим sRGB → линейный.
            const auto un = [&](unsigned char c) {
                if (a <= 0.f) return 0u;
                const float srgb = std::min(1.f, static_cast<float>(c) / a);
                return static_cast<unsigned>(std::pow(srgb, 2.2f) * 255.f + 0.5f);
            };
            const ImU32 col = IM_COL32(un(v.colour.red), un(v.colour.green), un(v.colour.blue), v.colour.alpha);
            dl->PrimWriteVtx(ImVec2(v.position.x + translation.x, v.position.y + translation.y), texture ? ImVec2(v.tex_coord.x, v.tex_coord.y) : white, col);
        }
        dl->PopTexture();
        if (scissor_) dl->PopClipRect();
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        if (source.size() < 5 || source.substr(source.size() - 5) != ".ktx2") {
            std::fprintf(stderr, "[rmlui] картинки поддерживаются только в .ktx2: %s\n", source.c_str());
            return 0;
        }
        render::TextureData t;
        std::string error;
        if (!render::loadKtx2File(source, t, error)) {
            std::fprintf(stderr, "[rmlui] %s\n", error.c_str());
            return 0;
        }
        dimensions = {static_cast<int>(t.width), static_cast<int>(t.height)};
        return createTexture(t.mips[0].data(), dimensions);
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override { return createTexture(source.data(), dimensions); }
    void ReleaseTexture(Rml::TextureHandle texture) override {
        auto* t = reinterpret_cast<ImTextureData*>(texture);
        if (std::getenv("HAN_RML_DEBUG")) std::fprintf(stderr, "[rml] текстура освобождена\n");
        // Список рисования ещё может ссылаться на текстуру в этом и ближайших кадрах: настоящее уничтожение откладываем.
        pending_.push_back({t, 4});
    }
    void EnableScissorRegion(bool enable) override { scissor_ = enable; }
    void SetScissorRegion(Rml::Rectanglei r) override {
        clipMin_ = ImVec2(static_cast<float>(r.Left()), static_cast<float>(r.Top()));
        clipMax_ = ImVec2(static_cast<float>(r.Right()), static_cast<float>(r.Bottom()));
    }

    // Раз в кадр: убирает из списка ImGui текстуры, которые бэкенд уже уничтожил.
    void collectGarbage() {
        for (std::size_t i = 0; i < pending_.size();) {
            if (--pending_[i].framesLeft > 0) {
                ++i;
                continue;
            }
            ImTextureData* t = pending_[i].texture;
            t->WantDestroyNextFrame = true;
            t->Status = ImTextureStatus_WantDestroy;
            dying_.push_back(t);
            pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
        }
        for (std::size_t i = 0; i < dying_.size();) {
            ImTextureData* t = dying_[i];
            if (t->Status == ImTextureStatus_Destroyed) {
                ImGui::UnregisterUserTexture(t);
                delete t;
                dying_.erase(dying_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
    }
    int liveTextures() const { return live_; }

private:
    Rml::TextureHandle createTexture(const Rml::byte* rgba, Rml::Vector2i size) {
        auto* t = new ImTextureData();
        t->Create(ImTextureFormat_RGBA32, size.x, size.y);
        std::memcpy(t->GetPixels(), rgba, static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) * 4);
        t->UseColors = true;
        t->Status = ImTextureStatus_WantCreate;
        ImGui::RegisterUserTexture(t);
        ++live_;
        if (std::getenv("HAN_RML_DEBUG")) std::fprintf(stderr, "[rml] текстура %dx%d создана (всего %d)\n", size.x, size.y, live_);
        return reinterpret_cast<Rml::TextureHandle>(t);
    }
    void dropAllTextures() {
        // Контекст ImGui может быть уже уничтожен: тогда текстуры освобождает он сам, у нас только указатели.
        for (ImTextureData* t : dying_) delete t;
        dying_.clear();
    }

    bool scissor_ = false;
    ImVec2 clipMin_{0, 0}, clipMax_{0, 0};
    struct Pending {
        ImTextureData* texture;
        int framesLeft;
    };
    std::vector<Pending> pending_;        // освобождены RmlUi, ждут нескольких кадров
    std::vector<ImTextureData*> dying_;   // помечены на уничтожение, ждут бэкенда ImGui
    int live_ = 0;
};

}  // namespace

struct RmlLayer::Impl {
    SystemInterface system;
    RenderInterface render;
    Rml::Context* context = nullptr;
    std::vector<Rml::ElementDocument*> documents;
    bool initialised = false;
    bool leftWasDown = false;
    std::string clicked;
};

namespace {
// Клики по элементам: слушатель на документе ловит всплывшее событие click и запоминает id элемента (или ближайшего предка с id).
class ClickListener final : public Rml::EventListener {
public:
    explicit ClickListener(std::string* target) : target_(target) {}
    void ProcessEvent(Rml::Event& event) override {
        for (Rml::Element* e = event.GetTargetElement(); e; e = e->GetParentNode()) {
            if (e->GetId().rfind("btn-", 0) == 0) {  // кнопками считаются элементы с id, начинающимся на «btn-»
                *target_ = e->GetId();
                return;
            }
        }
    }
    void OnDetach(Rml::Element*) override {}

private:
    std::string* target_;
};
std::unique_ptr<ClickListener> gListener;
}  // namespace

RmlLayer::RmlLayer() : impl_(std::make_unique<Impl>()) {}

RmlLayer::~RmlLayer() {
    Impl& s = *impl_;
    if (s.initialised) {
        s.documents.clear();
        Rml::RemoveContext("han");
        s.context = nullptr;
        Rml::Shutdown();
        gListener.reset();
    }
}

bool RmlLayer::init(int width, int height) {
    Impl& s = *impl_;
    Rml::SetSystemInterface(&s.system);
    Rml::SetRenderInterface(&s.render);
    if (!Rml::Initialise()) return false;
    s.initialised = true;
    s.context = Rml::CreateContext("han", Rml::Vector2i(width, height));
    if (!s.context) return false;
    gListener = std::make_unique<ClickListener>(&s.clicked);
    return true;
}

bool RmlLayer::loadFont(const std::string& path, bool fallback) { return Rml::LoadFontFace(path, fallback); }

bool RmlLayer::loadDocument(const std::string& path) {
    Impl& s = *impl_;
    if (!s.context) return false;
    Rml::ElementDocument* doc = s.context->LoadDocument(path);
    if (!doc) return false;
    doc->AddEventListener("click", gListener.get());
    doc->Show();
    s.documents.push_back(doc);
    return true;
}

void RmlLayer::closeAll() {
    Impl& s = *impl_;
    for (Rml::ElementDocument* d : s.documents) d->Close();
    s.documents.clear();
}

void RmlLayer::beginFrame(int width, int height) {
    Impl& s = *impl_;
    if (!s.context) return;
    s.render.collectGarbage();
    if (s.context->GetDimensions() != Rml::Vector2i(width, height)) s.context->SetDimensions(Rml::Vector2i(width, height));
    float x = 0.f, y = 0.f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&x, &y);
    s.context->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
    const bool down = (buttons & SDL_BUTTON_LMASK) != 0;
    if (down && !s.leftWasDown) s.context->ProcessMouseButtonDown(0, 0);
    if (!down && s.leftWasDown) s.context->ProcessMouseButtonUp(0, 0);
    s.leftWasDown = down;
    s.context->Update();
}

void RmlLayer::draw() {
    if (impl_->context) impl_->context->Render();
}

bool RmlLayer::wantsMouse() const { return impl_->context && impl_->context->GetHoverElement() && impl_->context->GetHoverElement() != impl_->context->GetRootElement(); }

std::string RmlLayer::takeClicked() {
    std::string out;
    out.swap(impl_->clicked);
    return out;
}

bool RmlLayer::setText(const std::string& elementId, const std::string& text) {
    for (Rml::ElementDocument* d : impl_->documents) {
        if (Rml::Element* e = d->GetElementById(elementId)) {
            e->SetInnerRML(text);
            return true;
        }
    }
    return false;
}

void RmlLayer::injectMouse(int x, int y, bool leftDown) {
    Impl& s = *impl_;
    if (!s.context) return;
    s.context->Update();
    s.context->ProcessMouseMove(x, y, 0);
    if (leftDown && !s.leftWasDown) s.context->ProcessMouseButtonDown(0, 0);
    if (!leftDown && s.leftWasDown) s.context->ProcessMouseButtonUp(0, 0);
    s.leftWasDown = leftDown;
    s.context->Update();
}

bool RmlLayer::elementCenter(const std::string& elementId, int& x, int& y) const {
    for (Rml::ElementDocument* d : impl_->documents) {
        if (Rml::Element* e = d->GetElementById(elementId)) {
            const Rml::Vector2f o = e->GetAbsoluteOffset(Rml::BoxArea::Border), size = e->GetBox().GetSize(Rml::BoxArea::Border);
            x = static_cast<int>(o.x + size.x * 0.5f);
            y = static_cast<int>(o.y + size.y * 0.5f);
            return true;
        }
    }
    return false;
}

int RmlLayer::count(const std::string& cssSelector) const {
    int n = 0;
    for (Rml::ElementDocument* d : impl_->documents) {
        Rml::ElementList list;
        d->QuerySelectorAll(list, cssSelector);
        n += static_cast<int>(list.size());
    }
    return n;
}

}  // namespace han::ui
