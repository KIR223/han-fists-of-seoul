#include <doctest/doctest.h>
#include <filesystem>
#include <memory>
#include "han/combat/sim.h"
#include "han/combat/input_scheme.h"
#include <set>
using namespace han::combat;
namespace {
struct BoxingRig {
    CombatRules rules;MoveLibrary moves;std::unique_ptr<CombatSim> sim;
    BoxingRig(float distance=.65f) {
        std::string error;const std::filesystem::path root=HAN_REPO_DIR;
        REQUIRE(rules.load((root/"data/combat/combat_rules.json").string(),error));
        REQUIRE(moves.loadDirectory((root/"data/combat/moves").string(),error));
        sim=std::make_unique<CombatSim>(rules,11);
        FighterStats stats;stats.technique=0;
        sim->addFighter({0,0},stats);sim->addFighter({0,-distance},stats);sim->setTarget(0,1);sim->setTarget(1,0);
    }
    const MoveData* move(const char* name) {const auto* m=moves.byId(std::string("boxing_")+name);REQUIRE(m);return m;}
    void start(const char* name) {FighterInput input;input.attack=move(name);sim->step({input,{}});}
    void idle(int n) {for(int i=0;i<n;++i)sim->step({});}
};
}
TEST_CASE("boxing: double jab and Dempsey have separate contacts, not multiplied damage") {
    for(const char* name:{"double_jab_step","dempsey_roll"}) {
        BoxingRig rig;const auto* move=rig.move(name);
        const float expected=rig.sim->expectedDamage(rig.sim->fighter(0),rig.sim->fighter(1),*move,move->movement);
        rig.start(name);rig.idle(move->totalFrames()+2);
        std::vector<CombatEvent> hits;
        for(const auto& e:rig.sim->log())if(e.type==CombatEventType::Hit && e.attacker==0)hits.push_back(e);
        REQUIRE(hits.size()==move->strikes.size());float total=0;
        for(std::size_t i=0;i<hits.size();++i) {CHECK(hits[i].frame==move->strikes[i].frame);CHECK(hits[i].strikeIndex==static_cast<int>(i));total+=hits[i].damage;}
        CHECK(total==doctest::Approx(expected));
    }
}
TEST_CASE("boxing: missed first jab is not delayed until second contact") {
    BoxingRig rig(4);rig.start("double_jab_step");rig.idle(7);
    rig.sim->fighterMutable(1).pos={0,-.8f};rig.idle(30);
    int count=0;
    for(const auto& e:rig.sim->log())if(e.type==CombatEventType::Hit) {++count;CHECK(e.strikeIndex==1);CHECK(e.frame==16);}
    CHECK(count==1);
}
TEST_CASE("boxing: parry interrupts remaining jabs") {
    BoxingRig rig;rig.start("double_jab_step");rig.idle(3);
    FighterInput parry;parry.defense=DefenseAction::Parry;rig.sim->step({{},parry});rig.idle(30);
    int hits=0,parries=0;
    for(const auto& e:rig.sim->log()) {hits+=e.type==CombatEventType::Hit;parries+=e.type==CombatEventType::PerfectParry;}
    CHECK(hits==0);CHECK(parries==1);
}
TEST_CASE("boxing: clinch binds two fighters, restores stamina, can be escaped") {
    BoxingRig rig;rig.sim->fighterMutable(0).stamina=40;rig.sim->fighterMutable(1).stamina=40;
    rig.start("clinch");rig.idle(10);
    REQUIRE(rig.sim->fighter(0).state==FighterState::Clinching);REQUIRE(rig.sim->fighter(1).state==FighterState::Clinching);
    CHECK(rig.sim->fighter(0).clinchPartner==1);
    const float stamina=rig.sim->fighter(0).stamina;rig.idle(10);CHECK(rig.sim->fighter(0).stamina>stamina);
    FighterInput escape;escape.defense=DefenseAction::StepBack;rig.sim->step({escape,{}});
    CHECK(rig.sim->fighter(0).state==FighterState::Defending);CHECK(rig.sim->fighter(1).state==FighterState::Idle);
    CHECK(rig.sim->fighter(0).clinchPartner==-1);CHECK(rig.sim->fighter(1).clinchPartner==-1);
}
TEST_CASE("boxing: clinch releases automatically and third-party hits release partner") {
    {BoxingRig rig;rig.start("clinch");rig.idle(150);CHECK(rig.sim->fighter(0).state!=FighterState::Clinching);CHECK(rig.sim->fighter(1).state!=FighterState::Clinching);}
    BoxingRig rig;rig.start("clinch");rig.idle(10);
    FighterStats stats;stats.technique=0;rig.sim->addFighter({0,-1.1f},stats);rig.sim->setTarget(2,1);
    FighterInput jab;jab.attack=rig.move("jab");rig.sim->step({{},{},jab});rig.idle(10);
    CHECK(rig.sim->fighter(0).clinchPartner==-1);CHECK(rig.sim->fighter(1).clinchPartner==-1);
}
TEST_CASE("boxing: official rules prevent hits on fallen opponent") {
    BoxingRig rig;rig.sim->setOfficialRules(true);
    rig.sim->fighterMutable(1).state=FighterState::Knockdown;
    const float health=rig.sim->fighter(1).health;
    rig.start("jab");rig.idle(20);
    CHECK(rig.sim->fighter(1).health==health);
    CHECK(rig.sim->fighter(0).state!=FighterState::Attacking);
}
TEST_CASE("boxing: all twenty actions can be selected from keyboard and mouse") {
    BoxingRig rig;std::set<std::string> selected;
    const auto record=[&](SimpleScheme& scheme,RawInput raw) {
        const auto input=scheme.map(raw,*rig.sim,0);REQUIRE(input.attack);selected.insert(input.attack->id);
    };
    SimpleScheme chain(rig.moves);
    RawInput click;click.lmbPressed=true;
    for(int i=0;i<4;++i)record(chain,click);
    for(int route=0;route<13;++route) {
        SimpleScheme scheme(rig.moves);RawInput raw=click;
        switch(route) {
            case 0:raw.right=1;break;
            case 1:raw.right=-1;raw.rmbHeld=true;break;
            case 2:raw.right=1;raw.rmbHeld=true;break;
            case 3:raw.rmbHeld=true;break;
            case 4:raw.forward=-1;break;
            case 5:raw.alternate=true;break;
            case 6:raw.dash=true;raw.forward=1;break;
            case 7:raw.dash=true;raw.right=-1;break;
            case 8:raw.specialPressed=true;break;
            case 9:raw.specialPressed=true;raw.forward=-1;break;
            case 10:raw.specialPressed=true;raw.right=1;break;
            case 11:raw.clinchPressed=true;break;
            case 12:raw.lmbPressed=false;raw.lmbHeld=true;for(int i=0;i<19;++i)scheme.map(raw,*rig.sim,0);break;
        }
        rig.sim->fighterMutable(0).focus=100;record(scheme,raw);
    }
    for(int route=0;route<3;++route) {
        SimpleScheme scheme(rig.moves);RawInput defense;
        if(route==0)defense.duckPressed=true;
        if(route==1)defense.spacePressed=true;
        if(route==2)defense.shoulderPressed=true;
        scheme.map(defense,*rig.sim,0);record(scheme,click);
    }
    CHECK(selected.size()==20);
    for(const auto& move:rig.moves.all())if(move.style=="boxing")CHECK(selected.count(move.id)==1);
}
