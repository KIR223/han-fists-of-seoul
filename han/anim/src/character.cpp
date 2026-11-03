#include "han/anim/character.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/transform.h>

namespace han::anim {
namespace {
static_assert(std::endian::native == std::endian::little);
struct Reader {
    std::vector<char> bytes;
    std::size_t at = 0;
    template<class T> T read() {
        if (sizeof(T) > bytes.size() - at) throw std::runtime_error("HCH1 обрезан");
        T v; std::memcpy(&v, bytes.data() + at, sizeof v); at += sizeof v; return v;
    }
    float scalar() { const float f = read<float>(); if (!std::isfinite(f)) throw std::runtime_error("HCH1 содержит NaN/Inf"); return f; }
    std::string string() {
        const auto n = read<std::uint32_t>();
        if (n > 256 || n > bytes.size() - at) throw std::runtime_error("неверное имя HCH1");
        std::string s(bytes.data() + at, n); at += n; return s;
    }
    ozz::math::Transform transform() {
        ozz::math::Transform t;
        t.translation = {scalar(), scalar(), scalar()};
        t.rotation = {scalar(), scalar(), scalar(), scalar()};
        t.scale = {scalar(), scalar(), scalar()};
        const auto& q = t.rotation;
        const float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
        if (std::fabs(norm - 1.f) > 0.002f || t.scale.x <= 0 || t.scale.y <= 0 || t.scale.z <= 0)
            throw std::runtime_error("неверный TRS HCH1");
        return t;
    }
};
}
struct Character::Impl {
    render::MeshData mesh;
    ozz::unique_ptr<ozz::animation::Skeleton> skeleton;
    std::map<std::string, ozz::unique_ptr<ozz::animation::Animation>> clips;
    std::vector<core::Mat4> inverseBind;
};
Character::Character() : impl_(std::make_unique<Impl>()) {}
Character::~Character() = default;
bool Character::load(const std::string& path, std::string& error) {
    try {
        auto data = std::make_unique<Impl>();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto size = file.tellg();
        if (!file || size < 20 || size > 256 * 1024 * 1024) throw std::runtime_error("HCH1 не найден или неверный размер");
        Reader r; r.bytes.resize(static_cast<std::size_t>(size)); file.seekg(0);
        if (!file.read(r.bytes.data(), size)) throw std::runtime_error("HCH1 не прочитан");
        if (r.read<std::uint32_t>() != 0x31484348) throw std::runtime_error("неверный magic HCH1");
        const auto nb = r.read<std::uint32_t>(), nv = r.read<std::uint32_t>(), ni = r.read<std::uint32_t>(), nc = r.read<std::uint32_t>();
        if (!nb || nb > 128 || !nv || nv > 1000000 || !ni || ni > 3000000 || ni % 3 || !nc || nc > 256)
            throw std::runtime_error("превышены лимиты HCH1");
        struct Bone { std::string name; int parent; ozz::math::Transform rest; };
        std::vector<Bone> bones;
        data->inverseBind.resize(nb);
        for (std::uint32_t i = 0; i < nb; ++i) {
            Bone b; b.name = r.string(); b.parent = r.read<std::int32_t>(); b.rest = r.transform();
            if (b.name.empty() || b.parent < -1 || b.parent >= static_cast<int>(i)) throw std::runtime_error("неверная иерархия HCH1");
            for (const auto& prev : bones) if (prev.name == b.name) throw std::runtime_error("повтор кости");
            bones.push_back(std::move(b));
            for (float& v : data->inverseBind[i].m) v = r.scalar();
        }
        ozz::animation::offline::RawSkeleton rawSkeleton;
        auto build = [&](auto&& self, int parent, auto& children) -> void {
            for (std::uint32_t i = 0; i < nb; ++i) if (bones[i].parent == parent) {
                children.emplace_back(); auto& joint = children.back();
                joint.name = bones[i].name.c_str(); joint.transform = bones[i].rest;
                self(self, static_cast<int>(i), joint.children);
            }
        };
        build(build, -1, rawSkeleton.roots);
        data->skeleton = ozz::animation::offline::SkeletonBuilder()(rawSkeleton);
        if (!data->skeleton || data->skeleton->num_joints() != static_cast<int>(nb)) throw std::runtime_error("ozz skeleton build failed");
        for (std::uint32_t i = 0; i < nb; ++i)
            if (bones[i].name != data->skeleton->joint_names()[i]) throw std::runtime_error("HCH1 должен хранить кости в DFS-порядке");
        data->mesh.vertices.resize(nv); data->mesh.skin.resize(nv);
        for (std::uint32_t i = 0; i < nv; ++i) {
            auto& v = data->mesh.vertices[i];
            for (float& f : v.position) f = r.scalar();
            for (float& f : v.normal) f = r.scalar();
            for (float& f : v.color) f = r.scalar();
            for (float& f : v.uv) f = r.scalar();
            for (auto& j : data->mesh.skin[i].joints) { j = r.read<std::uint32_t>(); if (j >= nb) throw std::runtime_error("joint index вне skin"); }
            float sum = 0;
            for (float& w : data->mesh.skin[i].weights) { w = r.scalar(); if (w < 0 || w > 1) throw std::runtime_error("неверный вес"); sum += w; }
            if (std::fabs(sum - 1.f) > 0.001f) throw std::runtime_error("веса не нормализованы");
        }
        data->mesh.indices.resize(ni);
        for (auto& i : data->mesh.indices) { i = r.read<std::uint32_t>(); if (i >= nv) throw std::runtime_error("index вне mesh"); }
        data->mesh.submeshes = {{0, ni, -1, 0.f, 0.75f}};
        for (std::uint32_t clip = 0; clip < nc; ++clip) {
            auto name = r.string(); const auto samples = r.read<std::uint32_t>(); const float duration = r.scalar();
            if (name.empty() || data->clips.contains(name) || samples < 2 || samples > 18000 || duration <= 0 || duration > 600)
                throw std::runtime_error("неверный клип HCH1");
            if (std::size_t(samples) * nb * 10 * sizeof(float) > r.bytes.size() - r.at)
                throw std::runtime_error("обрезанные ключи HCH1");
            ozz::animation::offline::RawAnimation raw; raw.name = name.c_str(); raw.duration = duration; raw.tracks.resize(nb);
            for (std::uint32_t sample = 0; sample < samples; ++sample) {
                const float t = sample + 1 == samples ? duration : duration * (static_cast<float>(sample) / static_cast<float>(samples - 1));
                for (auto& track : raw.tracks) {
                    const auto tr = r.transform();
                    track.translations.push_back({t, tr.translation}); track.rotations.push_back({t, tr.rotation}); track.scales.push_back({t, tr.scale});
                }
            }
            auto animation = ozz::animation::offline::AnimationBuilder()(raw);
            if (!animation) throw std::runtime_error("ozz animation build failed: " + name);
            data->clips.emplace(std::move(name), std::move(animation));
        }
        if (r.at != r.bytes.size()) throw std::runtime_error("лишние данные HCH1");
        impl_ = std::move(data); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
const render::MeshData& Character::mesh() const { return impl_->mesh; }
int Character::boneCount() const { return impl_->skeleton ? impl_->skeleton->num_joints() : 0; }
std::vector<std::string> Character::clips() const { std::vector<std::string> names; for (const auto& [n, _] : impl_->clips) names.push_back(n); return names; }
float Character::duration(const std::string& name) const { auto i = impl_->clips.find(name); return i == impl_->clips.end() ? 0 : i->second->duration(); }

struct Pose::Impl {
    const Character::Impl& asset;
    ozz::animation::SamplingJob::Context context;
    ozz::animation::SamplingJob::Context lowerContext;
    std::vector<ozz::math::SoaTransform> local, sampled, previous;
    std::vector<ozz::math::Float4x4> global;
    std::vector<core::Mat4> palette;
    std::vector<core::Mat4> jointWorld;
    std::vector<ozz::math::SoaTransform> lowerLocal, masked;
    std::vector<ozz::math::SimdFloat4> mainMask, lowerMask;
    std::vector<bool> isLeg;
    int pelvis = -1;
    std::string clip;
    float fade = 0;
    explicit Impl(const Character::Impl& a) : asset(a), context(a.skeleton ? a.skeleton->num_joints() : 0), lowerContext(a.skeleton ? a.skeleton->num_joints() : 0) {
        if (!a.skeleton) return;
        const auto rest = a.skeleton->joint_rest_poses();
        local.assign(rest.begin(), rest.end()); sampled = previous = local;
        lowerLocal=masked=local;mainMask.resize(local.size());lowerMask.resize(local.size());
        global.resize(a.skeleton->num_joints()); palette.resize(global.size());jointWorld.resize(global.size());
        for(const char* name:a.skeleton->joint_names()) {
            const std::string n=name;
            if(n=="pelvis")pelvis=static_cast<int>(isLeg.size());
            isLeg.push_back(n.starts_with("thigh_") || n.starts_with("calf_") || n.starts_with("foot_") || n.starts_with("ball_"));
        }
    }
};
Pose::Pose(const Character& c) : impl_(std::make_unique<Impl>(*c.impl_)) {}
Pose::~Pose() = default;
bool Pose::sample(const std::string& name, float seconds, float dt, float fadeSeconds, bool loop,
                  const std::string& lowerBodyClip,float lowerBodyPhase,float lowerBodyWeight) {
    auto& s = *impl_; const auto it = s.asset.clips.find(name);
    if (it == s.asset.clips.end() || !std::isfinite(seconds) || !std::isfinite(dt) || dt < 0 || !std::isfinite(fadeSeconds) || fadeSeconds < 0) return false;
    if (name != s.clip) { s.previous = s.local; s.fade = s.clip.empty() ? fadeSeconds : 0.f; s.clip = name; s.context.Invalidate(); }
    const float duration = it->second->duration();
    const float time = loop ? std::fmod(std::max(0.f, seconds), duration) : std::clamp(seconds, 0.f, duration);
    ozz::animation::SamplingJob sampling; sampling.animation = it->second.get(); sampling.context = &s.context;
    sampling.ratio = time / duration; sampling.output = ozz::make_span(s.sampled);
    if (!sampling.Run()) return false;
    s.fade += dt;
    const float weight = fadeSeconds > 0 ? std::clamp(s.fade / fadeSeconds, 0.f, 1.f) : 1.f;
    ozz::animation::BlendingJob::Layer layers[2];
    layers[0].transform = ozz::make_span(s.previous); layers[0].weight = 1.f - weight;
    layers[1].transform = ozz::make_span(s.sampled); layers[1].weight = weight;
    ozz::animation::BlendingJob blend; blend.layers = ozz::make_span(layers); blend.rest_pose = s.asset.skeleton->joint_rest_poses(); blend.output = ozz::make_span(s.local);
    if (!blend.Run()) return false;
    auto finalLocal=ozz::make_span(s.local);
    if(!lowerBodyClip.empty() && lowerBodyWeight>0) {
        auto lower=s.asset.clips.find(lowerBodyClip);
        if(lower==s.asset.clips.end() || !std::isfinite(lowerBodyPhase) || !std::isfinite(lowerBodyWeight))return false;
        ozz::animation::SamplingJob gait;gait.animation=lower->second.get();gait.context=&s.lowerContext;gait.ratio=lowerBodyPhase-std::floor(lowerBodyPhase);gait.output=ozz::make_span(s.lowerLocal);
        if(!gait.Run())return false;
        for(std::size_t group=0;group<s.local.size();++group) {
            float a[4]={1,1,1,1},b[4]={0,0,0,0};
            for(int k=0;k<4;++k)if(group*4+k<s.isLeg.size() && s.isLeg[group*4+k]) {b[k]=std::clamp(lowerBodyWeight,0.f,1.f);a[k]=1-b[k];}
            s.mainMask[group]=ozz::math::simd_float4::Load(a[0],a[1],a[2],a[3]);
            s.lowerMask[group]=ozz::math::simd_float4::Load(b[0],b[1],b[2],b[3]);
        }
        ozz::animation::BlendingJob::Layer gaitLayers[2];
        gaitLayers[0].weight=1;gaitLayers[0].transform=ozz::make_span(s.local);gaitLayers[0].joint_weights=ozz::make_span(s.mainMask);
        gaitLayers[1].weight=1;gaitLayers[1].transform=ozz::make_span(s.lowerLocal);gaitLayers[1].joint_weights=ozz::make_span(s.lowerMask);
        ozz::animation::BlendingJob masked;masked.layers=ozz::make_span(gaitLayers);masked.rest_pose=s.asset.skeleton->joint_rest_poses();masked.output=ozz::make_span(s.masked);
        if(!masked.Run())return false;
        // Перенос таза нужен для согнутых опорных ног. Вращение таза/корпуса
        // остаётся у удара; gait не должен поднимать стопы над полом.
        if(s.pelvis>=0) {
            const auto group=static_cast<std::size_t>(s.pelvis/4);
            float weights[4]={0,0,0,0};weights[s.pelvis%4]=std::clamp(lowerBodyWeight,0.f,1.f);
            const auto w=ozz::math::simd_float4::Load(weights[0],weights[1],weights[2],weights[3]);
            s.masked[group].translation=s.local[group].translation+(s.lowerLocal[group].translation-s.local[group].translation)*w;
        }
        finalLocal=ozz::make_span(s.masked);
    }
    ozz::animation::LocalToModelJob model; model.skeleton = s.asset.skeleton.get(); model.input = finalLocal; model.output = ozz::make_span(s.global);
    if (!model.Run()) return false;
    for (std::size_t i = 0; i < s.global.size(); ++i) {
        core::Mat4 matrix;
        for (int col = 0; col < 4; ++col) ozz::math::StorePtrU(s.global[i].cols[col], matrix.m + col * 4);
        s.jointWorld[i]=matrix;
        s.palette[i] = matrix * s.asset.inverseBind[i];
    }
    return true;
}
std::span<const core::Mat4> Pose::palette() const { return impl_->palette; }
bool Pose::jointTransform(const std::string& name,core::Mat4& out) const {
    const auto& s=*impl_;if(!s.asset.skeleton)return false;
    const auto names=s.asset.skeleton->joint_names();
    for(std::size_t i=0;i<names.size();++i)if(name==names[i]) {out=s.jointWorld[i];return true;}
    return false;
}
bool Pose::reachHand(bool left,core::Vec3 target,float weight) {
    auto& s=*impl_;
    if(!s.asset.skeleton || !std::isfinite(weight) || !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z))return false;
    weight=std::clamp(weight,0.f,1.f);
    const std::string side=left?"l":"r";
    const auto names=s.asset.skeleton->joint_names();const auto parents=s.asset.skeleton->joint_parents();
    const auto find=[&](const std::string& name){for(int i=0;i<static_cast<int>(names.size());++i)if(name==names[i])return i;return -1;};
    const int upper=find("upperarm_"+side),lower=find("lowerarm_"+side),hand=find("hand_"+side);
    if(upper<0 || lower<0 || hand<0)return false;
    const auto position=[&](int i){const auto& m=s.jointWorld[static_cast<std::size_t>(i)];return core::Vec3{m.m[12],m.m[13],m.m[14]};};
    const auto a=position(upper),b=position(lower),c=position(hand);
    target=c+(target-c)*weight;
    const float l1=core::length(b-a),l2=core::length(c-b);
    if(l1<.0001f || l2<.0001f)return false;
    auto delta=target-a;
    if(core::length(delta)<.0001f)return false;
    const auto direction=core::normalize(delta);
    const float distance=std::clamp(core::length(delta),std::fabs(l1-l2)+.0001f,(l1+l2)*.98f);
    auto pole=(b-a)-direction*core::dot(b-a,direction);
    if(core::length(pole)<.0001f) {
        const core::Vec3 fallback{left?1.f:-1.f,-.7f,0};pole=fallback-direction*core::dot(fallback,direction);
    }
    pole=core::normalize(pole);
    const float along=(l1*l1-l2*l2+distance*distance)/(2*distance);
    const auto elbow=a+direction*along+pole*std::sqrt(std::max(0.f,l1*l1-along*along));
    const auto wrist=a+direction*distance;
    const auto rotation=[](core::Vec3 from,core::Vec3 to) {
        from=core::normalize(from);to=core::normalize(to);const float cosine=std::clamp(core::dot(from,to),-1.f,1.f);
        core::Mat4 result;
        if(cosine>.999999f)return result;
        auto axis=core::cross(from,to);
        if(cosine<-.999999f) {
            axis=core::normalize(core::cross(from,std::fabs(from.x)<.8f?core::Vec3{1,0,0}:core::Vec3{0,1,0}));
            const float v[3]={axis.x,axis.y,axis.z};
            for(int col=0;col<3;++col)for(int row=0;row<3;++row)result.m[col*4+row]=2*v[row]*v[col]-(col==row?1.f:0.f);
            return result;
        }
        const float k[3][3]={{0,-axis.z,axis.y},{axis.z,0,-axis.x},{-axis.y,axis.x,0}};
        for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
            float squared=0;for(int j=0;j<3;++j)squared+=k[row][j]*k[j][col];
            result.m[col*4+row]=(row==col?1.f:0.f)+k[row][col]+squared/(1+cosine);
        }
        return result;
    };
    const auto turn=[&](int joint,core::Vec3 from,core::Vec3 to) {
        const auto pivot=position(joint);
        const auto transform=core::Mat4::translation(pivot)*rotation(from,to)*core::Mat4::translation(pivot*-1.f);
        for(int i=joint;i<static_cast<int>(s.jointWorld.size());++i) {
            int parent=i;while(parent>=0 && parent!=joint)parent=parents[static_cast<std::size_t>(parent)];
            if(parent==joint)s.jointWorld[static_cast<std::size_t>(i)]=transform*s.jointWorld[static_cast<std::size_t>(i)];
        }
    };
    turn(upper,b-a,elbow-a);
    turn(lower,position(hand)-position(lower),wrist-position(lower));
    for(std::size_t i=0;i<s.palette.size();++i)s.palette[i]=s.jointWorld[i]*s.asset.inverseBind[i];
    return true;
}
}  // namespace han::anim
