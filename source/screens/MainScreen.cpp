#include "MainScreen.hpp"
#include "Gfx.hpp"
#include "MenuScreen.hpp"
#include "common.h"
#include "../utils/LanguageManager.hpp"
#include <mocha/mocha.h>
#include <utility>
#include <cmath>

bool MainScreen::sMochaAvailable = false;

MainScreen::~MainScreen() {
    if (sMochaAvailable && mState > STATE_INIT_MOCHA) {
        Mocha_DeInitLibrary();
    }
}

void MainScreen::DrawLoadingSpinner(int x, int y, int size, float progress) {
    const int numDots = 8;
    const float radius = size / 2.0f;
    
    for (int i = 0; i < numDots; i++) {
        float angle = (progress * 2.0f * M_PI) + (i * 2.0f * M_PI / numDots);
        int dotX = x + (int)(cos(angle) * radius);
        int dotY = y + (int)(sin(angle) * radius);
        
        // Fade dots based on position
        float fade = (float)(numDots - i) / (float)numDots;
        SDL_Color dotColor = Gfx::COLOR_ACCENT;
        dotColor.a = (Uint8)(255 * fade);
        
        int dotSize = 8 + (int)(fade * 8);
        Gfx::DrawRectRounded(dotX - dotSize/2, dotY - dotSize/2, dotSize, dotSize, dotSize/2, dotColor);
    }
}

void MainScreen::Draw() {
    mFrameCount++;
    mLoadingAnim.Update();
    
    Gfx::Clear(Gfx::COLOR_BACKGROUND);

    if (mMenuScreen) {
        mMenuScreen->Draw();
        return;
    }

    // Draw gradient background
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);

    // 使用简化的顶部栏绘制
    float loadProgress = mLoadingAnim.GetValue();
    
    // draw top bar
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, 120, Gfx::COLOR_BARS);
    
    int titleY = 25 - (int)((1.0f - loadProgress) * 50);
    SDL_Color titleColor = Gfx::COLOR_TEXT;
    titleColor.a = (Uint8)(255 * loadProgress);
    
    Gfx::DrawIcon(60, titleY + 40, 60, Gfx::COLOR_ACCENT, 0xf53f, Gfx::ALIGN_VERTICAL);
    Gfx::Print(140, titleY + 40, 56, titleColor, _("app_name"), Gfx::ALIGN_VERTICAL);
    
    // 显示描述文字而不是版本号
    SDL_Color descColor = Gfx::COLOR_ALT_TEXT;
    descColor.a = (Uint8)(180 * loadProgress);
    Gfx::Print(140, titleY + 85, 28, descColor, _("app_description"), Gfx::ALIGN_VERTICAL);
    
    // Draw animated accent line
    SDL_Color accentColor = Gfx::COLOR_ACCENT;
    accentColor.a = (Uint8)(180 * loadProgress);
    Gfx::DrawRectFilled(0, 115, (int)(Gfx::SCREEN_WIDTH * loadProgress), 5, accentColor);

    // Center loading card
    int cardW = 800;
    int cardH = 400;
    int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
    int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
    
    // Draw card shadow and background - simple shadow for performance
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = 80;
    Gfx::DrawRectRounded(cardX + 8, cardY + 8, cardW, cardH, 25, shadowColor);
    Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 25, Gfx::COLOR_CARD_BG);

    switch (mState) {
        case STATE_INIT:
            break;
        case STATE_INIT_MOCHA:
            if (mStateFailure) {
                Gfx::DrawIcon(cardX + cardW/2, cardY + 100, 80, Gfx::COLOR_WARNING, 0xf06a, Gfx::ALIGN_CENTER);
                Gfx::Print(cardX + cardW/2, cardY + 200, 48, Gfx::COLOR_WARNING, _("common.local_mode"), Gfx::ALIGN_CENTER);
                Gfx::Print(cardX + cardW/2, cardY + 260, 32, Gfx::COLOR_ALT_TEXT, 
                          _("common.mocha_unavailable"), Gfx::ALIGN_CENTER);
                Gfx::Print(cardX + cardW/2, cardY + 300, 28, Gfx::COLOR_ALT_TEXT, 
                          _("common.other_features_available"), Gfx::ALIGN_CENTER);
                          
                // 显示继续提示
                if ((mFrameCount / 30) % 2 == 0) {
                    Gfx::Print(cardX + cardW/2, cardY + 350, 32, Gfx::COLOR_ACCENT, 
                              _("common.press_a_continue"), Gfx::ALIGN_CENTER);
                }
            } else {
                DrawLoadingSpinner(cardX + cardW/2, cardY + 120, 80, (mFrameCount % 60) / 60.0f);
                Gfx::Print(cardX + cardW/2, cardY + 220, 44, Gfx::COLOR_TEXT, _("common.init_mocha"), Gfx::ALIGN_CENTER);
            }
            break;
        case STATE_INIT_FS:
            if (mStateFailure) {
                Gfx::DrawIcon(cardX + cardW/2, cardY + 100, 80, Gfx::COLOR_ERROR, 0xf071, Gfx::ALIGN_CENTER);
                Gfx::Print(cardX + cardW/2, cardY + 200, 48, Gfx::COLOR_ERROR, _("common.filesystem_error"), Gfx::ALIGN_CENTER);
            } else {
                DrawLoadingSpinner(cardX + cardW/2, cardY + 120, 80, (mFrameCount % 60) / 60.0f);
                Gfx::Print(cardX + cardW/2, cardY + 220, 44, Gfx::COLOR_TEXT, _("common.mount_filesystem"), Gfx::ALIGN_CENTER);
            }
            break;
        case STATE_LOAD_MENU:
            DrawLoadingSpinner(cardX + cardW/2, cardY + 120, 80, (mFrameCount % 60) / 60.0f);
            Gfx::Print(cardX + cardW/2, cardY + 220, 44, Gfx::COLOR_SUCCESS, _("common.load_complete"), Gfx::ALIGN_CENTER);
            break;
        case STATE_IN_MENU:
            break;
    }

    // Bottom bar
    if (mStateFailure) {
        Gfx::DrawRectFilled(0, Gfx::SCREEN_HEIGHT - 80, Gfx::SCREEN_WIDTH, 80, Gfx::COLOR_BARS);
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT - 40, 40, Gfx::COLOR_TEXT, 
                  (std::string("\ue044 ") + _("input.exit")).c_str(), Gfx::ALIGN_CENTER);
    }
}

bool MainScreen::Update(Input &input) {
    if (mMenuScreen) {
        return mMenuScreen->Update(input);
    }

    // 在 Mocha 失败状态下,允许按 A 继续到本地模式
    if (mStateFailure) {
        if (mState == STATE_INIT_MOCHA && (input.data.buttons_d & Input::BUTTON_A)) {
            mStateFailure = false;
            mState = STATE_LOAD_MENU;  // 直接跳到加载菜单
        }
        return true;
    }

    switch (mState) {
        case STATE_INIT:
            mState = STATE_INIT_MOCHA;
            break;
        case STATE_INIT_MOCHA: {
            MochaUtilsStatus status = Mocha_InitLibrary();
            if (status == MOCHA_RESULT_SUCCESS) {
                sMochaAvailable = true;
                mState = STATE_INIT_FS;
                break;
            }

            // Mocha不可用,但不阻止程序运行
            sMochaAvailable = false;
            mStateFailure = true;
            break;
        }
        case STATE_INIT_FS: {
            // 只有Mocha可用时才挂载
            if (sMochaAvailable) {
                auto res = Mocha_MountFS(MLC_STORAGE_PATH, "/dev/mlc01", "/vol/storage_mlc01");
                if (res == MOCHA_RESULT_ALREADY_EXISTS) {
                    res = Mocha_MountFS(MLC_STORAGE_PATH, nullptr, "/vol/storage_mlc01");
                }
                if (res == MOCHA_RESULT_SUCCESS) {
                    mState = STATE_LOAD_MENU;
                    break;
                }

                mStateFailure = true;
                break;
            } else {
                // 没有Mocha,直接进入菜单
                mState = STATE_LOAD_MENU;
                break;
            }
        }
        case STATE_LOAD_MENU:
            mMenuScreen = std::make_unique<MenuScreen>();
            break;
        case STATE_IN_MENU:
            break;
    };

    return true;
}

void MainScreen::DrawStatus(std::string status, SDL_Color color) {
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, color, std::move(status), Gfx::ALIGN_CENTER);
}
