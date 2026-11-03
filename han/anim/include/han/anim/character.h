#pragma once
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "han/render/mesh.h"

namespace han::anim {
// HCH1 — офлайн-экспорт Blender из GLB. Skeleton и клипы разделяются всеми бойцами.
class Character {
public:
    Character();
    ~Character();
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;
    bool load(const std::string& path, std::string& error);
    const render::MeshData& mesh() const;
    std::vector<std::string> clips() const;
    float duration(const std::string& clip) const;
    int boneCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class Pose;
};

// Character должен жить дольше всех его Pose и не перезагружаться при живых Pose.
// Разные Pose используют независимые
// sampling context и палитры: у экземпляров одного mesh нет общей изменяемой позы.
class Pose {
public:
    explicit Pose(const Character& character);
    ~Pose();
    Pose(const Pose&) = delete;
    Pose& operator=(const Pose&) = delete;
    bool sample(const std::string& clip, float seconds, float deltaSeconds,
                float fadeSeconds = 0.1f, bool loop = true,
                const std::string& lowerBodyClip = "", float lowerBodyPhase = 0, float lowerBodyWeight = 0);
    std::span<const core::Mat4> palette() const;
    // Контактный IK руки в пространстве mesh. Дальность ограничена длиной костей;
    // цепь не растягивается и не меняет боевые события/урон.
    bool reachHand(bool left, core::Vec3 target, float weight);
    bool jointTransform(const std::string& name, core::Mat4& out) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace han::anim
