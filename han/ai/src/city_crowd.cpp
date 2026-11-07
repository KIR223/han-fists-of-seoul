#include "han/ai/city_crowd.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace han::ai {

namespace {
// Контуры, пересекающие квадрат сетки, в координатах навигации; остальное в сетку не нужно (и не копируется).
void gather(const std::vector<std::vector<world::Vec2>>& polygons, float minX, float minZ, float maxX, float maxZ, std::vector<std::vector<Vec2>>& out) {
    for (const auto& poly : polygons) {
        float lox = 1e30f, hix = -1e30f, loz = 1e30f, hiz = -1e30f;
        for (const auto& p : poly) {
            lox = std::min(lox, p.x);
            hix = std::max(hix, p.x);
            loz = std::min(loz, p.y);
            hiz = std::max(hiz, p.y);
        }
        if (hix < minX || lox > maxX || hiz < minZ || loz > maxZ) continue;
        std::vector<Vec2> converted;
        converted.reserve(poly.size());
        for (const auto& p : poly) converted.push_back({p.x, p.y});
        out.push_back(std::move(converted));
    }
}
}  // namespace

CityCrowd::CityCrowd(const std::vector<std::vector<world::Vec2>>& buildings, const world::WalkLayout& layout, std::vector<const Archetype*> archetypes, const CityCrowdSettings& settings)
    : buildings_(buildings), layout_(layout), archetypes_(std::move(archetypes)), settings_(settings) {}

void CityCrowd::rebuild(Vec2 camera, Vec2 direction) {
    const auto start = std::chrono::steady_clock::now();
    const float radius = settings_.navRadius;
    const float minX = camera.x - radius, minZ = camera.z - radius, maxX = camera.x + radius, maxZ = camera.z + radius;
    std::vector<std::vector<Vec2>> hard, roads, openings;
    gather(buildings_, minX, minZ, maxX, maxZ, hard);
    gather(layout_.water, minX, minZ, maxX, maxZ, hard);
    gather(layout_.carriageways, minX, minZ, maxX, maxZ, roads);
    gather(layout_.crossings, minX, minZ, maxX, maxZ, openings);
    NavGrid::Settings navSettings;
    navSettings.cellSize = settings_.navCell;
    navSettings.maxExpansions = settings_.pathExpansions;
    grid_ = NavGrid::build(minX, minZ, maxX, maxZ, hard, roads, openings, navSettings);  // тот же объект: Crowd держит на него ссылку
    center_ = camera;
    built_ = true;
    ++stats_.rebuilds;
    stats_.lastRecycled = 0;
    if (!grid_.valid() || archetypes_.empty()) return;

    static const char* categories[] = {"work", "cafe", "shop", "eat", "park", "campus", "market", "nightlife", "sights", "delivery", "security"};
    std::vector<Poi> pois, nearby;
    for (std::size_t i = 0; i < buildings_.size(); ++i) {
        const auto& fp = buildings_[i];
        if (fp.size() < 2) continue;
        const Vec2 edge{(fp[0].x + fp[1].x) * 0.5f, (fp[0].y + fp[1].y) * 0.5f};
        if (std::fabs(edge.x - camera.x) > radius || std::fabs(edge.z - camera.z) > radius) continue;
        Vec2 door;
        if (!grid_.nearestWalkable(edge, 12.f, door)) continue;
        pois.push_back({door, categories[i % std::size(categories)]});
        const float dx = door.x - camera.x, dz = door.z - camera.z;
        if (dx * dx + dz * dz < settings_.spawnRadius * settings_.spawnRadius) nearby.push_back(pois.back());
    }
    stats_.lastPois = pois.size();

    CrowdSettings crowdSettings;
    crowdSettings.maxAgents = settings_.agentCount;
    crowdSettings.maxPathQueriesPerFrame = settings_.pathQueriesPerFrame;
    crowdSettings.seed = settings_.seed;
    if (!crowd_ || !settings_.keepAgents) {
        if (nearby.empty()) {
            crowd_.reset();
        } else {
            crowd_ = std::make_unique<Crowd>(grid_, std::move(pois), crowdSettings);
            for (std::size_t i = 0; i < settings_.agentCount; ++i) {
                const auto& place = nearby[(i * 73) % nearby.size()];
                const float jitterX = static_cast<float>((i * 17) % 11 - 5) * 0.7f, jitterZ = static_cast<float>((i * 23) % 13 - 6) * 0.7f;
                crowd_->spawn(*archetypes_[i % archetypes_.size()], {place.pos.x + jitterX, place.pos.z + jitterZ});
            }
        }
    } else {
        Crowd::RebaseView view;
        view.camera = camera;
        view.direction = direction;
        view.minDistance = settings_.minRecycleDistance;
        const float half = settings_.viewHalfAngleDegrees * 0.01745329f;
        view.viewCos = std::cos(half);
        view.farVisible = settings_.farVisible;
        stats_.lastRecycled = crowd_->rebase(std::move(pois), view);
        stats_.recycled += stats_.lastRecycled;
    }
    stats_.lastRebuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void CityCrowd::update(float dt, Vec2 camera, Vec2 direction, float hour) {
    const float dx = camera.x - center_.x, dz = camera.z - center_.z;
    const float limit = settings_.navRadius * settings_.recenterFraction;
    if (!built_ || dx * dx + dz * dz > limit * limit) rebuild(camera, direction);
    if (!crowd_) return;
    if (settings_.treadmill && settings_.keepAgents) {
        treadmillTimer_ += dt;
        if (treadmillTimer_ >= settings_.treadmillInterval) {
            treadmillTimer_ = 0.f;
            Crowd::RebaseView view;
            view.camera = camera;
            view.direction = direction;
            view.minDistance = settings_.minRecycleDistance;
            view.viewCos = std::cos(settings_.viewHalfAngleDegrees * 0.01745329f);
            view.farVisible = settings_.farVisible;
            stats_.recycled += crowd_->recycleFar(view, settings_.treadmillMinDistance * settings_.navRadius, settings_.treadmillBatch);
        }
    }
    crowd_->setHour(hour);
    crowd_->update(std::min(dt, 0.1f), camera);
}

}  // namespace han::ai
