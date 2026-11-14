#pragma once
namespace ToolboxIcons
{
// Retained original GameInterface icon for future controls.
inline void gameInterface(ID2D1RenderTarget *target, ID2D1Brush *brush, float cx, float cy)
{
    target->DrawRectangle({cx - 48, cy - 33, cx + 48, cy + 33}, brush, 3);
    target->DrawLine({cx - 65, cy}, {cx + 65, cy}, brush, 2);
    target->DrawLine({cx, cy - 48}, {cx, cy + 48}, brush, 2);
    target->DrawEllipse(D2D1::Ellipse({cx, cy}, 15, 15), brush, 2);
}
inline void settings(ID2D1RenderTarget *target, ID2D1Brush *brush, float cx, float cy)
{
    D2D1_POINT_2F previous{};
    for (int i = 0; i <= 64; ++i)
    {
        int j = i % 64;
        float angle = j * 6.28318530718f / 64.f;
        float radius = (j % 8 >= 2 && j % 8 <= 5) ? 49.f : 37.f;
        D2D1_POINT_2F point{cx + std::cos(angle) * radius, cy + std::sin(angle) * radius};
        if (i)
            target->DrawLine(previous, point, brush, 3);
        previous = point;
    }
    target->DrawEllipse(D2D1::Ellipse({cx, cy}, 17, 17), brush, 3);
}
} // namespace ToolboxIcons
