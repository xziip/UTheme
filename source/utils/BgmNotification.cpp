#include "BgmNotification.hpp"
#include "../Gfx.hpp"
#include "../utils/LanguageManager.hpp"
#include <coreinit/time.h>

BgmNotification::BgmNotification() 
    : mVisible(false)
    , mIsError(false)
    , mShowTime(0)
    , mDisplayDuration(4000) { // 默认显示4秒
}

void BgmNotification::ShowNowPlaying(const std::string& musicName) {
    mMusicName = musicName;
    mMessage = "";
    mIsError = false;
    mVisible = true;
    mShowTime = OSGetTime();
    
    // 设置动画 - 从右侧滑入
    mSlideAnim.SetImmediate(1.0f); // 从屏幕外开始
    mSlideAnim.SetTarget(0.0f, 400); // 滑入
    
    mFadeAnim.SetImmediate(0.0f);
    mFadeAnim.SetTarget(1.0f, 300);
}

void BgmNotification::ShowError(const std::string& message) {
    mMusicName = "";
    mMessage = message;
    mIsError = true;
    mVisible = true;
    mShowTime = OSGetTime();
    
    // 设置动画
    mSlideAnim.SetImmediate(1.0f);
    mSlideAnim.SetTarget(0.0f, 400);
    
    mFadeAnim.SetImmediate(0.0f);
    mFadeAnim.SetTarget(1.0f, 300);
}

void BgmNotification::Hide() {
    if (mVisible && mFadeAnim.GetTarget() > 0.0f) {
        mFadeAnim.SetTarget(0.0f, 300);
        mSlideAnim.SetTarget(1.0f, 400);
    }
}

void BgmNotification::Update() {
    if (!mVisible) return;
    
    mSlideAnim.Update();
    mFadeAnim.Update();
    
    // 检查是否应该隐藏
    uint64_t currentTime = OSGetTime();
    uint64_t elapsed = OSTicksToMilliseconds(currentTime - mShowTime);
    
    if (elapsed > mDisplayDuration) {
        // 开始淡出动画
        if (mFadeAnim.GetTarget() > 0.0f) {
            mFadeAnim.SetTarget(0.0f, 300);
            mSlideAnim.SetTarget(1.0f, 400);
        }
        
        // 动画完成后隐藏
        if (mFadeAnim.GetValue() <= 0.01f) {
            mVisible = false;
        }
    }
}

void BgmNotification::Draw() {
    if (!mVisible) return;
    
    float fadeAlpha = mFadeAnim.GetValue();
    float slideOffset = mSlideAnim.GetValue();
    
    if (fadeAlpha <= 0.0f) return;
    
    // 计算位置 - 从右侧滑入,显示在右上角
    int x = Gfx::SCREEN_WIDTH - NOTIFICATION_WIDTH - 40 + (int)(slideOffset * (NOTIFICATION_WIDTH + 100));
    int y = 140; // 在顶部栏下方
    
    // 绘制阴影
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = (Uint8)(100 * fadeAlpha);
    Gfx::DrawRectRounded(x + 5, y + 5, NOTIFICATION_WIDTH, NOTIFICATION_HEIGHT, 16, shadowColor);
    
    // 绘制背景
    SDL_Color bgColor = mIsError ? SDL_Color{50, 20, 20, (Uint8)(240 * fadeAlpha)} 
                                 : SDL_Color{30, 35, 50, (Uint8)(240 * fadeAlpha)};
    Gfx::DrawRectRounded(x, y, NOTIFICATION_WIDTH, NOTIFICATION_HEIGHT, 16, bgColor);
    
    // 绘制左侧装饰条
    SDL_Color accentColor = mIsError ? Gfx::COLOR_ERROR : Gfx::COLOR_ACCENT;
    accentColor.a = (Uint8)(255 * fadeAlpha);
    Gfx::DrawRectRounded(x, y, 6, NOTIFICATION_HEIGHT, 16, accentColor);
    
    if (mIsError) {
        // 错误模式 - 显示错误图标和消息
        SDL_Color iconColor = Gfx::COLOR_ERROR;
        iconColor.a = (Uint8)(255 * fadeAlpha);
        Gfx::DrawIcon(x + 35, y + NOTIFICATION_HEIGHT / 2, 36, iconColor, 0xf06a, Gfx::ALIGN_CENTER);
        
        SDL_Color textColor = Gfx::COLOR_TEXT;
        textColor.a = (Uint8)(255 * fadeAlpha);
        Gfx::Print(x + 70, y + 30, 28, textColor, "BGM Error", Gfx::ALIGN_VERTICAL);
        
        SDL_Color msgColor = Gfx::COLOR_ALT_TEXT;
        msgColor.a = (Uint8)(220 * fadeAlpha);
        Gfx::Print(x + 70, y + 60, 24, msgColor, mMessage.c_str(), Gfx::ALIGN_VERTICAL);
    } else {
        // 正常模式 - 显示音乐图标和名称
        SDL_Color iconColor = Gfx::COLOR_ACCENT;
        iconColor.a = (Uint8)(255 * fadeAlpha);
        Gfx::DrawIcon(x + 35, y + NOTIFICATION_HEIGHT / 2, 40, iconColor, 0xf001, Gfx::ALIGN_CENTER);
        
        // "Now Playing" 标签
        SDL_Color labelColor = Gfx::COLOR_ALT_TEXT;
        labelColor.a = (Uint8)(200 * fadeAlpha);
        Gfx::Print(x + 70, y + 22, 22, labelColor, "Now Playing", Gfx::ALIGN_VERTICAL);
        
        // 音乐名称
        SDL_Color nameColor = Gfx::COLOR_TEXT;
        nameColor.a = (Uint8)(255 * fadeAlpha);
        Gfx::Print(x + 70, y + 50, 28, nameColor, mMusicName.c_str(), Gfx::ALIGN_VERTICAL);
    }
}
