#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cstring>
#include "MinHook.h"
#include "galaxy.h"
#pragma comment(lib,"psapi.lib")
namespace NativeGalaxy {
static uintptr_t base{};
static std::atomic<uint64_t> inputCalls{},cacheCount{},queued{},selectedCount{},rejected{};
static std::atomic<bool> enabled{};
static std::atomic<uintptr_t> input{};
static std::mutex cacheMutex,requestMutex;
static std::vector<Star> cache;
static Star request{};
static ULONGLONG requested{};
static void (__fastcall* original)(void*);
static uintptr_t q(uintptr_t p){return *reinterpret_cast<uintptr_t*>(p);}
static bool decode(uintptr_t object,Star& star){
 __try {
  if(!object||*reinterpret_cast<unsigned*>(object+0x20)!=2)return false;
  const auto data=q(object+0x38);const uint64_t id=q(object+0x28);
  if(!id||q(data)!=id||*reinterpret_cast<unsigned char*>(data+0x58)!=5)return false;
  const unsigned n=unsigned(id&7);
  const auto local=[&](unsigned offset){return *reinterpret_cast<unsigned short*>(data+offset);};
  star={float(((((id>>(30-2*n))<<n)&0x3fff)*320+local(0x42)))/32,
        float(((((id>>(17-n))<<n)&0x1fff)*320+local(0x44)))/32,
        float(((((id>>3)<<n)&0x3fff)*320+local(0x46)))/32,id,object};
  return true;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool less(const Star& a,const Star& b){if(a.x!=b.x)return a.x<b.x;if(a.y!=b.y)return a.y<b.y;return a.z<b.z;}
static bool same(const Star& a,const Star& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
static void execute(uintptr_t owner,const Star& selected){
 __try {
  if(q(owner)!=base+0x552b380)return;
  const auto ui=q(owner+0xd8),nav=q(owner+0xa8);
  if(q(nav)!=base+0x552a9b8||q(ui)!=base+0x551fe98)return;
  if(*reinterpret_cast<int*>(ui+0x250)!=-1||!q(ui+0x258)||*reinterpret_cast<unsigned char*>(ui+0x804))return;
  Star live{};if(!decode(selected.object,live)||live.id!=selected.id||!same(live,selected))return;
  uintptr_t reference=live.object;
  reinterpret_cast<void(__fastcall*)(void*,const uintptr_t*)>(base+0x3aad490)(reinterpret_cast<void*>(ui),&reference);
  if(q(ui+0x5f0)==reference)++selectedCount;else ++rejected;
 }__except(EXCEPTION_EXECUTE_HANDLER){}
}
static void __fastcall update(void* owner){
 original(owner);input=reinterpret_cast<uintptr_t>(owner);
 if(!enabled)return;
 Star value{};ULONGLONG stamp{};
 {std::unique_lock lock(requestMutex,std::try_to_lock);if(!lock)return;value=request;stamp=requested;requested=0;}
 ++inputCalls;
 if(stamp&&GetTickCount64()-stamp<200)execute(reinterpret_cast<uintptr_t>(owner),value);
}
static bool identity(uintptr_t& manager,uintptr_t& type){
 __try{const auto owner=input.load();if(!owner||q(owner)!=base+0x552b380)return false;
 const auto nav=q(owner+0xa8);if(q(nav)!=base+0x552a9b8)return false;
 const auto star=q(nav+0x260);Star value{};if(!decode(star,value))return false;
 manager=q(star+8);type=q(star+0x10);return manager&&type;
 }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
static bool candidateMatches(const unsigned char* page,SIZE_T bytes,SIZE_T offset,uintptr_t address,uintptr_t manager,uintptr_t type){
 if(offset+8>bytes)return false;
 uintptr_t first{},second{};memcpy(&first,page+offset,8);if(first!=manager)return false;
 if(offset+16<=bytes)memcpy(&second,page+offset+8,8);
 else {
  // The identity pair can straddle pages even for an aligned resource handle.
  const auto next=address+offset+8;PSAPI_WORKING_SET_EX_INFORMATION resident{};
  resident.VirtualAddress=reinterpret_cast<void*>(next);
  if(!QueryWorkingSetEx(GetCurrentProcess(),&resident,sizeof(resident))||!resident.VirtualAttributes.Valid)return false;
  SIZE_T got{};if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(next),&second,8,&got)||got!=8)return false;
 }
 return second==type;
}
static DWORD WINAPI discover(void*){
 ULONGLONG next=0;
 for(;;){Sleep(100);if(!enabled||GetTickCount64()<next)continue;
  uintptr_t manager{},type{};if(!identity(manager,type))continue;
  std::vector<Star> found;found.reserve(32768);
  uintptr_t at=0;MEMORY_BASIC_INFORMATION mbi{};unsigned char page[4096]{};uint64_t slice=0;
  while(enabled&&VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof(mbi))==sizeof(mbi)){
   const auto end=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;if(end<=at)break;
   const auto protect=mbi.Protect&255;
   if(mbi.State==MEM_COMMIT&&mbi.Type==MEM_PRIVATE&&!(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&
      (protect==PAGE_READWRITE||protect==PAGE_WRITECOPY)){
    for(uintptr_t block=at;block<end&&enabled;){
     PSAPI_WORKING_SET_EX_INFORMATION resident[256]{};
     const size_t count=size_t(std::min<uintptr_t>(256,(end-block+4095)/4096));
     for(size_t n=0;n<count;++n)resident[n].VirtualAddress=reinterpret_cast<void*>(block+n*4096);
     if(QueryWorkingSetEx(GetCurrentProcess(),resident,DWORD(count*sizeof(resident[0]))))
      for(size_t n=0;n<count&&enabled;++n){
       if(!resident[n].VirtualAttributes.Valid)continue;
       const auto pageAt=block+n*4096;SIZE_T got{};
       if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(pageAt),page,4096,&got))continue;
       for(size_t off=0;off+8<=got;off+=8){
        if(pageAt+off<8||!candidateMatches(page,got,off,pageAt,manager,type))continue;
        Star value{};if(decode(pageAt+off-8,value))found.push_back(value);
       }
       slice+=got;if(slice>=8*1024*1024){Sleep(2);slice=0;}
      }
     block+=count*4096;
    }
   }at=end;
  }
  std::sort(found.begin(),found.end(),less);
  {std::lock_guard lock(cacheMutex);cache.swap(found);cacheCount=cache.size();}
  next=GetTickCount64()+15000;
 }
}
bool initialize(uintptr_t value){
 if(base)return true;base=value;
 constexpr unsigned char prefix[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
 if(memcmp(reinterpret_cast<void*>(base+0x3b8f7d0),prefix,sizeof(prefix)))return false;
 if(MH_CreateHook(reinterpret_cast<void*>(base+0x3b8f7d0),reinterpret_cast<void*>(&update),reinterpret_cast<void**>(&original))!=MH_OK)return false;
 if(MH_EnableHook(reinterpret_cast<void*>(base+0x3b8f7d0))!=MH_OK)return false;
 HANDLE worker=CreateThread(nullptr,0,discover,nullptr,0,nullptr);if(worker)CloseHandle(worker);return worker!=nullptr;
}
void enable(bool value){enabled=value;if(!value){std::lock_guard lock(requestMutex);requested=0;}}
bool resolve(float x,float y,float z,Star& result){
 if(!enabled)return false;std::unique_lock lock(cacheMutex,std::try_to_lock);if(!lock)return false;
 Star needle{x,y,z};auto entry=std::lower_bound(cache.begin(),cache.end(),needle,less);
 if(entry==cache.end()||!same(*entry,needle))return false;
 auto next=entry;for(++next;next!=cache.end()&&same(*next,needle);++next)if(next->id!=entry->id)return false;
 result=*entry;return true;
}
void nearby(float x,float y,float z,float radius,std::vector<Star>& result){
 if(!enabled||radius<=0||radius>32)return;
 std::unique_lock lock(cacheMutex,std::try_to_lock);if(!lock)return;
 Star low{x-radius,-3.4e38f,-3.4e38f};
 const float square=radius*radius;
 auto entry=std::lower_bound(cache.begin(),cache.end(),low,less);
 for(;entry!=cache.end()&&entry->x<=x+radius;++entry){
  const float dx=entry->x-x,dy=entry->y-y,dz=entry->z-z;
  if(dx*dx+dy*dy+dz*dz>square)continue;
  const auto end=std::upper_bound(entry,cache.end(),*entry,less);
  bool ambiguous=false;for(auto next=entry;next!=end;++next)if(next->id!=entry->id){ambiguous=true;break;}
  if(!ambiguous)result.push_back(*entry);
  entry=end-1;
 }
}

void counters(uint64_t (&v)[5]){v[0]=inputCalls;v[1]=cacheCount;v[2]=queued;v[3]=selectedCount;v[4]=rejected;}
void select(const Star& value){if(!enabled)return;std::lock_guard lock(requestMutex);request=value;requested=GetTickCount64();++queued;}
}
