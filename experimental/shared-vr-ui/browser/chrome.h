#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <cmath>
#include <cwchar>
#include <initializer_list>

namespace BrowserChrome {
inline constexpr float Width=1200.f;
inline constexpr float FooterHeight=56.f;
inline D2D1_RECT_F controlRect(int id,float width=Width)
    {
        switch (id)
        {
        case 400:
            return D2D1::RectF(10, 10, 55, 55);
        case 401:
            return D2D1::RectF(65, 10, 110, 55);
        case 402:
            return D2D1::RectF(120, 10, 165, 55);
        case 415:
            return D2D1::RectF(540, 10, 585, 55);
        case 416:
            return D2D1::RectF(595, 10, 640, 55);
        case 417:
            return D2D1::RectF(340, 10, 430, 55);
        case 418:
            return D2D1::RectF(440, 10, 530, 55);
        case 412:
            return D2D1::RectF(285, 10, 330, 55);
        case 406:
            return D2D1::RectF(230, 10, 275, 55);
        case 405:
            return D2D1::RectF(width-55, 10, width-10, 55);
        case 403:
            return D2D1::RectF(10, 125, width-115, 175);
        case 404:
            return D2D1::RectF(width-105, 125, width-10, 175);
        case 408:
            return D2D1::RectF(175, 10, 220, 55);
        default:
            return D2D1::RectF(0, 0, 0, 0);
        }
    }

inline D2D1_RECT_F resizeRect(int side,float bottom,float width=Width){
    return side<0?D2D1::RectF(4,bottom-52,64,bottom-4):D2D1::RectF(width-64,bottom-52,width-4,bottom-4);
}
inline bool inside(D2D1_RECT_F r,float x,float y){
    return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;
}
inline constexpr int Icons[]={400,401,402,408,406,412,415,416,405};
template<class Theme>
void drawIcon(int id,D2D1_RECT_F rect,ID2D1RenderTarget* iconTarget,ID2D1SolidColorBrush* iconBrush,
              float browserPageZoom,bool available,Theme ThemeColor){
        float x = (rect.left + rect.right) * .5f, y = (rect.top + rect.bottom) * .5f;
        iconBrush->SetColor(ThemeColor(available?0xeaf1ff:0x75808d));
        auto line = [&](float ax, float ay, float bx, float by) {
            iconTarget->DrawLine(D2D1::Point2F(x + ax, y + ay), D2D1::Point2F(x + bx, y + by),
                                    iconBrush, 2.5f);
        };
        if (id == 415 || id == 416)
        {
            bool enabled = id == 415 ? browserPageZoom > .5f : browserPageZoom < 2.f;
            if (!enabled)
                iconBrush->SetColor(ThemeColor(0x75808d));
            line(-10, 0, 10, 0);
            if (id == 416)
                line(0, -10, 0, 10);
        }
        else if (id == 405)
        {
            line(-8, -8, 8, 8);
            line(-8, 8, 8, -8);
        }
        else if (id == 412)
        {
            iconTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 9, 9), iconBrush, 2.5f);
            iconTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 3, 3), iconBrush, 2);
            for (int i = 0; i < 8; ++i)
            {
                float angle = i * 3.14159265f / 4;
                line(9 * std::cos(angle), 9 * std::sin(angle), 14 * std::cos(angle), 14 * std::sin(angle));
            }
        }
        else if (id == 400 || id == 401)
        {
            float d = id == 400 ? -1.f : 1.f;
            line(-10 * d, 0, 10 * d, 0);
            line(10 * d, 0, 2 * d, -8);
            line(10 * d, 0, 2 * d, 8);
        }
        else if (id == 406)
        {
            iconTarget->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(x - 15, y - 11, x + 15, y + 11), 3, 3), iconBrush, 2);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 6; ++col)
                {
                    float px = x - 12 + col * 4.4f, py = y - 7 + row * 4.f;
                    iconTarget->FillRectangle(D2D1::RectF(px, py, px + 2.5f, py + 2.5f),
                                                 iconBrush);
                }
            iconTarget->FillRectangle(D2D1::RectF(x - 7, y + 6, x + 7, y + 8), iconBrush);
        }
        else if (id == 408)
        {
            line(-13, 0, 0, -11);
            line(0, -11, 13, 0);
            line(-9, -3, -9, 11);
            line(-9, 11, -3, 11);
            line(-3, 11, -3, 3);
            line(-3, 3, 3, 3);
            line(3, 3, 3, 11);
            line(3, 11, 9, 11);
            line(9, 11, 9, -3);
        }
        else
        {
            constexpr float end = 5.3407075f;
            for (int i = 0; i < 24; ++i)
            {
                float a = end * i / 24.f, b = end * (i + 1) / 24.f;
                line(11 * std::cos(a), 11 * std::sin(a), 11 * std::cos(b), 11 * std::sin(b));
            }
            float px = 11 * std::cos(end), py = 11 * std::sin(end), tx = -std::sin(end), ty = std::cos(end);
            line(px, py, px - 6 * tx + 4 * std::cos(end), py - 6 * ty + 4 * std::sin(end));
            line(px, py, px - 6 * tx - 4 * std::cos(end), py - 6 * ty - 4 * std::sin(end));
        }
}
template<class Theme>
void drawFooter(ID2D1RenderTarget* target,ID2D1SolidColorBrush* brush,IDWriteTextFormat* font,
                float bottom,int hover,int corner,Theme ThemeColor,float width=Width){
        brush->SetColor(ThemeColor(0x172b40));
        target->FillRectangle({0, bottom - 56, width, bottom}, brush);
        for (int side : {-1, 1})
        {
            auto rect = resizeRect(side,bottom,width);
            int id = side < 0 ? 460 : 461;
            bool selected = corner == side || hover == id;
            brush->SetColor(ThemeColor(selected ? 0x396385 : 0x22374e));
            target->FillRoundedRectangle(D2D1::RoundedRect(rect, 7, 7), brush);
            brush->SetColor(ThemeColor(selected ? 0x55d4ff : 0xeaf1ff));
            float x = side < 0 ? rect.left + 14 : rect.right - 14, y = rect.bottom - 12;
            target->DrawLine({x, y - 22}, {x, y}, brush, 3);
            target->DrawLine({x, y}, {x - side * 22, y}, brush, 3);
            target->DrawLine({x - side * 9, y - 9}, {x - side * 21, y - 21}, brush, 3);
        }
        const wchar_t *hint = L"Hold Trigger on a corner to resize";
        brush->SetColor(ThemeColor(0xa8c0d8));
        target->DrawText(hint, static_cast<UINT32>(wcslen(hint)), font,
                                {390, bottom - 44, 1120, bottom - 4}, brush,
                                D2D1_DRAW_TEXT_OPTIONS_CLIP);

}
}
