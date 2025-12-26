#include "ManageScreen.hpp"
#include "ThemeDetailScreen.hpp"
#include "DownloadScreen.hpp"
#include "Gfx.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/FileLogger.hpp"
#include "../utils/ImageLoader.hpp"
#include "../utils/SimpleJsonParser.hpp"
#include "../utils/Utils.hpp"
#include "../input/CombinedInput.h"
#include "../input/VPADInput.h"
#include "../input/WPADInput.h"
#include <SDL2/SDL_image.h>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include <thread>
#include <algorithm>

// 静态成员定义
bool ManageScreen::sReturnedDueToEmpty = false;

ManageScreen::ManageScreen() {
    mTitleAnim.Start(0, 1, 500);
    mContentAnim.Start(0, 1, 600);
    
    // 重置返回标志
    sReturnedDueToEmpty = false;
    
    // 初始化 ImageLoader
    ImageLoader::Init();
    
    // 异步扫描本地主题
    std::thread([this]() {
        ScanLocalThemes();
        
        // 初始化动画
        InitAnimations();
        
        mIsLoading = false;
    }).detach();
}

ManageScreen::~ManageScreen() {
    FileLogger::GetInstance().LogInfo("ManageScreen destructor called");
    
    // 等待扫描线程完成(如果还在运行)
    // 注意: 由于使用了 detach(),无法直接 join,但可以设置标志让线程尽快退出
    if (mIsLoading) {
        FileLogger::GetInstance().LogInfo("Warning: ManageScreen destroyed while still loading themes");
    }
    
    // 释放纹理
    for (auto& theme : mThemes) {
        if (theme.collageThumbTexture) {
            SDL_DestroyTexture(theme.collageThumbTexture);
            theme.collageThumbTexture = nullptr;
        }
    }
    
    FileLogger::GetInstance().LogInfo("ManageScreen destructor completed");
}

void ManageScreen::InitAnimations() {
    mThemeAnims.clear();
    mThemeAnims.resize(mThemes.size());
    
    for (size_t i = 0; i < mThemes.size(); i++) {
        mThemeAnims[i].scaleAnim.SetImmediate(1.0f);
        mThemeAnims[i].highlightAnim.SetImmediate(0.0f);
    }
    
    // 选中第一个主题的动画
    if (mThemes.size() > 0) {
        mThemeAnims[0].scaleAnim.SetTarget(1.05f, 300);
        mThemeAnims[0].highlightAnim.SetTarget(1.0f, 300);
    }
}

void ManageScreen::UpdateAnimations() {
    for (auto& anim : mThemeAnims) {
        anim.scaleAnim.Update();
        anim.highlightAnim.Update();
    }
}

void ManageScreen::ScanLocalThemes() {
    mThemes.clear();
    
    const char* themesPath = "fs:/vol/external01/wiiu/themes";
    DIR* dir = opendir(themesPath);
    if (!dir) {
        FileLogger::GetInstance().LogError("Failed to open themes directory");
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR) continue;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        LocalTheme theme;
        theme.name = entry->d_name;
        theme.path = std::string(themesPath) + "/" + entry->d_name;
        
        // 加载元数据
        LoadThemeMetadata(theme);
        
        // 检查是否有修补完成的文件 (Men.pack 或 Men2.pack)
        std::string patchedPath = theme.path + "/patched/Common/Package";
        struct stat st;
        theme.hasPatched = false;
        
        if (stat(patchedPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            std::string menPath = patchedPath + "/Men.pack";
            std::string men2Path = patchedPath + "/Men2.pack";
            
            struct stat menSt, men2St;
            bool hasMen = (stat(menPath.c_str(), &menSt) == 0);
            bool hasMen2 = (stat(men2Path.c_str(), &men2St) == 0);
            
            theme.hasPatched = (hasMen || hasMen2);
        }
        
        // 统计 BPS 文件数量
        theme.bpsCount = 0;
        DIR* themeDir = opendir(theme.path.c_str());
        if (themeDir) {
            struct dirent* themeEntry;
            while ((themeEntry = readdir(themeDir)) != nullptr) {
                std::string filename = themeEntry->d_name;
                if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".bps") {
                    theme.bpsCount++;
                }
            }
            closedir(themeDir);
        }
        
        if (theme.bpsCount > 0) {
            mThemes.push_back(theme);
            FileLogger::GetInstance().LogInfo("Found theme: %s (%d BPS files)", theme.name.c_str(), theme.bpsCount);
        }
    }
    
    closedir(dir);
    FileLogger::GetInstance().LogInfo("Total local themes found: %d", (int)mThemes.size());
}

void ManageScreen::LoadThemeMetadata(LocalTheme& theme) {
    std::string metadataPath = theme.path + "/theme_info.json";
    FILE* fp = fopen(metadataPath.c_str(), "r");
    if (!fp) {
        // 如果没有元数据文件，使用默认值
        FileLogger::GetInstance().LogInfo("No metadata file for theme: %s, using defaults", theme.name.c_str());
        theme.author = "Unknown";
        theme.description = "";
        theme.downloads = 0;
        theme.likes = 0;
        theme.updatedAt = "";
        return;
    }
    
    // 读取文件内容
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(fileSize + 1);
    fread(buffer, 1, fileSize, fp);
    buffer[fileSize] = '\0';
    fclose(fp);
    
    FileLogger::GetInstance().LogInfo("Parsing metadata for: %s (size: %ld bytes)", theme.name.c_str(), fileSize);
    
    // 解析 JSON
    JsonValue root = SimpleJsonParser::Parse(buffer);
    
    if (root.has("id")) {
        theme.id = root["id"].asString();
        FileLogger::GetInstance().LogInfo("  ID: %s", theme.id.c_str());
    }
    if (root.has("author")) {
        theme.author = root["author"].asString();
        FileLogger::GetInstance().LogInfo("  Author: %s", theme.author.c_str());
    } else {
        theme.author = "Unknown";
    }
    if (root.has("description")) theme.description = root["description"].asString();
    if (root.has("downloads")) theme.downloads = root["downloads"].asInt();
    if (root.has("likes")) theme.likes = root["likes"].asInt();
    if (root.has("updatedAt")) theme.updatedAt = root["updatedAt"].asString();
    
    // 解析 tags 数组
    if (root.has("tags") && root["tags"].isArray()) {
        for (size_t i = 0; i < root["tags"].size(); i++) {
            if (root["tags"][i].isString()) {
                theme.tags.push_back(root["tags"][i].asString());
            }
        }
    }
    
    free(buffer);
    
    // 设置图片路径 - 从 images/ 子目录加载(新格式),如果不存在则尝试旧格式
    std::string imagesDir = theme.path + "/images";
    struct stat st;
    bool hasImagesDir = (stat(imagesDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    
    if (hasImagesDir) {
        // 新格式: images/ 子目录
        theme.collageThumbPath = imagesDir + "/collage_thumb.jpg";
        theme.collageHdPath = imagesDir + "/collage.jpg";
        theme.launcherThumbPath = imagesDir + "/launcher_thumb.jpg";
        theme.launcherHdPath = imagesDir + "/launcher.jpg";
        theme.warawaraThumbPath = imagesDir + "/warawara_thumb.jpg";
        theme.warawaraHdPath = imagesDir + "/warawara.jpg";
        FileLogger::GetInstance().LogInfo("Loading images from /images subdirectory");
    } else {
        // 旧格式: 直接在主题根目录
        theme.collageThumbPath = theme.path + "/collage_thumb.jpg";
        theme.collageHdPath = theme.path + "/collage.jpg";
        theme.launcherThumbPath = theme.path + "/launcher_thumb.jpg";
        theme.launcherHdPath = theme.path + "/launcher.jpg";
        theme.warawaraThumbPath = theme.path + "/warawara_thumb.jpg";
        theme.warawaraHdPath = theme.path + "/warawara.jpg";
        FileLogger::GetInstance().LogInfo("Loading images from theme root (legacy)");
    }
    
    FileLogger::GetInstance().LogInfo("Loaded metadata for theme: %s (by %s)", theme.name.c_str(), theme.author.c_str());
}

void ManageScreen::Draw() {
    mFrameCount++;
    
    // 更新图片加载器
    ImageLoader::Update();
    
    // 更新动画
    mTitleAnim.Update();
    mContentAnim.Update();
    UpdateAnimations();
    
    // 绘制渐变背景
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);
    
    DrawAnimatedTopBar(_("manage.title"), mTitleAnim, 0xf07c);
    
    // 如果正在加载
    if (mIsLoading) {
        const int cardW = 700;
        const int cardH = 350;
        const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
        const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
        
        // 现代化卡片样式
        SDL_Color shadowColor = Gfx::COLOR_SHADOW;
        shadowColor.a = 100;
        Gfx::DrawRectRounded(cardX + 8, cardY + 8, cardW, cardH, 24, shadowColor);
        Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 24, Gfx::COLOR_CARD_BG);
        
        // 旋转加载图标
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(cardX + cardW/2, cardY + 120, 70, Gfx::COLOR_ACCENT, 0xf110, Gfx::ALIGN_CENTER, angle);
        
        Gfx::Print(cardX + cardW/2, cardY + 220, 40, Gfx::COLOR_TEXT, _("manage.loading"), Gfx::ALIGN_CENTER);
        Gfx::Print(cardX + cardW/2, cardY + 275, 30, Gfx::COLOR_ALT_TEXT, _("manage.loading_desc"), Gfx::ALIGN_CENTER);
    }
    // 如果没有主题
    else if (mThemes.empty()) {
        const int cardW = 900;
        const int cardH = 450;
        const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
        const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
        
        // 现代化空状态卡片
        SDL_Color shadowColor = Gfx::COLOR_SHADOW;
        shadowColor.a = 100;
        Gfx::DrawRectRounded(cardX + 8, cardY + 8, cardW, cardH, 24, shadowColor);
        Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 24, Gfx::COLOR_CARD_BG);
        
        // 大图标
        Gfx::DrawIcon(cardX + cardW/2, cardY + 130, 90, Gfx::COLOR_ICON, 0xf07c, Gfx::ALIGN_CENTER);
        
        // 主标题
        Gfx::Print(cardX + cardW/2, cardY + 250, 52, Gfx::COLOR_TEXT, _("manage.no_themes"), Gfx::ALIGN_CENTER);
        
        // 副标题
        Gfx::Print(cardX + cardW/2, cardY + 320, 36, Gfx::COLOR_ALT_TEXT, _("manage.download_first"), Gfx::ALIGN_CENTER);
        
        // 提示：图标在左边，文字在右边 (向左移动80像素)
        int hintY = cardY + 380;
        int hintOffset = -80;  // 整体向左偏移
        Gfx::DrawIcon(cardX + cardW/2 - 80 + hintOffset, hintY, 24, Gfx::COLOR_ACCENT, 0xf019, Gfx::ALIGN_CENTER);
        Gfx::Print(cardX + cardW/2 - 50 + hintOffset, hintY, 28, Gfx::COLOR_ACCENT, _("manage.go_download"), Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
    }
    // 显示主题网格
    else {
        DrawThemeList();
        
        // 显示主题计数和选择信息
        if (!mThemes.empty()) {
            char infoText[128];
            snprintf(infoText, sizeof(infoText), "%d / %zu", mSelectedIndex + 1, mThemes.size());
            Gfx::Print(Gfx::SCREEN_WIDTH - 100, Gfx::SCREEN_HEIGHT - 140, 28, Gfx::COLOR_ALT_TEXT, 
                      infoText, Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);
        }
    }
    
    // 底部提示 - 只保留查看详情和退出
    std::string bottomHint = "\ue000 " + std::string(_("manage.view_details"));
    
    DrawBottomBar(bottomHint.c_str(), 
                 (std::string("\ue044 ") + _("input.exit")).c_str(), 
                 (std::string("\ue001 ") + _("input.back")).c_str());
}

void ManageScreen::DrawThemeList() {
    if (mThemes.empty()) {
        return;
    }
    
    // 绘制主题列表 - 和 DownloadScreen 一样
    const int listX = LIST_X;
    const int listY = LIST_Y;
    const int cardW = CARD_WIDTH;
    const int cardH = CARD_HEIGHT;
    const int cardSpacing = CARD_SPACING;
    const int visibleCount = VISIBLE_COUNT;
    
    int currentY = listY;
    int endIndex = std::min(mScrollOffset + visibleCount, (int)mThemes.size());
    
    for (int i = mScrollOffset; i < endIndex; i++) {
        bool selected = (i == mSelectedIndex);
        DrawThemeCard(mThemes[i], listX, currentY, cardW, cardH, selected, i);
        currentY += cardH + cardSpacing;
    }
    
    // 绘制滚动指示器
    if (mThemes.size() > visibleCount) {
        char scrollInfo[32];
        snprintf(scrollInfo, sizeof(scrollInfo), "%d / %zu", mSelectedIndex + 1, mThemes.size());
        Gfx::Print(Gfx::SCREEN_WIDTH - 100, Gfx::SCREEN_HEIGHT - 150, 32, Gfx::COLOR_ALT_TEXT, 
                  scrollInfo, Gfx::ALIGN_VERTICAL | Gfx::ALIGN_RIGHT);
    }
}

void ManageScreen::DrawThemeCard(LocalTheme& theme, int x, int y, int w, int h, bool selected, int themeIndex) {
    // 获取动画值
    float scale = 1.0f;
    float highlight = 0.0f;
    if (themeIndex >= 0 && themeIndex < (int)mThemeAnims.size()) {
        scale = mThemeAnims[themeIndex].scaleAnim.GetValue();
        highlight = mThemeAnims[themeIndex].highlightAnim.GetValue();
    }
    
    // 应用缩放
    int scaledW = (int)(w * scale);
    int scaledH = (int)(h * scale);
    int offsetX = (w - scaledW) / 2;
    int offsetY = (h - scaledH) / 2;
    
    x += offsetX;
    y += offsetY;
    w = scaledW;
    h = scaledH;
    
    // 绘制阴影
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = (uint8_t)(selected ? 120 : 60);
    Gfx::DrawRectRounded(x + 6, y + 6, w, h, 16, shadowColor);
    
    // 绘制发光效果
    if (highlight > 0.01f) {
        SDL_Color glowColor = Gfx::COLOR_ACCENT;
        glowColor.a = (uint8_t)(100 * highlight);
        Gfx::DrawRectRounded(x - 4, y - 4, w + 8, h + 8, 20, glowColor);
    }
    
    // 绘制卡片背景
    SDL_Color bgColor = selected ? Gfx::COLOR_CARD_HOVER : Gfx::COLOR_CARD_BG;
    Gfx::DrawRectRounded(x, y, w, h, 16, bgColor);
    
    // 绘制边框(如果选中)
    if (selected) {
        SDL_Color borderColor = Gfx::COLOR_ACCENT;
        borderColor.a = (uint8_t)(150 + 100 * highlight);
        Gfx::DrawRectRoundedOutline(x, y, w, h, 16, 3, borderColor);
    }
    
    // 左侧缩略图区域 - 16:9 比例
    const int thumbH = h - 40;
    const int thumbW = (int)(thumbH * 16.0f / 9.0f);
    const int thumbX = x + 20;
    const int thumbY = y + 20;
    
    // 绘制缩略图 - 使用 ImageLoader 异步加载 webp
    if (theme.collageThumbTexture) {
        // 已加载,绘制纹理
        SDL_Rect dstRect = {thumbX, thumbY, thumbW, thumbH};
        
        // 获取纹理尺寸
        int texW, texH;
        SDL_QueryTexture(theme.collageThumbTexture, nullptr, nullptr, &texW, &texH);
        
        // 计算缩放以保持纵横比
        float imgScale = std::min((float)thumbW / texW, (float)thumbH / texH);
        int scaledImgW = (int)(texW * imgScale);
        int scaledImgH = (int)(texH * imgScale);
        
        // 居中显示
        dstRect.x = thumbX + (thumbW - scaledImgW) / 2;
        dstRect.y = thumbY + (thumbH - scaledImgH) / 2;
        dstRect.w = scaledImgW;
        dstRect.h = scaledImgH;
        
        // 背景
        Gfx::DrawRectFilled(thumbX, thumbY, thumbW, thumbH, Gfx::COLOR_ALT_BACKGROUND);
        
        // 绘制纹理
        SDL_RenderCopy(Gfx::GetRenderer(), theme.collageThumbTexture, nullptr, &dstRect);
        
    } else if (!theme.collageThumbPath.empty() && !theme.collageThumbLoaded) {
        // 还未加载,显示占位符并异步加载
        Gfx::DrawRectFilled(thumbX, thumbY, thumbW, thumbH, Gfx::COLOR_ALT_BACKGROUND);
        
        // 加载动画 (旋转圈)
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(thumbX + thumbW/2, thumbY + thumbH/2 - 15, 40, Gfx::COLOR_ICON, 0xf1ce, Gfx::ALIGN_CENTER, angle);
        
        // 加载文本
        Gfx::Print(thumbX + thumbW/2, thumbY + thumbH/2 + 30, 24, Gfx::COLOR_ALT_TEXT, 
                  _("download.loading_image"), Gfx::ALIGN_CENTER);
        
        // 标记为正在加载
        theme.collageThumbLoaded = true;
        
        // 异步加载 webp 文件 - 直接使用本地路径
        ImageLoader::LoadRequest request;
        request.url = theme.collageThumbPath;  // 本地文件路径
        request.highPriority = selected;
        request.callback = [this, themeIndex](SDL_Texture* texture) {
            if (themeIndex >= 0 && themeIndex < (int)mThemes.size()) {
                mThemes[themeIndex].collageThumbTexture = texture;
                if (texture) {
                    FileLogger::GetInstance().LogInfo("Loaded webp image for theme %d: %s", 
                        themeIndex, mThemes[themeIndex].name.c_str());
                } else {
                    FileLogger::GetInstance().LogError("Failed to load webp image for theme %d", themeIndex);
                }
            }
        };
        ImageLoader::LoadAsync(request);
        
    } else {
        // 没有缩略图,显示默认图标
        Gfx::DrawRectRounded(thumbX, thumbY, thumbW, thumbH, 12, Gfx::COLOR_ALT_BACKGROUND);
        Gfx::DrawIcon(thumbX + thumbW/2, thumbY + thumbH/2, 50, Gfx::COLOR_ICON, 0xf03e, Gfx::ALIGN_CENTER);
    }
    
    // 主题信息区域
    const int infoX = thumbX + thumbW + 30;
    const int infoY = y + 30;
    
    // 主题名称 - 清理特殊字符用于显示
    std::string displayName = Utils::SanitizeThemeNameForDisplay(theme.name);
    if (displayName.length() > 45) {
        displayName = displayName.substr(0, 42) + "...";
    }
    Gfx::Print(infoX, infoY, 38, Gfx::COLOR_TEXT, displayName.c_str(), Gfx::ALIGN_VERTICAL);
    
    // 作者
    int currentInfoY = infoY + 48;
    Gfx::DrawIcon(infoX, currentInfoY, 20, Gfx::COLOR_ALT_TEXT, 0xf007, Gfx::ALIGN_VERTICAL);
    std::string authorText = theme.author.empty() ? "Unknown" : theme.author;
    if (authorText.length() > 35) {
        authorText = authorText.substr(0, 32) + "...";
    }
    Gfx::Print(infoX + 28, currentInfoY, 28, Gfx::COLOR_ALT_TEXT, authorText.c_str(), Gfx::ALIGN_VERTICAL);
    
    // 统计信息
    currentInfoY += 40;
    if (theme.downloads > 0) {
        char statsText[64];
        Gfx::DrawIcon(infoX, currentInfoY, 18, Gfx::COLOR_ALT_TEXT, 0xf019, Gfx::ALIGN_VERTICAL);
        snprintf(statsText, sizeof(statsText), "%d", theme.downloads);
        Gfx::Print(infoX + 25, currentInfoY, 24, Gfx::COLOR_ALT_TEXT, statsText, Gfx::ALIGN_VERTICAL);
        
        if (theme.likes > 0) {
            Gfx::DrawIcon(infoX + 120, currentInfoY, 18, Gfx::COLOR_ALT_TEXT, 0xf004, Gfx::ALIGN_VERTICAL);
            snprintf(statsText, sizeof(statsText), "%d", theme.likes);
            Gfx::Print(infoX + 145, currentInfoY, 24, Gfx::COLOR_ALT_TEXT, statsText, Gfx::ALIGN_VERTICAL);
        }
    }
    
    // 状态标签（右上角,与 DownloadScreen 样式一致）
    if (theme.hasPatched) {
        // "已安装"标签 - 右上角显示
        const int badgeW = 140;
        const int badgeH = 45;
        const int badgeX = x + w - badgeW - 20;
        const int badgeY = y + 20;
        
        // 背景 - 纯绿色
        SDL_Color badgeBg = Gfx::COLOR_SUCCESS;
        badgeBg.a = 220;
        Gfx::DrawRectRounded(badgeX, badgeY, badgeW, badgeH, 8, badgeBg);
        
        // 图标 - 勾选标记
        Gfx::DrawIcon(badgeX + 15, badgeY + badgeH/2, 28, Gfx::COLOR_WHITE, 0xf00c, Gfx::ALIGN_VERTICAL);
        
        // 文字
        Gfx::Print(badgeX + 50, badgeY + badgeH/2, 28, Gfx::COLOR_WHITE, 
                  _("manage.installed"), Gfx::ALIGN_VERTICAL);
    } else if (theme.bpsCount > 0) {
        // "就绪"标签 - 右上角显示
        const int badgeW = 140;
        const int badgeH = 45;
        const int badgeX = x + w - badgeW - 20;
        const int badgeY = y + 20;
        
        // 背景 - 橙色
        SDL_Color badgeBg = Gfx::COLOR_WARNING;
        badgeBg.a = 220;
        Gfx::DrawRectRounded(badgeX, badgeY, badgeW, badgeH, 8, badgeBg);
        
        // 图标 - 下载标记
        Gfx::DrawIcon(badgeX + 15, badgeY + badgeH/2, 28, Gfx::COLOR_WHITE, 0xf019, Gfx::ALIGN_VERTICAL);
        
        // 文字
        Gfx::Print(badgeX + 50, badgeY + badgeH/2, 28, Gfx::COLOR_WHITE, 
                  _("manage.ready"), Gfx::ALIGN_VERTICAL);
    }
}

bool ManageScreen::Update(Input &input) {
    // 如果正在加载，不处理输入
    if (mIsLoading) {
        return true;
    }
    
    // 更新图片加载器
    ImageLoader::Update();
    
    // 如果没有主题,按 A 返回主菜单(让用户选择下载)
    if (mThemes.empty()) {
        if (input.data.buttons_d & Input::BUTTON_A) {
            FileLogger::GetInstance().LogInfo("No themes, returning to menu with download hint");
            sReturnedDueToEmpty = true;  // 设置标志
            return false;
        }
        
        // 按 B 也返回主菜单
        if (input.data.buttons_d & Input::BUTTON_B) {
            return false;
        }
        
        return true;
    }
    
    // 保存旧的选择
    int prevSelected = mSelectedIndex;
    
    // 处理列表导航 - 和 DownloadScreen 一样
    if (!mThemes.empty()) {
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
        
        // 上下选择(支持循环)
        const int themeCount = (int)mThemes.size();
        if (shouldMoveUp) {
            if (mSelectedIndex > 0) {
                mSelectedIndex--;
            } else {
                // 循环到底部
                mSelectedIndex = themeCount - 1;
                mScrollOffset = std::max(0, themeCount - VISIBLE_COUNT);
            }
            // 调整滚动
            if (mSelectedIndex < mScrollOffset) {
                mScrollOffset = mSelectedIndex;
            }
        } else if (shouldMoveDown) {
            if (mSelectedIndex < themeCount - 1) {
                mSelectedIndex++;
            } else {
                // 循环到顶部
                mSelectedIndex = 0;
                mScrollOffset = 0;
            }
            // 调整滚动 (显示3个)
            if (mSelectedIndex >= mScrollOffset + VISIBLE_COUNT) {
                mScrollOffset = mSelectedIndex - VISIBLE_COUNT + 1;
            }
        }
        
        // 如果选择改变，更新动画
        if (prevSelected != mSelectedIndex) {
            // 重置旧的选择
            if (prevSelected >= 0 && prevSelected < (int)mThemeAnims.size()) {
                mThemeAnims[prevSelected].scaleAnim.SetTarget(1.0f, 300);
                mThemeAnims[prevSelected].highlightAnim.SetTarget(0.0f, 300);
            }
            
            // 高亮新的选择
            if (mSelectedIndex >= 0 && mSelectedIndex < (int)mThemeAnims.size()) {
                mThemeAnims[mSelectedIndex].scaleAnim.SetTarget(1.05f, 300);
                mThemeAnims[mSelectedIndex].highlightAnim.SetTarget(1.0f, 300);
            }
        }
        
        // 处理触摸输入
        if (input.data.touched && input.data.validPointer && !input.lastData.touched) {
            // 转换触摸坐标
            float scaleX = 1920.0f / 1280.0f;
            float scaleY = 1080.0f / 720.0f;
            int touchX = (int)((input.data.x * scaleX) + 960);
            int touchY = (int)(540 - (input.data.y * scaleY));
            
            FileLogger::GetInstance().LogInfo("Touch at (%d, %d)", touchX, touchY);
            
            // 检查是否点击了某个主题卡片
            const int cardX = 100;
            const int cardY = 170;
            const int cardW = 1720;
            const int cardH = 200;
            const int cardSpacing = 15;
            
            for (int i = 0; i < VISIBLE_COUNT && (mScrollOffset + i) < (int)mThemes.size(); i++) {
                int y = cardY + i * (cardH + cardSpacing);
                
                if (touchX >= cardX && touchX <= (cardX + cardW) &&
                    touchY >= y && touchY <= (y + cardH)) {
                    // 点击了这个主题
                    int clickedIndex = mScrollOffset + i;
                    
                    if (clickedIndex != mSelectedIndex) {
                        // 先更新选择
                        int prevSel = mSelectedIndex;
                        mSelectedIndex = clickedIndex;
                        
                        // 更新动画
                        if (prevSel >= 0 && prevSel < (int)mThemeAnims.size()) {
                            mThemeAnims[prevSel].scaleAnim.SetTarget(1.0f, 300);
                            mThemeAnims[prevSel].highlightAnim.SetTarget(0.0f, 300);
                        }
                        if (mSelectedIndex >= 0 && mSelectedIndex < (int)mThemeAnims.size()) {
                            mThemeAnims[mSelectedIndex].scaleAnim.SetTarget(1.05f, 300);
                            mThemeAnims[mSelectedIndex].highlightAnim.SetTarget(1.0f, 300);
                        }
                        
                        FileLogger::GetInstance().LogInfo("Theme selected by touch: %d", mSelectedIndex);
                    } else {
                        // 双击效果: 如果已经选中,直接进入详情
                        FileLogger::GetInstance().LogInfo("Double-tap detected, opening details");
                        // 触发 A 按钮效果
                        input.data.buttons_d |= Input::BUTTON_A;
                    }
                    break;
                }
            }
        }
        
        // 按 A 进入详情
        if (input.data.buttons_d & Input::BUTTON_A) {
            if (mSelectedIndex >= 0 && mSelectedIndex < (int)mThemes.size()) {
                LocalTheme& localTheme = mThemes[mSelectedIndex];
                FileLogger::GetInstance().LogInfo("Opening details for theme: %s", localTheme.name.c_str());
                
                // 将 LocalTheme 转换为 Theme 结构
                Theme theme;
                theme.id = localTheme.id;
                theme.name = localTheme.name;
                theme.author = localTheme.author;
                theme.description = localTheme.description;
                theme.downloads = localTheme.downloads;
                theme.likes = localTheme.likes;
                theme.updatedAt = localTheme.updatedAt;
                theme.tags = localTheme.tags;
                
                // 设置图片 URL - 直接使用本地路径,不添加 file:// 前缀
                theme.collagePreview.thumbUrl = localTheme.collageThumbPath;
                theme.collagePreview.hdUrl = localTheme.collageHdPath;
                theme.launcherScreenshot.thumbUrl = localTheme.launcherThumbPath;
                theme.launcherScreenshot.hdUrl = localTheme.launcherHdPath;
                theme.waraWaraScreenshot.thumbUrl = localTheme.warawaraThumbPath;
                theme.waraWaraScreenshot.hdUrl = localTheme.warawaraHdPath;
                
                // 如果已经加载了缩略图,直接设置纹理
                if (localTheme.collageThumbTexture) {
                    theme.collagePreview.thumbTexture = localTheme.collageThumbTexture;
                    theme.collagePreview.thumbLoaded = true;
                }
                
                // 创建详情屏幕 - 传入 nullptr 作为 ThemeManager (本地模式)
                ThemeDetailScreen* detailScreen = new ThemeDetailScreen(&theme, nullptr);
                
                // 创建输入对象
                CombinedInput detailBaseInput;
                VPadInput detailVpadInput;
                WPADInput detailWpadInputs[4] = {WPAD_CHAN_0, WPAD_CHAN_1, WPAD_CHAN_2, WPAD_CHAN_3};
                
                // 进入详情屏幕循环
                while (true) {
                    detailBaseInput.reset();
                    if (detailVpadInput.update(1280, 720)) {
                        detailBaseInput.combine(detailVpadInput);
                    }
                    for (auto &wpadInput : detailWpadInputs) {
                        if (wpadInput.update(1280, 720)) {
                            detailBaseInput.combine(wpadInput);
                        }
                    }
                    detailBaseInput.process();
                    
                    if (!detailScreen->Update(detailBaseInput)) {
                        break; // 返回管理列表
                    }
                    
                    detailScreen->Draw();
                    Gfx::Render();
                }
                
                // 清理详情屏幕
                delete detailScreen;
                
                FileLogger::GetInstance().LogInfo("Returned from local theme detail screen");
                
                // 返回后立即跳过当前帧处理
                return true;
            }
        }
    }
    
    // 按 B 返回
    if (input.data.buttons_d & Input::BUTTON_B) {
        return false;
    }
    
    return true;
}
