#pragma once
#include <map>
#include <string>
namespace han::player {
struct CommonSample {std::string clip;float phase=0,fadeSeconds=.1f;bool loop=true;};
class CommonVisuals {
public:
    bool load(const std::string& path,std::string& error);
    CommonSample advance(float dt,bool grounded,float verticalSpeed,float horizontalSpeed,bool sprint);
    void reset();
private:
    std::map<std::string,std::string> clips_;
    float air_=0,land_=100,clock_=0,phase_=0;
    float startSeconds_=.14f,landSeconds_=.2f,deadzone_=.1f,walkSpeed_=1.8f,fade_=.1f;
    float walkCycle_=1.2f,runCycle_=2.f,sprintCycle_=3.f,idleSeconds_=2.f;
    bool initialized_=false,grounded_=false;
};
}
