#include "MenuScreen.hpp"
#include "AboutScreen.hpp"
#include "DownloadScreen.hpp"
#include "ManageScreen.hpp"
#include "SettingsScreen.hpp"
#include "RebootScreen.hpp"
#include "Gfx.hpp"
#include "common.h"
#include "utils/logger.h"
#include "utils/FileLogger.hpp"
#include "utils/LanguageManager.hpp"
#include <coreinit/time.h>

MenuScreen::MenuScreen()
    : mEntries({
              {MENU_ID_DOWNLOAD_THEMES, {0xf019, "下载主题", "从网络下载 Wii U 主题"}},
              {MENU_ID_MANAGE_THEMES, {0xf07c, "管理主题", "查看和管理已安装的主题"}},
              {MENU_ID_SETTINGS, {0xf013, "设置", "配置下载源和其他选项"}},
              {MENU_ID_REBOOT, {0xf021, "重启系统", "重新启动 Wii U 主机"}},
              {MENU_ID_ABOUT, {0xf05a, "关于 UTheme", "查看应用信息和制作人员"}},
      }) {
    // Initialize animations
    for (auto &entry : mEntries) {
        entry.second.scaleAnim.SetImmediate(1.0f);
        entry.second.offsetAnim.SetImmediate(0.0f);
        entry.second.glowAnim.SetImmediate(0.0f);
    }
    
    mCurrentSelectorY = 170 + (int)mSelectedEntry * 160;  // 从150改为170
    mSelectorAnimation.SetImmediate(mCurrentSelectorY);
    mTitleAnimation.SetImmediate(0.0f);
    mTitleAnimation.SetTarget(1.0f, 800);
}

MenuScreen::~MenuScreen() = default;

void MenuScreen::RefreshMenuTexts() {
    mEntries[MENU_ID_DOWNLOAD_THEMES].name = _("menu.download_themes");
    mEntries[MENU_ID_DOWNLOAD_THEMES].description = _("menu.download_themes_desc");
    
    mEntries[MENU_ID_MANAGE_THEMES].name = _("menu.manage_themes");
    mEntries[MENU_ID_MANAGE_THEMES].description = _("menu.manage_themes_desc");
    
    mEntries[MENU_ID_SETTINGS].name = _("menu.settings");
    mEntries[MENU_ID_SETTINGS].description = _("menu.settings_desc");
    
    mEntries[MENU_ID_REBOOT].name = _("menu.reboot");
    mEntries[MENU_ID_REBOOT].description = _("menu.reboot_desc");
    
    mEntries[MENU_ID_ABOUT].name = _("menu.about");
    mEntries[MENU_ID_ABOUT].description = _("menu.about_desc");
}

void MenuScreen::UpdateAnimations() {
    mSelectorAnimation.Update();
    mTitleAnimation.Update();
    
    for (MenuID id = MENU_ID_MIN; id <= MENU_ID_MAX; id = static_cast<MenuID>(id + 1)) {
        auto &entry = mEntries[id];
        entry.scaleAnim.Update();
        entry.offsetAnim.Update();
        entry.glowAnim.Update();
    }
}

void MenuScreen::DrawCard(int x, int y, int w, int h, MenuEntry &entry, bool selected) {
    float scale = entry.scaleAnim.GetValue();
    float offset = entry.offsetAnim.GetValue();
    float glow = entry.glowAnim.GetValue();
    
    int scaledW = (int)(w * scale);
    int scaledH = (int)(h * scale);
    int scaledX = x + (w - scaledW) / 2 + (int)offset;
    int scaledY = y + (h - scaledH) / 2;
    
    // Draw enlarged shadow behind card - no offset, just bigger
    int shadowExpand = selected ? 12 : 8;  // How much bigger than card
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = selected ? 100 : 60;
    Gfx::DrawRectRounded(scaledX - shadowExpand/2, scaledY - shadowExpand/2, 
                         scaledW + shadowExpand, scaledH + shadowExpand, 20, shadowColor);
    
    // Draw card background with rounded corners - always
    SDL_Color cardColor = selected ? Gfx::COLOR_CARD_HOVER : Gfx::COLOR_CARD_BG;
    // Make unselected cards slightly brighter to stand out from background
    if (!selected) {
        cardColor.r = (Uint8)((cardColor.r < 220) ? cardColor.r * 1.2f : 255);
        cardColor.g = (Uint8)((cardColor.g < 220) ? cardColor.g * 1.2f : 255);
        cardColor.b = (Uint8)((cardColor.b < 220) ? cardColor.b * 1.2f : 255);
    }
    
    // Draw rounded rectangle as base
    Gfx::DrawRectRounded(scaledX, scaledY, scaledW, scaledH, 20, cardColor);
    
    // Draw accent border for selected
    if (selected) {
        SDL_Color glowColor = Gfx::COLOR_ACCENT;
        glowColor.a = (Uint8)(255 * glow * 0.8f);
        Gfx::DrawRectRoundedOutline(scaledX - 3, scaledY - 3, scaledW + 6, scaledH + 6, 22, 3, glowColor);
    }
    
    // Draw icon with glow
    int iconSize = 64;
    SDL_Color iconColor = selected ? Gfx::COLOR_ACCENT : Gfx::COLOR_WHITE;
    
    Gfx::DrawIcon(scaledX + 40, scaledY + scaledH / 2, iconSize, iconColor, entry.icon, Gfx::ALIGN_VERTICAL);
    
    // Draw text
    int textX = scaledX + 150;
    SDL_Color textColor = selected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT;
    Gfx::Print(textX, scaledY + scaledH / 2 - 20, 42, textColor, entry.name.c_str(), Gfx::ALIGN_VERTICAL);
    
    SDL_Color descColor = Gfx::COLOR_ALT_TEXT;
    descColor.a = (Uint8)(descColor.a * 0.8f);
    Gfx::Print(textX, scaledY + scaledH / 2 + 20, 28, descColor, entry.description.c_str(), Gfx::ALIGN_VERTICAL);
}

void MenuScreen::Draw() {
    // Update transition animation
    mTransition.Update();
    
    // If transitioning, handle screen switching effect
    if (mTransition.IsActive() && mSubscreen) {
        float progress = mTransition.GetProgress();
        
        // Simple fade transition for now
        // Old screen fades out, new screen fades in
        if (progress < 0.5f) {
            // First half: fade out old screen (menu)
            DrawMenuContent();
            // Draw dark overlay with increasing alpha
            SDL_Color overlay = {0, 0, 0, (Uint8)(progress * 2.0f * 200)};
            Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, overlay);
        } else {
            // Second half: fade in new screen
            mSubscreen->Draw();
            // Draw dark overlay with decreasing alpha
            SDL_Color overlay = {0, 0, 0, (Uint8)((1.0f - progress) * 2.0f * 200)};
            Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, overlay);
        }
        return;
    }
    
    // Normal drawing (no transition)
    if (mSubscreen) {
        mSubscreen->Draw();
        return;
    }

    DrawMenuContent();
}

void MenuScreen::DrawMenuContent() {

    UpdateAnimations();
    
    // 刷新菜单文本（以防语言改变）
    RefreshMenuTexts();

    // Draw animated background gradient
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);

    // Use base class method to draw animated top bar with local mode indicator
    DrawAnimatedTopBar("", mTitleAnimation, 0xf53f);

    // Draw menu cards
    int cardY = 170;  // 从150改为170，向下移动20px
    int cardSpacing = 160;
    
    for (MenuID id = MENU_ID_MIN; id <= MENU_ID_MAX; id = static_cast<MenuID>(id + 1)) {
        bool selected = (id == mSelectedEntry);
        DrawCard(200, cardY, 1520, 140, mEntries[id], selected);
        cardY += cardSpacing;
    }

    // Draw bottom hint bar
    Gfx::DrawRectFilled(0, Gfx::SCREEN_HEIGHT - 80, Gfx::SCREEN_WIDTH, 80, Gfx::COLOR_BARS);
    Gfx::Print(60, Gfx::SCREEN_HEIGHT - 40, 40, Gfx::COLOR_TEXT, (std::string("\ue07d ") + _("input.select")).c_str(), Gfx::ALIGN_VERTICAL);
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT - 40, 40, Gfx::COLOR_TEXT, (std::string("\ue044 ") + _("input.exit")).c_str(), Gfx::ALIGN_CENTER);
    Gfx::Print(Gfx::SCREEN_WIDTH - 60, Gfx::SCREEN_HEIGHT - 40, 40, Gfx::COLOR_TEXT, (std::string("\ue000 ") + _("input.confirm")).c_str(), Gfx::ALIGN_VERTICAL | Gfx::ALIGN_RIGHT);
    
    // Debug: Show touch status only if debug is enabled
    if (mShowDebug) {
        char touchInfo[256];
        snprintf(touchInfo, sizeof(touchInfo), "T:%d V:%d LT:%d P:%d Raw:(%d,%d) Scr:(%d,%d)", 
                mDebugTouched ? 1 : 0, 
                mDebugValid ? 1 : 0,
                mDebugLastTouched ? 1 : 0,
                mDebugProcessing ? 1 : 0,
                mDebugRawX, mDebugRawY,
                mDebugTouchX, mDebugTouchY);
        Gfx::Print(20, 150, 24, Gfx::COLOR_WARNING, touchInfo, Gfx::ALIGN_VERTICAL);
    }
}

bool MenuScreen::Update(Input &input) {
    // 处理子页面
    if (mSubscreen) {
        if (!mSubscreen->Update(input)) {
            // subscreen wants to exit
            mSubscreen.reset();
            // Restart title animation
            mTitleAnimation.SetImmediate(0.0f);
            mTitleAnimation.SetTarget(1.0f, 500);
            // 设置返回冷却,防止立即重新进入子页面
            mJustReturnedFromSubscreen = true;
            mReturnCooldownFrames = 10; // 10帧冷却时间(约1/6秒)
            
            // 检查是否从空的 ManageScreen 返回
            if (ManageScreen::sReturnedDueToEmpty) {
                // 切换到下载主题选项并高亮
                MenuID prevEntry = mSelectedEntry;
                mSelectedEntry = MENU_ID_DOWNLOAD_THEMES;
                
                // 更新动画
                mEntries[prevEntry].scaleAnim.SetTarget(1.0f, 400);
                mEntries[prevEntry].glowAnim.SetTarget(0.0f, 400);
                
                // 先缩放和发光,然后启动呼吸效果
                mEntries[mSelectedEntry].scaleAnim.SetTarget(1.15f, 400);
                mEntries[mSelectedEntry].glowAnim.SetTarget(1.0f, 400);
                
                // 使用 offsetAnim 创建呼吸效果 - 先向右移动再回中心,然后重复
                mEntries[mSelectedEntry].offsetAnim.SetImmediate(0.0f);
                // 由于不支持循环,我们先移动一次
                mEntries[mSelectedEntry].offsetAnim.SetTarget(10.0f, 300);
                
                // 更新选择器位置
                mPrevSelectedEntry = prevEntry;
                mCurrentSelectorY = 170 + (int)mSelectedEntry * 160;
                mSelectorAnimation.SetTarget(mCurrentSelectorY, 400);
                
                // 重置标志
                ManageScreen::sReturnedDueToEmpty = false;
                
                FileLogger::GetInstance().LogInfo("Switched to Download Themes after empty ManageScreen");
            }
            
            return true;
        }
        return true;
    }
    
    // 处理返回冷却
    if (mJustReturnedFromSubscreen) {
        mReturnCooldownFrames--;
        if (mReturnCooldownFrames <= 0) {
            mJustReturnedFromSubscreen = false;
        }
        // 冷却期间不处理任何会触发子页面的输入
        return true;
    }

    // Check for version number click to enable debug (7 clicks in 3 seconds)
    if (input.data.touched && input.data.validPointer && !input.lastData.touched) {
        // Get version text position and size
        int versionX = 140 + Gfx::GetTextWidth(56, _("app_name")) + 20;
        int versionY = 25 + 45;  // titleY + 45
        int versionW = Gfx::GetTextWidth(32, APP_VERSION_FULL);
        
        // Check if clicking on version number
        float scaleX = 1920.0f / 1280.0f;
        float scaleY = 1080.0f / 720.0f;
        int touchX = (int)((input.data.x * scaleX) + 960);
        int touchY = (int)(540 - (input.data.y * scaleY));
        
        if (touchX >= versionX && touchX < (versionX + versionW) && 
            touchY >= (versionY - 16) && touchY < (versionY + 16)) {
            
            uint64_t currentTime = OSGetTime();
            uint64_t timeDiff = OSTicksToMilliseconds(currentTime - mLastVersionClickTime);
            
            if (timeDiff < 3000) {
                // Within 3 seconds
                mVersionClickCount++;
                if (mVersionClickCount >= 7) {
                    mShowDebug = !mShowDebug;  // Toggle debug
                    mVersionClickCount = 0;
                }
            } else {
                // Reset counter if too much time passed
                mVersionClickCount = 1;
            }
            
            mLastVersionClickTime = currentTime;
        }
    }

    bool selectionChanged = false;

    // Update debug touch info - store raw and processed values
    mDebugTouched = input.data.touched;
    mDebugValid = input.data.validPointer;
    mDebugRawX = input.data.x;
    mDebugRawY = input.data.y;
    mDebugLastTouched = input.lastData.touched;
    mDebugProcessing = false;
    
    if (input.data.touched && input.data.validPointer) {
        float scaleX = 1920.0f / 1280.0f;
        float scaleY = 1080.0f / 720.0f;
        mDebugTouchX = (int)((input.data.x * scaleX) + 960);
        // Y coordinate is inverted - flip it
        mDebugTouchY = (int)(540 - (input.data.y * scaleY));
    } else {
        mDebugTouchX = 0;
        mDebugTouchY = 0;
    }

    // Debug: Print touch data when touched
    if (input.data.touched) {
        DEBUG_FUNCTION_LINE("Touch detected: x=%d, y=%d, valid=%d, lastTouched=%d", 
                           input.data.x, input.data.y, input.data.validPointer, input.lastData.touched);
    }

    // Check for touch input on menu cards
    if (input.data.touched && input.data.validPointer && !input.lastData.touched) {
        mDebugProcessing = true;
        int cardY = 170;
        int cardSpacing = 160;
        
        DEBUG_FUNCTION_LINE("Processing touch for card selection");
        
        for (MenuID id = MENU_ID_MIN; id <= MENU_ID_MAX; id = static_cast<MenuID>(id + 1)) {
            if (IsTouchInRect(input, 200, cardY, 1520, 140)) {
                DEBUG_FUNCTION_LINE("Touch hit card %d", id);
                if (id != mSelectedEntry) {
                    // Change selection if touching a different card
                    mPrevSelectedEntry = mSelectedEntry;
                    mSelectedEntry = id;
                    selectionChanged = true;
                } else {
                    // If touching already selected card, trigger it
                    input.data.buttons_d |= Input::BUTTON_A;
                }
                break;
            }
            cardY += cardSpacing;
        }
    }

    // 检测上下按键和摇杆输入(支持循环和长按连续)
    bool upPressed = (input.data.buttons_d & Input::BUTTON_UP) || (input.data.buttons_d & Input::STICK_L_UP);
    bool downPressed = (input.data.buttons_d & Input::BUTTON_DOWN) || (input.data.buttons_d & Input::STICK_L_DOWN);
    bool upHeld = (input.data.buttons_h & Input::BUTTON_UP) || (input.data.buttons_h & Input::STICK_L_UP);
    bool downHeld = (input.data.buttons_h & Input::BUTTON_DOWN) || (input.data.buttons_h & Input::STICK_L_DOWN);
    
    // 处理按下事件
    if (upPressed || downPressed) {
        mHoldFrames = 0;  // 重置计数器
    }
    
    // 长按连续选择逻辑
    bool shouldMoveUp = upPressed;
    bool shouldMoveDown = downPressed;
    
    if (upHeld && !upPressed) {
        mHoldFrames++;
        if (mHoldFrames > mRepeatDelay) {
            if ((mHoldFrames - mRepeatDelay) % mRepeatRate == 0) {
                shouldMoveUp = true;
            }
        }
    } else if (downHeld && !downPressed) {
        mHoldFrames++;
        if (mHoldFrames > mRepeatDelay) {
            if ((mHoldFrames - mRepeatDelay) % mRepeatRate == 0) {
                shouldMoveDown = true;
            }
        }
    } else if (!upHeld && !downHeld) {
        mHoldFrames = 0;
    }

    if (shouldMoveDown) {
        mPrevSelectedEntry = mSelectedEntry;
        // 循环选择:到底部后回到顶部
        if (mSelectedEntry >= MENU_ID_MAX) {
            mSelectedEntry = MENU_ID_MIN;
        } else {
            mSelectedEntry = static_cast<MenuID>(mSelectedEntry + 1);
        }
        selectionChanged = true;
    } else if (shouldMoveUp) {
        mPrevSelectedEntry = mSelectedEntry;
        // 循环选择:到顶部后回到底部
        if (mSelectedEntry <= MENU_ID_MIN) {
            mSelectedEntry = MENU_ID_MAX;
        } else {
            mSelectedEntry = static_cast<MenuID>(mSelectedEntry - 1);
        }
        selectionChanged = true;
    }

    if (selectionChanged) {
        // Animate selector position
        float targetY = 170 + (int)mSelectedEntry * 160;  // 从150改为170
        mSelectorAnimation.SetTarget(targetY, 400);
        
        // 立即触发新选中项的动画
        mEntries[mSelectedEntry].scaleAnim.SetTarget(1.05f, 400);
        mEntries[mSelectedEntry].glowAnim.SetTarget(1.0f, 400);
        mEntries[mSelectedEntry].offsetAnim.SetTarget(10.0f, 120);
        mEntries[mSelectedEntry].offsetAnim.SetTarget(0.0f, 120);
        
        // 取消之前选中项的动画
        mEntries[mPrevSelectedEntry].scaleAnim.SetTarget(1.0f, 400);
        mEntries[mPrevSelectedEntry].glowAnim.SetTarget(0.0f, 400);
    }

    if (input.data.buttons_d & Input::BUTTON_A) {
        std::unique_ptr<Screen> newScreen;
        
        switch (mSelectedEntry) {
            case MENU_ID_DOWNLOAD_THEMES:
                newScreen = std::make_unique<DownloadScreen>();
                break;
            case MENU_ID_MANAGE_THEMES:
                newScreen = std::make_unique<ManageScreen>();
                break;
            case MENU_ID_SETTINGS:
                newScreen = std::make_unique<SettingsScreen>();
                break;
            case MENU_ID_REBOOT:
                newScreen = std::make_unique<RebootScreen>();
                break;
            case MENU_ID_ABOUT:
                newScreen = std::make_unique<AboutScreen>();
                break;
        }
        
        if (newScreen) {
            // Start transition animation
            mTransition.Start(ScreenTransition::SLIDE_LEFT, this, newScreen.get());
            mSubscreen = std::move(newScreen);
        }
    }

    return true;
}
