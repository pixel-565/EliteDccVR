#define NOMINMAX
#include <windows.h>
#include <limits>
#include <iostream>
#include <stdexcept>
#include "../game_mouse_wheel.h"
#include "../live_mouse.h"
#include "../game_mouse_drag.h"
static void require(bool v){if(!v)throw std::runtime_error("Game wheel regression");}
int main(){try{
    require(LiveMouse::button(true,true).mi.dwFlags==MOUSEEVENTF_RIGHTDOWN);
    require(LiveMouse::button(false,true).mi.dwFlags==MOUSEEVENTF_RIGHTUP);
    GameMouseDrag drag;
    drag.update(true,.3f,.4f,.35f,.45f,1,.53f,8);
    require(std::abs(drag.u-.35f)<.00001f && std::abs(drag.v-.45f)<.00001f);
    drag.update(true,.7f,.8f,.75f,.85f,1,.53f,8);
    require(std::abs(drag.u-.4f)<.00001f && std::abs(drag.v-.4765f)<.00001f);
    drag.update(true,.7f,.8f,.75f,.85f,1,.53f,20);
    require(std::abs(drag.u-.4f)<.00001f);
    drag.update(false,.7f,.8f,.75f,.85f,1,.53f,8);
    require(!drag.active && drag.u==.75f && drag.v==.85f);
    drag.update(true,.7f,.8f,.75f,.85f,1,.53f,1);
    drag.update(true,.8f,.9f,.85f,.95f,1,.53f,1);
    require(std::abs(drag.u-.85f)<.00001f && std::abs(drag.v-.903f)<.00001f);
    drag.reset();require(!drag.active);
    GameMouseWheel wheel;
    for(float axis:{0.f,.1f,-.1f,.3f,-.3f,std::numeric_limits<float>::quiet_NaN()})
        require(wheel.update(axis,1000)==0);
    require(wheel.update(1,1000)==120);
    int total=120;
    for(uint64_t now=1010;now<=2000;now+=10){int value=wheel.update(1,now);require(value==0 || value==120);total+=value;}
    require(total>=8*120 && total<=9*120);
    require(wheel.update(-1,2010)==-120);
    require(wheel.update(0,2020)==0 && wheel.direction==0 && wheel.remainder==0);
    require(wheel.update(1,5000)==120);
    require(wheel.update(1,10000)==0);
    require(wheel.update(1,9990)==0);
    auto count=[](unsigned step){GameMouseWheel w;int sum=0;for(uint64_t now=1000;now<3000;now+=step)sum+=w.update(.8f,now);return sum;};
    require(std::abs(count(11)-count(22))<=120);
    INPUT up=LiveMouse::wheelInput(120),down=LiveMouse::wheelInput(-120);
    require(up.type==INPUT_MOUSE && up.mi.dwFlags==MOUSEEVENTF_WHEEL && up.mi.mouseData==120);
    require(static_cast<LONG>(down.mi.mouseData)==-120 && down.mi.dwFlags==MOUSEEVENTF_WHEEL);
    require(!LiveMouse::scroll(0,120));
    std::cout<<"PASS game wheel dead zone, tick direction/rate, reversal, idle reset, pause bounds, time-step independence and Windows event encoding\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
