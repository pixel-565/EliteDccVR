#include "../cursor_align.h"
#include <iostream>
#include <deque>
#include <source_location>
#include <string>
#include <stdexcept>
void require(bool b, std::source_location where=std::source_location::current())
{
    if (!b)
        throw std::runtime_error("Alignment regression at line "+std::to_string(where.line()));
}
int main()
{
    try
    {
        for (float base : {.5f, .8f})
        {
            CursorAlign a;
            float target = .5f, cursor = .99f, lastOutput = base;
            uint64_t settled = 0;
            unsigned corrections = 0;
            for (uint64_t now = 1000; now < 11000; now += 34)
            {
                if(a.update(now, now, target, target, target, cursor, .5f, base, false)) ++corrections;
                float next = base + a.y;
                cursor += 1.91f * (next - lastOutput);
                lastOutput = next;
                if(!settled && std::abs(cursor-target)*1050 <= 6.1f) settled=now-1000;
            }
            require(std::abs(cursor - target) * 1050 <= 6.1f);
            require(settled > 0 && settled < 250 && corrections == 1);
            std::cout << "Recovery simulation: " << settled << " ms\n";
            require(std::abs(a.x) < .00001f);
            float before = a.y;
            for(int i=0;i<200;++i)a.suspendObservation();
            require(a.y==before && a.x==0 && !a.tracking && a.lastSample==0 && a.errors==0);
            // The first returned output retains its correction before feedback can run.
            require(std::abs((base+a.y)-lastOutput)<.000001f);
            require(!a.update(11000,10900,.5f,.5f,.5f,.99f,.5f,base,false));
            require(a.y==before);
            for (uint64_t now = 11000; now < 12000; now += 34)
                a.update(now, now, .5f, .5f, .5f, .9f, .5f, base, true);
            require(before == a.y);
            a.reset();
            require(a.x == 0 && a.y == 0 && !a.tracking);
        }
        for(float response : {1.5f,2.3f}) {
            CursorAlign a;float cursor=.95f,previous=.5f;
            for(uint64_t now=1000;now<4000;now+=34){
                a.update(now,now,.5f,.5f,.5f,cursor,.5f,.5f,false);
                float next=.5f+a.y;cursor+=response*(next-previous);previous=next;
            }
            require(std::abs(cursor-.5f)*1050<=6.1f);
        }
        CursorAlign delayed;
        delayed.update(1000,1000,.5f,.5f,.5f,.99f,.5f,.5f,false);
        require(delayed.update(1034,1034,.5f,.5f,.5f,.99f,.5f,.5f,false));
        float heldOffset=delayed.y;
        delayed.update(1090,1040,.5f,.5f,.5f,.99f,.5f,.5f,false);
        delayed.update(1100,1050,.5f,.5f,.5f,.99f,.5f,.5f,false);
        require(delayed.y==heldOffset);
        CursorAlign stale;
        for (uint64_t now = 1000; now < 10000; now += 34)
            stale.update(now, 1000, .5f, .5f, .5f, .99f, .5f, .5f, false);
        require(stale.y == 0);
        CursorAlign moving;
        for (uint64_t now = 1000; now < 10000; now += 34)
        {
            float u = (now / 34) % 2 ? .3f : .7f;
            moving.update(now, now, u, .5f, .5f, .99f, u, .5f, false);
        }
        require(moving.y == 0);
        CursorAlign clamp;
        for (uint64_t now = 1000; now < 30000; now += 34)
            clamp.update(now, now, .5f, .5f, .5f, 1.f, .5f, .2f, false);
        require(clamp.y >= -.2f && clamp.y <= .45f);
        CursorAlign recovery;
        recovery.x=.2f; recovery.y=-.1f;
        recovery.lastSample=900; recovery.lastCorrection=950;
        recovery.observationLost(1000);
        recovery.observationLost(1499);
        require(recovery.x==.2f && recovery.y==-.1f);
        recovery.observationLost(1500);
        require(recovery.x==.2f && recovery.y==-.1f && recovery.lastSample==0 && recovery.lastCorrection==0);
        recovery.update(1534,1534,.5f,.5f,.5f,.99f,.5f,.5f,false);
        require(recovery.missingSince==0);
        require(recovery.update(1568,1568,.5f,.5f,.5f,.99f,.5f,.5f,false));
        CursorAlign brief;
        brief.x=.2f;
        brief.observationLost(1000);
        brief.update(1100,1100,.5f,.5f,.5f,.5f,.5f,.5f,false);
        brief.observationLost(1400);
        require(brief.x==.2f && brief.missingSince==1400);
        // Simulate queued input delivery and separately sampled render observations.
        for (unsigned latency : {16u, 32u, 64u}) for (float gain : {.53f, .75f, 1.f})
        {
            CursorAlign feedback;
            float actual=.95f, sent=.5f, output=.5f, observedCursor=actual;
            uint64_t stamp=0;
            std::deque<std::pair<uint64_t,float>> pending;
            unsigned changes=0;
            for(uint64_t now=1000;now<3000;now+=16)
            {
                while(!pending.empty() && pending.front().first<=now) {
                    actual=std::clamp(actual+pending.front().second,0.f,1.f);
                    pending.pop_front();
                }
                if((now-1000)%32==0) { observedCursor=actual;stamp=now; }
                if(feedback.update(now,stamp,.5f,.5f,.5f,observedCursor,.5f,.5f,false,1.f,1.f/gain)) ++changes;
                output=.5f+feedback.y;
                if(std::abs(output-sent)>.000001f) {
                    pending.push_back({now+latency,(output-sent)/gain});sent=output;
                }
            }
            require(std::abs(actual-.5f)*1050<=6.1f && changes==1);
            float saved=feedback.y;
            for(uint64_t now=3000;now<6000;now+=16)feedback.observationLost(now);
            require(feedback.y==saved);
        }
        CursorAlign outlier;
        outlier.update(1000,1000,.5f,.5f,.5f,.5f,.5f,.5f,false);
        require(!outlier.update(1080,1080,.5f,.5f,.9f,.9f,.5f,.5f,false));
        require(!outlier.update(1114,1114,.5f,.5f,.5f,.5f,.5f,.5f,false));
        require(outlier.x==0 && outlier.y==0);
        outlier.update(1200,1190,.5f,.5f,.9f,.9f,.5f,.5f,false);
        require(!outlier.update(1210,1180,.5f,.5f,.9f,.9f,.5f,.5f,false));
        require(outlier.x==0 && outlier.y==0);
        // Exercise the production history path, with independently delayed game input.
        for(unsigned delay : {16u,33u,66u}) for(float gain : {.53f,1.f}) {
            CursorAlign feedback;
            struct Pending {uint64_t when;float delta;};std::deque<Pending> queue;
            float actual=.2f,previous=.5f+(.2f-.5f)*gain,observedCursor=actual;
            uint64_t stamp=0,jumpTime=0,settled=0;
            unsigned corrections=0;
            for(unsigned frame=0;frame<300;++frame) {
                uint64_t now=1000+frame*11;
                float target=now<1200?.2f:now<1600?.2f+.6f*(now-1200)/400.f:.8f;
                while(!queue.empty() && queue.front().when<=now) {
                    actual=std::clamp(actual+queue.front().delta,0.f,1.f);queue.pop_front();
                }
                if(now>=2000 && !jumpTime){jumpTime=now;actual=.98f;}
                if(frame%3==0){stamp=now;observedCursor=actual;}
                float base=.5f+(target-.5f)*gain;
                bool corrected=feedback.observe(now,stamp,.5f,target,.5f,observedCursor,.5f,base,false,1.f,1.f/gain);
                if(corrected){require(jumpTime!=0);++corrections;}
                float output=std::clamp(base+feedback.y,0.f,1.f);
                if(std::abs(output-previous)>.000001f){queue.push_back({now+delay,(output-previous)/gain});previous=output;}
                feedback.record(now,.5f,target,.5f,output,0,output-base);
                if(jumpTime && !settled && std::abs(actual-target)*1050<=6.1f)settled=now-jumpTime;
            }
            require(settled>0 && settled<220 && corrections==1 && std::abs(actual-.8f)*1050<=6.1f);
            std::cout<<"History feedback, input delay "<<delay<<" ms: settled "<<settled<<" ms, one correction\n";
        }
        CursorAlign noHistory;
        require(!noHistory.observe(1000,1000,.5f,.5f,1.f,1.f,.5f,.5f,false));
        std::cout << "PASS delayed input delivery, sensitivity response, outliers, ordered samples and loss recovery; alignment convergence, axis isolation, trigger pause, stale samples, moving ray, "
                     "bounds and reset\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
