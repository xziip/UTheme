#pragma once

#include "../utils/Animation.hpp"
#include <string>
#include <SDL2/SDL.h>

// BGM播放通知组件 - 显示当前播放的音乐名称
class BgmNotification {
public:
    BgmNotification();
    
    // 显示当前播放的音乐
    void ShowNowPlaying(const std::string& musicName);
    
    // 显示错误消息
    void ShowError(const std::string& message);
    
    // 更新和绘制
    void Update();
    void Draw();
    
    // 状态查询
    bool IsVisible() const { return mVisible; }
    
    // 立即隐藏
    void Hide();
    
private:
    bool mVisible;
    bool mIsError;
    std::string mMusicName;
    std::string mMessage;
    Animation mFadeAnim;
    Animation mSlideAnim;
    uint64_t mShowTime;
    uint64_t mDisplayDuration; // 显示持续时间(毫秒)
    
    static const int NOTIFICATION_WIDTH = 500;
    static const int NOTIFICATION_HEIGHT = 90;
};
