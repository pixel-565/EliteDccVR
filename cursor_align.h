#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
// Bounded feedback offsets in normalized Windows input coordinates.
// Only stationary, fresh observations may update the offsets.
struct CursorAlign
{
    struct Command { uint64_t stamp=0; float u=0,v=0,outX=0,outY=0,offsetX=0,offsetY=0; };
    std::array<Command,128> history{};
    unsigned historyCount=0,historyNext=0;
    float x = 0, y = 0, rayX = 0, rayY = 0;
    uint64_t stableSince = 0, lastSample = 0, lastCorrection = 0, missingSince = 0;
    unsigned errors = 0;
    float previousErrorX = 0, previousErrorY = 0;
    bool tracking = false;
    void record(uint64_t stamp,float u,float v,float outX,float outY,float offsetX,float offsetY)
    {
        history[historyNext]={stamp,u,v,outX,outY,offsetX,offsetY};
        historyNext=(historyNext+1)%history.size();historyCount=std::min(historyCount+1,unsigned(history.size()));
    }
    bool observe(uint64_t now,uint64_t sample,float u,float v,float cursorU,float cursorV,float baseX,
                 float baseY,bool pause,float responseX=1.f,float responseY=1.91f)
    {
        if(pause){suspendObservation();return false;}
        if(!sample || sample>now || now-sample>150 || sample<64)return false;
        const Command *reference=nullptr,*boundary=nullptr;
        for(unsigned i=0;i<historyCount;++i) {
            const auto &entry=history[(historyNext+history.size()-1-i)%history.size()];
            if(!reference && entry.stamp<=sample-32)reference=&entry;
            if(entry.stamp<=sample-64){boundary=&entry;break;}
        }
        // Account for uncertain input-to-render delay. Never infer mapping error
        // from a command transition or an observation predating the sent history.
        if(!reference || !boundary || sample-boundary->stamp>100)return false;
        if(std::abs(u-reference->u)*1680>8 || std::abs(v-reference->v)*1050>8) {errors=0;return false;}
        for(unsigned i=0;i<historyCount;++i) {
            const auto &entry=history[(historyNext+history.size()-1-i)%history.size()];
            if(entry.stamp>sample)continue;
            if(std::abs(entry.u-reference->u)*1680>4 || std::abs(entry.v-reference->v)*1050>4 ||
               std::abs(entry.outX-reference->outX)*responseX*1680>4 ||
               std::abs(entry.outY-reference->outY)*responseY*1050>4) {errors=0;return false;}
            if(&entry==boundary)break;
        }
        // Convert the historical residual into the correction still needed now.
        // A delayed sample must not add an already-applied correction twice.
        float errorX=reference->u-cursorU+responseX*(reference->offsetX-x);
        float errorY=reference->v-cursorV+responseY*(reference->offsetY-y);
        return update(now,sample,u,v,u-errorX,v-errorY,baseX,baseY,false,responseX,responseY);
    }
    void reset()
    {
        *this = {};
    }
    void observationLost(uint64_t now)
    {
        // Loss of measurement is not evidence that the input mapping changed.
        // Keep the last working offsets and restart only observation timing.
        if (!missingSince) suspendObservation();
        missingSince = now;
    }
    void suspendObservation()
    {
        const float savedX = x, savedY = y;
        reset();
        x = savedX;
        y = savedY;
    }
    bool update(uint64_t now, uint64_t sample, float u, float v, float cursorU, float cursorV, float baseX,
                float baseY, bool pause, float responseX = 1.f, float responseY = 1.91f)
    {
        missingSince = 0;
        if (!sample || sample > now || now - sample > 150 ||
            !std::isfinite(u) || !std::isfinite(v) || !std::isfinite(cursorU) || !std::isfinite(cursorV) ||
            !std::isfinite(baseX) || !std::isfinite(baseY) ||
            !std::isfinite(responseX) || !std::isfinite(responseY) || responseX <= 0 || responseY <= 0)
            return false;
        if (!tracking || std::abs(u - rayX) * 1680 > 8 || std::abs(v - rayY) * 1050 > 8)
        {
            rayX = u;
            rayY = v;
            stableSince = now;
            tracking = true;
            errors = 0;
        }
        if (pause)
        {
            stableSince = now;
            errors = 0;
            lastSample = sample;
            return false;
        }
        if (sample <= lastSample)
            return false;
        lastSample = sample;
        // Command history in observe() supplies settling evidence. Keep the
        // post-correction guard until the game has had time to render feedback.
        if (lastCorrection && (now - lastCorrection < 90 || sample <= lastCorrection + 80))
            return false;
        float ex = (u - cursorU) * 1680, ey = (v - cursorV) * 1050;
        if (std::abs(ex) <= 6 && std::abs(ey) <= 6)
        {
            errors = 0;
            return false;
        }
        auto consistent = [](float a, float b) {
            return (std::abs(a) <= 6 && std::abs(b) <= 6) ||
                   (a * b > 0 && std::abs(a - b) <= std::max(12.f, std::abs(b) * .2f));
        };
        if (!errors || !consistent(ex, previousErrorX) || !consistent(ey, previousErrorY))
        {
            previousErrorX = ex;
            previousErrorY = ey;
            errors = 1;
            return false;
        }
        errors = 0;
        lastCorrection = now;
        auto step = [](float error, float pixels, float response) {
            // Invert the measured game response; verify the result with later samples.
            return std::abs(error) <= 6 ? 0.f : error / (pixels * response);
        };
        x = std::clamp(x + step(ex, 1680, responseX), std::max(-.45f, -baseX), std::min(.45f, 1 - baseX));
        y = std::clamp(y + step(ey, 1050, responseY), std::max(-.45f, -baseY), std::min(.45f, 1 - baseY));
        return true;
    }
};
