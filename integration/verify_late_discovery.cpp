#include "late_discovery.h"
#include "reconnect.h"
#include <iostream>
#include <set>
#include <stdexcept>
static void require(bool value){if(!value)throw std::runtime_error("Late discovery/control regression");}
int main(){try{
    LateDiscovery discovery;std::set<unsigned> visited;
    unsigned allowed=0;
    for(uint64_t now=1000;now<6000;now+=16) {
        unsigned attempts=0;
        for(unsigned i=0;i<8;++i) {
            if(discovery.allow(now,{i+1,7,0,0})){++attempts;++allowed;visited.insert(i);}
        }
        require(attempts<=1);
    }
    require(allowed<=20 && visited.size()==8);
    LateDiscovery evidence;
    require(!evidence.confirm(1000,{1,2,0,0}));
    require(!evidence.confirm(1010,{1,2,0,0}));
    require(!evidence.confirm(1050,{3,4,0,0}));
    require(evidence.confirm(1100,{3,4,0,0}));
    require(!evidence.allow(1200,{3,4,0,0}));
    // Valid geometry must get its second inspection despite many unrelated candidates.
    LateDiscovery crowded;
    auto valid=LateDiscovery::pipelineKey(18,7);
    bool first=false,confirmed=false;uint64_t firstStamp=0,secondStamp=0;
    for(uint64_t now=1000;now<10000 && !confirmed;now+=16) {
        unsigned attempts=0;
        for(unsigned i=0;i<32;++i) {
            auto key=LateDiscovery::pipelineKey(i+1,7);
            if(crowded.allow(now,key)) {
                ++attempts;
                if(key==valid) {
                    if(!first){first=true;firstStamp=now;require(!crowded.confirm(now,key));}
                    else {secondStamp=now;confirmed=crowded.confirm(now,key);}
                }
            }
        }
        require(attempts<=1);
    }
    require(confirmed && secondStamp-firstStamp<=272);
    // A full table must admit new pipelines after obsolete entries disappear.
    LateDiscovery churn;
    for(unsigned i=0;i<64;++i)churn.allow(1000,LateDiscovery::pipelineKey(i+1,7));
    require(churn.entries.size()==64);
    churn.allow(3101,LateDiscovery::pipelineKey(999,7));
    require(std::any_of(churn.entries.begin(),churn.entries.end(),[](const auto &entry){return entry.key[0]==999;}));
    LateDiscovery sharedPipeline;
    LateDiscovery::Key pipeline=LateDiscovery::pipelineKey(18,7),draw={18,7,6,4};
    require(sharedPipeline.allow(1000,pipeline));require(!sharedPipeline.allow(1000,draw));
    require(!sharedPipeline.allow(1250,pipeline));require(sharedPipeline.allow(1250,draw));
    require(!sharedPipeline.confirm(1250,pipeline,draw));
    require(!sharedPipeline.allow(1500,pipeline));require(sharedPipeline.allow(1500,draw));
    require(sharedPipeline.confirm(1500,pipeline,draw));
    Reconnect::Control server;
    require(server.open(GetCurrentProcessId(),true));
    Reconnect::Control client;require(client.open(GetCurrentProcessId(),false));
    InterlockedExchange(&client.state->requestedOwner,123);
    LONG sequence=InterlockedIncrement(&client.state->sequence);
    require(server.state->requestedOwner==123 && server.state->sequence==sequence);
    InterlockedExchange(&server.state->acknowledged,sequence);
    require(client.state->acknowledged==sequence);
    server.state->protocol^=1;
    {Reconnect::Control wrong;require(!wrong.open(GetCurrentProcessId(),false));}
    server.state->protocol=LiveUI::Magic;
    require(Reconnect::ownerAlive(GetCurrentProcessId()) && !Reconnect::ownerAlive(0));
    std::cout<<"PASS bounded fair discovery, independent confirmations, discovery stop, versioned reconnect IPC and owner liveness\n";
    return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
