#include <doctest/doctest.h>
#include "han/player/boxing_visuals.h"
#include <filesystem>
#include <set>

TEST_CASE("boxing visuals: contact aligns with startup and recovery finishes at guard") {
    han::player::BoxingVisuals visuals;std::string error;
    const std::filesystem::path root=HAN_REPO_DIR;
    REQUIRE(visuals.load((root/"data/combat/boxing_visuals.json").string(),error));
    han::combat::CombatRules rules;
    REQUIRE(rules.load((root/"data/combat/combat_rules.json").string(),error));
    han::combat::MoveLibrary library;
    REQUIRE(library.loadDirectory((root/"data/combat/moves").string(),error));
    han::combat::Fighter fighter;fighter.state=han::combat::FighterState::Attacking;
    for(const auto& move:library.all()) {
        if(move.style!="boxing")continue;
        fighter.move=&move;fighter.stateFrame=move.startup;
        auto sample=visuals.choose(fighter,rules,0,false);
        CHECK(sample.clip!="Boxing_Guard");CHECK_FALSE(sample.loop);
        if(move.strikes.empty()) CHECK(sample.normalizedTime==doctest::Approx(.4f));
        fighter.stateFrame=move.startup+move.active;
        if(move.strikes.empty()) CHECK(visuals.choose(fighter,rules,0,false).normalizedTime==doctest::Approx(.46f));
        fighter.stateFrame=move.totalFrames();
        CHECK(visuals.choose(fighter,rules,0,false).normalizedTime==doctest::Approx(1.f));
    }
    fighter.state=han::combat::FighterState::Defending;
    fighter.action=han::combat::DefenseAction::SlipLeft;
    CHECK(visuals.choose(fighter,rules,0,false).clip=="Boxing_SlipLeft");
    fighter.action=han::combat::DefenseAction::SlipRight;
    CHECK(visuals.choose(fighter,rules,0,false).clip=="Boxing_SlipRight");
}

TEST_CASE("boxing visuals: stun progress, fall hold, forward get-up and gait distance") {
    han::player::BoxingVisuals visuals;std::string error;const std::filesystem::path root=HAN_REPO_DIR;
    REQUIRE(visuals.load((root/"data/combat/boxing_visuals.json").string(),error));
    han::combat::CombatRules rules;REQUIRE(rules.load((root/"data/combat/combat_rules.json").string(),error));
    han::combat::Fighter f;f.id=0;f.state=han::combat::FighterState::HitStun;f.stateFrame=3;f.stunFrames=10;
    const float early=visuals.choose(f,rules,0,false).normalizedTime;
    f.stateFrame=6;f.stunFrames=7;CHECK(visuals.choose(f,rules,0,false).normalizedTime==doctest::Approx(early*2));
    han::player::BoxingMotion motion;
    han::combat::CombatEvent event;event.type=han::combat::CombatEventType::Hit;event.defender=0;event.zone=han::contracts::HitZone::Torso;event.doubled=true;
    f.state=han::combat::FighterState::Knockdown;f.stateFrame=0;
    visuals.advance(motion,f,rules,{event});CHECK(motion.fallSide=="Front");
    for(int i=0;i<120;++i)visuals.advance(motion,f,rules,{});
    auto sample=visuals.choose(f,rules,0,false,&motion);CHECK(sample.clip=="Boxing_FallFront");CHECK(sample.normalizedTime==doctest::Approx(1.f));
    f.state=han::combat::FighterState::GettingUp;f.stateFrame=0;
    sample=visuals.choose(f,rules,0,false,&motion);CHECK(sample.clip=="Boxing_GetUpFront");CHECK(sample.normalizedTime==0);
    f.stateFrame=rules.getupFrames;CHECK(visuals.choose(f,rules,0,false,&motion).normalizedTime==1);
    f.state=han::combat::FighterState::Moving;f.pos={0,0};han::player::BoxingMotion gait;
    visuals.advance(gait,f,rules,{});
    for(int i=0;i<12;++i) {f.pos.x+=.01f;visuals.advance(gait,f,rules,{});}
    sample=visuals.choose(f,rules,123,true,&gait);CHECK(sample.clip=="Boxing_ShuffleRight");
    const float phase=sample.normalizedTime;
    for(int i=0;i<20;++i)visuals.advance(gait,f,rules,{});
    CHECK(gait.gaitPhase==phase);
}
TEST_CASE("boxing visuals: every boxing move has its own clip and series contacts match") {
    han::player::BoxingVisuals visuals;std::string error;const std::filesystem::path root=HAN_REPO_DIR;
    REQUIRE(visuals.load((root/"data/combat/boxing_visuals.json").string(),error));
    han::combat::CombatRules rules;REQUIRE(rules.load((root/"data/combat/combat_rules.json").string(),error));
    han::combat::MoveLibrary moves;REQUIRE(moves.loadDirectory((root/"data/combat/moves").string(),error));
    std::set<std::string> clips;
    for(const auto& move:moves.all()) {
        if(move.style!="boxing")continue;
        han::combat::Fighter f;f.state=han::combat::FighterState::Attacking;f.move=&move;f.stateFrame=move.startup;
        const auto sample=visuals.choose(f,rules,0,false);CHECK(clips.insert(sample.clip).second);
        for(std::size_t i=0;i<move.strikes.size();++i) {
            f.stateFrame=move.strikes[i].frame;const auto hit=visuals.choose(f,rules,0,false);
            if(move.id=="boxing_double_jab_step")CHECK(hit.normalizedTime==doctest::Approx(i==0?.24f:.64f));
            if(move.id=="boxing_dempsey_roll")CHECK(hit.normalizedTime==doctest::Approx(i==0?.30f:i==1?.55f:.8f));
            CHECK(hit.leftHand==(move.strikes[i].limb=="left_hand"));
        }
    }
    CHECK(clips.size()==20);
}
