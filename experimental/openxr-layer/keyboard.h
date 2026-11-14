#pragma once
#include <array>

namespace SharedKeyboard {
inline constexpr int Width=1180,Height=535;
inline constexpr int First=1000,Space=37,Backspace=38,Enter=39,Paste=40,Shift=49;
inline constexpr int Keys=1050,Settings=1051,Hide=1052,Less=1053,Flat=1054,More=1055,Snap=1056,Opacity=1057;
inline constexpr wchar_t Characters[]=L"1234567890qwertyuiopasdfghjklzxcvbnm-";
inline constexpr wchar_t Symbols[]=L"./:_?=&@";
inline constexpr std::array<D2D1_RECT_F,50> keyRects=[]{
 std::array<D2D1_RECT_F,50> boxes{};
 int index=0,row=0;
 for(int count:{10,10,9,8}){
  for(int col=0;col<count;++col){
   const float x=40.f+(10-count)*46.f+col*92.f,y=65.f+row*74.f;
   boxes[index++]={x,y,x+84,y+64};
  }
  ++row;
 }
 boxes[Space]={132,370,952,440};
 for(int i=0;i<3;++i)boxes[Backspace+i]={970.f,65.f+i*74,1140.f,129.f+i*74};
 for(int i=0;i<8;++i)boxes[41+i]={40.f+i*138,452,168.f+i*138,510};
 boxes[Shift]={970,287,1140,351};
 return boxes;
}();
inline constexpr D2D1_RECT_F toolbar[]={{10,10,55,55},{65,10,110,55},{1125,10,1170,55}};
inline constexpr D2D1_RECT_F settings[]={{40,130,390,195},{415,130,765,195},{790,130,1140,195},
                                        {40,225,540,290},{300,365,1115,425}};
inline wchar_t character(int key,bool shifted){
 if(key>=41&&key<Shift)return Symbols[key-41];
 if(key==Space)return L' ';
 if(key<0||key>=37)return 0;
 const auto ch=Characters[key];
 if(!shifted)return ch;
 if(key<10)return L"!@#$%^&*()"[key];
 return ch==L'-'?L'_':static_cast<wchar_t>(ch-L'a'+L'A');
}
inline int hit(float x,float y,bool showSettings){
 for(int i=0;i<3;++i)if(BrowserChrome::inside(toolbar[i],x,y))return Keys+i;
 if(showSettings){
  for(int i=0;i<5;++i)if(BrowserChrome::inside(settings[i],x,y))return Less+i;
 }else{
  for(int i=0;i<static_cast<int>(keyRects.size());++i)
   if(BrowserChrome::inside(keyRects[i],x,y))return First+i;
 }
 return -1;
}
inline void eliteKey(int key,wchar_t ch){
 const auto window=GetForegroundWindow();
 DWORD pid=0;
 const auto thread=GetWindowThreadProcessId(window,&pid);
 const auto process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
 if(!process)return;
 wchar_t path[MAX_PATH]{};
 DWORD size=MAX_PATH;
 const bool isElite=QueryFullProcessImageNameW(process,0,path,&size)&&
    _wcsicmp(fs::path(path).filename().c_str(),L"EliteDangerous64.exe")==0;
 CloseHandle(process);
 if(!isElite||window!=GetForegroundWindow())return;
 INPUT events[6]{};
 UINT count=0;
 auto pair=[&](WORD vk,bool hold=false){
  const WORD scan=static_cast<WORD>(MapVirtualKeyExW(vk,MAPVK_VK_TO_VSC,GetKeyboardLayout(thread)));
  if(!scan)return;
  auto& down=events[count++];
  down.type=INPUT_KEYBOARD;
  down.ki.wScan=scan;
  down.ki.dwFlags=KEYEVENTF_SCANCODE;
  if(!hold){events[count]=down;events[count++].ki.dwFlags|=KEYEVENTF_KEYUP;}
 };
 if(key==Paste){
  pair(VK_LCONTROL,true);
  if(!count)return;
  pair('V');
  events[count]=events[0];events[count++].ki.dwFlags|=KEYEVENTF_KEYUP;
 }else if(key==Backspace||key==Enter)pair(key==Enter?VK_RETURN:VK_BACK);
 else if(ch){
  const SHORT mapping=VkKeyScanExW(ch,GetKeyboardLayout(thread));
  if(mapping==-1||(HIBYTE(mapping)&~1))return;
  if(HIBYTE(mapping)&1)pair(VK_LSHIFT,true);
  const bool shifted=count!=0;
  pair(LOBYTE(mapping));
  if(shifted){events[count]=events[0];events[count++].ki.dwFlags|=KEYEVENTF_KEYUP;}
 }
 if(count&&window==GetForegroundWindow()){
  const UINT accepted=SendInput(count,events,sizeof(INPUT));
  for(UINT i=accepted;i<count;++i)
   if(events[i].ki.dwFlags&KEYEVENTF_KEYUP)SendInput(1,&events[i],sizeof(INPUT));
 }
}
}
