#include "../shared-vr-ui/game-native/movement.cpp"
#include <cassert>
#include "../shared-vr-ui/game-native/context_input.h"
#include <cstdio>
#pragma comment(lib,"xinput9_1_0.lib")
static bool menu=false;
namespace NativeUI {bool movementBlocked(){return menu;}}
static DWORD WINAPI physical(DWORD,XINPUT_STATE* state){*state={};state->Gamepad.sThumbLX=1234;state->Gamepad.wButtons=XINPUT_GAMEPAD_A;return ERROR_SUCCESS;}
int main(){
 using namespace OnFootMovement;
 XINPUT_STATE state{};XInputGetState(0,&state); // Force the same import Elite uses.
 assert(axes(.1f,.1f)==0&&axes(NAN,1)==0);
 assert(movementAxes(.15f,.15f,.2f)==0);
 const auto isolated=movementAxes(.1f,.6f,.2f);assert(static_cast<SHORT>(isolated&0xffff)==0&&static_cast<SHORT>(isolated>>16)>0);
 const auto diagonal=movementAxes(1,1,.2f);assert(static_cast<SHORT>(diagonal&0xffff)>23000&&static_cast<SHORT>(diagonal&0xffff)<23300);
 const auto slow=static_cast<SHORT>(axes(.4f,0)&0xffff),fast=static_cast<SHORT>(axes(.8f,0)&0xffff);
 assert(slow>8000&&slow<8400&&fast>24000&&fast<25000);
 assert(onFootStatus(nlohmann::json::parse(R"({"Flags2":1,"GuiFocus":0})")));
 for(auto text:{R"({"Flags2":0})",R"({"Flags2":3})",R"({"Flags2":1,"GuiFocus":5})",R"({"Flags":0})"})assert(!onFootStatus(nlohmann::json::parse(text)));
 assert(installBridge()&&slot()<4);nativeRegistered=true;assert(ready());assert(configure(true));setFocused(true);statusAllowed=true;statusTime=GetTickCount();
 submit(.4f,.8f,true);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX>0&&state.Gamepad.sThumbLY>0&&state.Gamepad.wButtons==0);
 setFocused(false);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0);
 setFocused(true);submit(.4f,.8f,true);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX>0);
 const auto initial=state;assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.dwPacketNumber==initial.dwPacketNumber);
 menu=true;assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0);menu=false;
 submit(1,0,false);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0);
 submit(1,0,true);statusAllowed=false;assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0);statusAllowed=true;
 submit(1,0,true);Sleep(220);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0);
 submit(1,0,true);const auto real=original;original=physical;hardwareProbeTime=GetTickCount()-501;assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==1234&&state.Gamepad.wButtons==XINPUT_GAMEPAD_A);original=real;physicalAttached=false;
 setDeadZones(40,10);submit(.3f,0,true);submitTurn(.3f,true);
 assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0&&state.Gamepad.sThumbRX>0&&state.Gamepad.sThumbRY==0);
 setDeadZones(10,40);submit(.3f,0,true);submitTurn(.3f,true);
 assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX>0&&state.Gamepad.sThumbRX==0);
 setDeadZones(20,20);submit(1,0,true);submitTurn(-1,true);
 assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==32767&&state.Gamepad.sThumbRX==-32767);
 Sleep(220);submit(.6f,0,true);
 assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX>0&&state.Gamepad.sThumbRX==0);
 assert(configureInteraction(true));statusAllowed=true;statusTime=GetTickCount();
 submitInteraction(true);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==XINPUT_GAMEPAD_X);
 configure(false);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==XINPUT_GAMEPAD_X&&state.Gamepad.sThumbLX==0);
 menu=true;assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==0);menu=false;
 submitInteraction(true);setFocused(false);setFocused(true);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==0);
 submitInteraction(true);Sleep(220);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==0);
 configureInteraction(false);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==0);
 InteractionLatch latch;assert(!latch.update(true,true));assert(!latch.update(false,true));assert(latch.update(true,true));
 assert(!latch.update(true,false)&&!latch.update(true,true));latch.update(false,true);assert(latch.update(true,true));latch.cancel();assert(!latch.update(true,true));
 configure(false);assert(XInputGetState(slot(),&state)==ERROR_SUCCESS&&state.Gamepad.sThumbLX==0&&state.Gamepad.sThumbLY==0&&state.Gamepad.sThumbRX==0);
 puts("PASS actual XInput IAT bridge, continuous axes, packet stability, on-foot/menu gating, watchdog, real-controller precedence and disable neutralization");
}
