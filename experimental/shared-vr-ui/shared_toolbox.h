#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <cmath>
#include <cwchar>
#include "toolbox_icons.h"
namespace SharedToolbox {
using Microsoft::WRL::ComPtr;
inline constexpr int ToolW=970, ToolH=210;
struct ToolboxButton { D2D1_RECT_F r; const wchar_t* label; };
inline constexpr ToolboxButton toolButtons[] = {{{20, 20, 190, 190}, L"Keyboard"},
                              {{210, 20, 380, 190}, L"Browser"},
                              {{400, 20, 570, 190}, L"Settings"},
                              {{590, 20, 760, 190}, L"Game UI"},
                              {{780, 20, 950, 190}, L"Hide"}};

struct State { bool keyboard{},browser{},gameInterface{},navigation{}; };
template<class Workspace>
bool activate(int id, Workspace& state, bool& collapsed) {
 switch(id){
 case 300:state.keyboard=!state.keyboard;break;
 case 301:state.browser=!state.browser;break;
 case 302:state.gameInterface=!state.gameInterface;break;
 case 303:state.navigation=!state.navigation;break;
 case 304:collapsed=true;break;
 case 305:collapsed=false;break;
 default:return false;
 }
 return true;
}

inline int hit(float x,float y,bool collapsed) {
 for(int i=0;i<(collapsed?1:5);++i){
  const auto r=toolButtons[i].r;
  if(x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom)return collapsed?305:300+i;
 }
 return -1;
}
template<class Theme>
HRESULT draw(ComPtr<ID2D1RenderTarget> activeTarget,ComPtr<ID2D1SolidColorBrush> activeBrush,
 IDWriteFactory* write,IDWriteTextFormat* font,bool toolboxCollapsed,State workspace,
 int hover,int pressed,float centerProgress,Theme ThemeColor) {
        activeTarget->BeginDraw();
        activeTarget->Clear(ThemeColor(0x101b2b));
        activeBrush->SetColor(ThemeColor(0xeaf1ff));

        if (toolboxCollapsed) {
            activeBrush->SetColor(ThemeColor(pressed == 305 ? 0x995a20 : hover == 305 ? 0x396385 : 0x22374e));
            activeTarget->FillRoundedRectangle(D2D1::RoundedRect(toolButtons[0].r, 12, 12), activeBrush.Get());
            activeBrush->SetColor(ThemeColor(0xeaf1ff));
            ComPtr<IDWriteTextLayout> caption;
            auto result=write->CreateTextLayout(L"Show", 4, font, 170, 170, &caption);
            if(FAILED(result)){activeTarget->EndDraw();return result;}
            caption->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            caption->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            activeTarget->DrawTextLayout({20, 20}, caption.Get(), activeBrush.Get());
        } else {
        for (int i = 0; i < 5; ++i)
        {
            activeBrush->SetColor(ThemeColor(pressed == 300 + i ? 0x995a20
                                             : hover == 300 + i ? 0x396385
                                             : (i == 0   ? workspace.keyboard
                                                : i == 1 ? workspace.browser
                                                         : i == 2 ? workspace.gameInterface : i == 3 ? workspace.navigation : false)
                                                 ? 0x375c7c
                                                 : 0x22374e));
            activeTarget->FillRoundedRectangle(D2D1::RoundedRect(toolButtons[i].r, 12, 12), activeBrush.Get());
            activeBrush->SetColor(ThemeColor(0xeaf1ff));
            auto r = toolButtons[i].r;
            float cx = (r.left + r.right) * .5f, cy = r.top + 68;
            if (i == 0)
            {
                activeTarget->DrawRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(cx - 57, cy - 35, cx + 57, cy + 35), 6, 6), activeBrush.Get(),
                    3);
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 6; ++col)
                    {
                        float x = cx - 44 + col * 16.f, y = cy - 23 + row * 14.f;
                        activeTarget->FillRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(x, y, x + 10, y + 8), 2, 2), activeBrush.Get());
                    }
                activeTarget->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(cx - 28, cy + 20, cx + 28, cy + 26), 2, 2),
                    activeBrush.Get());
            }
            else if (i == 3)
                ToolboxIcons::gameInterface(activeTarget.Get(), activeBrush.Get(), cx, cy);
            else if (i == 2)
                ToolboxIcons::settings(activeTarget.Get(),activeBrush.Get(),cx,cy);
            else if (i == 4) {
                activeTarget->DrawLine({cx - 30, cy}, {cx + 30, cy}, activeBrush.Get(), 5);
            }
            else
            {
                activeTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 39, 39), activeBrush.Get(), 3);
                activeTarget->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 17, 39), activeBrush.Get(), 2);
                activeTarget->DrawLine(D2D1::Point2F(cx - 39, cy), D2D1::Point2F(cx + 39, cy), activeBrush.Get(),
                                     2);
                activeTarget->DrawLine(D2D1::Point2F(cx - 33, cy - 20), D2D1::Point2F(cx + 33, cy - 20),
                                     activeBrush.Get(), 2);
                activeTarget->DrawLine(D2D1::Point2F(cx - 33, cy + 20), D2D1::Point2F(cx + 33, cy + 20),
                                     activeBrush.Get(), 2);
            }
            activeTarget->DrawText(toolButtons[i].label, static_cast<UINT32>(wcslen(toolButtons[i].label)),
                                 font, D2D1::RectF(r.left + 4, r.top + 125, r.right - 2, r.bottom),
                                 activeBrush.Get());
        }
        }
        if (centerProgress >= 0)
        {
            activeBrush->SetColor(ThemeColor(0x30475d));
            activeTarget->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(20, 196, (toolboxCollapsed ? float(ToolH) : float(ToolW)) - 20, 202), 5, 5),
                                             activeBrush.Get());
            activeBrush->SetColor(ThemeColor(0xffbd55));
            activeTarget->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(20, 196, 20 + ((toolboxCollapsed ? float(ToolH) : float(ToolW)) - 40) * centerProgress, 202), 5, 5),
                activeBrush.Get());
        }
        return activeTarget->EndDraw();
 }
}
