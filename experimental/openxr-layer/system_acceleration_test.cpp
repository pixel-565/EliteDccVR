#include "../shared-vr-ui/game-native/system_navigation.h"
#include <cassert>
#include <cstdio>
using namespace NativeSystem;
static float at(unsigned elapsed,unsigned step){
 PanAcceleration ramp;NavigationAxes axes{1,0,0};ramp.apply(axes,1000,1500,false);
 for(unsigned t=step;t<elapsed;t+=step)ramp.apply(axes,1000+t,1500,false);
 return ramp.apply(axes,1000+elapsed,1500,false).x;
}
int main(){
 for(unsigned step:{7u,14u,33u}){assert(std::abs(at(750,step)-.525f)<.00001f);assert(std::abs(at(1500,step)-1)<.00001f);}
 PanAcceleration ramp;NavigationAxes right{1,0,0};
 assert(std::abs(ramp.apply(right,1000,1500,false).x-.05f)<.00001f);
 float previous=0;for(unsigned t=0;t<=1500;t+=10){const auto v=ramp.apply(right,1000+t,1500,false);assert(v.x>=previous&&v.x<=1);previous=v.x;}
 assert(ramp.apply({},2510,1500,false).x==0&&!ramp.active);
 assert(std::abs(ramp.apply(right,2520,1500,false).x-.05f)<.00001f);
 assert(std::abs(ramp.apply({-1,0,0},2530,1500,false).x+.05f)<.00001f);
 assert(std::abs(ramp.apply({0,1,0},2540,1500,false).y-.05f)<.00001f);
 assert(std::abs(ramp.apply({0,1,0},2800,1500,false).y-.05f)<.00001f); // Frame gap resets.
 const auto zoom=ramp.apply({0,0,-.7f},2810,1500,true);assert(zoom.zoom==-.7f&&!ramp.active);
 assert(std::abs(ramp.apply(right,2820,1500,false).x-.05f)<.00001f);
 assert(ramp.apply(right,2830,0,false).x==1&&!ramp.active);
 assert(std::abs(ramp.apply(right,2840,2000,false).x-.05f)<.00001f);
 ramp.reset();assert(!ramp.active);
 puts("Pan acceleration: frame-rate-independent smooth ramp, immediate stop, direction/gap/mode resets and zero-duration bypass passed.");
}
