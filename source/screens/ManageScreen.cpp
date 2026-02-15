#include "ManageScreen.hpp"
#include "ThemeDetailScreen.hpp"
#include "DownloadScreen.hpp"
#include "LocalInstallScreen.hpp"
#include "Gfx.hpp"
#include "../utils/Config.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/FileLogger.hpp"
#include "../utils/ImageLoader.hpp"
#include "../utils/ThemePatcher.hpp"
#include "../utils/Utils.hpp"
#include "../utils/SwkbdManager.hpp"
#include "rapidjson/document.h"
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
    FileLogger::GetInstance().LogInfo("ManageScreen: Initializing...");
    
    mTitleAnim.Start(0, 1, 500);
    mContentAnim.Start(0, 1, 600);
    
    // 重置返回标志
    sReturnedDueToEmpty = false;
    
    // 获取当前主题名称（用于UI指示器）
    ThemePatcher patcher;
    mCurrentThemeName = patcher.GetCurrentTheme();
    FileLogger::GetInstance().LogInfo("Current theme in StyleMiiU: %s", 
        mCurrentThemeName.empty() ? "(none)" : mCurrentThemeName.c_str());
    
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
        std::string patchedPath = theme.path + "/content/Common/Package";
        struct stat st;
        theme.hasPatched = false;
        
        FileLogger::GetInstance().LogInfo("Checking patched status for: %s", theme.name.c_str());
        FileLogger::GetInstance().LogInfo("Patched path: %s", patchedPath.c_str());
        
        if (stat(patchedPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            std::string menPath = patchedPath + "/Men.pack";
            std::string men2Path = patchedPath + "/Men2.pack";
            
            struct stat menSt, men2St;
            bool hasMen = (stat(menPath.c_str(), &menSt) == 0);
            bool hasMen2 = (stat(men2Path.c_str(), &men2St) == 0);
            
            FileLogger::GetInstance().LogInfo("Patch check - Men.pack: %d, Men2.pack: %d", hasMen ? 1 : 0, hasMen2 ? 1 : 0);
            
            theme.hasPatched = (hasMen || hasMen2);
        } else {
            FileLogger::GetInstance().LogInfo("Patched directory not found or not accessible");
        }
        
        FileLogger::GetInstance().LogInfo("Theme %s hasPatched = %d", theme.name.c_str(), theme.hasPatched ? 1 : 0);
        
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
    
    // 使用 rapidjson 解析
    try {
        rapidjson::Document root;
        root.Parse(buffer);
        
        if (root.HasParseError()) {
            FileLogger::GetInstance().LogError("JSON parsing failed for %s: parse error", theme.name.c_str());
        } else {
            if (root.HasMember("id") && root["id"].IsString()) {
                theme.id = root["id"].GetString();
                FileLogger::GetInstance().LogInfo("  ID: %s", theme.id.c_str());
            }
            if (root.HasMember("shortId") && root["shortId"].IsString()) {
                theme.shortId = root["shortId"].GetString();
                FileLogger::GetInstance().LogInfo("  Short ID: %s", theme.shortId.c_str());
            }
            if (root.HasMember("author") && root["author"].IsString()) {
                theme.author = root["author"].GetString();
                FileLogger::GetInstance().LogInfo("  Author: %s", theme.author.c_str());
            } else {
                theme.author = "Unknown";
            }
            if (root.HasMember("description") && root["description"].IsString()) 
                theme.description = root["description"].GetString();
            if (root.HasMember("downloads") && root["downloads"].IsInt()) 
                theme.downloads = root["downloads"].GetInt();
            if (root.HasMember("likes") && root["likes"].IsInt()) 
                theme.likes = root["likes"].GetInt();
            if (root.HasMember("updatedAt") && root["updatedAt"].IsString()) 
                theme.updatedAt = root["updatedAt"].GetString();
            
            // 解析 tags 数组
            if (root.HasMember("tags") && root["tags"].IsArray()) {
                const auto& tags = root["tags"];
                for (rapidjson::SizeType i = 0; i < tags.Size(); i++) {
                    if (tags[i].IsString()) {
                        theme.tags.push_back(tags[i].GetString());
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        FileLogger::GetInstance().LogError("JSON parsing exception for %s: %s", theme.name.c_str(), e.what());
    }
    
    free(buffer);
    
    // 设置图片路径 - 从 images/ 子目录加载(新格式),如果不存在则尝试旧格式
    // 支持多种图片格式: .webp, .jpg, .jpeg, .png
    std::string imagesDir = theme.path + "/images";
    struct stat st;
    bool hasImagesDir = (stat(imagesDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    
    // 辅助函数: 查找存在的图片文件(尝试多种扩展名)
    auto findImage = [](const std::string& basePath) -> std::string {
        const char* extensions[] = {".webp", ".jpg", ".jpeg", ".png"};
        struct stat st;
        for (const char* ext : extensions) {
            std::string path = basePath + ext;
            if (stat(path.c_str(), &st) == 0) {
                FileLogger::GetInstance().LogInfo("Found image: %s", path.c_str());
                return path;
            }
        }
        FileLogger::GetInstance().LogWarning("No image found for: %s (tried .webp, .jpg, .jpeg, .png)", basePath.c_str());
        return basePath + ".jpg"; // 默认返回 .jpg (兼容旧逻辑)
    };
    
    if (hasImagesDir) {
        // 新格式: images/ 子目录
        theme.collageThumbPath = findImage(imagesDir + "/collage_thumb");
        theme.collageHdPath = findImage(imagesDir + "/collage");
        theme.launcherThumbPath = findImage(imagesDir + "/launcher_thumb");
        theme.launcherHdPath = findImage(imagesDir + "/launcher");
        theme.warawaraThumbPath = findImage(imagesDir + "/warawara_thumb");
        theme.warawaraHdPath = findImage(imagesDir + "/warawara");
        FileLogger::GetInstance().LogInfo("Loading images from /images subdirectory");
    } else {
        // 旧格式: 直接在主题根目录
        theme.collageThumbPath = findImage(theme.path + "/collage_thumb");
        theme.collageHdPath = findImage(theme.path + "/collage");
        theme.launcherThumbPath = findImage(theme.path + "/launcher_thumb");
        theme.launcherHdPath = findImage(theme.path + "/launcher");
        theme.warawaraThumbPath = findImage(theme.path + "/warawara_thumb");
        theme.warawaraHdPath = findImage(theme.path + "/warawara");
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
        DrawSearchBox();  // 绘制搜索框
        DrawThemeList();
        
        // 显示主题计数和选择信息
        if (!mThemes.empty()) {
            size_t displayCount = mSearchActive ? mFilteredIndices.size() : mThemes.size();
            if (displayCount > 0) {
                char infoText[128];
                snprintf(infoText, sizeof(infoText), "%d / %zu", mSelectedIndex + 1, displayCount);
                Gfx::Print(Gfx::SCREEN_WIDTH - 100, Gfx::SCREEN_HEIGHT - 140, 28, Gfx::COLOR_ALT_TEXT, 
                          infoText, Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);
            }
        }
    }
    
    // 底部提示 - 添加本地安装和快速设置选项
    std::string bottomHint = "\ue000 " + std::string(_("manage.view_details")) + 
                             "  |  \ue002 " + std::string(_("manage.install_local")) +
                             "  |  \ue003 " + std::string(_("manage.set_current"));
    
    DrawBottomBar(bottomHint.c_str(), 
                 (std::string("\ue044 ") + _("input.exit")).c_str(), 
                 (std::string("\ue001 ") + _("input.back")).c_str());
    
    // 绘制圆形返回按钮
    Screen::DrawBackButton();
    
    // 更新和绘制通知
    mNotification.Update();
    mNotification.Draw();
}

void ManageScreen::DrawThemeList() {
    if (mThemes.empty()) {
        return;
    }
    
    // 使用过滤后的主题列表
    size_t displayCount = mSearchActive ? mFilteredIndices.size() : mThemes.size();
    
    // 如果搜索结果为空
    if (mSearchActive && mFilteredIndices.empty()) {
        const int cardW = 800;
        const int cardH = 300;
        const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
        const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
        
        SDL_Color shadowColor = Gfx::COLOR_SHADOW;
        shadowColor.a = 80;
        Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
        Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
        
        Gfx::DrawIcon(cardX + cardW/2, cardY + 100, 70, Gfx::COLOR_WARNING, 0xf002, Gfx::ALIGN_CENTER);
        Gfx::Print(cardX + cardW/2, cardY + 190, 44, Gfx::COLOR_TEXT, "No matching themes", Gfx::ALIGN_CENTER);
        return;
    }
    
    // 绘制主题列表 - 和 DownloadScreen 一样
    const int listX = LIST_X;
    const int listY = 240;  // 向下移动以留出搜索框空间
    const int cardW = CARD_WIDTH;
    const int cardH = CARD_HEIGHT;
    const int cardSpacing = CARD_SPACING;
    const int visibleCount = VISIBLE_COUNT;
    
    int currentY = listY;
    int endIndex = std::min(mScrollOffset + visibleCount, (int)displayCount);
    
    for (int i = mScrollOffset; i < endIndex; i++) {
        bool selected = (i == mSelectedIndex);
        // 获取实际主题索引
        size_t realIndex = mSearchActive ? mFilteredIndices[i] : i;
        DrawThemeCard(mThemes[realIndex], listX, currentY, cardW, cardH, selected, realIndex);
        currentY += cardH + cardSpacing;
    }
    
    // 绘制滚动指示器
    if (displayCount > visibleCount) {
        char scrollInfo[32];
        snprintf(scrollInfo, sizeof(scrollInfo), "%d / %zu", mSelectedIndex + 1, displayCount);
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
                if (texture) {
                    // 加载成功
                    mThemes[themeIndex].collageThumbTexture = texture;
                    FileLogger::GetInstance().LogInfo("Loaded webp image for theme %d: %s", 
                        themeIndex, mThemes[themeIndex].name.c_str());
                } else {
                    // 加载失败,检查重试次数
                    mThemes[themeIndex].collageThumbRetryCount++;
                    
                    if (mThemes[themeIndex].collageThumbRetryCount < 3) {
                        // 重试 (最多3次)
                        FileLogger::GetInstance().LogWarning("Failed to load webp image for theme %d, retry %d/3", 
                            themeIndex, mThemes[themeIndex].collageThumbRetryCount);
                        
                        // 重置加载标志以触发重新加载
                        mThemes[themeIndex].collageThumbLoaded = false;
                    } else {
                        // 重试次数已用尽,停止加载
                        FileLogger::GetInstance().LogError("Failed to load webp image for theme %d after 3 retries, giving up", 
                            themeIndex);
                    }
                }
            }
        };
        ImageLoader::LoadAsync(request);
        
    } else if (!theme.collageThumbPath.empty() && theme.collageThumbLoaded && 
               !theme.collageThumbTexture && theme.collageThumbRetryCount >= 3) {
        // 加载失败且已达到最大重试次数,显示错误图标
        Gfx::DrawRectRounded(thumbX, thumbY, thumbW, thumbH, 12, Gfx::COLOR_ALT_BACKGROUND);
        Gfx::DrawIcon(thumbX + thumbW/2, thumbY + thumbH/2, 50, Gfx::COLOR_ERROR, 0xf071, Gfx::ALIGN_CENTER); // warning icon
        
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
    // 首先检查是否是当前主题
    bool isCurrentTheme = !mCurrentThemeName.empty() && (theme.name == mCurrentThemeName);
    
    // 调试日志 - 只在选中的卡片上输出
    if (selected && mFrameCount % 60 == 0) {  // 每秒输出一次
        FileLogger::GetInstance().LogInfo("[DrawThemeCard] Checking current theme:");
        FileLogger::GetInstance().LogInfo("  theme.name: '%s'", theme.name.c_str());
        FileLogger::GetInstance().LogInfo("  mCurrentThemeName: '%s'", mCurrentThemeName.c_str());
        FileLogger::GetInstance().LogInfo("  isCurrentTheme: %d", isCurrentTheme ? 1 : 0);
    }
    
    if (isCurrentTheme) {
        // "当前"标签 - 最高优先级，蓝色背景
        const int badgeW = 140;
        const int badgeH = 45;
        const int badgeX = x + w - badgeW - 20;
        const int badgeY = y + 20;
        
        // 背景 - 蓝色（当前激活）
        SDL_Color badgeBg = Gfx::COLOR_ACCENT;
        badgeBg.a = 220;
        Gfx::DrawRectRounded(badgeX, badgeY, badgeW, badgeH, 8, badgeBg);
        
        // 图标 - 星星标记
        Gfx::DrawIcon(badgeX + 15, badgeY + badgeH/2, 28, Gfx::COLOR_WHITE, 0xf005, Gfx::ALIGN_VERTICAL);
        
        // 文字
        Gfx::Print(badgeX + 50, badgeY + badgeH/2, 28, Gfx::COLOR_WHITE, 
                  _("manage.current"), Gfx::ALIGN_VERTICAL);
    } else if (theme.hasPatched) {
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
    // 检测返回按钮点击
    if (Screen::UpdateBackButton(input)) {
        return false;  // 返回上一级
    }
    
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
        
        // 按 X 进入本地安装屏幕 (即使没有主题也可以安装)
        if (input.data.buttons_d & Input::BUTTON_X) {
            FileLogger::GetInstance().LogInfo("Opening LocalInstallScreen (empty theme list)");
            
            // 创建本地安装屏幕
            LocalInstallScreen* installScreen = new LocalInstallScreen();
            
            // 创建输入对象
            CombinedInput installBaseInput;
            VPadInput installVpadInput;
            WPADInput installWpadInputs[4] = {WPAD_CHAN_0, WPAD_CHAN_1, WPAD_CHAN_2, WPAD_CHAN_3};
            
            // 进入本地安装屏幕循环
            while (true) {
                installBaseInput.reset();
                if (installVpadInput.update(1280, 720)) {
                    installBaseInput.combine(installVpadInput);
                }
                for (auto &wpadInput : installWpadInputs) {
                    if (wpadInput.update(1280, 720)) {
                        installBaseInput.combine(wpadInput);
                    }
                }
                installBaseInput.process();
                
                if (!installScreen->Update(installBaseInput)) {
                    break; // 返回管理列表
                }
                
                installScreen->Draw();
                Gfx::Render();
            }
            
            // 清理本地安装屏幕
            delete installScreen;
            
            FileLogger::GetInstance().LogInfo("Returned from LocalInstallScreen");
            
            // 重新加载主题列表(可能刚刚安装了新主题)
            ScanLocalThemes();
            InitAnimations();
            
            return true;
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
        size_t displayCount = mSearchActive ? mFilteredIndices.size() : mThemes.size();
        const int themeCount = (int)displayCount;
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
            // 获取实际索引
            size_t prevRealIndex = mSearchActive ? (prevSelected >= 0 && prevSelected < (int)mFilteredIndices.size() ? mFilteredIndices[prevSelected] : 0) : prevSelected;
            size_t currentRealIndex = mSearchActive ? (mSelectedIndex >= 0 && mSelectedIndex < (int)mFilteredIndices.size() ? mFilteredIndices[mSelectedIndex] : 0) : mSelectedIndex;
            
            // 重置旧的选择
            if (prevSelected >= 0 && prevRealIndex < mThemeAnims.size()) {
                mThemeAnims[prevRealIndex].scaleAnim.SetTarget(1.0f, 300);
                mThemeAnims[prevRealIndex].highlightAnim.SetTarget(0.0f, 300);
            }
            
            // 高亮新的选择
            if (mSelectedIndex >= 0 && currentRealIndex < mThemeAnims.size()) {
                mThemeAnims[currentRealIndex].scaleAnim.SetTarget(1.05f, 300);
                mThemeAnims[currentRealIndex].highlightAnim.SetTarget(1.0f, 300);
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
            
            // 首先检查是否点击搜索框
            const int searchBoxX = 100;
            const int searchBoxY = 150;
            const int searchBoxW = 1520;
            const int searchBoxH = 70;
            
            // 如果有搜索文本，优先检查是否点击清除按钮
            if (!mSearchText.empty()) {
                const int clearBtnX = searchBoxX + searchBoxW - 200;
                const int clearBtnW = 200;
                const int clearBtnY = searchBoxY;
                const int clearBtnH = searchBoxH;
                if (IsTouchInRect(touchX, touchY, clearBtnX, clearBtnY, clearBtnW, clearBtnH)) {
                    // 点击了清除按钮
                    FileLogger::GetInstance().LogInfo("Clearing search filter");
                    mSearchText.clear();
                    mSearchActive = false;
                    mFilteredIndices.clear();
                    mSelectedIndex = 0;
                    mScrollOffset = 0;
                    return true;
                }
            }
            
            // 检查是否点击搜索框其他区域（打开键盘）
            if (IsTouchInRect(touchX, touchY, searchBoxX, searchBoxY, searchBoxW, searchBoxH)) {
                // 打开键盘
                ShowKeyboard();
                return true;
            }
            
            // 检查是否点击了某个主题卡片
            const int cardX = 100;
            const int cardY = 240;  // 因为搜索框占据了上面的空间，主题卡片Y位置向下移动
            const int cardW = 1720;
            const int cardH = 200;
            const int cardSpacing = 15;
            
            // 确定显示的主题数量
            size_t displayCount = mSearchActive ? mFilteredIndices.size() : mThemes.size();
            
            for (int i = 0; i < VISIBLE_COUNT && (mScrollOffset + i) < (int)displayCount; i++) {
                int y = cardY + i * (cardH + cardSpacing);
                
                if (touchX >= cardX && touchX <= (cardX + cardW) &&
                    touchY >= y && touchY <= (y + cardH)) {
                    // 点击了这个主题
                    int clickedIndex = mScrollOffset + i;
                    
                    if (clickedIndex != mSelectedIndex) {
                        // 先更新选择
                        int prevSel = mSelectedIndex;
                        mSelectedIndex = clickedIndex;
                        
                        // 更新动画（使用实际索引）
                        size_t realIndex = mSearchActive ? mFilteredIndices[mSelectedIndex] : mSelectedIndex;
                        size_t prevRealIndex = mSearchActive ? (prevSel < (int)mFilteredIndices.size() ? mFilteredIndices[prevSel] : 0) : prevSel;
                        
                        if (prevSel >= 0 && prevRealIndex < mThemeAnims.size()) {
                            mThemeAnims[prevRealIndex].scaleAnim.SetTarget(1.0f, 300);
                            mThemeAnims[prevRealIndex].highlightAnim.SetTarget(0.0f, 300);
                        }
                        if (realIndex < mThemeAnims.size()) {
                            mThemeAnims[realIndex].scaleAnim.SetTarget(1.05f, 300);
                            mThemeAnims[realIndex].highlightAnim.SetTarget(1.0f, 300);
                        }
                        
                        FileLogger::GetInstance().LogInfo("Theme selected by touch: %d (real: %zu)", mSelectedIndex, realIndex);
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
            size_t displayCount = mSearchActive ? mFilteredIndices.size() : mThemes.size();
            if (mSelectedIndex >= 0 && mSelectedIndex < (int)displayCount) {
                // 获取实际主题索引
                size_t realIndex = mSearchActive ? mFilteredIndices[mSelectedIndex] : mSelectedIndex;
                LocalTheme& localTheme = mThemes[realIndex];
                FileLogger::GetInstance().LogInfo("Opening details for theme: %s (display index: %d, real index: %zu)", 
                                                  localTheme.name.c_str(), mSelectedIndex, realIndex);
                
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
    
    // 按 Y 快速设置为当前主题
    if (input.data.buttons_d & Input::BUTTON_Y) {
        size_t displayCount = mSearchActive ? mFilteredIndices.size() : mThemes.size();
        if (mSelectedIndex >= 0 && mSelectedIndex < (int)displayCount) {
            // 获取实际主题索引
            size_t realIndex = mSearchActive ? mFilteredIndices[mSelectedIndex] : mSelectedIndex;
            LocalTheme& selectedTheme = mThemes[realIndex];
            
            FileLogger::GetInstance().LogInfo("[ManageScreen] Y pressed, setting current theme: %s", 
                                             selectedTheme.name.c_str());
            FileLogger::GetInstance().LogInfo("[ManageScreen] Theme ID: %s", 
                                             selectedTheme.id.empty() ? "(empty)" : selectedTheme.id.c_str());
            FileLogger::GetInstance().LogInfo("[ManageScreen] Theme hasPatched: %d", selectedTheme.hasPatched ? 1 : 0);
            
            // 检查主题是否已安装（hasPatched）
            if (!selectedTheme.hasPatched) {
                FileLogger::GetInstance().LogWarning("[ManageScreen] Theme not installed: %s", 
                                                     selectedTheme.name.c_str());
                mNotification.ShowWarning(_("manage.not_installed"));
                return true;
            }
            
            // 调用SetCurrentTheme
            ThemePatcher patcher;
            if (!selectedTheme.id.empty()) {
                bool success = patcher.SetCurrentTheme(selectedTheme.id);
                if (success) {
                    FileLogger::GetInstance().LogInfo("[ManageScreen] Successfully set current theme to: %s", 
                                                     selectedTheme.name.c_str());
                    
                    // 验证是否真的写入成功
                    std::string verifyTheme = patcher.GetCurrentTheme();
                    FileLogger::GetInstance().LogInfo("[ManageScreen] Verification: GetCurrentTheme() returned: '%s'", 
                                                     verifyTheme.c_str());
                    
                    // 更新当前主题名称
                    mCurrentThemeName = selectedTheme.name;
                    FileLogger::GetInstance().LogInfo("[ManageScreen] Updated mCurrentThemeName to: '%s'", 
                                                     mCurrentThemeName.c_str());
                    
                    // 标记主题已更改（用于退出时软重启）
                    Config::GetInstance().SetThemeChanged(true);
                    FileLogger::GetInstance().LogInfo("[ManageScreen] Marked theme as changed for soft reboot on exit");
                    
                    // 显示成功通知
                    std::string successMsg = std::string(_("manage.set_current_success")) + ": " + 
                                           Utils::SanitizeThemeNameForDisplay(selectedTheme.name);
                    mNotification.ShowInfo(successMsg);
                } else {
                    FileLogger::GetInstance().LogError("[ManageScreen] Failed to set current theme: %s", 
                                                      selectedTheme.name.c_str());
                    mNotification.ShowError(_("manage.set_current_failed"));
                }
            } else {
                FileLogger::GetInstance().LogError("[ManageScreen] Theme has no ID: %s", 
                                                  selectedTheme.name.c_str());
                mNotification.ShowError("Theme has no ID (missing theme_info.json)");
            }
        }
        return true;
    }
    
    // 按 B 返回
    if (input.data.buttons_d & Input::BUTTON_B) {
        return false;
    }
    
    // 按 X 进入本地安装屏幕
    if (input.data.buttons_d & Input::BUTTON_X) {
        FileLogger::GetInstance().LogInfo("Opening LocalInstallScreen");
        
        // 创建本地安装屏幕
        LocalInstallScreen* installScreen = new LocalInstallScreen();
        
        // 创建输入对象
        CombinedInput installBaseInput;
        VPadInput installVpadInput;
        WPADInput installWpadInputs[4] = {WPAD_CHAN_0, WPAD_CHAN_1, WPAD_CHAN_2, WPAD_CHAN_3};
        
        // 进入本地安装屏幕循环
        while (true) {
            installBaseInput.reset();
            if (installVpadInput.update(1280, 720)) {
                installBaseInput.combine(installVpadInput);
            }
            for (auto &wpadInput : installWpadInputs) {
                if (wpadInput.update(1280, 720)) {
                    installBaseInput.combine(wpadInput);
                }
            }
            installBaseInput.process();
            
            if (!installScreen->Update(installBaseInput)) {
                break; // 返回管理列表
            }
            
            installScreen->Draw();
            Gfx::Render();
        }
        
        // 清理本地安装屏幕
        delete installScreen;
        
        FileLogger::GetInstance().LogInfo("Returned from LocalInstallScreen");
        
        // 清理B键状态,避免立即返回到MenuScreen
        input.data.buttons_d &= ~Input::BUTTON_B;
        input.data.buttons_h &= ~Input::BUTTON_B;
        
        // 重新扫描主题列表(可能有新安装的主题)
        mIsLoading = true;
        mThemes.clear();
        std::thread([this]() {
            ScanLocalThemes();
            InitAnimations();
            mIsLoading = false;
        }).detach();
        
        return true;
    }
    
    return true;
}

// 搜索相关方法实现

void ManageScreen::DrawSearchBox() {
    const int boxX = 100;
    const int boxY = 150;
    const int boxW = 1520;  // 增加宽度
    const int boxH = 70;
    
    // 绘制搜索框背景
    SDL_Color bgColor = Gfx::COLOR_CARD_BG;
    Gfx::DrawRectRounded(boxX, boxY, boxW, boxH, 12, bgColor);
    
    // 绘制边框
    SDL_Color borderColor = mSearchActive ? Gfx::COLOR_ACCENT : Gfx::COLOR_ALT_TEXT;
    borderColor.a = mSearchActive ? 200 : 100;
    Gfx::DrawRectRoundedOutline(boxX, boxY, boxW, boxH, 12, 2, borderColor);
    
    // 绘制搜索图标
    Gfx::DrawIcon(boxX + 30, boxY + boxH/2, 32, Gfx::COLOR_ALT_TEXT, 0xf002, Gfx::ALIGN_VERTICAL);
    
    // 绘制搜索文本或提示
    if (mSearchText.empty()) {
        std::string hintText = std::string("") + _("download.search_hint");
        Gfx::Print(boxX + 80, boxY + boxH/2, 32, Gfx::COLOR_ALT_TEXT, 
                  hintText.c_str(), Gfx::ALIGN_VERTICAL);
    } else {
        Gfx::Print(boxX + 80, boxY + boxH/2, 32, Gfx::COLOR_TEXT, 
                  mSearchText.c_str(), Gfx::ALIGN_VERTICAL);
    }
    
    // 如果有搜索文本，显示清除按钮
    if (!mSearchText.empty()) {
        const int clearX = boxX + boxW - 200;
        const int clearY = boxY + boxH/2;
        SDL_Color clearColor = {160, 160, 160, 255}; // 灰色
        Gfx::DrawIcon(clearX, clearY, 28, clearColor, 0xf00d, Gfx::ALIGN_VERTICAL);
        Gfx::Print(clearX + 40, clearY, 28, clearColor, _("download.search_clear"), Gfx::ALIGN_VERTICAL);
    }
    
    // 显示搜索结果计数
    if (mSearchActive && !mSearchText.empty()) {
        char countText[64];
        snprintf(countText, sizeof(countText), "%zu %s", mFilteredIndices.size(), _("download.search_results").c_str());
        Gfx::Print(boxX + boxW + 30, boxY + boxH/2, 28, Gfx::COLOR_ALT_TEXT, countText, Gfx::ALIGN_VERTICAL);
    }
}

void ManageScreen::ShowKeyboard() {
    FileLogger::GetInstance().LogInfo("[ManageScreen::ShowKeyboard] Opening keyboard");
    
    std::string result;
    std::string hint = _("download.search_keyboard_hint");
    if (SwkbdManager::GetInstance().ShowKeyboard(result, hint, mSearchText, 128)) {
        // 用户按了确认
        if (!result.empty()) {
            mSearchText = result;
            ApplySearch();
            
            // 重置选择
            mSelectedIndex = 0;
            mScrollOffset = 0;
        }
    } else {
        // 用户取消
        FileLogger::GetInstance().LogInfo("[ManageScreen::ShowKeyboard] User cancelled");
    }
}

void ManageScreen::ApplySearch() {
    mFilteredIndices.clear();
    
    if (mSearchText.empty()) {
        mSearchActive = false;
        return;
    }
    
    mSearchActive = true;
    
    // 转换搜索文本为小写
    std::string searchLower = mSearchText;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
    
    // 检查是否是 T+ID 格式搜索
    bool isIdSearch = false;
    std::string searchId;
    if (searchLower.length() >= 2 && searchLower[0] == 't') {
        // 提取 T 后面的内容作为 ID
        searchId = searchLower.substr(1);
        isIdSearch = true;
        FileLogger::GetInstance().LogInfo("[ManageScreen::ApplySearch] ID search mode: T%s", searchId.c_str());
    }
    
    for (size_t i = 0; i < mThemes.size(); ++i) {
        const auto& theme = mThemes[i];
        
        // 如果是 ID 搜索模式（T+ID）
        if (isIdSearch && !theme.shortId.empty()) {
            std::string shortIdLower = theme.shortId;
            std::transform(shortIdLower.begin(), shortIdLower.end(), shortIdLower.begin(), ::tolower);
            
            // 完全匹配（例如 T1 只匹配 T1，不匹配 T123）
            if (shortIdLower == searchId) {
                FileLogger::GetInstance().LogInfo("[ManageScreen::ApplySearch] Matched ID: %s (theme: %s)", 
                                                  theme.shortId.c_str(), theme.name.c_str());
                mFilteredIndices.push_back(i);
                continue;
            }
        }
        
        // 搜索主题名称
        std::string nameLower = theme.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find(searchLower) != std::string::npos) {
            mFilteredIndices.push_back(i);
            continue;
        }
        
        // 搜索作者
        std::string authorLower = theme.author;
        std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(), ::tolower);
        if (authorLower.find(searchLower) != std::string::npos) {
            mFilteredIndices.push_back(i);
            continue;
        }
        
        // 搜索标签
        for (const auto& tag : theme.tags) {
            std::string tagLower = tag;
            std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), ::tolower);
            if (tagLower.find(searchLower) != std::string::npos) {
                mFilteredIndices.push_back(i);
                break;
            }
        }
    }
    
    FileLogger::GetInstance().LogInfo("[ManageScreen::ApplySearch] Search '%s' matched %zu themes", 
                                      mSearchText.c_str(), mFilteredIndices.size());
}

bool ManageScreen::IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH) {
    return touchX >= rectX && touchX <= rectX + rectW &&
           touchY >= rectY && touchY <= rectY + rectH;
}
