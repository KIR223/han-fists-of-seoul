#include <doctest/doctest.h>
#include "han/player/common_visuals.h"
#include <filesystem>

TEST_CASE("common visuals: physical takeoff, airborne loop, landing and reset") {
    han::player::CommonVisuals visuals;std::string error;
    REQUIRE(visuals.load((std::filesystem::path(HAN_REPO_DIR)/"data/combat/common_visuals.json").string(),error));
    CHECK(visuals.advance(0,true,0,0,false).clip=="Idle_Loop");
    CHECK(visuals.advance(.016f,true,0,1,false).clip=="Walk_Loop");
    CHECK(visuals.advance(.016f,true,0,4,false).clip=="Jog_Fwd_Loop");
    CHECK(visuals.advance(.016f,true,0,6,true).clip=="Sprint_Loop");
    auto jump=visuals.advance(.016f,false,5,4,false);
    CHECK(jump.clip=="Jump_Start");CHECK_FALSE(jump.loop);
    for(int i=0;i<20;++i)jump=visuals.advance(.016f,false,-2,4,false);
    CHECK(jump.clip=="Jump_Loop");CHECK(jump.loop);
    auto land=visuals.advance(.016f,true,0,4,false);
    CHECK(land.clip=="Jump_Land");CHECK_FALSE(land.loop);
    for(int i=0;i<20;++i)land=visuals.advance(.016f,true,0,4,false);
    CHECK(land.clip=="Jog_Fwd_Loop");
    visuals.reset();CHECK(visuals.advance(0,true,0,0,false).clip=="Idle_Loop");
}
TEST_CASE("common visuals: gait phase follows distance and stops advancing at rest") {
    han::player::CommonVisuals a,b;std::string error;
    const auto config=(std::filesystem::path(HAN_REPO_DIR)/"data/combat/common_visuals.json").string();
    REQUIRE(a.load(config,error));REQUIRE(b.load(config,error));
    han::player::CommonSample x,y;
    for(int i=0;i<30;++i)x=a.advance(1.f/60,true,0,1,false);
    for(int i=0;i<60;++i)y=b.advance(1.f/60,true,0,.5f,false);
    CHECK(x.phase==doctest::Approx(y.phase));
    for(int i=0;i<20;++i)a.advance(1.f/60,true,0,0,false);
    CHECK(a.advance(0,true,0,1,false).phase==doctest::Approx(x.phase));
}
