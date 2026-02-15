#pragma once
#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include "../utils/ThemeManager.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <thread>
#include <atomic>

class ThemeDetailScreen : public Screen {
public:
    ThemeDetailScreen(const Theme* theme, ThemeManager* themeManager);
    ~ThemeDetailScreen() override;

    void Draw() override;
    bool Update(Input &input) override;

private:
    const Theme* mTheme;
    ThemeManager* mThemeManager;
    bool mIsLocalMode = false; // 是否为本地模式(已下载的主题)
    
    // 当前激活的主题名称（来自 StyleMiiU 配置）
    std::string mCurrentThemeName;
    
    enum State {
        STATE_VIEWING,
        STATE_DOWNLOADING,
        STATE_DOWNLOAD_COMPLETE,
        STATE_INSTALLING,
        STATE_INSTALL_COMPLETE,
        STATE_INSTALL_ERROR,
        STATE_DOWNLOAD_ERROR,
        STATE_UNINSTALL_CONFIRM,  // 卸载确认对话框
        STATE_UNINSTALLING,        // 正在卸载
        STATE_UNINSTALL_COMPLETE,  // 卸载完成
        STATE_SET_CURRENT_CONFIRM, // 设置当前主题确认对话框
        STATE_SETTING_CURRENT,     // 正在设置当前主题
        STATE_SET_CURRENT_COMPLETE,// 设置完成
        STATE_SET_CURRENT_ERROR,   // 设置失败
        STATE_FULLSCREEN_PREVIEW   // 全屏预览模式
    };
    
    std::atomic<State> mState{STATE_VIEWING};
    int mFrameCount = 0;
    int mEnterFrame = 0; // 进入屏幕的帧数，用于输入冷却
    int mDownloadStartFrame = 0;
    std::atomic<float> mDownloadProgress{0.0f};
    std::atomic<float> mInstallProgress{0.0f};
    std::string mInstallError;
    int mErrorDisplayFrames = 0; // 错误显示帧计数
    
    // 安装线程管理
    std::thread mInstallThread;
    std::atomic<bool> mInstallThreadRunning{false};
    
    // 动画
    Animation mTitleAnim;
    Animation mContentAnim;
    Animation mButtonHoverAnim;
    Animation mPreviewSwitchAnim;
    Animation mPreviewSlideAnim; // 滑动动画
    Animation mFullscreenSlideAnim; // 全屏预览滑动动画
    
    // 预览图切换
    int mCurrentPreview = 0; // 0=collage, 1=launcher, 2=warawara
    int mPreviousPreview = 0; // 用于滑动动画
    int mSlideDirection = 0; // 1=向左, -1=向右, 0=无滑动
    int mFullscreenPrevPreview = 0; // 全屏预览上一张图片索引
    int mFullscreenSlideDir = 0; // 全屏滑动方向
    
    // 按钮状态
    bool mDownloadButtonHovered = false;
    
    // 触摸滑动检测 - 存储原始1280x720坐标（参考MenuScreen）
    bool mTouchStarted = false;
    int mTouchStartRawX = 0;
    int mTouchStartRawY = 0;
    int mTouchCurrentRawX = 0;
    int mTouchCurrentRawY = 0;
    int mTouchDragOffsetX = 0; // 跟手拖动的偏移量(屏幕坐标)
    bool mIsDragging = false;   // 是否正在拖动
    
    // 卸载请求标志
    bool mUninstallRequested = false;
    
    // 调试信息 - 保存最后的输入状态用于绘制
    Input mLastInput;
    
    // 辅助函数
    void DrawPreviewSection(int yOffset);
    void DrawInfoSection(int yOffset);
    void DrawDownloadProgress();
    void DrawFullscreenPreview(); // 全屏预览绘制
    
    bool IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH);
    void HandleTouchInput(const Input& input);
    bool UninstallTheme(); // 卸载主题
};
