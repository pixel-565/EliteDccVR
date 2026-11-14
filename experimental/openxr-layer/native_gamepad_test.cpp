#include "../shared-vr-ui/game-native/native_gamepad.h"
#include <cassert>
#include <cstdio>
using namespace NativeGamepad;
static unsigned calls=0;static bool fail=false;static unsigned char objects[4][0x60]{};
static void __fastcall registrar(void* raw,const DIDEVICEINSTANCEW* device){
 ++calls;const auto m=reinterpret_cast<uintptr_t>(raw);
 assert(device->dwSize==sizeof(DIDEVICEINSTANCEW)&&device->dwDevType==DI8DEVTYPE_GAMEPAD);
 const auto* discovery=*reinterpret_cast<const Discovery* const*>(m+0x950);
 assert(discovery->count==1&&*discovery->data==device->guidProduct.Data1);
 if(fail)RaiseException(0xe1234567,0,0,nullptr);
 auto& count=*reinterpret_cast<uint32_t*>(m+0x8e0);auto& total=*reinterpret_cast<uint32_t*>(m+0x8e4);
 auto* object=objects[count];memset(object,0,0x60);*reinterpret_cast<uint32_t*>(object+0x20)=0x7000000;
 memcpy(object+0x38,&device->guidInstance,sizeof(GUID));*reinterpret_cast<uintptr_t*>(m+0x6e0+count*16)=reinterpret_cast<uintptr_t>(object);++count;++total;
}
int main(){
 alignas(16) unsigned char manager[0x980]{};*reinterpret_cast<uintptr_t*>(manager)=0x12345678;
 auto* lock=reinterpret_cast<CRITICAL_SECTION*>(manager+0x8e8);InitializeCriticalSection(lock);
 auto& cache=*reinterpret_cast<uintptr_t*>(manager+0x950);cache=0x5550;
 assert(add(nullptr,0,0,registrar)==Result::NotReady);
 assert(add(manager,0x9999,0,registrar)==Result::Invalid&&calls==0);
 assert(add(manager,0x12345678,2,registrar)==Result::Registered&&calls==1);
 assert(cache==0x5550&&*reinterpret_cast<uint32_t*>(objects[0]+0x24)==2&&lock->RecursionCount==0);
 assert(add(manager,0x12345678,1,registrar)==Result::Existing&&calls==1);
 assert(*reinterpret_cast<uint32_t*>(objects[0]+0x24)==1);
 memset(objects[0]+0x38,0,sizeof(GUID)); // A real device must be retained.
 assert(add(manager,0x12345678,3,registrar)==Result::Registered&&calls==2&&*reinterpret_cast<uint32_t*>(manager+0x8e0)==2);
 assert(*reinterpret_cast<uint32_t*>(objects[1]+0x24)==3);
 *reinterpret_cast<uint32_t*>(manager+0x8e0)=0;*reinterpret_cast<uint32_t*>(manager+0x8e4)=0;fail=true;
 assert(add(manager,0x12345678,0,registrar)==Result::Failed&&cache==0x5550&&lock->RecursionCount==0);
 fail=false;*reinterpret_cast<uint32_t*>(manager+0x8e4)=16;
 assert(add(manager,0x12345678,0,registrar)==Result::Full);
 DeleteCriticalSection(lock);
 puts("PASS native registration: exact identity, device construction, idempotence, real-device retention, capacity and cache/lock restoration after failure");
}
