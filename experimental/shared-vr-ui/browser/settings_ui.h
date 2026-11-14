#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <string>

namespace SharedBrowserSettings {
inline constexpr float Width=1200.f;
inline D2D1_RECT_F opacityRect(){return {400,160,1100,220};}
inline D2D1_RECT_F curveRect(int id)
{
    if (id == 488)
        return {60, 375, 385, 435};
    if (id == 489)
        return {405, 375, 755, 435};
    return {775, 375, 1140, 435};
}

inline int hit(float x,float y){
 auto inside=[&](D2D1_RECT_F r){return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;};
 if(inside(opacityRect()))return 480;
 for(int id:{488,489,491})if(inside(curveRect(id)))return id;
 return -1;
}

template<class Theme>
void draw(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,IDWriteTextFormat* font,
          float browserOpacity,float browserCurveDegrees,int hover,int pressed,int browserSettingDrag,Theme ThemeColor){
        auto label = [&](const std::wstring &value, D2D1_RECT_F rect) {
            brush->SetColor(ThemeColor(0xeaf1ff));
            target->DrawText(value.c_str(), static_cast<UINT32>(value.size()), font, rect,
                                    brush);
        };
        label(L"Browser settings", D2D1::RectF(60, 35, 1140, 80));
        label(L"Opacity", D2D1::RectF(60, 165, 350, 210));
        label(std::to_wstring(int(std::round(browserOpacity * 100))) + L"%",
              D2D1::RectF(400, 115, 1100, 155));
        auto rect = opacityRect();
        float y = (rect.top + rect.bottom) * .5f;
        float knob = rect.left + (browserOpacity - .3f) / .7f * (rect.right - rect.left);
        brush->SetColor(ThemeColor(0x3b526b));
        target->DrawLine(D2D1::Point2F(rect.left, y), D2D1::Point2F(rect.right, y), brush,
                                6);
        brush->SetColor(ThemeColor(0x55d4ff));
        target->DrawLine(D2D1::Point2F(rect.left, y), D2D1::Point2F(knob, y), brush, 6);
        float radius = hover == 480 || browserSettingDrag == 480 ? 17.f : 14.f;
        target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knob, y), radius, radius), brush);
        label(L"Adjusts the browser panel only. Changes are saved automatically.",
              D2D1::RectF(60, 245, 1140, 290));
        label(L"Horizontal curve: " + std::to_wstring(int(browserCurveDegrees)) + L" deg",
              D2D1::RectF(60, 315, 1140, 360));
        for (int id : {488, 489, 491})
        {
            auto r = curveRect(id);
            brush->SetColor(ThemeColor(pressed == id ? 0x995a20
                                                : hover == id ? 0x396385
                                                              : 0x22374e));
            target->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), brush);
            r.left += 12;
            r.top += 12;
            label(id == 488 ? L"Curve -" : id == 489 ? L"Flat" : L"Curve +", r);
        }
 }
}
