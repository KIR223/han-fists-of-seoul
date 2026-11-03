#include "han/player/common_visuals.h"
#include "han/core/json.h"
#include <algorithm>
#include <cmath>
namespace han::player {
bool CommonVisuals::load(const std::string& path,std::string& error) {
    core::Json j;if(!core::loadJsonFile(path,j,error))return false;
    std::map<std::string,std::string> clips;
    for(const char* key:{"idle","walk","run","sprint","jump_start","jump_loop","jump_land"}) {
        if(j["clips"][key].asString().empty()){error="нет общего клипа "+std::string(key);return false;}
        clips[key]=j["clips"][key].asString();
    }
    const auto read=[&](const char* key,float& value){const float v=static_cast<float>(j[key].asNumber(-1));if(!(v>0 && v<100))return false;value=v;return true;};
    if(!read("jump_start_seconds",startSeconds_) || !read("land_seconds",landSeconds_) || !read("speed_deadzone",deadzone_) || !read("walk_speed",walkSpeed_) ||
       !read("walk_cycle_distance",walkCycle_) || !read("run_cycle_distance",runCycle_) || !read("sprint_cycle_distance",sprintCycle_) || !read("idle_seconds",idleSeconds_) || !read("fade_seconds",fade_)) {error="неверные параметры общих анимаций";return false;}
    clips_=std::move(clips);reset();error.clear();return true;
}
void CommonVisuals::reset(){air_=clock_=phase_=0;land_=100;initialized_=grounded_=false;}
CommonSample CommonVisuals::advance(float dt,bool grounded,float verticalSpeed,float speed,bool sprint) {
    dt=std::clamp(std::isfinite(dt)?dt:0.f,0.f,.1f);speed=std::max(0.f,std::isfinite(speed)?speed:0.f);
    if(!initialized_) {grounded_=grounded;initialized_=true;}
    if(grounded && !grounded_)land_=0;
    if(!grounded && grounded_)air_=0;
    grounded_=grounded;clock_+=dt;
    CommonSample out;out.fadeSeconds=fade_;
    if(!grounded) {
        air_+=dt;out.loop=false;
        if(verticalSpeed>0 && air_<startSeconds_) {out.clip=clips_["jump_start"];out.phase=air_/startSeconds_;}
        else {out.clip=clips_["jump_loop"];out.phase=std::fmod(air_,1.f);out.loop=true;}
    } else if(land_<landSeconds_) {land_+=dt;out.clip=clips_["jump_land"];out.phase=std::min(1.f,land_/landSeconds_);out.loop=false;}
    else if(speed>deadzone_) {
        const std::string key=sprint?"sprint":speed<=walkSpeed_?"walk":"run";
        const float cycle=sprint?sprintCycle_:speed<=walkSpeed_?walkCycle_:runCycle_;
        phase_=std::fmod(phase_+speed*dt/cycle,1.f);out.clip=clips_[key];out.phase=phase_;
    } else {out.clip=clips_["idle"];out.phase=std::fmod(clock_/idleSeconds_,1.f);}
    return out;
}
}
