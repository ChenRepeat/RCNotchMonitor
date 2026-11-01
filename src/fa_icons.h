#pragma once
#include <windows.h>
#include <d2d1.h>
#include <cmath>

enum FaIconType {
    FA_ICON_CPU = 0,
    FA_ICON_GPU,
    FA_ICON_RAM,
    FA_ICON_FPS,
    FA_ICON_GEAR,
    FA_ICON_LAYOUT,
    FA_ICON_LOCK,
    FA_ICON_UNLOCK,
    FA_ICON_UP,
    FA_ICON_DOWN,
    FA_ICON_RESET,
    FA_ICON_SAVE,
    FA_ICON_CHECK,
    FA_ICON_THERM,
    FA_ICON_BOLT,
    FA_ICON_SLIDERS,
    FA_ICON_MINUS,
    FA_ICON_XMARK,
    FA_ICON_LOGO,
    FA_ICON_GRIP,
    FA_ICON_MUSIC,
    FA_ICON_PLAY,
    FA_ICON_PAUSE,
    FA_ICON_STEP_BACK,
    FA_ICON_STEP_FWD,
    FA_ICON_BACKWARD,
    FA_ICON_FORWARD,
    FA_ICON_EXCHANGE,
    FA_ICON_DESKTOP
};

// Crisp Direct2D Vector Renderer for Font Awesome 6 Solid Icons
inline void DrawFaIcon(ID2D1Factory* factory, ID2D1RenderTarget* rt, FaIconType icon,
                       float cx, float cy, float size, ID2D1Brush* brush,
                       D2D1_COLOR_F holeColor = D2D1::ColorF(0.06f, 0.07f, 0.10f, 1.0f),
                       float layoutAnim = 0.0f) {
    if (!rt || !brush) return;
    float r = size * 0.5f;

    switch (icon) {
    case FA_ICON_CPU: { // fa-microchip
        float body = r * 0.66f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - body, cy - body, cx + body, cy + body), 1.8f, 1.8f), brush, 1.3f);
        float core = r * 0.30f;
        rt->FillRectangle(D2D1::RectF(cx - core, cy - core, cx + core, cy + core), brush);
        for (int i = -1; i <= 1; ++i) {
            float off = i * (body * 0.58f);
            rt->DrawLine(D2D1::Point2F(cx + off, cy - r), D2D1::Point2F(cx + off, cy - body), brush, 1.2f);
            rt->DrawLine(D2D1::Point2F(cx + off, cy + body), D2D1::Point2F(cx + off, cy + r), brush, 1.2f);
            rt->DrawLine(D2D1::Point2F(cx - r, cy + off), D2D1::Point2F(cx - body, cy + off), brush, 1.2f);
            rt->DrawLine(D2D1::Point2F(cx + body, cy + off), D2D1::Point2F(cx + r, cy + off), brush, 1.2f);
        }
        break;
    }
    case FA_ICON_GPU: { // GPU Card + Fan shroud
        float w = r * 0.92f;
        float h = r * 0.65f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - w, cy - h, cx + w, cy + h), 1.8f, 1.8f), brush, 1.3f);
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.1f, cy), h * 0.58f, h * 0.58f), brush, 1.2f);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + r * 0.1f, cy), 1.3f, 1.3f), brush);
        rt->DrawLine(D2D1::Point2F(cx - w - 1.5f, cy - h - 1.5f), D2D1::Point2F(cx - w - 1.5f, cy + h + 1.5f), brush, 1.3f);
        break;
    }
    case FA_ICON_RAM: { // fa-memory
        float w = r * 0.92f;
        float h = r * 0.58f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - w, cy - h, cx + w, cy + h * 0.65f), 1.5f, 1.5f), brush, 1.3f);
        for (int i = -1; i <= 1; ++i) {
            float ix = cx + i * (w * 0.56f);
            rt->FillRectangle(D2D1::RectF(ix - 1.6f, cy - h * 0.55f, ix + 1.6f, cy + h * 0.15f), brush);
            rt->DrawLine(D2D1::Point2F(ix, cy + h * 0.65f), D2D1::Point2F(ix, cy + h * 1.05f), brush, 1.3f);
        }
        break;
    }
    case FA_ICON_FPS: { // fa-gauge-high
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 0.5f), r * 0.85f, r * 0.85f), brush, 1.35f);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + 1.0f), 1.5f, 1.5f), brush);
        rt->DrawLine(D2D1::Point2F(cx, cy + 1.0f), D2D1::Point2F(cx + r * 0.52f, cy - r * 0.45f), brush, 1.5f);
        break;
    }
    case FA_ICON_GEAR: { // fa-gear
        float outerR = r * 0.66f;
        float toothR = r * 0.92f;
        for (int i = 0; i < 4; ++i) {
            float ang = i * 3.14159265f * 0.25f;
            float dx = std::cos(ang) * toothR;
            float dy = std::sin(ang) * toothR;
            rt->DrawLine(D2D1::Point2F(cx - dx, cy - dy), D2D1::Point2F(cx + dx, cy + dy), brush, 2.4f);
        }
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), outerR, outerR), brush);
        ID2D1SolidColorBrush* brHole = nullptr;
        rt->CreateSolidColorBrush(holeColor, &brHole);
        if (brHole) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.32f, r * 0.32f), brHole);
            brHole->Release();
        }
        break;
    }
    case FA_ICON_LAYOUT: { // 4-Dot Layout Icon with Smooth Tween Rotation (Left pair -> Top-Left, Right pair -> Bottom-Right)
        float u = (layoutAnim < 0.0f) ? 0.0f : ((layoutAnim > 1.0f) ? 1.0f : layoutAnim);
        u = u * u * (3.0f - 2.0f * u); // Smoothstep easing

        const float PI = 3.14159265f;
        float stepX = r * 0.30f;
        float pairOff = (1.45f + 0.55f * u) * stepX;
        float pairRad = (1.45f - 0.45f * u) * stepX;
        float theta = -PI * 0.5f - u * (PI * 0.5f); // Rotates 90 deg CCW: top swings to left (-PI), bottom swings to right (0)
        float arcY = (r * 0.24f) * std::sin(PI * u);

        float dx = std::cos(theta) * pairRad;
        float dy = std::sin(theta) * pairRad;
        float dotR = r * 0.24f;

        // Left pair center (arcs toward top-left during transition)
        float lcx = cx - pairOff;
        float lcy = cy - arcY;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lcx + dx, lcy + dy), dotR, dotR), brush);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(lcx - dx, lcy - dy), dotR, dotR), brush);

        // Right pair center (arcs toward bottom-right during transition)
        float rcx = cx + pairOff;
        float rcy = cy + arcY;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcx + dx, rcy + dy), dotR, dotR), brush);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rcx - dx, rcy - dy), dotR, dotR), brush);
        break;
    }
    case FA_ICON_LOCK: { // fa-lock
        float bw = r * 0.72f;
        float topY = cy - r * 0.08f;
        float botY = cy + r * 0.85f;
        float shW = r * 0.44f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - shW, cy - r * 0.82f, cx + shW, topY + 2.0f), shW, shW), brush, 1.6f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - bw, topY, cx + bw, botY), 2.2f, 2.2f), brush);
        break;
    }
    case FA_ICON_UNLOCK: { // fa-lock-open
        float bw = r * 0.72f;
        float topY = cy - r * 0.08f;
        float botY = cy + r * 0.85f;
        float shW = r * 0.44f;
        rt->DrawLine(D2D1::Point2F(cx - shW, cy - r * 0.55f), D2D1::Point2F(cx - shW, topY + 1.0f), brush, 1.6f);
        rt->DrawLine(D2D1::Point2F(cx - shW, cy - r * 0.75f), D2D1::Point2F(cx + shW * 0.7f, cy - r * 0.75f), brush, 1.6f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - bw, topY, cx + bw, botY), 2.2f, 2.2f), brush);
        break;
    }
    case FA_ICON_UP: { // fa-chevron-up
        float w = r * 0.72f;
        float h = r * 0.42f;
        rt->DrawLine(D2D1::Point2F(cx - w, cy + h * 0.6f), D2D1::Point2F(cx, cy - h), brush, 1.9f);
        rt->DrawLine(D2D1::Point2F(cx, cy - h), D2D1::Point2F(cx + w, cy + h * 0.6f), brush, 1.9f);
        break;
    }
    case FA_ICON_DOWN: { // fa-chevron-down
        float w = r * 0.72f;
        float h = r * 0.42f;
        rt->DrawLine(D2D1::Point2F(cx - w, cy - h * 0.6f), D2D1::Point2F(cx, cy + h), brush, 1.9f);
        rt->DrawLine(D2D1::Point2F(cx, cy + h), D2D1::Point2F(cx + w, cy - h * 0.6f), brush, 1.9f);
        break;
    }
    case FA_ICON_RESET: { // fa-rotate-left (counter-clockwise circular arrow)
        float ar = r * 0.72f;
        rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), ar, ar), brush, 1.6f);
        // Cut a small gap in top-left quadrant & draw arrow head pointing counter-clockwise
        ID2D1SolidColorBrush* brHole = nullptr;
        rt->CreateSolidColorBrush(holeColor, &brHole);
        if (brHole) {
            rt->FillRectangle(D2D1::RectF(cx - ar - 2.0f, cy - ar - 1.0f, cx - 1.0f, cy), brHole);
            brHole->Release();
        }
        // Arrowhead at top pointing left-down
        float ax = cx - ar * 0.25f;
        float ay = cy - ar * 0.92f;
        rt->DrawLine(D2D1::Point2F(ax - 3.2f, ay + 1.0f), D2D1::Point2F(ax + 1.5f, ay - 2.5f), brush, 1.7f);
        rt->DrawLine(D2D1::Point2F(ax - 3.2f, ay + 1.0f), D2D1::Point2F(ax + 1.8f, ay + 3.0f), brush, 1.7f);
        break;
    }
    case FA_ICON_SAVE: { // fa-floppy-disk
        float s = r * 0.80f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - s, cy - s, cx + s, cy + s), 2.0f, 2.0f), brush, 1.6f);
        // Top shutter bar
        rt->FillRectangle(D2D1::RectF(cx - s * 0.55f, cy - s, cx + s * 0.45f, cy - s * 0.22f), brush);
        // Bottom label area
        rt->DrawRectangle(D2D1::RectF(cx - s * 0.60f, cy + s * 0.12f, cx + s * 0.60f, cy + s), brush, 1.3f);
        break;
    }
    case FA_ICON_CHECK: { // fa-check
        rt->DrawLine(D2D1::Point2F(cx - r * 0.68f, cy + r * 0.05f), D2D1::Point2F(cx - r * 0.15f, cy + r * 0.58f), brush, 2.0f);
        rt->DrawLine(D2D1::Point2F(cx - r * 0.18f, cy + r * 0.58f), D2D1::Point2F(cx + r * 0.72f, cy - r * 0.52f), brush, 2.0f);
        break;
    }
    case FA_ICON_THERM: { // fa-temperature-half
        float stemW = r * 0.28f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - stemW, cy - r * 0.85f, cx + stemW, cy + r * 0.35f), stemW, stemW), brush, 1.4f);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy + r * 0.50f), r * 0.46f, r * 0.46f), brush);
        break;
    }
    case FA_ICON_BOLT: { // fa-bolt
        if (factory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
                    sink->BeginFigure(D2D1::Point2F(cx + r * 0.15f, cy - r * 0.90f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx - r * 0.65f, cy + r * 0.08f));
                    sink->AddLine(D2D1::Point2F(cx - r * 0.05f, cy + r * 0.08f));
                    sink->AddLine(D2D1::Point2F(cx - r * 0.20f, cy + r * 0.90f));
                    sink->AddLine(D2D1::Point2F(cx + r * 0.65f, cy - r * 0.08f));
                    sink->AddLine(D2D1::Point2F(cx + r * 0.05f, cy - r * 0.08f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }
                rt->FillGeometry(path, brush);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_SLIDERS: { // fa-sliders
        for (int i = -1; i <= 1; ++i) {
            float ly = cy + i * (r * 0.58f);
            rt->DrawLine(D2D1::Point2F(cx - r * 0.85f, ly), D2D1::Point2F(cx + r * 0.85f, ly), brush, 1.4f);
            float kx = cx + (i == 0 ? r * 0.35f : (i < 0 ? -r * 0.30f : -r * 0.15f));
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, ly), 2.3f, 2.3f), brush);
        }
        break;
    }
    case FA_ICON_MINUS: { // fa-minus (Window minimize)
        rt->DrawLine(D2D1::Point2F(cx - r * 0.72f, cy), D2D1::Point2F(cx + r * 0.72f, cy), brush, 1.8f);
        break;
    }
    case FA_ICON_XMARK: { // fa-xmark (Window close)
        float d = r * 0.65f;
        rt->DrawLine(D2D1::Point2F(cx - d, cy - d), D2D1::Point2F(cx + d, cy + d), brush, 1.8f);
        rt->DrawLine(D2D1::Point2F(cx + d, cy - d), D2D1::Point2F(cx - d, cy + d), brush, 1.8f);
        break;
    }
    case FA_ICON_LOGO: { // Vector 1.png: Interlocking double-hexagon "C" logo
        if (factory) {
            const float PI = 3.14159265f;
            auto deg2rad = [&](float deg) { return deg * PI / 180.0f; };
            auto ptAt = [&](float rad, float deg) {
                float a = deg2rad(deg);
                return D2D1::Point2F(cx + std::cos(a) * rad, cy + std::sin(a) * rad);
            };

            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    // Outer Hexagon-C (open on right)
                    float rOut = r * 0.92f;
                    sink->BeginFigure(ptAt(rOut, -28.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(ptAt(rOut, -90.0f));
                    sink->AddLine(ptAt(rOut, -150.0f));
                    sink->AddLine(ptAt(rOut, 150.0f));
                    sink->AddLine(ptAt(rOut, 90.0f));
                    sink->AddLine(ptAt(rOut, 28.0f));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);

                    // Inner Hexagon-C (open on left)
                    float rIn = r * 0.48f;
                    sink->BeginFigure(ptAt(rIn, -152.0f), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(ptAt(rIn, -90.0f));
                    sink->AddLine(ptAt(rIn, -30.0f));
                    sink->AddLine(ptAt(rIn, 30.0f));
                    sink->AddLine(ptAt(rIn, 90.0f));
                    sink->AddLine(ptAt(rIn, 152.0f));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);

                    sink->Close();
                    sink->Release();
                }
                rt->DrawGeometry(path, brush, r * 0.24f);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_GRIP: { // fa-grip-vertical (2x3 6-dot drag handle)
        float dx = r * 0.36f;
        float dy = r * 0.56f;
        float dotR = r * 0.18f;
        for (int row = -1; row <= 1; ++row) {
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - dx, cy + row * dy), dotR, dotR), brush);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dx, cy + row * dy), dotR, dotR), brush);
        }
        break;
    }
    case FA_ICON_MUSIC: { // fa-music (beamed eighth notes)
        float xL = cx - r * 0.38f;
        float xR = cx + r * 0.42f;
        float yTopL = cy - r * 0.58f;
        float yTopR = cy - r * 0.76f;
        float yBotL = cy + r * 0.48f;
        float yBotR = cy + r * 0.32f;
        float headR = r * 0.24f;
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(xL - headR * 0.5f, yBotL), headR, headR * 0.80f), brush);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(xR - headR * 0.5f, yBotR), headR, headR * 0.80f), brush);
        rt->DrawLine(D2D1::Point2F(xL + headR * 0.25f, yBotL), D2D1::Point2F(xL + headR * 0.25f, yTopL), brush, 1.5f);
        rt->DrawLine(D2D1::Point2F(xR + headR * 0.25f, yBotR), D2D1::Point2F(xR + headR * 0.25f, yTopR), brush, 1.5f);
        rt->DrawLine(D2D1::Point2F(xL + headR * 0.25f, yTopL), D2D1::Point2F(xR + headR * 0.25f, yTopR), brush, 2.2f);
        break;
    }
    case FA_ICON_PLAY: { // fa-play (solid right-pointing triangle)
        if (factory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(cx - r * 0.48f, cy - r * 0.68f), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx + r * 0.68f, cy));
                    sink->AddLine(D2D1::Point2F(cx - r * 0.48f, cy + r * 0.68f));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }
                rt->FillGeometry(path, brush);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_PAUSE: { // fa-pause (two vertical bars)
        float bw = r * 0.26f;
        float bh = r * 0.68f;
        float off = r * 0.36f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - off - bw * 0.5f, cy - bh, cx - off + bw * 0.5f, cy + bh), 1.2f, 1.2f), brush);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + off - bw * 0.5f, cy - bh, cx + off + bw * 0.5f, cy + bh), 1.2f, 1.2f), brush);
        break;
    }
    case FA_ICON_STEP_BACK: { // fa-backward-step (bar + left triangle)
        float bh = r * 0.64f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - r * 0.68f, cy - bh, cx - r * 0.44f, cy + bh), 1.0f, 1.0f), brush);
        if (factory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(cx + r * 0.64f, cy - bh), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx - r * 0.38f, cy));
                    sink->AddLine(D2D1::Point2F(cx + r * 0.64f, cy + bh));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }
                rt->FillGeometry(path, brush);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_STEP_FWD: { // fa-forward-step (right triangle + bar)
        float bh = r * 0.64f;
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx + r * 0.44f, cy - bh, cx + r * 0.68f, cy + bh), 1.0f, 1.0f), brush);
        if (factory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(cx - r * 0.64f, cy - bh), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx + r * 0.38f, cy));
                    sink->AddLine(D2D1::Point2F(cx - r * 0.64f, cy + bh));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }
                rt->FillGeometry(path, brush);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_BACKWARD: { // fa-backward (double left triangles for -10s seek)
        float bh = r * 0.58f;
        if (factory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(cx - r * 0.02f, cy - bh), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx - r * 0.76f, cy));
                    sink->AddLine(D2D1::Point2F(cx - r * 0.02f, cy + bh));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

                    sink->BeginFigure(D2D1::Point2F(cx + r * 0.74f, cy - bh), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx, cy));
                    sink->AddLine(D2D1::Point2F(cx + r * 0.74f, cy + bh));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }
                rt->FillGeometry(path, brush);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_FORWARD: { // fa-forward (double right triangles for +10s seek)
        float bh = r * 0.58f;
        if (factory) {
            ID2D1PathGeometry* path = nullptr;
            if (SUCCEEDED(factory->CreatePathGeometry(&path)) && path) {
                ID2D1GeometrySink* sink = nullptr;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(D2D1::Point2F(cx - r * 0.74f, cy - bh), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx, cy));
                    sink->AddLine(D2D1::Point2F(cx - r * 0.74f, cy + bh));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);

                    sink->BeginFigure(D2D1::Point2F(cx + r * 0.02f, cy - bh), D2D1_FIGURE_BEGIN_FILLED);
                    sink->AddLine(D2D1::Point2F(cx + r * 0.76f, cy));
                    sink->AddLine(D2D1::Point2F(cx + r * 0.02f, cy + bh));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                    sink->Release();
                }
                rt->FillGeometry(path, brush);
                path->Release();
            }
        }
        break;
    }
    case FA_ICON_EXCHANGE: { // fa-arrows-rotate / switch source
        float w = r * 0.62f;
        float y1 = cy - r * 0.28f;
        float y2 = cy + r * 0.28f;
        rt->DrawLine(D2D1::Point2F(cx - w, y1), D2D1::Point2F(cx + w, y1), brush, 1.4f);
        rt->DrawLine(D2D1::Point2F(cx + w * 0.4f, y1 - r * 0.28f), D2D1::Point2F(cx + w, y1), brush, 1.4f);
        rt->DrawLine(D2D1::Point2F(cx - w, y2), D2D1::Point2F(cx + w, y2), brush, 1.4f);
        rt->DrawLine(D2D1::Point2F(cx - w * 0.4f, y2 + r * 0.28f), D2D1::Point2F(cx - w, y2), brush, 1.4f);
        break;
    }
    case FA_ICON_DESKTOP: { // fa-desktop
        float scrW = r * 0.85f;
        float scrH = r * 0.58f;
        float scrTop = cy - r * 0.68f;
        rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - scrW, scrTop, cx + scrW, scrTop + scrH), 1.6f, 1.6f), brush, 1.3f);
        // Stand neck
        rt->DrawLine(D2D1::Point2F(cx, scrTop + scrH), D2D1::Point2F(cx, cy + r * 0.48f), brush, 1.4f);
        // Base plate
        float baseW = r * 0.48f;
        rt->DrawLine(D2D1::Point2F(cx - baseW, cy + r * 0.48f), D2D1::Point2F(cx + baseW, cy + r * 0.48f), brush, 1.5f);
        break;
    }
    }
}
