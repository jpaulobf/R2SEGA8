#pragma once

#include <algorithm>

namespace R2Window
{
    struct VideoRect
    {
        float x, y, width, height;
    };

    // Calcula a área relativa ao canto superior esquerdo disponível.
    inline VideoRect videoLayout(float width, float height, float sourceWidth, float sourceHeight, bool stretched)
    {
        if (width <= 0 || height <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
            return {0, 0, 0, 0};
        const float scale = std::min(width / sourceWidth, height / sourceHeight);
        const float w = stretched ? width : sourceWidth * scale;
        const float h = stretched ? height : sourceHeight * scale;
        return {(width - w) * 0.5f, (height - h) * 0.5f, w, h};
    }

    // O renderer SDL do NES utiliza coordenadas inteiras e trunca as dimensões.
    inline VideoRect videoLayoutInteger(int width, int height, double aspect, bool stretched)
    {
        if (width <= 0 || height <= 0 || aspect <= 0)
            return {0, 0, 0, 0};
        if (stretched)
            return {0, 0, static_cast<float>(width), static_cast<float>(height)};
        const double potentialHeight = width / aspect;
        const int w = potentialHeight <= height ? width : static_cast<int>(height * aspect);
        const int h = potentialHeight <= height ? static_cast<int>(potentialHeight) : height;
        return {static_cast<float>((width - w) / 2), static_cast<float>((height - h) / 2),
                static_cast<float>(w), static_cast<float>(h)};
    }
}