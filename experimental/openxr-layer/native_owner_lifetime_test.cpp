#include "../shared-vr-ui/game-native/native_ui.cpp"
#include <cassert>
#include <cstdio>
int main(){
 using namespace NativeUI;
 alignas(16) unsigned char wrapper[0x800]{},resource[0x80]{};
 const char* name="StationMenu2";imageBase=0x10000000;
 auto put=[&](unsigned offset,uintptr_t value){memcpy(wrapper+offset,&value,8);};
 put(0,imageBase+0x55dc548);put(0x108,reinterpret_cast<uintptr_t>(&name));put(0x3a8,reinterpret_cast<uintptr_t>(resource));
 wrapper[0x478]=wrapper[0x481]=1;
 uintptr_t movie=0x12340000;memcpy(resource+0x18,&movie,8);
 uint16_t dimensions[2]={2445,1375};memcpy(resource+0x48,dimensions,4);
 active=true;registerGpu(reinterpret_cast<uintptr_t>(wrapper),StationHomeKind);
 gpuEntries[0].stamp=GetTickCount64()-60000;
 GpuOwner owner{};assert(gpuTexture(2445,1375,TerminalProfile,owner));
 assert(owner.wrapper==reinterpret_cast<uintptr_t>(wrapper)&&owner.nativeMovie==movie);
 Hit hit{};assert(gpuTarget(owner,hit)&&hit.nativeMovie==movie);
 wrapper[0x481]=0;assert(!gpuTexture(2445,1375,TerminalProfile,owner));wrapper[0x481]=1;
 wrapper[0x478]=0;assert(!gpuTexture(2445,1375,TerminalProfile,owner));wrapper[0x478]=1;
 assert(!gpuTexture(2445,1375,StripProfile,owner));assert(!gpuTexture(1680,945,TerminalProfile,owner));
 movie++;memcpy(resource+0x18,&movie,8);assert(!gpuTarget(owner,hit));
 assert(gpuTexture(2445,1375,TerminalProfile,owner)&&owner.nativeMovie==movie);
 active=false;assert(!gpuTexture(2445,1375,TerminalProfile,owner));
 puts("PASS dormant terminal owner: 60s registration age, live visibility/input, profile/viewport and replaced movie guards");
}
