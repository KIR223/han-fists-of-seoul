#include "han/player/boxing_visuals.h"
#include <algorithm>
#include <cmath>
#include "han/core/json.h"

namespace han::player {
bool BoxingVisuals::load(const std::string& path, std::string& error) {
    core::Json j;
    if(!core::loadJsonFile(path,j,error)) return false;
    std::map<std::string,std::string> moves,defenses;
    for(const auto& [key,value]:j["moves"].members()) {
        if(!value.isString() || value.asString().empty()) {error="неверный visual clip";return false;}
        moves[key]=value.asString();
    }
    for(const auto& [key,value]:j["defenses"].members()) {
        if(!value.isString() || value.asString().empty()) {error="неверная visual defense";return false;}
        defenses[key]=value.asString();
    }
    const float contact=static_cast<float>(j["contact"].asNumber(-1));
    const float end=static_cast<float>(j["active_end"].asNumber(-1));
    const float fade=static_cast<float>(j["fade_seconds"].asNumber(-1));
    const float attackFade=static_cast<float>(j["attack_fade_seconds"].asNumber(-1));
    if(moves.empty() || !(contact>0 && contact<end && end<1) || fade<0 || attackFade<0) {error="неверные visual markers";return false;}
    std::map<std::string,std::vector<float>> contacts;
    for(const auto& [name,keys]:j["contacts"].members()) {
        if(!keys.isArray() || keys.size()<2 || keys.size()>8) {error="неверные контакты серии";return false;}
        float previous=0;
        for(const auto& key:keys.items()) {
            const float value=static_cast<float>(key.asNumber(-1));
            if(!(value>previous && value<1)) {error="контакты серии должны возрастать";return false;}
            contacts[name].push_back(value);previous=value;
        }
    }
    const auto& motion=j["motion"];
    const float cycle=static_cast<float>(motion["cycle_distance"].asNumber(-1));
    const float fall=static_cast<float>(motion["fall_seconds"].asNumber(-1));
    const float deadzone=static_cast<float>(motion["speed_deadzone"].asNumber(-1));
    const float stagger=static_cast<float>(motion["stagger_damage"].asNumber(-1));
    const float guard=static_cast<float>(motion["guard_seconds"].asNumber(2.8));
    const float head=static_cast<float>(motion["head_contact_height"].asNumber(-1));
    const float body=static_cast<float>(motion["body_contact_height"].asNumber(-1));
    const float radius=static_cast<float>(motion["contact_radius"].asNumber(-1));
    const float aim=static_cast<float>(motion["arm_aim_weight"].asNumber(-1));
    if(!(head>body && body>0 && radius>0 && aim>=0 && aim<=1)) {error="неверные параметры контактного IK";return false;}
    headContact_=head;bodyContact_=body;contactRadius_=radius;aimWeight_=aim;
    if(!(cycle>.1f && fall>0 && deadzone>=0 && stagger>0 && guard>0)) {error="неверные параметры движения";return false;}
    contacts_=std::move(contacts);cycleDistance_=cycle;fallSeconds_=fall;speedDeadzone_=deadzone;staggerDamage_=stagger;guardSeconds_=guard;
    moves_=std::move(moves);defenses_=std::move(defenses);contact_=contact;activeEnd_=end;fade_=fade;attackFade_=attackFade;
    error.clear();return true;
}
void BoxingVisuals::advance(BoxingMotion& m,const combat::Fighter& f,const combat::CombatRules& r,const std::vector<combat::CombatEvent>& events) const {
    const float fps=static_cast<float>(r.fps);
    if(!m.initialized) {m.previousPosition=f.pos;m.initialized=true;}
    const float dx=f.pos.x-m.previousPosition.x,dz=f.pos.z-m.previousPosition.z;
    m.previousPosition=f.pos;
    m.speed=std::hypot(dx,dz)*fps;
    m.forward=(dx*f.facing.x+dz*f.facing.z)*fps;
    m.right=(dx*(-f.facing.z)+dz*f.facing.x)*fps;
    const bool stepping=f.state==combat::FighterState::Moving || f.state==combat::FighterState::Attacking;
    if(stepping)m.gaitPhase=std::fmod(m.gaitPhase+std::hypot(dx,dz)/cycleDistance_,1.f);
    const float target=stepping && m.speed>speedDeadzone_?1.f:0.f;
    m.locomotionWeight+=(target-m.locomotionWeight)*std::min(1.f,1.f/(fps*std::max(fade_,.001f)));
    if(f.down() || !f.alive() || f.state==combat::FighterState::Clinching)m.locomotionWeight=0;
    for(const auto& e:events) {
        if(e.attacker==f.id && e.type==combat::CombatEventType::PerfectParry) m.reaction="Boxing_Stagger";
        if(e.defender!=f.id || e.type!=combat::CombatEventType::Hit)continue;
        const float direction=e.direction.x*f.facing.x+e.direction.z*f.facing.z;
        const auto previousSide=m.fallSide;
        m.fallSide=direction>0 ? "Front" : "Back";
        if(e.zone==contracts::HitZone::Head) {
            m.reaction=e.damage>=staggerDamage_?"Boxing_Stagger":"Boxing_HitHeadFront";
            auto trajectory=e.move?e.move->trajectory:combat::Trajectory::Straight;
            if(e.move && !e.move->strikes.empty() && e.strikeIndex>=0 && e.strikeIndex<static_cast<int>(e.move->strikes.size()) &&
               (trajectory==combat::Trajectory::ArcLeft || trajectory==combat::Trajectory::ArcRight))
                trajectory=e.move->strikes[e.strikeIndex].limb=="left_hand"?combat::Trajectory::ArcLeft:combat::Trajectory::ArcRight;
            if(trajectory==combat::Trajectory::ArcLeft) {m.reaction="Boxing_HitHeadRight";m.fallSide="Right";}
            if(trajectory==combat::Trajectory::ArcRight) {m.reaction="Boxing_HitHeadLeft";m.fallSide="Left";}
            if(direction>0)m.reaction="Boxing_HitBack";
        } else {
            m.reaction=e.doubled?"Boxing_HitLiver":"Boxing_HitBody";
            if(e.doubled)m.fallSide="Front";
        }
        if(m.wasDown)m.fallSide=previousSide;
    }
    const bool down=f.down() || !f.alive();
    if(down && !m.wasDown)m.downSeconds=0;
    else if(down && f.state!=combat::FighterState::GettingUp)m.downSeconds+=1.f/fps;
    m.wasDown=down;
}
VisualSample BoxingVisuals::choose(const combat::Fighter& f,const combat::CombatRules& r,float clock,bool moving,const BoxingMotion* motion) const {
    using namespace combat;
    VisualSample out;out.clip="Boxing_Guard";out.fadeSeconds=fade_;out.loop=true;out.normalizedTime=std::fmod(std::max(0.f,clock)/guardSeconds_,1.f);
    const float frame=static_cast<float>(f.stateFrame);
    const auto gaitClip=[&] {
        if(!motion)return std::string("Boxing_ShuffleForward");
        const float fw=motion->forward,rt=motion->right;
        std::string direction;
        if(std::fabs(fw)>std::fabs(rt)*2)direction=fw>=0?"Forward":"Back";
        else if(std::fabs(rt)>std::fabs(fw)*2)direction=rt>=0?"Right":"Left";
        else direction=std::string(fw>=0?"Forward":"Back")+(rt>=0?"Right":"Left");
        return "Boxing_Shuffle"+direction;
    };
    if(f.state==FighterState::Attacking && f.move) {
        auto i=moves_.find(f.move->id);
        if(i==moves_.end()) return out;
        out.clip=i->second;out.loop=false;out.fadeSeconds=attackFade_;
        if(motion && motion->locomotionWeight>.001f) {out.lowerBodyClip=gaitClip();out.lowerBodyPhase=motion->gaitPhase;out.lowerBodyWeight=motion->locomotionWeight;}
        const float startup=static_cast<float>(std::max(1,f.move->startup)),active=static_cast<float>(std::max(1,f.move->active));
        if(frame<startup) out.normalizedTime=contact_*frame/startup;
        else if(frame<startup+active) out.normalizedTime=contact_+(activeEnd_-contact_)*(frame-startup)/active;
        else out.normalizedTime=activeEnd_+(1-activeEnd_)*(frame-startup-active)/static_cast<float>(std::max(1,f.move->recovery));
        out.normalizedTime=std::clamp(out.normalizedTime,0.f,1.f);
        if(!f.move->strikes.empty()) {
            const auto keys=contacts_.find(f.move->id);
            if(keys==contacts_.end() || keys->second.size()!=f.move->strikes.size()) return {};
            float previousFrame=0,previousTime=0;
            for(std::size_t k=0;k<=f.move->strikes.size();++k) {
                const float nextFrame=k<f.move->strikes.size()?static_cast<float>(f.move->strikes[k].frame):static_cast<float>(f.move->totalFrames());
                const float nextTime=k<keys->second.size()?keys->second[k]:1.f;
                if(frame<=nextFrame) {out.normalizedTime=previousTime+(nextTime-previousTime)*std::clamp((frame-previousFrame)/(nextFrame-previousFrame),0.f,1.f);break;}
                previousFrame=nextFrame;previousTime=nextTime;
            }
        }
        if(f.move->type!="clinch") {
            out.aimHand=true;out.leftHand=f.move->limb=="left_hand";out.targetHeight=f.move->height==Height::Head?headContact_:bodyContact_;out.contactRadius=contactRadius_;
            float start=0,peak=static_cast<float>(f.move->startup),end=static_cast<float>(f.move->totalFrames());
            if(!f.move->strikes.empty()) {
                std::size_t nearest=0;
                for(std::size_t k=1;k<f.move->strikes.size();++k)
                    if(std::fabs(frame-f.move->strikes[k].frame)<std::fabs(frame-f.move->strikes[nearest].frame))nearest=k;
                peak=static_cast<float>(f.move->strikes[nearest].frame);
                start=nearest?(.5f*(f.move->strikes[nearest-1].frame+peak)):0;
                end=nearest+1<f.move->strikes.size()?(.5f*(f.move->strikes[nearest+1].frame+peak)):end;
                out.leftHand=f.move->strikes[nearest].limb=="left_hand";
            }
            float weight=frame<=peak?std::clamp((frame-start)/std::max(1.f,peak-start),0.f,1.f):std::clamp((end-frame)/std::max(1.f,end-peak),0.f,1.f);
            weight=weight*weight*(3-2*weight);out.aimWeight=weight*aimWeight_;
        }
    } else if(f.state==FighterState::Clinching) {
        out.clip="Boxing_Clinch";out.normalizedTime=std::fmod(clock/guardSeconds_,1.f);out.loop=true;
    } else if(f.state==FighterState::Blocking || f.state==FighterState::BlockStun) {
        out.clip="Boxing_Block";out.normalizedTime=.5f;out.loop=false;
    } else if(f.state==FighterState::Defending) {
        const std::string action=defenseActionName(f.action);
        std::string visual=action;
        switch(f.action) {
            case DefenseAction::SlipLeft: visual="slip_left";break;
            case DefenseAction::SlipRight: visual="slip_right";break;
            case DefenseAction::BobLeft: visual="bob_left";break;
            case DefenseAction::BobRight: visual="bob_right";break;
            case DefenseAction::SidestepLeft: visual="sidestep_left";break;
            case DefenseAction::SidestepRight: visual="sidestep_right";break;
            case DefenseAction::ShoulderRollLeft: visual="shoulder_roll_left";break;
            case DefenseAction::ShoulderRollRight: visual="shoulder_roll_right";break;
            default:break;
        }
        auto i=defenses_.find(visual);
        if(i!=defenses_.end()) out.clip=i->second;
        const auto* def=r.defense(action);
        out.normalizedTime=def?std::clamp(frame/static_cast<float>(std::max(1,def->total)),0.f,1.f):0;
        out.loop=false;out.fadeSeconds=attackFade_;
    } else if(f.state==FighterState::HitStun) {
        out.clip=motion?motion->reaction:"Boxing_HitHeadFront";
        out.normalizedTime=std::clamp(frame/static_cast<float>(std::max(1,f.stateFrame+f.stunFrames)),0.f,1.f);out.loop=false;out.fadeSeconds=attackFade_;
    } else if(f.state==FighterState::Knockdown || f.state==FighterState::KnockedOut) {
        out.clip="Boxing_Fall"+(motion?motion->fallSide:std::string("Back"));
        out.normalizedTime=std::min(1.f,(motion?motion->downSeconds:frame/static_cast<float>(r.fps))/fallSeconds_);out.loop=false;out.fadeSeconds=attackFade_;
    } else if(f.state==FighterState::GettingUp) {
        out.clip="Boxing_GetUp"+(motion?motion->fallSide:std::string("Back"));
        out.normalizedTime=std::clamp(frame/static_cast<float>(std::max(1,r.getupFrames)),0.f,1.f);out.loop=false;
    } else if(moving && (!motion || motion->speed>speedDeadzone_)) {
        out.clip=gaitClip();out.normalizedTime=motion?motion->gaitPhase:std::fmod(clock,1.f);
    }
    return out;
}
}  // namespace han::player
