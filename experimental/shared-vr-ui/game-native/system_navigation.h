#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace NativeSystem {
struct NavigationAxes {float x{},y{},zoom{};};
inline NavigationAxes navigationAxes(float x,float y,bool zoom){
 if(!std::isfinite(x)||!std::isfinite(y))return {};
 x=std::clamp(x,-1.f,1.f);y=std::clamp(y,-1.f,1.f);
 if(zoom){const float a=std::abs(y);return {0,0,a>.2f?-std::copysign((a-.2f)/.8f,y):0};}
 const float length=std::sqrt(x*x+y*y);if(length<=.2f)return {};
 const float amplitude=(std::min(length,1.f)-.2f)/.8f;
 return {x/length*amplitude,y/length*amplitude,0};
}
struct PanAcceleration {
 uint64_t started{},last{};
 float directionX{},directionY{};
 int duration{};
 bool active{};
 void reset(){active=false;started=last=0;directionX=directionY=0;}
 NavigationAxes apply(NavigationAxes axes,uint64_t now,int milliseconds,bool zoom){
  milliseconds=std::clamp(milliseconds,0,5000);
  const float length=std::sqrt(axes.x*axes.x+axes.y*axes.y);
  if(zoom||length<=0||!milliseconds){reset();return axes;}
  const float x=axes.x/length,y=axes.y/length;
  if(!active||now<last||now-last>200||duration!=milliseconds||x*directionX+y*directionY<.70710678f){
   active=true;started=now;directionX=x;directionY=y;duration=milliseconds;
  }
  last=now;const float t=std::min(1.f,float(now-started)/float(milliseconds));
  // Smooth speed ramp, not inertia: neutral input still stops immediately.
  const float gain=.05f+.95f*t*t*(3-2*t);axes.x*=gain;axes.y*=gain;return axes;
 }
};
}
