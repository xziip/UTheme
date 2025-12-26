#pragma once
#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include "../utils/ThemeManager.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>

// 本地主题结构（从磁盘加载）
struct LocalTheme {
    std::string name;
    std::string path;
    std::string id;
    std::string author;
    std::string description;
    int downloads;
    int likes;
    std::string updatedAt;
    std::vector<std::string> tags;
    
    // 图片路径（本地文件 - webp格式）
    std::string collageThumbPath;
    std::string collageHdPath;
    std::string launcherThumbPath;
    std::string launcherHdPath;
    std::string warawaraThumbPath;
    std::string warawaraHdPath;
    
    // 图片纹理（异步加载）
    SDL_Texture* collageThumbTexture = nullptr;
    bool collageThumbLoaded = false;  // 标记是否已请求加载
    
    bool hasPatched;
    int bpsCount;
};

class ManageScreen : public Screen {
public:
    ManageScreen();
    ~ManageScreen() override;

    void Draw() override;
    bool Update(Input &input) override;
    
    // 静态标志: 是否因为空状态而返回（用于提示用户去下载）
    static bool sReturnedDueToEmpty;

private:
    int mFrameCount = 0;
    Animation mTitleAnim;
    Animation mContentAnim;
    Animation mCardHoverAnim;
    
    std::vector<LocalTheme> mThemes;
    int mSelectedIndex = 0;
    int mPreviousSelectedIndex = 0;
    int mScrollOffset = 0;
    bool mIsLoading = true;
    
    // 长按连续选择
    int mHoldFrames = 0;
    int mRepeatDelay = 30;  // 初始延迟帧数 (约0.5秒)
    int mRepeatRate = 6;    // 重复间隔帧数 (约0.1秒)
    
    // 动画系统 - 每个主题卡片的动画
    struct ThemeAnimation {
        Animation scaleAnim;
        Animation highlightAnim;
    };
    std::vector<ThemeAnimation> mThemeAnims;
    
    // 横向卡片列表布局 - 和 DownloadScreen 一样
    static constexpr int LIST_X = 100;
    static constexpr int LIST_Y = 150;
    static constexpr int CARD_WIDTH = 1720;
    static constexpr int CARD_HEIGHT = 200;
    static constexpr int CARD_SPACING = 20;
    static constexpr int VISIBLE_COUNT = 3;
    
    void ScanLocalThemes();
    void LoadThemeMetadata(LocalTheme& theme);
    void InitAnimations();
    void UpdateAnimations();
    void DrawThemeList();
    void DrawThemeCard(LocalTheme& theme, int x, int y, int w, int h, bool selected, int themeIndex);
};
