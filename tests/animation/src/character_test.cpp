#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include "han/anim/character.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
template<class T> void put(std::ostream& out, T v) { out.write(reinterpret_cast<const char*>(&v), sizeof v); }
void text(std::ostream& out, const std::string& s) { put(out, static_cast<std::uint32_t>(s.size())); out.write(s.data(), s.size()); }
void transform(std::ostream& out, float x) { for(float f : {x,0.f,0.f,0.f,0.f,0.f,1.f,1.f,1.f,1.f}) put(out,f); }
std::string fixture() {
    std::ostringstream out(std::ios::binary);
    put(out, std::uint32_t(0x31484348)); for (auto v : {1u,3u,3u,2u}) put(out,v);
    text(out,"root"); put(out, std::int32_t(-1)); transform(out,0);
    for(float f : han::core::Mat4{}.m) put(out,f);
    for(int i=0;i<3;++i) {
        for(float f : {float(i),0.f,0.f,0.f,1.f,0.f,1.f,1.f,1.f,0.f,0.f}) put(out,f);
        for(int k=0;k<4;++k) put(out,std::uint32_t(0));
        for(float f : {1.f,0.f,0.f,0.f}) put(out,f);
    }
    for(auto i : {0u,1u,2u}) put(out,i);
    for(const std::string name : {"move","idle"}) {
        text(out,name); put(out,2u); put(out,1.f); transform(out,0); transform(out,name=="move"?1.f:0.f);
    }
    return out.str();
}
struct File {
    std::filesystem::path path = std::filesystem::temp_directory_path()/"han_character_test.hchar";
    explicit File(const std::string& data) { std::ofstream out(path,std::ios::binary); out.write(data.data(),data.size()); }
    ~File() { std::filesystem::remove(path); }
};
}
TEST_CASE("animation: ozz interpolation, looping, clamping and independent instances") {
    File file(fixture()); han::anim::Character c; std::string error;
    REQUIRE_MESSAGE(c.load(file.path.string(),error),error);
    CHECK(c.boneCount()==1); CHECK(c.mesh().skin.size()==3);
    han::anim::Pose a(c),b(c);
    REQUIRE(a.sample("move",.5f,0,0,false));
    CHECK(a.palette()[0].m[12]==doctest::Approx(.5f).epsilon(.002));
    REQUIRE(b.sample("move",1,0,0,false)); CHECK(b.palette()[0].m[12]==doctest::Approx(1.f).epsilon(.002));
    CHECK(a.palette()[0].m[12]==doctest::Approx(.5f).epsilon(.002));
    REQUIRE(a.sample("move",1.25f,0,0,true)); CHECK(a.palette()[0].m[12]==doctest::Approx(.25f).epsilon(.002));
    REQUIRE(a.sample("move",2,0,0,false)); CHECK(a.palette()[0].m[12]==doctest::Approx(1.f).epsilon(.002));
    CHECK_FALSE(a.sample("missing",0,0));
    CHECK_FALSE(a.sample("move",NAN,0));
}
TEST_CASE("animation: crossfade starts from displayed pose without a bind-pose pop") {
    File file(fixture()); han::anim::Character c; std::string error;
    REQUIRE(c.load(file.path.string(),error)); han::anim::Pose pose(c);
    REQUIRE(pose.sample("move",1,0,0,false));
    REQUIRE(pose.sample("idle",0,.05f,.1f,false)); CHECK(pose.palette()[0].m[12]==doctest::Approx(.5f).epsilon(.002));
    REQUIRE(pose.sample("idle",0,.05f,.1f,false)); CHECK(pose.palette()[0].m[12]==doctest::Approx(0.f).epsilon(.002));
}
TEST_CASE("animation: truncated and corrupt HCH1 rejected, previous asset preserved") {
    han::anim::Character c; std::string error;
    {File f(fixture()); REQUIRE(c.load(f.path.string(),error));}
    for(auto bytes : {std::string("HCH1"),fixture().substr(0,200),fixture()+"bad"}) {
        File f(bytes); CHECK_FALSE(c.load(f.path.string(),error)); CHECK(c.boneCount()==1);
    }
}
TEST_CASE("animation: real CC0 asset all clips sample to finite palettes") {
    const char* path=std::getenv("HAN_CHARACTER_ASSET");
    if(!path) { MESSAGE("HAN_CHARACTER_ASSET не задан: интеграционный ассет не проверяется"); return; }
    han::anim::Character c; std::string error; REQUIRE_MESSAGE(c.load(path,error),error);
    CHECK(c.boneCount()==65); CHECK(c.clips().size()>=43);
    han::anim::Pose pose(c);
    for(const auto& name:c.clips()) for(float t:{0.f,.25f,.5f,.75f,1.f}) {
        REQUIRE(pose.sample(name,c.duration(name)*t,0,0,false));
        for(const auto& matrix:pose.palette()) for(float f:matrix.m) CHECK(std::isfinite(f));
        for(std::size_t v=0;v<c.mesh().vertices.size();v+=97) {
            const auto& vertex=c.mesh().vertices[v];const auto& skin=c.mesh().skin[v];
            han::core::Vec3 point{};
            for(int k=0;k<4;++k) {
                const auto p=han::core::transform(pose.palette()[skin.joints[k]],{vertex.position[0],vertex.position[1],vertex.position[2]});
                point=point+han::core::Vec3{p.x,p.y,p.z}*skin.weights[k];
            }
            CHECK(std::fabs(point.x)<5);CHECK(std::fabs(point.y)<5);CHECK(std::fabs(point.z)<5);
        }
    }
    REQUIRE(pose.sample("A_TPose",0,0,0,false));
    for(const auto& matrix:pose.palette()) for(int i=0;i<16;++i)
        CHECK(matrix.m[i]==doctest::Approx(i%5==0?1.f:0.f).epsilon(.015).scale(1));
}
TEST_CASE("animation: contact IK preserves lengths and gait preserves upper-body pose relative to pelvis") {
    const char* path=std::getenv("HAN_CHARACTER_ASSET");if(!path)return;
    han::anim::Character c;std::string error;REQUIRE_MESSAGE(c.load(path,error),error);
    if(c.duration("Boxing_ShuffleForward")<=0) {MESSAGE("Нужен полный Boxing.hchar");return;}
    han::anim::Pose pose(c);REQUIRE(pose.sample("Boxing_Jab",.4f,0,0,false));
    const auto joint=[&](const char* name){han::core::Mat4 m;REQUIRE(pose.jointTransform(name,m));return han::core::Vec3{m.m[12],m.m[13],m.m[14]};};
    auto shoulder=joint("upperarm_l"),elbow=joint("lowerarm_l"),wrist=joint("hand_l"),right=joint("hand_r");
    const float upper=han::core::length(elbow-shoulder),lower=han::core::length(wrist-elbow);
    REQUIRE(pose.reachHand(true,{100,1.5f,100},1));
    CHECK(han::core::length(joint("lowerarm_l")-joint("upperarm_l"))==doctest::Approx(upper).epsilon(.001));
    CHECK(han::core::length(joint("hand_l")-joint("lowerarm_l"))==doctest::Approx(lower).epsilon(.001));
    CHECK(han::core::length(joint("hand_l")-joint("upperarm_l"))<=upper+lower+.001f);
    CHECK(han::core::length(joint("hand_r")-right)<.0001f);
    REQUIRE(pose.sample("Boxing_Jab",.4f,0,0,false));
    const auto headOffset=joint("Head")-joint("pelvis");
    REQUIRE(pose.sample("Boxing_Jab",.4f,0,0,false,"Boxing_ShuffleForward",.4f,1));
    CHECK(han::core::length((joint("Head")-joint("pelvis"))-headOffset)<.0001f);
    const auto gaitFoot=joint("foot_l");
    REQUIRE(pose.sample("Boxing_Jab",.4f,0,0,false,"Boxing_ShuffleForward",.1f,1));
    CHECK(han::core::length(joint("foot_l")-gaitFoot)>.05f);
}
