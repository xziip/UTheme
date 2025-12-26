#pragma once

#include <SDL.h>
#include <string>

namespace Gfx {
    constexpr uint32_t SCREEN_WIDTH  = 1920;
    constexpr uint32_t SCREEN_HEIGHT = 1080;

    constexpr SDL_Color COLOR_BLACK          = {0x00, 0x00, 0x00, 0xff};
    constexpr SDL_Color COLOR_WHITE          = {0xff, 0xff, 0xff, 0xff};
    constexpr SDL_Color COLOR_BACKGROUND     = {0x1a, 0x1a, 0x2e, 0xff};  // 深紫蓝色背景
    constexpr SDL_Color COLOR_ALT_BACKGROUND = {0x25, 0x25, 0x42, 0xff};  // 稍亮的紫蓝色
    constexpr SDL_Color COLOR_HIGHLIGHTED    = {0x8b, 0x5c, 0xf6, 0xff};  // 紫色高亮
    constexpr SDL_Color COLOR_TEXT           = {0xf8, 0xf8, 0xf8, 0xff};
    constexpr SDL_Color COLOR_ICON           = {0x9b, 0x7e, 0xf7, 0xff};  // 亮紫色图标
    constexpr SDL_Color COLOR_ALT_TEXT       = {0xa0, 0xa8, 0xb8, 0xff};
    constexpr SDL_Color COLOR_ACCENT         = {0x8b, 0x5c, 0xf6, 0xff};  // 主紫色
    constexpr SDL_Color COLOR_ALT_ACCENT     = {0x6d, 0x4a, 0xc7, 0xff};  // 深紫色
    constexpr SDL_Color COLOR_BARS           = {0x1f, 0x1f, 0x38, 0xf0};  // 半透明顶栏
    constexpr SDL_Color COLOR_ERROR          = {0xff, 0x44, 0x55, 0xff};
    constexpr SDL_Color COLOR_ERROR_HOVER    = {0xff, 0x66, 0x77, 0xff};
    constexpr SDL_Color COLOR_WARNING        = {0xff, 0xcc, 0x00, 0xff};
    constexpr SDL_Color COLOR_WIIU           = {0x00, 0x95, 0xc7, 0xff};
    constexpr SDL_Color COLOR_SUCCESS        = {0x4e, 0xcc, 0x7e, 0xff};  // 绿色成功
    constexpr SDL_Color COLOR_CARD_BG        = {0x2d, 0x2d, 0x4a, 0xff};  // 卡片背景
    constexpr SDL_Color COLOR_CARD_HOVER     = {0x3a, 0x3a, 0x5e, 0xff};  // 悬停卡片
    constexpr SDL_Color COLOR_SHADOW         = {0x00, 0x00, 0x00, 0x60};
    constexpr SDL_Color COLOR_BORDER         = {0x8b, 0x5c, 0xf6, 0x80};  // 半透明紫色边框

    enum AlignFlags {
        ALIGN_LEFT       = 1 << 0,
        ALIGN_RIGHT      = 1 << 1,
        ALIGN_HORIZONTAL = 1 << 2,
        ALIGN_TOP        = 1 << 3,
        ALIGN_BOTTOM     = 1 << 4,
        ALIGN_VERTICAL   = 1 << 5,
        ALIGN_CENTER     = ALIGN_HORIZONTAL | ALIGN_VERTICAL,
    };

    static constexpr inline AlignFlags operator|(AlignFlags lhs, AlignFlags rhs) {
        return static_cast<AlignFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    bool Init();

    void Shutdown();

    void Clear(SDL_Color color);

    void Render();
    
    SDL_Renderer* GetRenderer();  // Get SDL renderer for custom operations
    
    void SetGlobalAlpha(float alpha);  // Set global alpha multiplier (0.0 - 1.0)
    
    float GetGlobalAlpha();  // Get current global alpha

    void DrawRectFilled(int x, int y, int w, int h, SDL_Color color);

    void DrawRect(int x, int y, int w, int h, int borderSize, SDL_Color color);

    void DrawRectRounded(int x, int y, int w, int h, int radius, SDL_Color color);

    void DrawRectRoundedOutline(int x, int y, int w, int h, int radius, int borderSize, SDL_Color color);

    void DrawGradientV(int x, int y, int w, int h, SDL_Color colorTop, SDL_Color colorBottom);

    void DrawShadow(int x, int y, int w, int h, int blur);

    void DrawIcon(int x, int y, int size, SDL_Color color, Uint16 icon, AlignFlags align = ALIGN_CENTER, double angle = 0.0);

    int GetIconWidth(int size, Uint16 icon);

    static inline int GetIconHeight(int size, Uint16 icon) { return size; }

    void Print(int x, int y, int size, SDL_Color color, std::string text, AlignFlags align = ALIGN_LEFT | ALIGN_TOP, bool monospace = false);

    int GetTextWidth(int size, std::string text, bool monospace = false);

    int GetTextHeight(int size, std::string text, bool monospace = false);
} // namespace Gfx
