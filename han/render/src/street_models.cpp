#include "han/render/street_models.h"
#include <cmath>
#include <algorithm>

namespace han::render {
core::Mat4 streetModelTransform(core::Vec3 position, float heading) {
    return core::Mat4::translation(position) * core::Mat4::rotationY(-heading);
}
namespace {
void groupMaterials(MeshData& mesh) {
    const auto parts = mesh.submeshes;
    std::vector<Submesh> groups;
    for (const auto& part : parts) {
        const auto equal = [&](const Submesh& g) { return g.texture == part.texture && g.metallic == part.metallic && g.roughness == part.roughness; };
        if (std::find_if(groups.begin(), groups.end(), equal) == groups.end()) groups.push_back(part);
    }
    std::vector<std::uint32_t> indices;
    for (auto& group : groups) {
        group.firstIndex = static_cast<std::uint32_t>(indices.size());
        for (const auto& part : parts)
            if (group.texture == part.texture && group.metallic == part.metallic && group.roughness == part.roughness)
                indices.insert(indices.end(), mesh.indices.begin() + part.firstIndex, mesh.indices.begin() + part.firstIndex + part.indexCount);
        group.indexCount = static_cast<std::uint32_t>(indices.size()) - group.firstIndex;
    }
    mesh.indices = std::move(indices);
    mesh.submeshes = std::move(groups);
}
void append(MeshData& target, MeshData part, core::Vec3 pos, core::Vec3 scale, float metallic, float roughness, int texture = -1) {
    const auto base = static_cast<std::uint32_t>(target.vertices.size());
    const auto first = static_cast<std::uint32_t>(target.indices.size());
    for (auto& v : part.vertices) {
        v.position[0] = v.position[0] * scale.x + pos.x;
        v.position[1] = v.position[1] * scale.y + pos.y;
        v.position[2] = v.position[2] * scale.z + pos.z;
        float nx = v.normal[0] / scale.x, ny = v.normal[1] / scale.y, nz = v.normal[2] / scale.z;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        v.normal[0] = nx / length; v.normal[1] = ny / length; v.normal[2] = nz / length;
        target.vertices.push_back(v);
    }
    for (const auto i : part.indices) target.indices.push_back(base + i);
    target.submeshes.push_back({first, static_cast<std::uint32_t>(part.indices.size()), texture, metallic, roughness});
}
}  // namespace

MeshData makeStreetCar(core::Vec3 d, core::Vec3 paint) {
    MeshData result;
    if (!(d.x > 0 && d.y > 0 && d.z > 0) || !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) return result;
    result.textures = {"vehicle_lights"};
    const auto box = [&](core::Vec3 half, core::Vec3 pos, core::Vec3 color, float metallic, float roughness, int texture = -1) {
        append(result, makeBox(half, color), pos, {1, 1, 1}, metallic, roughness, texture);
    };
    box({d.x * 0.48f, d.y * 0.19f, d.z * 0.47f}, {0, d.y * 0.36f, 0}, paint, 0.65f, 0.32f);
    auto cabin = makeBox({d.x * 0.41f, d.y * 0.22f, d.z * 0.25f}, {0.075f, 0.11f, 0.14f});
    for (auto& v : cabin.vertices) if (v.position[1] > 0) { v.position[0] *= 0.84f; v.position[2] *= 0.65f; }
    for (std::size_t face = 0; face + 3 < cabin.vertices.size(); face += 4) {
        const auto& a = cabin.vertices[face]; const auto& b = cabin.vertices[face + 1]; const auto& c = cabin.vertices[face + 2];
        const core::Vec3 ab{b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
        const core::Vec3 ac{c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]};
        const auto normal = core::normalize(core::cross(ab, ac));
        for (std::size_t k = face; k < face + 4; ++k) {
            cabin.vertices[k].normal[0] = normal.x; cabin.vertices[k].normal[1] = normal.y; cabin.vertices[k].normal[2] = normal.z;
        }
    }
    append(result, std::move(cabin), {0, d.y * 0.72f, d.z * 0.025f}, {1, 1, 1}, 0.35f, 0.12f);
    box({d.x * 0.35f, d.y * 0.035f, d.z * 0.17f}, {0, d.y * 0.96f, d.z * 0.025f}, paint, 0.65f, 0.32f);
    for (const float side : {-1.f, 1.f}) {
        box({d.x * 0.05f, d.y * 0.045f, d.z * 0.045f}, {side * d.x * 0.52f, d.y * 0.67f, -d.z * 0.20f}, paint, 0.6f, 0.32f);
        box({d.x * 0.13f, d.y * 0.045f, d.z * 0.007f}, {side * d.x * 0.30f, d.y * 0.44f, -d.z * 0.477f}, {1.f, 0.98f, 0.9f}, 0.f, 0.2f, 0);
        box({d.x * 0.13f, d.y * 0.045f, d.z * 0.007f}, {side * d.x * 0.30f, d.y * 0.44f, d.z * 0.477f}, {0.65f, 0.025f, 0.02f}, 0.f, 0.2f, 0);
        for (const float axle : {-1.f, 1.f}) {
            append(result, makeSphere(1.f, 10, 6, {0.045f, 0.045f, 0.05f}), {side * d.x * 0.45f, d.y * 0.21f, axle * d.z * 0.30f}, {d.x * 0.075f, d.y * 0.21f, d.y * 0.21f}, 0.f, 0.92f);
            append(result, makeSphere(1.f, 10, 5, {0.5f, 0.52f, 0.54f}), {side * d.x * 0.50f, d.y * 0.21f, axle * d.z * 0.30f}, {d.x * 0.03f, d.y * 0.12f, d.y * 0.12f}, 0.8f, 0.3f);
        }
    }
    box({d.x * 0.24f, d.y * 0.055f, d.z * 0.008f}, {0, d.y * 0.31f, -d.z * 0.48f}, {0.055f, 0.06f, 0.065f}, 0.25f, 0.6f);
    groupMaterials(result);
    return result;
}

MeshData makeCityMetroEntrance(core::Vec3 d) {
    MeshData result;
    if (!(d.x > 0 && d.y > 0 && d.z > 0) || !std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) return result;
    result.textures = {"metro_line_sign"};
    const auto box = [&](core::Vec3 half, core::Vec3 pos, core::Vec3 color, int texture = -1, float metallic = 0.6f) {
        append(result, makeBox(half, color), pos, {1, 1, 1}, metallic, 0.4f, texture);
    };
    // Открытый вход вместо сплошного зелёного куба: ограждения не закрывают вид на улицу.
    for (const float side : {-1.f, 1.f}) {
        for (int post = 0; post < 4; ++post) {
            const float z = d.z * (-0.42f + static_cast<float>(post) * 0.28f);
            box({0.035f, d.y * 0.20f, 0.035f}, {side * d.x * 0.46f, d.y * 0.20f, z}, {0.5f, 0.52f, 0.53f});
        }
        for (const float rail : {0.22f, 0.40f})
            box({0.035f, 0.035f, d.z * 0.44f}, {side * d.x * 0.46f, d.y * rail, 0}, {0.5f, 0.52f, 0.53f});
        box({0.045f, d.y * 0.48f, 0.045f}, {side * d.x * 0.46f, d.y * 0.48f, d.z * 0.36f}, {0.32f, 0.35f, 0.36f});
    }
    box({d.x * 0.52f, d.y * 0.025f, d.z * 0.45f}, {0, d.y * 0.97f, 0}, {0.18f, 0.26f, 0.29f});
    box({d.x * 0.52f, d.y * 0.045f, 0.045f}, {0, d.y * 0.91f, -d.z * 0.44f}, {0.0f, 0.58f, 0.28f});
    for (int step = 0; step < 3; ++step) {
        const float height = d.y * 0.055f * static_cast<float>(step + 1);
        box({d.x * 0.43f, height * 0.5f, d.z * 0.12f}, {0, height * 0.5f, d.z * (-0.42f + static_cast<float>(step) * 0.24f)}, {0.47f, 0.48f, 0.46f}, -1, 0.f);
    }
    box({0.045f, d.y * 0.5f, 0.045f}, {-d.x * 0.62f, d.y * 0.5f, -d.z * 0.35f}, {0.25f, 0.28f, 0.29f});
    box({d.x * 0.09f, d.y * 0.32f, 0.055f}, {-d.x * 0.62f, d.y * 0.66f, -d.z * 0.35f}, {1, 1, 1}, 0, 0.f);
    groupMaterials(result);
    return result;
}

MeshData makeAdultCityPerson(float height, core::Vec3 coat, float stride) {
    MeshData result;
    if (!(height > 0.f) || !std::isfinite(height) || !std::isfinite(stride)) return result;
    const auto box = [&](core::Vec3 half, core::Vec3 pos, core::Vec3 color) {
        append(result, makeBox(half, color), pos, {height, height, height}, 0.f, 0.88f);
    };
    // Все части пропорциональны взрослому росту; детских вариантов в игре нет.
    append(result, makeSphere(1.f, 10, 7, {0.61f, 0.45f, 0.34f}), {0, height * 0.92f, 0}, {height * 0.065f, height * 0.08f, height * 0.065f}, 0.f, 0.85f);
    append(result, makeSphere(1.f, 10, 5, {0.065f, 0.055f, 0.05f}), {0, height * 0.965f, height * 0.008f}, {height * 0.065f, height * 0.04f, height * 0.06f}, 0.f, 0.96f);
    box({0.115f, 0.15f, 0.065f}, {0, height * 0.67f, 0}, coat);
    box({0.10f, 0.07f, 0.065f}, {0, height * 0.47f, 0}, {0.12f, 0.13f, 0.15f});
    for (const float side : {-1.f, 1.f}) {
        const auto rotateLimb = [&](std::size_t begin, float pivot, float angle) {
            const float c = std::cos(angle), s = std::sin(angle);
            for (std::size_t i = begin; i < result.vertices.size(); ++i) {
                auto& v = result.vertices[i];
                const float y = v.position[1] - pivot, z = v.position[2];
                const float ny = v.normal[1], nz = v.normal[2];
                v.position[1] = pivot + y * c - z * s;
                v.position[2] = y * s + z * c;
                v.normal[1] = ny * c - nz * s;
                v.normal[2] = ny * s + nz * c;
            }
        };
        const auto arm = result.vertices.size();
        box({0.035f, 0.16f, 0.035f}, {side * height * 0.15f, height * 0.64f, 0}, coat);
        rotateLimb(arm, height * 0.80f, -side * stride * 0.28f);
        const auto leg = result.vertices.size();
        box({0.04f, 0.19f, 0.045f}, {side * height * 0.057f, height * 0.24f, 0}, {0.11f, 0.13f, 0.17f});
        box({0.045f, 0.035f, 0.075f}, {side * height * 0.057f, height * 0.035f, -height * 0.02f}, {0.075f, 0.065f, 0.06f});
        rotateLimb(leg, height * 0.43f, side * stride * 0.35f);
    }
    float minimum = height;
    for (const auto& v : result.vertices) minimum = std::min(minimum, v.position[1]);
    for (auto& v : result.vertices) v.position[1] -= minimum;
    groupMaterials(result);
    return result;
}
}  // namespace han::render
