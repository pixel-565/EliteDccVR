#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "movement.h"
#include "native_ui.h"
#include "native_gamepad.h"
#include "../../../vendor/json/json.hpp"
namespace OnFootMovement {
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
static GetState original=nullptr;
static std::atomic<bool> installed{},enabled{},statusAllowed{},focused{},nativeRegistered{};
static std::atomic<bool> registrationFailed{},interactionEnabled{};
static std::atomic<uint64_t> interactionSample{};
static bool registerNative();
static std::atomic<DWORD> statusTime{},packet{1},hardwareProbeTime{};
static std::atomic<bool> physicalAttached{};
static std::atomic<uint64_t> sample{},turnSample{},lastAxes{},pollCount{},outputCount{};
static std::atomic<int> movementDeadZone{20},turnDeadZone{20};
static unsigned controllerSlot=4;
static SRWLOCK installGuard=SRWLOCK_INIT;
static uint32_t axes(float x,float y,float deadZone=.2f){
 if(!std::isfinite(x)||!std::isfinite(y))return 0;
 const float magnitude=std::sqrt(x*x+y*y);
 if(magnitude<=deadZone||magnitude==0)return 0;
 const float amplitude=(std::min(magnitude,1.f)-deadZone)/(1.f-deadZone);
 const auto scaled=[&](float v){return static_cast<SHORT>(std::lround(std::clamp(v/magnitude*amplitude,-1.f,1.f)*32767.f));};
 return static_cast<uint16_t>(scaled(x))|(uint32_t(static_cast<uint16_t>(scaled(y)))<<16);
}
static uint32_t movementAxes(float x,float y,float deadZone){
 // Forward and strafe now come from separate physical sticks. Apply the
 // threshold independently, then cap the combined diagonal magnitude.
 const auto component=[&](float value){return static_cast<SHORT>(axes(value,0,deadZone)&0xffff)/32767.f;};
 return axes(component(x),component(y),0.f);
}
static bool onFootStatus(const nlohmann::json& value){
 if(!value.is_object()||!value.contains("Flags2")||!value["Flags2"].is_number_unsigned())return false;
 const auto flags=value["Flags2"].get<uint64_t>();
 const auto focus=value.find("GuiFocus");
 if(focus!=value.end()&&(!focus->is_number_integer()||focus->get<int>()!=0))return false;
 return (flags&1)!=0&&(flags&6)==0;
}
static void readStatus(){
 bool allowed=false;
 wchar_t profile[MAX_PATH]{},path[MAX_PATH]{};
 if(GetEnvironmentVariableW(L"USERPROFILE",profile,MAX_PATH)&&swprintf_s(path,L"%ls\\Saved Games\\Frontier Developments\\Elite Dangerous\\Status.json",profile)>0){
  const auto file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE){
   char data[16384]{};DWORD bytes=0;
   if(ReadFile(file,data,sizeof(data)-1,&bytes,nullptr)&&bytes){
    const auto json=nlohmann::json::parse(data,data+bytes,nullptr,false);
    try{allowed=onFootStatus(json);}catch(...){allowed=false;}
   }
   CloseHandle(file);
  }
 }
 statusAllowed=allowed;statusTime=GetTickCount();
}
static DWORD WINAPI watch(void*){
 for(;;){if(!nativeRegistered.load()&&!registrationFailed.load())registerNative();if(enabled.load()||interactionEnabled.load())readStatus();else statusAllowed=false;Sleep(250);}
}
static DWORD WINAPI getState(DWORD index,XINPUT_STATE* state){
 if(!state)return ERROR_BAD_ARGUMENTS;
 if(index!=controllerSlot)return original(index,state);
 const DWORD now=GetTickCount();
 // Do not poll a disconnected hardware slot on every frame: some XInput
 // drivers perform expensive discovery. Probe hot-plug at a bounded cadence.
 if(physicalAttached.load()||DWORD(now-hardwareProbeTime.load())>=500){
  const auto result=original(index,state);hardwareProbeTime=now;
  physicalAttached=result==ERROR_SUCCESS;
  if(result!=ERROR_DEVICE_NOT_CONNECTED)return result;
 }
 ++pollCount;
 const auto current=sample.load(),turn=turnSample.load();
 uint64_t value=0;
 if(focused.load()&&statusAllowed.load()&&DWORD(now-statusTime.load())<=1500&&!NativeUI::movementBlocked()){
  if(enabled.load()&&DWORD(now-DWORD(current>>32))<=200)value=static_cast<uint32_t>(current);
  if(enabled.load()&&DWORD(now-DWORD(turn>>32))<=200)value|=uint64_t(turn&0xffff)<<32;
  const auto press=interactionSample.load();
  if(interactionEnabled.load()&&(press&1)&&DWORD(now-DWORD(press>>32))<=200)value|=uint64_t(XINPUT_GAMEPAD_X)<<48;
 }
 if(value)++outputCount;
 if(lastAxes.exchange(value)!=value)++packet;
 *state={};state->dwPacketNumber=packet.load();
 state->Gamepad.sThumbLX=static_cast<SHORT>(value&0xffff);
 state->Gamepad.sThumbLY=static_cast<SHORT>(value>>16);
 state->Gamepad.sThumbRX=static_cast<SHORT>(value>>32);
 state->Gamepad.wButtons=static_cast<WORD>(value>>48);
 return ERROR_SUCCESS;
}
static bool installBridge(){
 if(installed)return true;
 const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
 if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)return false;
 const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!directory.VirtualAddress)return false;
 const auto* entry=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
 for(;entry->Name;++entry){
  if(_stricmp(reinterpret_cast<const char*>(base+entry->Name),"XINPUT9_1_0.dll")||!entry->OriginalFirstThunk)continue;
  const auto* names=reinterpret_cast<const IMAGE_THUNK_DATA64*>(base+entry->OriginalFirstThunk);
  auto* cells=reinterpret_cast<IMAGE_THUNK_DATA64*>(base+entry->FirstThunk);
  for(;names->u1.AddressOfData;++names,++cells){
   if(IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))continue;
   const auto* name=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
   if(strcmp(reinterpret_cast<const char*>(name->Name),"XInputGetState"))continue;
   original=reinterpret_cast<GetState>(cells->u1.Function);
   for(unsigned i=0;i<4;++i){XINPUT_STATE state{};if(original(i,&state)==ERROR_DEVICE_NOT_CONNECTED){controllerSlot=i;break;}}
   if(controllerSlot==4)return false;
   hardwareProbeTime=GetTickCount();
   HMODULE pinned=nullptr;
   if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(&getState),&pinned))return false;
   DWORD protect=0;if(!VirtualProtect(&cells->u1.Function,sizeof(void*),PAGE_READWRITE,&protect))return false;
   InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&cells->u1.Function),reinterpret_cast<void*>(&getState));
   DWORD ignored=0;VirtualProtect(&cells->u1.Function,sizeof(void*),protect,&ignored);
   installed=true;return true;
  }
 }
 return false;
}
static bool registerNative(){
 if(nativeRegistered.load())return true;
 const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
 constexpr unsigned char prefix[]={0x4c,0x8b,0xdc,0x55,0x53,0x49,0x8d,0xab,0x78,0xfd,0xff,0xff};
 if(nt->FileHeader.TimeDateStamp!=1792704584||nt->OptionalHeader.SizeOfImage!=0x640c000||
    memcmp(reinterpret_cast<void*>(base+0x5c7560),prefix,sizeof(prefix))){registrationFailed=true;return false;}
 const auto manager=*reinterpret_cast<void* const*>(base+0x5f2a968);
 const auto result=NativeGamepad::add(manager,base+0x50ca620,controllerSlot,reinterpret_cast<NativeGamepad::Register>(base+0x5c7560));
 if(result==NativeGamepad::Result::Registered||result==NativeGamepad::Result::Existing)nativeRegistered=true;
 else if(result!=NativeGamepad::Result::NotReady)registrationFailed=true;
 return nativeRegistered.load();
}
bool initialize(){
 if(installed.load())return registerNative();
 wchar_t executable[MAX_PATH]{};
 if(!GetModuleFileNameW(nullptr,executable,MAX_PATH))return false;
 const auto separator=wcsrchr(executable,L'\\');
 if(_wcsicmp(separator?separator+1:executable,L"EliteDangerous64.exe"))return false;
 AcquireSRWLockExclusive(&installGuard);
 const bool previous=installed;
 const bool ok=installBridge();
 if(ok&&!previous){const auto worker=CreateThread(nullptr,0,watch,nullptr,0,nullptr);if(worker)CloseHandle(worker);}
 const bool registered=ok&&registerNative();
 ReleaseSRWLockExclusive(&installGuard);return registered;
}
bool configure(bool value){
 if(value&&enabled.load()&&nativeRegistered.load())return true;
 if(value&&!initialize())return false;
 enabled=value;if(!value){neutral();if(!interactionEnabled.load())statusAllowed=false;}return true;
}
void submit(float x,float y,bool allowed){const uint32_t value=enabled.load()&&allowed?movementAxes(x,y,movementDeadZone.load()/100.f):0;sample=(uint64_t(GetTickCount())<<32)|value;}
void submitTurn(float x,bool allowed){const uint32_t value=enabled.load()&&allowed?(axes(x,0,turnDeadZone.load()/100.f)&0xffff):0;turnSample=(uint64_t(GetTickCount())<<32)|value;}
void setDeadZones(int movementPercent,int turnPercent){movementDeadZone=std::clamp(movementPercent,0,80);turnDeadZone=std::clamp(turnPercent,0,80);}
void neutral(){sample=uint64_t(GetTickCount())<<32;turnSample=uint64_t(GetTickCount())<<32;}
bool configureInteraction(bool value){
 if(value&&!initialize())return false;
 interactionEnabled=value;if(!value){clearInteraction();if(!enabled.load())statusAllowed=false;}return true;
}
bool interactionAllowed(){return interactionEnabled.load()&&focused.load()&&statusAllowed.load()&&DWORD(GetTickCount()-statusTime.load())<=1500&&!NativeUI::movementBlocked();}
void submitInteraction(bool down){interactionSample=(uint64_t(GetTickCount())<<32)|(down&&interactionEnabled.load()?1ull:0ull);}
void clearInteraction(){interactionSample=uint64_t(GetTickCount())<<32;}
void setFocused(bool value){focused=value;if(!value){neutral();clearInteraction();}}
bool ready(){return installed.load()&&nativeRegistered.load();}
unsigned slot(){return controllerSlot;}
void counters(uint64_t (&values)[5]){values[0]=pollCount;values[1]=outputCount;values[2]=statusAllowed?1:0;values[3]=lastAxes;values[4]=controllerSlot;}
}
