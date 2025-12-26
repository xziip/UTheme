#pragma once

#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include "../utils/ScreenTransition.hpp"
#include <map>
#include <memory>
#include <vector>
#include <string>

class MenuScreen : public Screen {
public:
    MenuScreen();

    ~MenuScreen() override;

    void Draw() override;

    bool Update(Input &input) override;

private:
    std::unique_ptr<Screen> mSubscreen;
    ScreenTransition mTransition;

    enum MenuID {
        MENU_ID_DOWNLOAD_THEMES,
        MENU_ID_MANAGE_THEMES,
        MENU_ID_SETTINGS,
        MENU_ID_REBOOT,
        MENU_ID_ABOUT,

        MENU_ID_MIN = MENU_ID_DOWNLOAD_THEMES,
        MENU_ID_MAX = MENU_ID_ABOUT,
    };

    struct MenuEntry {
        uint16_t icon;
        std::string name;
        std::string description;
        Animation scaleAnim;
        Animation offsetAnim;
        Animation glowAnim;
    };
    std::map<MenuID, MenuEntry> mEntries;
    MenuID mSelectedEntry = MENU_ID_MIN;
    MenuID mPrevSelectedEntry = MENU_ID_MIN;
    
    Animation mSelectorAnimation;
    Animation mTitleAnimation;
    float mCurrentSelectorY = 0;
    
    // Debug: touch info
    int mDebugTouchX = 0;
    int mDebugTouchY = 0;
    bool mDebugTouched = false;
    bool mDebugValid = false;
    int mDebugRawX = 0;
    int mDebugRawY = 0;
    bool mDebugLastTouched = false;
    bool mDebugProcessing = false;
    bool mShowDebug = false;
    
    // Debug activation
    int mVersionClickCount = 0;
    uint64_t mLastVersionClickTime = 0;
    
    // 防止从子页面返回后立即重新进入
    bool mJustReturnedFromSubscreen = false;
    int mReturnCooldownFrames = 0;
    
    // 长按连续选择
    int mHoldFrames = 0;
    int mRepeatDelay = 30;  // 初始延迟帧数 (约0.5秒)
    int mRepeatRate = 8;    // 重复间隔帧数
    
    void DrawCard(int x, int y, int w, int h, MenuEntry &entry, bool selected);
    void UpdateAnimations();
    void DrawMenuContent();
    void RefreshMenuTexts();  // 刷新菜单文本
};
