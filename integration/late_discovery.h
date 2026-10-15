#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
struct LateDiscovery
{
    using Key=std::array<uint64_t,4>;
    // Buffer allocation offsets are transient draw arguments, not object identity.
    // Each inspection still validates the current indices, vertices and transforms.
    static Key pipelineKey(uint64_t shader,uint64_t layout){return {shader,layout,0,0};}
    struct Entry {Key key;uint64_t seen=0,tried=0;};
    std::vector<Entry> entries;
    uint64_t window=0;
    int selected=-1;
    Key evidence{},priority{};
    uint64_t evidenceStamp=0;
    unsigned confirmations=0;
    bool resolved=false;
    bool allow(uint64_t now,const Key &key) {
        if(resolved)return false;
        auto entry=std::find_if(entries.begin(),entries.end(),[&](const auto &v){return v.key==key;});
        if(entry==entries.end()) {
            if(entries.size()>=64) {
                auto oldest=std::min_element(entries.begin(),entries.end(),[](const auto &a,const auto &b){return a.seen<b.seen;});
                if(now-oldest->seen<=2000)return false;
                int replaced=int(oldest-entries.begin());
                if(selected==replaced)selected=-1;
                *oldest={key,now,0};entry=oldest;
            } else {entries.push_back({key,now,0});entry=entries.end()-1;}
        }
        int index=int(entry-entries.begin());entry->seen=now;
        if(!window || now-window>=250) {
            window=now;selected=-1;
            // A full successful validation earns the next bounded inspection.
            // Otherwise a large candidate set can outlive the confirmation window.
            if(confirmations && now-evidenceStamp<=3000) {
                for(unsigned i=0;i<entries.size();++i)
                    if(entries[i].key==priority && now-entries[i].seen<=2000){selected=int(i);break;}
            }
            if(selected<0)
            for(unsigned i=0;i<entries.size();++i) {
                if(now-entries[i].seen>2000)continue;
                if(selected<0 || entries[i].tried<entries[selected].tried)selected=int(i);
            }
        }
        if(index!=selected)return false;
        selected=-1;entries[index].tried=now;return true;
    }
    bool confirm(uint64_t now,const Key &key) {return confirm(now,key,key);}
    bool confirm(uint64_t now,const Key &key,const Key &draw) {
        priority=draw;
        if(!confirmations || evidence!=key || now-evidenceStamp>3000) {
            evidence=key;evidenceStamp=now;confirmations=1;return false;
        }
        if(now-evidenceStamp<33)return false;
        resolved=true;return true;
    }
};
