#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <psapi.h>
#pragma comment(lib,"psapi.lib")
namespace NativeUI::Ship {
// This namespace runs AS3 calls only on the intercepted Movie::Advance thread.
struct Value {uint64_t prev=~0ull,next=~0ull;void* iface{};uint32_t type=3,pad{};uint64_t data[2]{};};
static_assert(sizeof(Value)==48);
using Invoke=bool(__fastcall*)(void*,void*,Value*,const char*,const Value*,uint64_t,bool);
using Closure=bool(__fastcall*)(void*,const Value*,Value*,const Value*,uint64_t);
using Advance=float(__fastcall*)(void*,float,uint32_t,bool);
inline Advance advanceOriginal{};
inline std::atomic<uintptr_t> wrapper{};
inline std::atomic<bool> enabled{};
inline uintptr_t base{};
inline std::array<std::atomic<uintptr_t>,16> knownPanels{};
inline std::atomic<unsigned> nextPanel{};
inline uintptr_t ptr(uintptr_t at){return *reinterpret_cast<const uintptr_t*>(at);}
inline bool classIs(uintptr_t object,const char* name){
 return object&&strcmp(reinterpret_cast<const char*>(ptr(ptr(ptr(object+0x28)+0xe0))),name)==0;
}
inline bool identity(uintptr_t object){
 __try{return object&&ptr(object)==base+0x55dc548&&
  !strcmp(reinterpret_cast<const char*>(ptr(ptr(object+0x108))),"Elite4CockpitServicesPanel");}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool alive(uintptr_t object){
 __try{return identity(object)&&ptr(object+0x3a8)&&ptr(ptr(object+0x3a8)+0x18);}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool visible(uintptr_t object){
 __try{return alive(object)&&*reinterpret_cast<const uint8_t*>(object+0x478)&&
  !*reinterpret_cast<const uint8_t*>(object+0x47c);}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool owns(uintptr_t object,void* movie){
 __try{return visible(object)&&ptr(ptr(object+0x3a8)+0x18)==reinterpret_cast<uintptr_t>(movie);}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline void remember(uintptr_t object){
 for(auto& item:knownPanels)if(item.load()==object)return;
 knownPanels[nextPanel.fetch_add(1)%knownPanels.size()]=object;
}
inline bool restoreKnown(){
 for(auto& item:knownPanels){const auto object=item.load();if(!object)continue;if(!identity(object)){item=0;continue;}if(visible(object)){wrapper=object;return true;}}
 return false;
}
inline bool scannerStackPage(uintptr_t address,uintptr_t bottom,uintptr_t top){
 return address>=bottom&&address<top;
}
inline DWORD WINAPI discover(void*){
 uintptr_t address=0;unsigned char page[4096]{};ULONGLONG lastScan=0;
 ULONG_PTR stackBottom=0,stackTop=0;GetCurrentThreadStackLimits(&stackBottom,&stackTop);
 while(true){
  if(!enabled.load()){Sleep(100);continue;}
  if(visible(wrapper.load())){Sleep(25);continue;}
  wrapper=0;
  if(restoreKnown()){Sleep(25);continue;}
  // Cached hidden panels are checked every 25 ms. Broad discovery is only a fallback.
  const auto now=GetTickCount64();
  bool cached=false;for(auto& item:knownPanels)cached|=alive(item.load());
  if(cached&&now-lastScan<1000){Sleep(25);continue;}
  lastScan=now;
  const auto until=GetTickCount64()+4;
  MEMORY_BASIC_INFORMATION region{};
  while(GetTickCount64()<until){
   if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))){address=0;break;}
   const auto end=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize;
   if(end<=address){address=0;break;}
   if(region.State!=MEM_COMMIT||region.Type!=MEM_PRIVATE||(region.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
      (region.Protect&0xff)!=PAGE_READWRITE){address=end;continue;}
   for(;address<end&&GetTickCount64()<until;){
    PSAPI_WORKING_SET_EX_INFORMATION resident[128]{};
    const auto count=static_cast<unsigned>(std::min<uintptr_t>(128,(end-address+4095)/4096));
    for(unsigned n=0;n<count;++n)resident[n].VirtualAddress=reinterpret_cast<void*>(address+n*4096);
    if(QueryWorkingSetEx(GetCurrentProcess(),resident,count*sizeof(resident[0]))){
     for(unsigned n=0;n<count;++n){
      if(!resident[n].VirtualAttributes.Valid)continue;
      const auto at=address+n*4096;SIZE_T got=0;
      // ReadProcessMemory copies complete wrapper bytes into page[] on this stack.
      // Scanning that buffer would register our own temporary copy as a live wrapper.
      if(scannerStackPage(at,stackBottom,stackTop))continue;
      if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),page,sizeof(page),&got))continue;
      for(size_t offset=0;offset+8<=got;offset+=8){
       uintptr_t value=0;memcpy(&value,page+offset,8);
       if(value==base+0x55dc548&&visible(at+offset)){remember(at+offset);wrapper=at+offset;break;}
      }
      if(wrapper.load())break;
     }
    }
    address+=count*4096;
    if(wrapper.load())break;
   }
   if(wrapper.load())break;
  }
  Sleep(25);
 }
}
inline uintptr_t eventReceiver(uintptr_t object,const char* wanted,Value* closure=nullptr){
 const auto registry=ptr(object+0x600),buckets=ptr(registry+0x30),count=ptr(registry+0x38);
 const auto manager=ptr(base+0x5eff5e0),stride=ptr(manager+0x10),end=ptr(manager+0x30);
 if(!buckets||count>4096||stride!=16)return 0;
 for(uintptr_t b=0;b<count;++b){
  const auto bucket=buckets+b*8;
  uintptr_t node=ptr(bucket);
  // Both empty buckets and the end of a collision chain point to the bucket itself.
  for(unsigned n=0;node&&node!=bucket&&n<128;++n,node=ptr(node)){
   const auto id=*reinterpret_cast<const uint16_t*>(node+8);
   const auto name=reinterpret_cast<const char*>((ptr(end-uintptr_t(id)*stride)&~uintptr_t{1})+6);
   if(strcmp(name,wanted))continue;
   const auto sentinel=node+0x10;
   auto link=ptr(sentinel);
   for(unsigned item=0;link&&link!=sentinel&&item<128;++item,link=ptr(link)){
    const auto* value=reinterpret_cast<const Value*>(link+0x10);
    if(value->type==75&&(value->data[0]&2)){
     if(closure)*closure=*value;
     return value->data[0]&~uintptr_t{2};
    }
   }
  }
 }
 return 0;
}
inline int target(float u,float v){
 if(!std::isfinite(u)||!std::isfinite(v)||u<.02f||u>.98f)return -1;
 if(v>=132.f/294&&v<=168.f/294){
  for(int i=0;i<4;++i)if(u>=(4.f+67*i)/269&&u<=(65.f+67*i)/269)return i+3;
 }
 constexpr float tops[]={171.f/294,210.f/294,250.f/294};
 for(int i=0;i<3;++i)if(v>=tops[i]&&v<=tops[i]+36.f/294)return i;
 return -1;
}
inline bool sendButton(Closure send,void* resource,const Value& input,const wchar_t* name){
 Value args[2]{};args[0].type=args[1].type=7;
 args[0].data[0]=reinterpret_cast<uintptr_t>(name);
 args[1].data[0]=reinterpret_cast<uintptr_t>(L"buttonDown");
 const bool down=send(resource,&input,nullptr,args,uint64_t{2});
 args[1].data[0]=reinterpret_cast<uintptr_t>(L"buttonUp");
 const bool up=send(resource,&input,nullptr,args,uint64_t{2});
 return down&&up;
}
inline bool enterIcons(Invoke invoke,Closure send,void* iface,uintptr_t icons,uintptr_t rows,void* resource,const Value& input,unsigned item){
 // IconButtonList.setIndex only sets its default index and validates layout.
 // Enter the group through the game's own focus routing, in this same UI update.
 // No OS input, selection confirmation or multi-frame navigation is generated.
 Value argument{};argument.data[0]=item;
 if(!invoke(iface,reinterpret_cast<void*>(icons),nullptr,"setIndex",&argument,uint64_t{1},false))return false;
 argument.data[0]=0;
 if(!invoke(iface,reinterpret_cast<void*>(rows),nullptr,"setIndex",&argument,uint64_t{1},false))return false;
 return sendButton(send,resource,input,L"UI_Up");
}
inline bool execute(uintptr_t object,void* movie,int index,bool click){
 if(index<0||index>6||!visible(object))return false;
 const auto resource=ptr(object+0x3a8);
 if(ptr(resource+0x18)!=reinterpret_cast<uintptr_t>(movie))return false;
 Value input{};
 const auto content=eventReceiver(object,"SetButtons"),manager=eventReceiver(object,"OnInputButton",&input);
 if(!classIs(content,"Content")||!classIs(manager,"InputManager")||input.data[1]!=19)return false;
 const auto list=ptr(content+(index<3?0x108:0x100));
 if(!classIs(list,index<3?"VerticleScrollingList":"IconButtonList"))return false;
 const auto vector=ptr(list+0x110),data=ptr(vector+0x58),count=ptr(vector+0x60);
 const unsigned item=index<3?index:index-3;
 if(count!=(index<3?3ull:4ull)||(*reinterpret_cast<const uint32_t*>(data+item*32)&31)!=12)return false;
 const auto button=ptr(data+item*32+16);
 if(!classIs(button,index<3?"Button":"IconButton")||ptr(reinterpret_cast<uintptr_t>(input.iface)+8)!=reinterpret_cast<uintptr_t>(movie))return false;
 if(!*reinterpret_cast<const uint8_t*>(button+0xb9))return false;
 // Focus is cheap to inspect; do not restart the focus animation every render frame.
 if(!*reinterpret_cast<const uint8_t*>(button+0xb8)){
  const auto invoke=reinterpret_cast<Invoke>(base+0x3a3020);
  if(index>=3){
   const auto rows=ptr(content+0x108);
   if(!classIs(rows,"VerticleScrollingList"))return false;
   if(!enterIcons(invoke,reinterpret_cast<Closure>(base+0x76f9b0),input.iface,list,rows,reinterpret_cast<void*>(resource),input,item))return false;
  }else{
   Value argument{};argument.data[0]=item;
   if(!invoke(input.iface,reinterpret_cast<void*>(list),nullptr,"setIndex",&argument,uint64_t{1},false))return false;
  }
 }
 if(!*reinterpret_cast<const uint8_t*>(button+0xb8))return false;
 if(click){
  return sendButton(reinterpret_cast<Closure>(base+0x76f9b0),reinterpret_cast<void*>(resource),input,L"UI_Select");
 }
 return true;
}
inline bool safeExecute(uintptr_t object,void* movie,int index,bool click){
 __try{return execute(object,movie,index,click);}__except(EXCEPTION_EXECUTE_HANDLER){wrapper=0;return false;}
}
}
