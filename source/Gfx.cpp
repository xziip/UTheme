#include "Gfx.hpp"
#include "utils/SDL_FontCache.h"
#include <cstdarg>
#include <cmath>
#include <map>

#include <coreinit/debug.h>
#include <coreinit/memory.h>

#include <fa-solid-900_ttf.h>
#include <font_ttf.h>
#include <ter-u32b_bdf.h>

namespace {

    SDL_Window *window = nullptr;

    SDL_Renderer *renderer = nullptr;

    void *fontData = nullptr;

    uint32_t fontSize = 0;
    
    float globalAlpha = 1.0f;  // Global alpha multiplier

    std::map<int, FC_Font *> fontMap;

    FC_Font *monospaceFont = nullptr;

    TTF_Font *iconFont = nullptr;

    std::map<Uint16, SDL_Texture *> iconCache;

    FC_Font *GetFontForSize(int size) {
        if (fontMap.contains(size)) {
            return fontMap[size];
        }

        FC_Font *font = FC_CreateFont();
        if (!font) {
            return font;
        }

        if (!FC_LoadFont_RW(font, renderer, SDL_RWFromMem(fontData, fontSize), 1, size, Gfx::COLOR_BLACK, TTF_STYLE_NORMAL)) {
            FC_FreeFont(font);
            return nullptr;
        }

        fontMap.insert({size, font});
        return font;
    }

    SDL_Texture *LoadIcon(Uint16 icon) {
        if (iconCache.contains(icon)) {
            return iconCache[icon];
        }

        SDL_Surface *iconSurface = TTF_RenderGlyph_Blended(iconFont, icon, Gfx::COLOR_WHITE);
        if (!iconSurface) {
            return nullptr;
        }

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, iconSurface);
        SDL_FreeSurface(iconSurface);
        if (!texture) {
            return nullptr;
        }

        iconCache.insert({icon, texture});
        return texture;
    }

} // namespace

namespace Gfx {

    bool Init() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            return false;
        }

        window = SDL_CreateWindow("UTheme", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
        if (!window) {
            OSReport("SDL_CreateWindow failed\n");
            return false;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            OSReport("SDL_CreateRenderer failed\n");
            SDL_DestroyWindow(window);
            window = nullptr;
            return false;
        }

        fontData = const_cast<uint8_t *>(font_ttf);
        fontSize = static_cast<uint32_t>(font_ttf_size);

        TTF_Init();

        monospaceFont = FC_CreateFont();
        if (!monospaceFont) {
            return false;
        }

        if (!FC_LoadFont_RW(monospaceFont, renderer, SDL_RWFromMem((void *) ter_u32b_bdf, ter_u32b_bdf_size), 1, 32, Gfx::COLOR_BLACK, TTF_STYLE_NORMAL)) {
            FC_FreeFont(monospaceFont);
            return false;
        }

        // icons @256 should be large enough for our needs
        iconFont = TTF_OpenFontRW(SDL_RWFromMem((void *) fa_solid_900_ttf, fa_solid_900_ttf_size), 1, 256);
        if (!iconFont) {
            return false;
        }

        return true;
    }

    void Shutdown() {
        for (const auto &[key, value] : fontMap) {
            FC_FreeFont(value);
        }

        for (const auto &[key, value] : iconCache) {
            SDL_DestroyTexture(value);
        }

        FC_FreeFont(monospaceFont);
        TTF_CloseFont(iconFont);
        TTF_Quit();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void Clear(SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderClear(renderer);
    }

    void Render() {
        SDL_RenderPresent(renderer);
    }
    
    SDL_Renderer* GetRenderer() {
        return renderer;
    }
    
    void SetGlobalAlpha(float alpha) {
        globalAlpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    }
    
    float GetGlobalAlpha() {
        return globalAlpha;
    }

    void DrawRectFilled(int x, int y, int w, int h, SDL_Color color) {
        SDL_Rect rect{x, y, w, h};
        Uint8 finalAlpha = (Uint8)(color.a * globalAlpha);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, finalAlpha);
        SDL_RenderFillRect(renderer, &rect);
    }

    void DrawRect(int x, int y, int w, int h, int borderSize, SDL_Color color) {
        DrawRectFilled(x, y, w, borderSize, color);
        DrawRectFilled(x, y + h - borderSize, w, borderSize, color);
        DrawRectFilled(x, y, borderSize, h, color);
        DrawRectFilled(x + w - borderSize, y, borderSize, h, color);
    }

    void DrawIcon(int x, int y, int size, SDL_Color color, Uint16 icon, AlignFlags align, double angle) {
        SDL_Texture *iconTex = LoadIcon(icon);
        if (!iconTex) {
            return;
        }

        SDL_SetTextureColorMod(iconTex, color.r, color.g, color.b);
        Uint8 finalAlpha = (Uint8)(color.a * globalAlpha);
        SDL_SetTextureAlphaMod(iconTex, finalAlpha);

        int w, h;
        SDL_QueryTexture(iconTex, nullptr, nullptr, &w, &h);

        SDL_Rect rect;
        rect.x = x;
        rect.y = y;
        // scale the width based on hight to keep AR
        rect.w = (int) (((float) w / h) * size);
        rect.h = size;

        if (align & ALIGN_RIGHT) {
            rect.x -= rect.w;
        } else if (align & ALIGN_HORIZONTAL) {
            rect.x -= rect.w / 2;
        }

        if (align & ALIGN_BOTTOM) {
            rect.y -= rect.h;
        } else if (align & ALIGN_VERTICAL) {
            rect.y -= rect.h / 2;
        }

        // draw the icon
        if (angle) {
            SDL_RenderCopyEx(renderer, iconTex, nullptr, &rect, angle, nullptr, SDL_FLIP_NONE);
        } else {
            SDL_RenderCopy(renderer, iconTex, nullptr, &rect);
        }
    }

    int GetIconWidth(int size, Uint16 icon) {
        SDL_Texture *iconTex = LoadIcon(icon);
        if (!iconTex) {
            return 0;
        }

        int w, h;
        SDL_QueryTexture(iconTex, nullptr, nullptr, &w, &h);

        return (int) (((float) w / h) * size);
    }

    void Print(int x, int y, int size, SDL_Color color, std::string text, AlignFlags align, bool monospace) {
        FC_Font *font = monospace ? monospaceFont : GetFontForSize(size);
        if (!font) {
            return;
        }

        FC_Effect effect;
        effect.color = color;
        effect.color.a = (Uint8)(color.a * globalAlpha);  // Apply global alpha

        // scale monospace font based on size
        if (monospace) {
            effect.scale = FC_MakeScale(size / 28.0f, size / 28.0f);
            // TODO figure out how to center this properly
            y += 5;
        } else {
            effect.scale = FC_MakeScale(1, 1);
        }

        if (align & ALIGN_LEFT) {
            effect.alignment = FC_ALIGN_LEFT;
        } else if (align & ALIGN_RIGHT) {
            effect.alignment = FC_ALIGN_RIGHT;
        } else if (align & ALIGN_HORIZONTAL) {
            effect.alignment = FC_ALIGN_CENTER;
        } else {
            // left by default
            effect.alignment = FC_ALIGN_LEFT;
        }

        if (align & ALIGN_BOTTOM) {
            y -= GetTextHeight(size, text, monospace);
        } else if (align & ALIGN_VERTICAL) {
            y -= GetTextHeight(size, text, monospace) / 2;
        }

        FC_DrawEffect(font, renderer, x, y, effect, "%s", text.c_str());
    }

    int GetTextWidth(int size, std::string text, bool monospace) {
        FC_Font *font = monospace ? monospaceFont : GetFontForSize(size);
        if (!font) {
            return 0;
        }

        float scale = monospace ? (size / 28.0f) : 1.0f;

        return FC_GetWidth(font, "%s", text.c_str()) * scale;
    }

    int GetTextHeight(int size, std::string text, bool monospace) {
        // TODO this doesn't work nicely with monospace yet
        monospace = false;

        FC_Font *font = monospace ? monospaceFont : GetFontForSize(size);
        if (!font) {
            return 0;
        }

        float scale = monospace ? (size / 28.0f) : 1.0f;

        return FC_GetHeight(GetFontForSize(size), "%s", text.c_str()) * scale;
    }

    void DrawRectRounded(int x, int y, int w, int h, int radius, SDL_Color color) {
        Uint8 finalAlpha = (Uint8)(color.a * globalAlpha);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, finalAlpha);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        // Draw center rectangle
        SDL_Rect centerRect = {x + radius, y, w - 2 * radius, h};
        SDL_RenderFillRect(renderer, &centerRect);

        // Draw left and right rectangles
        SDL_Rect leftRect = {x, y + radius, radius, h - 2 * radius};
        SDL_RenderFillRect(renderer, &leftRect);
        SDL_Rect rightRect = {x + w - radius, y + radius, radius, h - 2 * radius};
        SDL_RenderFillRect(renderer, &rightRect);

        // Draw corners (approximated with filled rectangles)
        for (int i = 0; i < radius; i++) {
            int offset = radius - i;
            int width = (int)(sqrt(radius * radius - offset * offset) + 0.5);
            
            // Top-left
            SDL_Rect tlRect = {x + radius - width, y + i, width, 1};
            SDL_RenderFillRect(renderer, &tlRect);
            
            // Top-right
            SDL_Rect trRect = {x + w - radius, y + i, width, 1};
            SDL_RenderFillRect(renderer, &trRect);
            
            // Bottom-left
            SDL_Rect blRect = {x + radius - width, y + h - i - 1, width, 1};
            SDL_RenderFillRect(renderer, &blRect);
            
            // Bottom-right
            SDL_Rect brRect = {x + w - radius, y + h - i - 1, width, 1};
            SDL_RenderFillRect(renderer, &brRect);
        }
    }

    void DrawRectRoundedOutline(int x, int y, int w, int h, int radius, int borderSize, SDL_Color color) {
        // Draw outer rounded rect
        DrawRectRounded(x, y, w, h, radius, color);
        
        // Draw inner rounded rect with background color to create outline effect
        if (borderSize < radius && borderSize * 2 < w && borderSize * 2 < h) {
            DrawRectRounded(x + borderSize, y + borderSize, 
                          w - borderSize * 2, h - borderSize * 2, 
                          radius - borderSize, COLOR_BACKGROUND);
        }
    }

    void DrawGradientV(int x, int y, int w, int h, SDL_Color colorTop, SDL_Color colorBottom) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        
        for (int i = 0; i < h; i++) {
            float ratio = (float)i / (float)h;
            SDL_Color color = {
                (Uint8)(colorTop.r + (colorBottom.r - colorTop.r) * ratio),
                (Uint8)(colorTop.g + (colorBottom.g - colorTop.g) * ratio),
                (Uint8)(colorTop.b + (colorBottom.b - colorTop.b) * ratio),
                (Uint8)((colorTop.a + (colorBottom.a - colorTop.a) * ratio) * globalAlpha)
            };
            
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_RenderDrawLine(renderer, x, y + i, x + w, y + i);
        }
    }

    void DrawShadow(int x, int y, int w, int h, int blur) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        
        // Simple rectangular shadow for performance - keep original fast version
        for (int i = 0; i < blur; i++) {
            int alpha = (COLOR_SHADOW.a * (blur - i)) / blur;
            SDL_Color shadowColor = COLOR_SHADOW;
            shadowColor.a = alpha;
            
            SDL_SetRenderDrawColor(renderer, shadowColor.r, shadowColor.g, shadowColor.b, shadowColor.a);
            SDL_Rect shadowRect = {x - i, y - i, w + i * 2, h + i * 2};
            SDL_RenderDrawRect(renderer, &shadowRect);
        }
    }

} // namespace Gfx
