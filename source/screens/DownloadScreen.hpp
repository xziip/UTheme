#pragma once
#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include "../utils/ThemeManager.hpp"
#include <memory>
#include <set>

class DownloadScreen : public Screen {
public:
    DownloadScreen();
    ~DownloadScreen() override;

    void Draw() override;
    bool Update(Input &input) override;

private:
    enum State {
        STATE_INIT,
        STATE_LOADING,
        STATE_SHOW_THEMES,
        STATE_DOWNLOADING,
        STATE_DONE,
        STATE_ERROR,
    };
    
    State mState = STATE_INIT;
    std::string mErrorMessage;
    std::string mLoadingMessage = "";  // 加载提示信息
    int mLoadedThemeCount = 0;         // 已加载的主题数量
    int mFrameCount = 0;
    int mDownloadStartFrame = 0;  // 下载开始的帧数
    int mReturnFromDetailFrame = 0;  // 从详情页面返回的帧数
    Animation mTitleAnim;
    
    // 主题管理
    std::unique_ptr<ThemeManager> mThemeManager;
    int mSelectedTheme = 0;
    int mPrevSelectedTheme = 0;
    int mScrollOffset = 0;
    
    // 长按连续选择
    int mHoldFrames = 0;
    int mRepeatDelay = 30;  // 初始延迟帧数 (约0.5秒)
    int mRepeatRate = 6;    // 重复间隔帧数 (约0.1秒)
    
    // 已安装主题缓存(用于快速检查,避免频繁磁盘IO)
    std::set<std::string> mInstalledThemeIds;
    
    // 详情屏幕
    class ThemeDetailScreen* mDetailScreen = nullptr;
    
    // 主题卡片动画
    struct ThemeCardAnim {
        Animation scaleAnim;
        Animation highlightAnim;
    };
    std::vector<ThemeCardAnim> mThemeAnims;
    
    // 初始化动画
    void InitAnimations(size_t themeCount);
    void UpdateAnimations();
    
    // 扫描已安装的主题
    void ScanInstalledThemes();
    
    // 触摸支持
    bool IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH);
    
    // 绘制主题列表
    void DrawThemeList();
    void DrawThemeCard(int x, int y, int w, int h, Theme& theme, bool selected, int themeIndex);
};
