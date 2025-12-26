#pragma once

#include "input/Input.h"
#include "utils/logger.h"
#include "utils/Animation.hpp"
#include "utils/BgmNotification.hpp"
#include <string>
#include <utility>
#include <vector>

class Screen {
public:
    Screen() : mFadeAnimation() {
        // Start with fade in animation
        mFadeAnimation.SetImmediate(0.0f);
        mFadeAnimation.SetTarget(1.0f, 600);  // 600ms fade in
    }

    virtual ~Screen() = default;

    virtual void Draw() = 0;

    virtual bool Update(Input &input) = 0;
    
    // Get current fade alpha (0.0 = fully transparent, 1.0 = fully opaque)
    float GetFadeAlpha() const {
        return mFadeAnimation.GetValue();
    }
    
    // Check if fade animation is complete
    bool IsFadeComplete() const {
        return !mFadeAnimation.IsAnimating();
    }
    
    // Start fade out animation (call before switching screens)
    void StartFadeOut() {
        mFadeAnimation.SetTarget(0.0f, 500);  // 500ms fade out
    }
    
    // Update fade animation
    void UpdateFade() {
        mFadeAnimation.Update();
    }

protected:
    static void DrawTopBar(const char *name);
    
    // 带动画的顶部栏
    void DrawAnimatedTopBar(const std::string& name, Animation& titleAnim, uint16_t icon = 0xf53f);

    static void DrawBottomBar(const char *leftHint, const char *centerHint, const char *rightHint);

    static int DrawHeader(int x, int y, int w, uint16_t icon, const char *text);

    // Helper function to check if touch point is within a rectangle
    static bool IsTouchInRect(const Input &input, int x, int y, int w, int h) {
        // Check both current touch (for new touch) and held touch
        if (input.data.touched && input.data.validPointer) {
            // Only trigger on new touch (not held)
            if (input.lastData.touched) {
                return false;
            }
            
            // Input uses 1280x720 coordinate system (centered at 0,0)
            // Coordinates range: x(-640 to 640), y(-360 to 360)
            // We need to scale to 1920x1080 screen space
            
            // Scale from 1280x720 to 1920x1080
            float scaleX = 1920.0f / 1280.0f;  // 1.5
            float scaleY = 1080.0f / 720.0f;   // 1.5
            
            int touchX = (int)((input.data.x * scaleX) + 960);  // Scale and convert to 0~1920
            // Y coordinate is inverted - flip it
            int touchY = (int)(540 - (input.data.y * scaleY));  // Flip Y: 540 - scaled_y
            
            // Check if touch is within rectangle bounds
            bool inRect = touchX >= x && touchX < (x + w) && touchY >= y && touchY < (y + h);
            
            // Debug output
            DEBUG_FUNCTION_LINE("Touch: raw(%d,%d) scaled(%d,%d) rect(%d,%d,%d,%d) hit=%d", 
                               input.data.x, input.data.y, touchX, touchY, x, y, w, h, inRect);
            
            return inRect;
        }
        return false;
    }

    struct ScreenListElement {
        explicit ScreenListElement(std::string string, bool monospace = false) : string(std::move(string)), monospace(monospace) {}

        explicit ScreenListElement(const char *string, bool monospace = false) : string(string), monospace(monospace) {}

        std::string string;
        bool monospace;
    };

    using ScreenList = std::vector<std::pair<std::string, ScreenListElement>>;

    static int DrawList(int x, int y, int w, const ScreenList &items);
    
    // 全局触摸调试信息绘制 (所有子类都可以调用)
    // 实现在 Screen.cpp 中以避免循环依赖
    static void DrawTouchDebugInfo(const Input &input, bool enabled = true);

public:
    // 全局BGM通知系统
    static BgmNotification& GetBgmNotification();
    static void UpdateBgmNotification();
    static void DrawBgmNotification();

private:
    Animation mFadeAnimation;
    static BgmNotification sBgmNotification; // 全局单例
};
