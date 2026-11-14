#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <cstdint>
#include <cstring>
namespace NativeGamepad {
inline constexpr GUID Instance={0x5650dcc0,0x5650,0x4dcc,{0x8e,0x11,0x56,0x50,0xdc,0xc0,0,1}};
struct Discovery {uint64_t capacity,allocated,count;const uint32_t* data;};
static_assert(sizeof(Discovery)==32);
using Register=void(__fastcall*)(void*,const DIDEVICEINSTANCEW*);
enum class Result {Registered,Existing,NotReady,Invalid,Full,Failed};
inline Result add(void* raw,uintptr_t expectedVtable,unsigned slot,Register registrar){
 if(!raw||!registrar)return Result::NotReady;
 const auto manager=reinterpret_cast<uintptr_t>(raw);
 __try{if(*reinterpret_cast<const uintptr_t*>(manager)!=expectedVtable||slot>=4)return Result::Invalid;}
 __except(EXCEPTION_EXECUTE_HANDLER){return Result::Invalid;}
 auto* lock=reinterpret_cast<CRITICAL_SECTION*>(manager+0x8e8);
 Result result=Result::Failed;
 EnterCriticalSection(lock);
 __try{
  __try{
   const auto before=*reinterpret_cast<const uint32_t*>(manager+0x8e0);
   const auto total=*reinterpret_cast<const uint32_t*>(manager+0x8e4);
   if(before>4||total>16)result=Result::Invalid;
   else {
    for(unsigned n=0;n<before;++n){
     const auto object=*reinterpret_cast<const uintptr_t*>(manager+0x6e0+n*16);
     if(object&&!memcmp(reinterpret_cast<const void*>(object+0x38),&Instance,sizeof(Instance))){
      *reinterpret_cast<uint32_t*>(object+0x24)=slot;result=Result::Existing;break;
     }
    }
    if(result!=Result::Existing){
     if(before>=4||total>=16)result=Result::Full;
     else {
      DIDEVICEINSTANCEW device{};device.dwSize=sizeof(device);device.dwDevType=DI8DEVTYPE_GAMEPAD;device.guidInstance=Instance;
      device.guidProduct={0x028e045e,0,0,{0xc0,0,0,0,0,0,0,0}};
      wcscpy_s(device.tszInstanceName,L"EliteDccVR Analog");wcscpy_s(device.tszProductName,L"EliteDccVR Analog");
      const uint32_t product=device.guidProduct.Data1;const Discovery discovery{1,1,1,&product};
      auto& cache=*reinterpret_cast<uintptr_t*>(manager+0x950);const auto saved=cache;
      // The native registrar classifies XInput devices using this short-lived
      // product list. Hold its own recursive lock and restore the cache before
      // releasing the lock; the original discovery object remains untouched.
      __try{cache=reinterpret_cast<uintptr_t>(&discovery);registrar(raw,&device);}
      __finally{cache=saved;}
      if(*reinterpret_cast<const uint32_t*>(manager+0x8e0)==before+1){
       const auto object=*reinterpret_cast<const uintptr_t*>(manager+0x6e0+before*16);
       if(object&&*reinterpret_cast<const uint32_t*>(object+0x20)==0x7000000&&
          !memcmp(reinterpret_cast<const void*>(object+0x38),&Instance,sizeof(Instance))){
        *reinterpret_cast<uint32_t*>(object+0x24)=slot;result=Result::Registered;
       }
      }
     }
    }
   }
  }__except(EXCEPTION_EXECUTE_HANDLER){result=Result::Failed;}
 }__finally{LeaveCriticalSection(lock);}
 return result;
}
}
