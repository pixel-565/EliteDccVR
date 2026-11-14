#include "../shared-vr-ui/game-native/galaxy.cpp"
#include <cassert>
#include <cstdio>
int main(){
 using namespace NativeGalaxy;
 alignas(8) unsigned char object[128]{},data[128]{};
 auto store=[](unsigned char* p,size_t off,uint64_t v){memcpy(p+off,&v,8);};
 const uint64_t id=48996147307082ull;
 store(object,0x20,2);store(object,0x28,id);store(object,0x38,reinterpret_cast<uintptr_t>(data));store(data,0,id);data[0x58]=5;
 const unsigned n=unsigned(id&7);
 auto put16=[&](size_t off,unsigned v){unsigned short value=static_cast<unsigned short>(v);memcpy(data+off,&value,2);};
 put16(0x42,1294466-unsigned((((id>>(30-2*n))<<n)&0x3fff)*320));
 put16(0x44,1281970-unsigned((((id>>(17-n))<<n)&0x1fff)*320));
 put16(0x46,1404932-unsigned((((id>>3)<<n)&0x3fff)*320));
 Star result{};assert(decode(reinterpret_cast<uintptr_t>(object),result));
 assert(result.x==40452.0625f&&result.y==40061.5625f&&result.z==43904.125f);
 store(object,0x28,id+8);assert(!decode(reinterpret_cast<uintptr_t>(object),result));store(object,0x28,id);
 data[0x58]=1;assert(!decode(reinterpret_cast<uintptr_t>(object),result));data[0x58]=5;
 store(object,0x20,1);assert(!decode(reinterpret_cast<uintptr_t>(object),result));store(object,0x20,2);
 assert(!decode(0,result));enabled=true;cache={{1,2,3,10,1},{1,2,3,10,2}};
 assert(resolve(1,2,3,result)&&result.id==10);
 std::vector<Star> nearbyStars;nearby(1,2,3,1,nearbyStars);assert(nearbyStars.size()==1&&nearbyStars[0].id==10);
 cache.push_back({1,2,3,11,3});assert(!resolve(1,2,3,result));assert(!resolve(9,9,9,result));nearbyStars.clear();nearby(1,2,3,1,nearbyStars);assert(nearbyStars.empty());
 select({1,2,3,10,1});assert(requested);enable(false);assert(!requested&&!resolve(1,2,3,result));
 // Regression: the manager is the last pointer of a page, the type starts the next page.
 auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
 const uintptr_t manager=0x1234567812345678ull,type=0x2345678923456789ull;
 memcpy(memory+4088,&manager,8);memcpy(memory+4096,&type,8);
 unsigned char snapshot[4096]{};memcpy(snapshot,memory,4096);
 assert(candidateMatches(snapshot,4096,4088,reinterpret_cast<uintptr_t>(memory),manager,type));
 unsigned matches=0;for(size_t off=0;off+8<=4096;off+=8)if(candidateMatches(snapshot,4096,off,reinterpret_cast<uintptr_t>(memory),manager,type))++matches;
 assert(matches==1);
 assert(!candidateMatches(snapshot,4096,4088,reinterpret_cast<uintptr_t>(memory),manager,type+1));
 assert(!candidateMatches(snapshot,4095,4088,reinterpret_cast<uintptr_t>(memory),manager,type));
 DWORD old{};assert(VirtualProtect(memory+4096,4096,PAGE_NOACCESS,&old));
 assert(!candidateMatches(snapshot,4096,4088,reinterpret_cast<uintptr_t>(memory),manager,type));
 VirtualFree(memory,0,MEM_RELEASE);
 puts("Page-boundary handle identity and unreadable-neighbour regression passed.");
 puts("Native star coordinates, identity consistency, load state, kind, ambiguity and disable cancellation passed.");
}
