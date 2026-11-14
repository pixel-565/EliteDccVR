#pragma once
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "theme.h"

static D2D1_RECT_F giRect(int id)
{
    if (id == 600)
        return {1145, 10, 1190, 55};
    if (id == 620)
        return {600, 100, 860, 165};
    if (id == 621)
        return {320, 100, 580, 165};
    if (id == 622)
        return {40, 100, 300, 165};
    if (id == 623) return {880,100,1160,165};
    if (id == 650) return {40,330,1160,410};
    if (id == 651) return {40,430,1160,510};
    if (id == 652) return {285,580,1130,650};
    if (id == 653) return {410,220,1160,300};
    if (id == 640)
        return {40, 310, 580, 400};
    if (id == 641)
        return {600, 310, 1160, 400};
    if (id == 642)
        return {40, 700, 580, 780};
    if (id == 643)
        return {600, 700, 1160, 780};
    if (id == 637) return {40, 210, 580, 275};
    if (id == 638) return {600, 210, 1160, 275};
    if (id == 630)
        return {40, 350, 580, 420};
    if (id == 631)
        return {600, 350, 1160, 420};
    if (id == 632)
        return {40, 430, 580, 510};
    if (id == 633)
        return {600, 430, 1160, 510};
    if (id == 635)
        return {900, 740, 1010, 825};
    if (id == 636)
        return {1050, 740, 1160, 825};
    if (id == 634)
        return {40, 620, 580, 700};
    if (id == 601)
        return {40, 220, 390, 300};
    if (id == 618) return {410,220,1160,300};
    if (id == 619) return {40,315,1160,380};
    if(id>=660&&id<=669){const float y=400.f+float((id-660)/2)*80.f;const float x=id%2?1050.f:900.f;return {x,y,x+110,y+60};}
    return {};
}

struct OverlaySettings {
 int tab=2, idle=5, sensitivity=100,movementDeadZone=20,turnDeadZone=20,mapPanSpeed=10,mapZoomSpeed=10,mapAccelerationMs=1500;
 float width=3, pitch=0;
 bool left=false, amber=false;
 bool visible=false;
 bool nativeControls=false,directGame=false,onFootMovement=false,movementReady=false,triggerInteract=false;

 static bool preferencesPath(wchar_t (&path)[MAX_PATH]){
  wchar_t dir[MAX_PATH]{};
  const auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",dir,MAX_PATH);
  return size>0&&size<MAX_PATH&&swprintf_s(path,L"%ls\\EliteDccVR-OpenXR-Settings.settings",dir)>0;
 }
 void load(){
  wchar_t path[MAX_PATH]{};
  FILE* file=nullptr;
  if(!preferencesPath(path)||_wfopen_s(&file,path,L"r")!=0)return;
  int savedIdle=0,savedSensitivity=0,savedLeft=0,savedAmber=0,savedDirect=0,savedMovement=0,savedMoveZone=20,savedTurnZone=20,savedInteract=0,savedMapPan=10,savedMapZoom=10,savedAcceleration=1500;
  float savedWidth=0,savedPitch=0;
  const int count=fscanf_s(file,"%d %d %f %f %d %d %d %d %d %d %d %d %d %d",&savedIdle,&savedSensitivity,&savedWidth,&savedPitch,&savedLeft,&savedAmber,&savedDirect,&savedMovement,&savedMoveZone,&savedTurnZone,&savedInteract,&savedMapPan,&savedMapZoom,&savedAcceleration);
  fclose(file);
  if(count<6||!std::isfinite(savedWidth)||!std::isfinite(savedPitch))return;
  idle=std::clamp(savedIdle,1,120);
  sensitivity=std::clamp(savedSensitivity,10,100);
  width=std::clamp(savedWidth,.5f,20.f);
  pitch=std::clamp(savedPitch,-60.f,60.f);
  left=savedLeft!=0;
  amber=savedAmber!=0;
  directGame=count>=7&&savedDirect!=0;
  onFootMovement=count>=8&&savedMovement!=0;
  movementDeadZone=count>=9?std::clamp(savedMoveZone,0,80):20;
  turnDeadZone=count>=10?std::clamp(savedTurnZone,0,80):20;
  triggerInteract=count>=11&&savedInteract!=0;
  mapPanSpeed=count>=12?std::clamp(savedMapPan,1,100):10;
  mapZoomSpeed=count>=13?std::clamp(savedMapZoom,1,100):10;
  mapAccelerationMs=count>=14?std::clamp(savedAcceleration,0,5000):1500;
 }
 void save()const{
  wchar_t path[MAX_PATH]{},temporary[MAX_PATH]{};
  FILE* file=nullptr;
  if(!preferencesPath(path)||swprintf_s(temporary,L"%ls.%lu.tmp",path,GetCurrentProcessId())<0||
     _wfopen_s(&file,temporary,L"w")!=0)return;
  const bool written=fprintf(file,"%d %d %g %g %d %d %d %d %d %d %d %d %d %d\n",idle,sensitivity,width,pitch,left?1:0,amber?1:0,directGame?1:0,onFootMovement?1:0,movementDeadZone,turnDeadZone,triggerInteract?1:0,mapPanSpeed,mapZoomSpeed,mapAccelerationMs)>0;
  const bool closed=fclose(file)==0;
  if(!written||!closed||!MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING))DeleteFileW(temporary);
 }
 template<class Button,class Label>
 void environmentRay(Button button,Label label) const {
  if(tab==2){
   label(L"Color scheme",{40,220,1160,285});
   button(640,L"Default",!amber);button(641,L"Elite",amber);
   label(amber?L"Elite-inspired amber interface":L"Original colors and font",{40,440,1160,505});
   label(L"Hide ray after inactivity: "+std::to_wstring(idle)+L" s",{40,620,1160,675});
   button(642,L"Timeout -1 s");button(643,L"Timeout +1 s");
   label(L"Movement or controller input restores the ray.",{40,815,1160,875});
  }else if(tab==1){
   button(637,L"Left Controller",left);button(638,L"Right Controller",!left);
   std::wostringstream value;
   value<<std::fixed<<std::setprecision(1)<<L"Thickness: "<<width<<L" mm    |    Tilt: "<<pitch<<L" deg";
   label(value.str(),{40,285,1160,335});
   button(630,L"Thinner (-0.5 mm)");button(631,L"Thicker (+0.5 mm)");
   button(632,L"Tilt Down (-1 deg)");button(633,L"Tilt Up (+1 deg)");
   label(L"Thickness: 0.5-20 mm   |   Tilt: -60 to +60 deg",{40,545,1160,600});
   button(634,L"Reset Ray");button(635,L"-");button(636,L"+");
   label(L"Sensitivity: "+std::to_wstring(sensitivity)+L"%",{40,740,860,795});
   label(L"Lower = steadier ray   |   100% = original response",{40,835,1160,895});
  }
 }
 template<class Button,class Label>
 void content(Button button,Label label) const {
  button(600,L"");
  button(622,L"Environment",tab==2);
  button(621,L"Ray",tab==1);
  button(620,L"Game Control",tab==0);
  button(623,L"Exobiology",tab==3);
  if(tab==2||tab==1)environmentRay(button,label);
  else if(tab==3){
   button(601,L"Integration: Off");button(653,L"General detection: Off");
   button(650,L"Exobiology markers: Off");button(651,L"Show markers on foot: On");
   label(L"Minimum marker spacing: 0 m",{40,530,1160,575});
   label(L"0 = show all",{40,590,290,640});
   label(L"Exobiology controls are inactive in this build.",{40,670,1160,735});
  }else{
   button(601,directGame?L"Integration: On":L"Integration: Off");
   button(618,onFootMovement?L"On-foot movement: On":L"On-foot movement: Off");
   button(619,triggerInteract?L"Trigger Interact: On":L"Trigger Interact: Off");
   label(L"Movement dead zone: "+std::to_wstring(movementDeadZone)+L"%",{40,400,860,460});
   button(660,L"-");button(661,L"+");
   label(L"Turning dead zone: "+std::to_wstring(turnDeadZone)+L"%",{40,480,860,540});
   button(662,L"-");button(663,L"+");
   label(L"System Map pan speed: "+std::to_wstring(mapPanSpeed)+L"%",{40,560,860,620});
   button(664,L"-");button(665,L"+");
   label(L"System Map zoom speed: "+std::to_wstring(mapZoomSpeed)+L"%",{40,640,860,700});
   button(666,L"-");button(667,L"+");
   std::wostringstream ramp;ramp<<std::fixed<<std::setprecision(1)<<L"Pan acceleration: "<<mapAccelerationMs/1000.f<<L" s (0 = off)";
   label(ramp.str(),{40,720,860,780});button(668,L"-");button(669,L"+");
   label(left?L"Forward / turn: Left   |   Strafe: Right":L"Forward / turn: Right   |   Strafe: Left",{40,800,1160,840});
   label(movementReady?L"Analog bridge ready. Preset: EliteDccVR-OnFoot.":L"Analog bridge is unavailable in this host.",{40,845,1160,890});
  }
 }
 int hit(float x,float y) const {
  int result=-1;
  content([&](int id,const wchar_t*,bool=false){
   const auto r=giRect(id);
   if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)result=id;
  },[](const std::wstring&,D2D1_RECT_F){});
  return result;
 }
 bool select(int id){
  switch(id){
  case 600:visible=false;break;
  case 601:if(!nativeControls||tab!=0)return false;directGame=!directGame;break;
  case 618:if(!nativeControls)return false;onFootMovement=!onFootMovement;break;
  case 619:if(!nativeControls)return false;triggerInteract=!triggerInteract;break;
  case 660:case 661:movementDeadZone=std::clamp(movementDeadZone+(id==661?1:-1),0,80);break;
  case 662:case 663:turnDeadZone=std::clamp(turnDeadZone+(id==663?1:-1),0,80);break;
  case 664:case 665:mapPanSpeed=std::clamp(mapPanSpeed+(id==665?1:-1),1,100);break;
  case 666:case 667:mapZoomSpeed=std::clamp(mapZoomSpeed+(id==667?1:-1),1,100);break;
  case 668:case 669:mapAccelerationMs=std::clamp(mapAccelerationMs+(id==669?100:-100),0,5000);break;
  case 620:case 621:case 622:case 623:tab=id-620;break;
  case 640:case 641:amber=id==641;break;
  case 642:case 643:idle=std::clamp(idle+(id==643?1:-1),1,120);break;
  case 630:case 631:width=std::clamp(width+(id==631?.5f:-.5f),.5f,20.f);break;
  case 632:case 633:pitch=std::clamp(pitch+(id==633?1.f:-1.f),-60.f,60.f);break;
  case 634:width=3;pitch=0;sensitivity=100;break;
  case 635:case 636:sensitivity=std::clamp(sensitivity+(id==636?5:-5),10,100);break;
  case 637:case 638:left=id==637;break;
  default:return false;
  }
  return true;
 }
};
