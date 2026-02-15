#include "ThemeDetailScreen.hpp"
#include "Gfx.hpp"
#include "../utils/Config.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/ImageLoader.hpp"
#include "../utils/ThemeDownloader.hpp"
#include "../utils/ThemePatcher.hpp"
#include "../utils/Utils.hpp"
#include "../utils/logger.h"
#include "../utils/FileLogger.hpp"
#include <algorithm>
#include <sstream>
#include <thread>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <coreinit/cache.h>  // Wii U 缓存刷新

// 递归删除目录的辅助函数
static bool RemoveDirectory(const std::string& path) {
    FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Opening directory: %s", path.c_str());
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        FileLogger::GetInstance().LogError("[REMOVE_DIR] Failed to open directory: %s (errno=%d)", path.c_str(), errno);
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Directory opened, starting enumeration");
    struct dirent* entry;
    bool success = true;
    int fileCount = 0;
    
    while ((entry = readdir(dir)) != nullptr) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        std::string entryPath = path + "/" + entry->d_name;
        fileCount++;
        
        FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Processing entry %d: %s", fileCount, entry->d_name);
        
        // 使用 stat 来判断文件类型 (d_type 在某些文件系统上不可靠)
        struct stat st;
        if (stat(entryPath.c_str(), &st) != 0) {
            FileLogger::GetInstance().LogError("[REMOVE_DIR] Failed to stat: %s (errno=%d)", entryPath.c_str(), errno);
            success = false;
            continue;
        }
        
        if (S_ISDIR(st.st_mode)) {
            // 递归删除子目录
            FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Recursing into subdirectory: %s", entryPath.c_str());
            if (!RemoveDirectory(entryPath)) {
                FileLogger::GetInstance().LogError("[REMOVE_DIR] Failed to remove subdirectory: %s", entryPath.c_str());
                success = false;
            }
        } else {
            // 删除文件
            FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Deleting file: %s", entryPath.c_str());
            if (unlink(entryPath.c_str()) != 0) {
                FileLogger::GetInstance().LogError("[REMOVE_DIR] Failed to delete file: %s (errno=%d)", entryPath.c_str(), errno);
                success = false;
            } else {
                FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Successfully deleted file: %s", entryPath.c_str());
            }
        }
    }
    
    FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Finished enumeration, processed %d entries", fileCount);
    closedir(dir);
    FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Directory closed");
    
    // 删除目录本身
    FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Attempting to remove directory: %s", path.c_str());
    if (rmdir(path.c_str()) != 0) {
        FileLogger::GetInstance().LogError("[REMOVE_DIR] Failed to delete directory: %s (errno=%d)", path.c_str(), errno);
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("[REMOVE_DIR] Successfully deleted directory: %s", path.c_str());
    return success;
}

ThemeDetailScreen::ThemeDetailScreen(const Theme* theme, ThemeManager* themeManager)
    : mTheme(theme), mThemeManager(themeManager) {
    mTitleAnim.Start(0, 1, 500);
    mContentAnim.Start(0, 1, 600);
    mButtonHoverAnim.SetImmediate(0.0f);
    mPreviewSwitchAnim.SetImmediate(1.0f);
    mFullscreenSlideAnim.SetImmediate(0.0f); // 初始化全屏滑动动画
    
    // 记录进入时间，用于输入冷却
    mEnterFrame = 0;
    
    // 获取当前主题名称（用于UI指示器）
    ThemePatcher patcher;
    mCurrentThemeName = patcher.GetCurrentTheme();
    
    FileLogger::GetInstance().LogInfo("ThemeDetailScreen: Opened for theme '%s'", theme->name.c_str());
    
    // 检查主题是否已下载(本地模式)
    // 1. 如果 themeManager == nullptr,说明从管理页面进入,一定是本地模式
    // 2. 如果 themeManager != nullptr,说明从下载页面进入,需要检查是否已下载
    if (themeManager == nullptr) {
        mIsLocalMode = true;
        FileLogger::GetInstance().LogInfo("Theme '%s' is local (from ManageScreen)", theme->name.c_str());
    } else if (!theme->id.empty()) {
        // 检查 installed 目录中是否存在该主题的 .json 文件
        std::string installedJsonPath = "fs:/vol/external01/UTheme/installed/" + theme->id + ".json";
        struct stat st;
        mIsLocalMode = (stat(installedJsonPath.c_str(), &st) == 0);
        FileLogger::GetInstance().LogInfo("Theme '%s' local mode: %d (json: %s)", 
            theme->name.c_str(), mIsLocalMode, installedJsonPath.c_str());
    }
    
    // 查找主题索引
    int themeIndex = -1;
    if (themeManager) {
        auto& themes = themeManager->GetThemes();
        for (size_t i = 0; i < themes.size(); i++) {
            if (&themes[i] == theme) {
                themeIndex = i;
                break;
            }
        }
    }
    
    FileLogger::GetInstance().LogInfo("Theme mode: %s (Local: %d), Index: %d", 
        themeManager ? "Network" : "Local", mIsLocalMode, themeIndex);
    FileLogger::GetInstance().LogInfo("Theme URLs - Collage HD: %s, Launcher HD: %s, WaraWara HD: %s", 
        theme->collagePreview.hdUrl.c_str(),
        theme->launcherScreenshot.hdUrl.c_str(),
        theme->waraWaraScreenshot.hdUrl.c_str());
    
    // 异步加载高清预览图
    // 网络模式: 更新 ThemeManager 中的主题数据
    if (themeIndex >= 0 && themeManager) {
        auto& themes = themeManager->GetThemes();
        
        // 加载 collagePreview 高清图
        if (!theme->collagePreview.hdUrl.empty() && !theme->collagePreview.hdLoaded) {
            themes[themeIndex].collagePreview.hdLoaded = true;
            ImageLoader::LoadRequest request;
            request.url = theme->collagePreview.hdUrl;
            request.highPriority = true;
            request.callback = [themeManager, themeIndex](SDL_Texture* texture) {
                if (themeManager) {
                    auto& themes = themeManager->GetThemes();
                    if (themeIndex >= 0 && themeIndex < (int)themes.size()) {
                        themes[themeIndex].collagePreview.hdTexture = texture;
                        FileLogger::GetInstance().LogInfo("Loaded HD collagePreview for theme %d: %p", themeIndex, texture);
                    }
                }
            };
            ImageLoader::LoadAsync(request);
        }
        
        // 加载 launcherScreenshot 高清图
        if (!theme->launcherScreenshot.hdUrl.empty() && !theme->launcherScreenshot.hdLoaded) {
            themes[themeIndex].launcherScreenshot.hdLoaded = true;
            ImageLoader::LoadRequest request;
            request.url = theme->launcherScreenshot.hdUrl;
            request.highPriority = true;
            request.callback = [themeManager, themeIndex](SDL_Texture* texture) {
                if (themeManager) {
                    auto& themes = themeManager->GetThemes();
                    if (themeIndex >= 0 && themeIndex < (int)themes.size()) {
                        themes[themeIndex].launcherScreenshot.hdTexture = texture;
                        FileLogger::GetInstance().LogInfo("Loaded HD launcherScreenshot for theme %d: %p", themeIndex, texture);
                    }
                }
            };
            ImageLoader::LoadAsync(request);
        }
        
        // 加载 waraWaraScreenshot 高清图
        if (!theme->waraWaraScreenshot.hdUrl.empty() && !theme->waraWaraScreenshot.hdLoaded) {
            themes[themeIndex].waraWaraScreenshot.hdLoaded = true;
            ImageLoader::LoadRequest request;
            request.url = theme->waraWaraScreenshot.hdUrl;
            request.highPriority = true;
            request.callback = [themeManager, themeIndex](SDL_Texture* texture) {
                if (themeManager) {
                    auto& themes = themeManager->GetThemes();
                    if (themeIndex >= 0 && themeIndex < (int)themes.size()) {
                        themes[themeIndex].waraWaraScreenshot.hdTexture = texture;
                        FileLogger::GetInstance().LogInfo("Loaded HD waraWaraScreenshot for theme %d: %p", themeIndex, texture);
                    }
                }
            };
            ImageLoader::LoadAsync(request);
        }
    }
    // 本地模式: 直接加载本地文件
    else if (!themeManager) {
        FileLogger::GetInstance().LogInfo("Local mode: Loading local images directly");
        
        // 加载 collagePreview 高清图
        if (!theme->collagePreview.hdUrl.empty()) {
            ImageLoader::LoadRequest request;
            request.url = theme->collagePreview.hdUrl;
            request.highPriority = true;
            request.callback = [this](SDL_Texture* texture) {
                if (mTheme) {
                    const_cast<Theme*>(mTheme)->collagePreview.hdTexture = texture;
                    const_cast<Theme*>(mTheme)->collagePreview.hdLoaded = true;
                    FileLogger::GetInstance().LogInfo("Loaded local HD collagePreview: %p", texture);
                }
            };
            ImageLoader::LoadAsync(request);
        }
        
        // 加载 launcherScreenshot 高清图
        if (!theme->launcherScreenshot.hdUrl.empty()) {
            ImageLoader::LoadRequest request;
            request.url = theme->launcherScreenshot.hdUrl;
            request.highPriority = true;
            request.callback = [this](SDL_Texture* texture) {
                if (mTheme) {
                    const_cast<Theme*>(mTheme)->launcherScreenshot.hdTexture = texture;
                    const_cast<Theme*>(mTheme)->launcherScreenshot.hdLoaded = true;
                    FileLogger::GetInstance().LogInfo("Loaded local HD launcherScreenshot: %p", texture);
                }
            };
            ImageLoader::LoadAsync(request);
        }
        
        // 加载 waraWaraScreenshot 高清图
        if (!theme->waraWaraScreenshot.hdUrl.empty()) {
            ImageLoader::LoadRequest request;
            request.url = theme->waraWaraScreenshot.hdUrl;
            request.highPriority = true;
            request.callback = [this](SDL_Texture* texture) {
                if (mTheme) {
                    const_cast<Theme*>(mTheme)->waraWaraScreenshot.hdTexture = texture;
                    const_cast<Theme*>(mTheme)->waraWaraScreenshot.hdLoaded = true;
                    FileLogger::GetInstance().LogInfo("Loaded local HD waraWaraScreenshot: %p", texture);
                }
            };
            ImageLoader::LoadAsync(request);
        }
    }
}

ThemeDetailScreen::~ThemeDetailScreen() {
    FileLogger::GetInstance().LogInfo("ThemeDetailScreen destructor called");
    
    // 如果正在下载,取消下载
    if (mState == STATE_DOWNLOADING) {
        FileLogger::GetInstance().LogInfo("Cancelling ongoing download...");
        mThemeManager->CancelDownload();
    }
    
    // 等待安装线程完成
    if (mInstallThread.joinable()) {
        FileLogger::GetInstance().LogInfo("Waiting for install thread to finish...");
        if (mInstallThreadRunning.load()) {
            FileLogger::GetInstance().LogInfo("Install thread is still running, waiting...");
        }
        mInstallThread.join();
        FileLogger::GetInstance().LogInfo("Install thread finished");
    }
    
    FileLogger::GetInstance().LogInfo("ThemeDetailScreen destructor completed");
}

void ThemeDetailScreen::Draw() {
    mFrameCount++;
    
    // 全屏预览模式
    if (mState == STATE_FULLSCREEN_PREVIEW) {
        DrawFullscreenPreview();
        return;
    }
    
    // 深色背景
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, Gfx::COLOR_BACKGROUND);
    
    // 左侧:大图预览区 (自动轮播)
    int dummy = 0;
    DrawPreviewSection(dummy);
    
    // 右侧:信息区域
    DrawInfoSection(dummy);
    
    // 下载进度覆盖层(下载中、安装中、卸载中、设置当前主题或完成)
    if (mState == STATE_DOWNLOADING || mState == STATE_DOWNLOAD_COMPLETE || 
        mState == STATE_INSTALLING || mState == STATE_INSTALL_COMPLETE || 
        mState == STATE_INSTALL_ERROR || mState == STATE_UNINSTALL_CONFIRM ||
        mState == STATE_UNINSTALLING || mState == STATE_UNINSTALL_COMPLETE ||
        mState == STATE_SET_CURRENT_CONFIRM || mState == STATE_SETTING_CURRENT ||
        mState == STATE_SET_CURRENT_COMPLETE || mState == STATE_SET_CURRENT_ERROR) {
        DrawDownloadProgress();
    }
    
    // 底部提示
    int tipY = Gfx::SCREEN_HEIGHT - 50;
    std::string hints = mIsLocalMode ? _("theme_detail.hints_local") : _("theme_detail.hints");
    
    // 替换 <Arrow> 为实际的箭头图标 (FontAwesome \ue07e)
    size_t pos = hints.find("<Arrow>");
    if (pos != std::string::npos) {
        hints.replace(pos, 7, "\ue07e");
    }
    
    // 临时使用 CJK 字体渲染（因为包含特殊 Unicode 字符）
    bool originalFontSetting = Gfx::GetUseLatinFont();
    Gfx::SetUseLatinFont(false);  // 强制使用 CJK 字体
    
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, tipY, 24, Gfx::COLOR_ALT_TEXT, 
               hints.c_str(), Gfx::ALIGN_CENTER);
    
    // 恢复原字体设置
    Gfx::SetUseLatinFont(originalFontSetting);
    
    // 移除全局触摸调试信息 - 不应该默认显示
    // 如果需要调试,应该在详情页单独激活,而不是继承主菜单的状态
    
    // 绘制圆形返回按钮
    Screen::DrawBackButton();
}

void ThemeDetailScreen::DrawPreviewSection(int yOffset) {
    // 左侧大图区域 (16:9比例，无背景，图片填满整个区 ?
    const int previewX = 60;
    const int previewY = 60;
    const int previewW = 1100;
    const int previewH = (int)(previewW * 9.0 / 16.0); // 16:9 = 618px ?
    
    // 绘制预览图区 ?(启用裁剪)
    SDL_Rect clipRect = {previewX, previewY, previewW, previewH};
    SDL_RenderSetClipRect(Gfx::GetRenderer(), &clipRect);
    
    // 获取预览图纹理（第一张使用高清图，其他使用缩略图 ?
    auto getPreviewTexture = [this](int index) -> SDL_Texture* {
        switch (index) {
            case 0: 
                // collagePreview 优先使用高清图，如果没有则使用缩略图
                return mTheme->collagePreview.hdTexture ? 
                       mTheme->collagePreview.hdTexture : 
                       mTheme->collagePreview.thumbTexture;
            case 1: 
                return mTheme->launcherScreenshot.hdTexture ? 
                       mTheme->launcherScreenshot.hdTexture : 
                       mTheme->launcherScreenshot.thumbTexture;
            case 2: 
                return mTheme->waraWaraScreenshot.hdTexture ? 
                       mTheme->waraWaraScreenshot.hdTexture : 
                       mTheme->waraWaraScreenshot.thumbTexture;
            default: return nullptr;
        }
    };
    
    // 滑动动画：绘制当前和前一个预览图
    float slideProgress = mPreviewSlideAnim.GetValue();
    int slideOffset = 0;
    
    // 如果正在拖动,使用实时拖动偏移;否则使用动画偏移
    if (mIsDragging) {
        slideOffset = -mTouchDragOffsetX; // 注意方向:手指向右拖,图片向右移
    } else {
        slideOffset = (int)(slideProgress * previewW * mSlideDirection);
    }
    
    // 绘制前一个预览图（滑出）- 仅在动画模式或拖动模式需要时显示
    bool shouldDrawPrevious = (mSlideDirection != 0 && slideProgress < 1.0f) || 
                              (mIsDragging && abs(mTouchDragOffsetX) > 10);
    
    if (shouldDrawPrevious) {
        // 确定要显示的"前一个"预览图
        int prevIndex = mPreviousPreview;
        if (mIsDragging) {
            // 拖动时根据方向决定显示哪一张
            if (mTouchDragOffsetX > 0) {
                // 向右拖动,显示上一张(左侧)
                prevIndex = (mCurrentPreview + 2) % 3;
            } else {
                // 向左拖动,显示下一张(右侧)
                prevIndex = (mCurrentPreview + 1) % 3;
            }
        }
        
        SDL_Texture* prevTexture = getPreviewTexture(prevIndex);
        if (prevTexture) {
            int texW, texH;
            SDL_QueryTexture(prevTexture, nullptr, nullptr, &texW, &texH);
            
            // 适应区域（保持比例，不裁剪）
            float scaleW = (float)previewW / texW;
            float scaleH = (float)previewH / texH;
            float scale = std::min(scaleW, scaleH); // 使用min以适应区域
            int scaledW = (int)(texW * scale);
            int scaledH = (int)(texH * scale);
            
            SDL_Rect dstRect;
            if (mIsDragging) {
                // 拖动模式:根据拖动方向决定位置
                if (mTouchDragOffsetX > 0) {
                    // 向右拖,前一张从左边出现
                    dstRect.x = previewX + (previewW - scaledW) / 2 + mTouchDragOffsetX - previewW;
                } else {
                    // 向左拖,下一张从右边出现
                    dstRect.x = previewX + (previewW - scaledW) / 2 + mTouchDragOffsetX + previewW;
                }
            } else {
                // 动画模式
                dstRect.x = previewX + (previewW - scaledW) / 2 - slideOffset;
            }
            dstRect.y = previewY + (previewH - scaledH) / 2;
            dstRect.w = scaledW;
            dstRect.h = scaledH;
            
            SDL_RenderCopy(Gfx::GetRenderer(), prevTexture, nullptr, &dstRect);
        }
    }
    
    // 绘制当前预览图（滑入 ?
    SDL_Texture* currentTexture = getPreviewTexture(mCurrentPreview);
    if (currentTexture) {
        int texW, texH;
        SDL_QueryTexture(currentTexture, nullptr, nullptr, &texW, &texH);
        
        // 适应区域（保持比例，不裁剪）
        float scaleW = (float)previewW / texW;
        float scaleH = (float)previewH / texH;
        float scale = std::min(scaleW, scaleH); // 使用min以适应区域
        int scaledW = (int)(texW * scale);
        int scaledH = (int)(texH * scale);
        
        SDL_Rect dstRect;
        if (mIsDragging) {
            // 拖动模式:当前图片跟随手指移动
            dstRect.x = previewX + (previewW - scaledW) / 2 + mTouchDragOffsetX;
        } else if (mSlideDirection != 0 && slideProgress < 1.0f) {
            // 动画模式：从边缘滑入
            dstRect.x = previewX + (previewW - scaledW) / 2 + previewW * mSlideDirection - slideOffset;
        } else {
            // 静止：居中
            dstRect.x = previewX + (previewW - scaledW) / 2;
        }
        dstRect.y = previewY + (previewH - scaledH) / 2;
        dstRect.w = scaledW;
        dstRect.h = scaledH;
        
        SDL_RenderCopy(Gfx::GetRenderer(), currentTexture, nullptr, &dstRect);
    } else {
        // 加载 ?- 显示在黑色背景上
        SDL_Color loadingBg = {20, 20, 20, 255};
        Gfx::DrawRectFilled(previewX, previewY, previewW, previewH, loadingBg);
        const std::string loadingText = _("theme_detail.loading_preview");
        Gfx::Print(previewX + previewW / 2, previewY + previewH / 2 - 30, 32, 
                   Gfx::COLOR_ALT_TEXT, loadingText.c_str(), Gfx::ALIGN_CENTER);
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(previewX + previewW / 2, previewY + previewH / 2 + 20, 50, 
                      Gfx::COLOR_ICON, 0xf1ce, Gfx::ALIGN_CENTER, angle);
    }
    
    // 取消裁剪
    SDL_RenderSetClipRect(Gfx::GetRenderer(), nullptr);
    
    // 左右箭头按钮
    const int arrowSize = 50;
    const int arrowY = previewY + previewH / 2 - arrowSize / 2;
    
    // 左箭 ?
    SDL_Color arrowBg = Gfx::COLOR_CARD_BG;
    arrowBg.a = 200;
    Gfx::DrawRectRounded(previewX + 20, arrowY, arrowSize, arrowSize, 25, arrowBg);
    Gfx::DrawIcon(previewX + 20 + arrowSize / 2, arrowY + arrowSize / 2, 32, 
                  Gfx::COLOR_TEXT, 0xf053, Gfx::ALIGN_CENTER); // chevron-left
    
    // 右箭 ?
    Gfx::DrawRectRounded(previewX + previewW - 70, arrowY, arrowSize, arrowSize, 25, arrowBg);
    Gfx::DrawIcon(previewX + previewW - 70 + arrowSize / 2, arrowY + arrowSize / 2, 32, 
                  Gfx::COLOR_TEXT, 0xf054, Gfx::ALIGN_CENTER); // chevron-right
    
    // 底部：预览指示器（无标签 ?
    const int indicatorY = previewY + previewH - 30;
    const int indicatorW = 80;
    const int indicatorH = 8;
    const int indicatorSpacing = 15;
    const int totalW = indicatorW * 3 + indicatorSpacing * 2;
    int indicatorX = previewX + (previewW - totalW) / 2;
    
    for (int i = 0; i < 3; i++) {
        SDL_Color color = (i == mCurrentPreview) ? Gfx::COLOR_ACCENT : Gfx::COLOR_ALT_BACKGROUND;
        Gfx::DrawRectRounded(indicatorX, indicatorY, indicatorW, indicatorH, 4, color);
        indicatorX += indicatorW + indicatorSpacing;
    }
}

void ThemeDetailScreen::DrawInfoSection(int yOffset) {
    // 右侧信息区域
    const int infoX = 1200;
    const int infoY = 60;
    const int infoW = 660;
    
    int currentY = infoY;
    
    // 主题名称 (大标题) - 清理特殊字符用于显示
    const int titlePadding = 30;
    std::string displayName = Utils::SanitizeThemeNameForDisplay(mTheme->name);
    Gfx::Print(infoX + titlePadding, currentY, 48, Gfx::COLOR_TEXT, 
               displayName.c_str(), Gfx::ALIGN_LEFT);
    currentY += 65;
    
    // 作者信 ?
    const std::string byText = _("theme_detail.by");
    std::string authorText = byText + " " + mTheme->author;
    Gfx::Print(infoX + titlePadding, currentY, 28, Gfx::COLOR_ALT_TEXT, 
               authorText.c_str(), Gfx::ALIGN_LEFT);
    currentY += 40;
    
    // 更新日期
    if (!mTheme->updatedAt.empty()) {
        const std::string updatedText = _("theme_detail.updated");
        std::string updateText = updatedText + " " + mTheme->updatedAt.substr(0, 10); // 只显示日期部 ?YYYY-MM-DD
        Gfx::DrawIcon(infoX + titlePadding, currentY + 4, 24, Gfx::COLOR_ALT_TEXT, 
                      0xf017, Gfx::ALIGN_LEFT); // calendar icon
        Gfx::Print(infoX + titlePadding + 35, currentY + 4, 24, Gfx::COLOR_ALT_TEXT, 
                   updateText.c_str(), Gfx::ALIGN_LEFT);
        currentY += 40;
    }
    
    // 分隔 ?
    SDL_Color lineColor = Gfx::COLOR_ALT_BACKGROUND;
    Gfx::DrawRectFilled(infoX + titlePadding, currentY, infoW - titlePadding * 2, 2, lineColor);
    currentY += 30;
    
    // 统计信息 (下载数和点赞 ?
    const int statIconSize = 32;
    
    // 下载 ?
    Gfx::DrawIcon(infoX + titlePadding, currentY, statIconSize, Gfx::COLOR_WIIU, 
                  0xf019, Gfx::ALIGN_LEFT);
    const std::string downloadsLabel = _("theme_detail.downloads");
    std::string downloadsText = std::to_string(mTheme->downloads) + " " + downloadsLabel;
    Gfx::Print(infoX + titlePadding + statIconSize + 15, currentY + 8, 28, 
               Gfx::COLOR_TEXT, downloadsText.c_str(), Gfx::ALIGN_LEFT);
    
    // 点赞 ?
    Gfx::DrawIcon(infoX + titlePadding + 280, currentY, statIconSize, Gfx::COLOR_ERROR, 
                  0xf004, Gfx::ALIGN_LEFT);
    const std::string likesLabel = _("theme_detail.likes");
    std::string likesText = std::to_string(mTheme->likes) + " " + likesLabel;
    Gfx::Print(infoX + titlePadding + 280 + statIconSize + 15, currentY + 8, 28, 
               Gfx::COLOR_TEXT, likesText.c_str(), Gfx::ALIGN_LEFT);
    
    currentY += statIconSize + 40;
    
    // 描述卡片
    SDL_Color descBg = Gfx::COLOR_CARD_BG;
    const int descH = 280;
    Gfx::DrawRectRounded(infoX + titlePadding, currentY, infoW - titlePadding * 2, descH, 12, descBg);
    
    // 描述标题
    const std::string descTitle = _("theme_detail.description");
    Gfx::Print(infoX + titlePadding + 20, currentY + 25, 32, Gfx::COLOR_TEXT, 
               descTitle.c_str(), Gfx::ALIGN_LEFT);
    
    // 描述内容 (多行，改进文本换行以避免重叠)
    const std::string noDescText = _("theme_detail.no_description");
    std::string desc = mTheme->description.empty() ? noDescText : mTheme->description;
    
    // 改进的文本换行逻辑
    const int maxLineWidth = infoW - titlePadding * 2 - 40; // 可用宽度（减去左右边距）
    const int fontSize = 24;
    const int lineHeight = 34; // 增加行高以避免重 ?
    int descY = currentY + 70;
    int lineCount = 0;
    size_t pos = 0;
    
    while (pos < desc.length() && lineCount < 6) {
        // 计算这一行能放多少字 ?
        std::string testLine;
        size_t endPos = pos;
        int lineWidth = 0;
        
        // 逐字符添加，直到超出宽度
        while (endPos < desc.length() && lineWidth < maxLineWidth) {
            char c = desc[endPos];
            testLine += c;
            
            // 估算字符宽度（ASCII ?0px，其他约20px ?
            int charWidth = (c >= 32 && c <= 126) ? 10 : 20;
            lineWidth += charWidth;
            
            endPos++;
            
            // 如果遇到换行符，强制断行
            if (c == '\n') {
                break;
            }
        }
        
        // 如果不是最后一行，尝试在空格或标点处断 ?
        if (endPos < desc.length()) {
            size_t breakPos = testLine.find_last_of(" \t,;.!?-");
            if (breakPos != std::string::npos && breakPos > testLine.length() / 2) {
                testLine = testLine.substr(0, breakPos);
                endPos = pos + breakPos;
            }
        }
        
        // 移除首尾空白
        size_t start = testLine.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) {
            testLine = testLine.substr(start);
        }
        
        // 绘制这一 ?
        if (!testLine.empty()) {
            Gfx::Print(infoX + titlePadding + 20, descY, fontSize, Gfx::COLOR_ALT_TEXT, 
                       testLine.c_str(), Gfx::ALIGN_LEFT);
            descY += lineHeight;
            lineCount++;
        }
        
        pos = endPos;
        // 跳过空白字符
        while (pos < desc.length() && (desc[pos] == ' ' || desc[pos] == '\t' || desc[pos] == '\n' || desc[pos] == '\r')) {
            pos++;
        }
    }
    
    currentY += descH + 40;
    
    // 标签区域
    if (!mTheme->tags.empty()) {
        const std::string tagsTitle = _("theme_detail.tags");
        Gfx::Print(infoX + titlePadding, currentY, 28, Gfx::COLOR_TEXT, 
                   tagsTitle.c_str(), Gfx::ALIGN_LEFT);
        currentY += 40;
        
        int tagX = infoX + titlePadding;
        int tagY = currentY;
        const int tagH = 35;
        const int tagSpacing = 10;
        
        for (const auto& tag : mTheme->tags) {
            int tagW = tag.length() * 12 + 30;
            
            // 换行检 ?
            if (tagX + tagW > infoX + infoW - titlePadding) {
                tagX = infoX + titlePadding;
                tagY += tagH + tagSpacing;
            }
            
            SDL_Color tagBg = Gfx::COLOR_ALT_ACCENT;
            tagBg.a = 80;
            Gfx::DrawRectRounded(tagX, tagY, tagW, tagH, 8, tagBg);
            Gfx::Print(tagX + tagW / 2, tagY + tagH / 2, 20, Gfx::COLOR_TEXT, 
                       tag.c_str(), Gfx::ALIGN_CENTER);
            
            tagX += tagW + tagSpacing;
        }
        
        currentY = tagY + tagH + 40;
    }
    
    // 操作按钮 (大而醒目)
    const int btnW = infoW - titlePadding * 2;
    const int btnH = 70;
    const int btnY = 890; // 固定在底部
    
    // 根据模式显示不同按钮 - 使用mIsLocalMode标志
    if (mIsLocalMode) {
        // 本地模式: 显示卸载按钮 (红色)
        SDL_Color btnBg = mDownloadButtonHovered ? Gfx::COLOR_ERROR_HOVER : Gfx::COLOR_ERROR;
        Gfx::DrawRectRounded(infoX + titlePadding, btnY, btnW, btnH, 12, btnBg);
        
        // 卸载图标 (trash icon)
        Gfx::DrawIcon(infoX + titlePadding + btnW / 2 - 120, btnY + btnH / 2, 40, 
                      Gfx::COLOR_WHITE, 0xf2ed, Gfx::ALIGN_CENTER);
        
        // 卸载文本
        const std::string uninstallBtnText = _("theme_detail.uninstall_theme");
        Gfx::Print(infoX + titlePadding + btnW / 2 + 40, btnY + btnH / 2, 36, 
                   Gfx::COLOR_WHITE, uninstallBtnText.c_str(), Gfx::ALIGN_CENTER);
    } else {
        // 网络模式: 显示下载按钮 (蓝色)
        SDL_Color btnBg = mDownloadButtonHovered ? Gfx::COLOR_HIGHLIGHTED : Gfx::COLOR_ACCENT;
        Gfx::DrawRectRounded(infoX + titlePadding, btnY, btnW, btnH, 12, btnBg);
        
        // 下载图标（往左移动）
        Gfx::DrawIcon(infoX + titlePadding + btnW / 2 - 150, btnY + btnH / 2, 40, 
                      Gfx::COLOR_WHITE, 0xf019, Gfx::ALIGN_CENTER);
        
        // 下载文本
        const std::string downloadBtnText = _("theme_detail.download_theme");
        Gfx::Print(infoX + titlePadding + btnW / 2 + 30, btnY + btnH / 2, 36, 
                   Gfx::COLOR_WHITE, downloadBtnText.c_str(), Gfx::ALIGN_CENTER);
    }
}

void ThemeDetailScreen::DrawDownloadProgress() {
    // 半透明背景
    SDL_Color overlay = {0, 0, 0, 200};
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, overlay);
    
    // 进度卡片
    const int cardW = 800;
    const int cardH = 300;
    const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
    const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
    
    Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
    
    // 根据状态显示不同内容
    if (mState == STATE_SET_CURRENT_CONFIRM) {
        // 设置当前主题确认对话框
        const std::string confirmText = _("theme_detail.set_current_confirm");
        Gfx::Print(cardX + cardW / 2, cardY + 60, 36, Gfx::COLOR_ACCENT, 
                   confirmText.c_str(), Gfx::ALIGN_CENTER);
        
        // 星星图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 130, 70, Gfx::COLOR_ACCENT, 
                      0xf005, Gfx::ALIGN_CENTER); // star icon
        
        // 主题名称 - 清理特殊字符用于显示
        std::string themeNameText = Utils::SanitizeThemeNameForDisplay(mTheme->name);
        if (themeNameText.length() > 40) {
            themeNameText = themeNameText.substr(0, 37) + "...";
        }
        Gfx::Print(cardX + cardW / 2, cardY + 200, 28, Gfx::COLOR_TEXT, 
                   themeNameText.c_str(), Gfx::ALIGN_CENTER);
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 240, 24, Gfx::COLOR_ALT_TEXT, 
                   "A: " + _("common.confirm"), Gfx::ALIGN_CENTER);
        Gfx::Print(cardX + cardW / 2, cardY + 270, 24, Gfx::COLOR_ALT_TEXT, 
                   "B: " + _("common.cancel"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_SETTING_CURRENT) {
        // 正在设置当前主题
        const std::string statusText = _("theme_detail.setting_current");
        Gfx::Print(cardX + cardW / 2, cardY + 50, 40, Gfx::COLOR_TEXT, 
                   statusText.c_str(), Gfx::ALIGN_CENTER);
        
        // 旋转图标
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 120, 60, Gfx::COLOR_ACCENT, 
                      0xf013, Gfx::ALIGN_CENTER, angle); // gear icon
        
        // 提示文本
        Gfx::Print(cardX + cardW / 2, cardY + 200, 28, Gfx::COLOR_ALT_TEXT, 
                   "Updating StyleMiiU config...", Gfx::ALIGN_CENTER);
    } else if (mState == STATE_SET_CURRENT_COMPLETE) {
        // 设置完成
        const std::string completeText = _("theme_detail.set_current_complete");
        Gfx::Print(cardX + cardW / 2, cardY + 80, 48, Gfx::COLOR_SUCCESS, 
                   completeText.c_str(), Gfx::ALIGN_CENTER);
        
        // 成功图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 160, 80, Gfx::COLOR_SUCCESS, 
                      0xf00c, Gfx::ALIGN_CENTER); // checkmark icon
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 250, 28, Gfx::COLOR_ALT_TEXT, 
                   "A/B: " + _("common.back"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_SET_CURRENT_ERROR) {
        // 设置失败
        const std::string errorText = _("theme_detail.set_current_error");
        Gfx::Print(cardX + cardW / 2, cardY + 80, 48, Gfx::COLOR_ERROR, 
                   errorText.c_str(), Gfx::ALIGN_CENTER);
        
        // 错误图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 160, 80, Gfx::COLOR_ERROR, 
                      0xf06a, Gfx::ALIGN_CENTER); // exclamation-circle icon
        
        // 提示信息 - 建议查看日志
        Gfx::Print(cardX + cardW / 2, cardY + 250, 24, Gfx::COLOR_ALT_TEXT, 
                   _("theme_detail.check_log"), Gfx::ALIGN_CENTER);
        
        Gfx::Print(cardX + cardW / 2, cardY + 290, 28, Gfx::COLOR_ALT_TEXT, 
                   "A/B: " + _("common.back"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_UNINSTALL_CONFIRM) {
        // 卸载确认对话框
        const std::string confirmText = _("theme_detail.uninstall_confirm");
        Gfx::Print(cardX + cardW / 2, cardY + 60, 40, Gfx::COLOR_WARNING, 
                   confirmText.c_str(), Gfx::ALIGN_CENTER);
        
        // 警告图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 130, 70, Gfx::COLOR_WARNING, 
                      0xf071, Gfx::ALIGN_CENTER); // warning icon
        
        // 主题名称 - 清理特殊字符用于显示
        std::string themeNameText = Utils::SanitizeThemeNameForDisplay(mTheme->name);
        if (themeNameText.length() > 40) {
            themeNameText = themeNameText.substr(0, 37) + "...";
        }
        Gfx::Print(cardX + cardW / 2, cardY + 200, 28, Gfx::COLOR_TEXT, 
                   themeNameText.c_str(), Gfx::ALIGN_CENTER);
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 240, 24, Gfx::COLOR_ALT_TEXT, 
                   "A: " + _("common.confirm"), Gfx::ALIGN_CENTER);
        Gfx::Print(cardX + cardW / 2, cardY + 270, 24, Gfx::COLOR_ALT_TEXT, 
                   "B: " + _("common.cancel"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_UNINSTALL_COMPLETE) {
        // 卸载完成
        const std::string completeText = _("theme_detail.uninstall_complete");
        Gfx::Print(cardX + cardW / 2, cardY + 80, 48, Gfx::COLOR_SUCCESS, 
                   completeText.c_str(), Gfx::ALIGN_CENTER);
        
        // 成功图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 160, 80, Gfx::COLOR_SUCCESS, 
                      0xf00c, Gfx::ALIGN_CENTER); // checkmark icon
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 250, 28, Gfx::COLOR_ALT_TEXT, 
                   "A/B: " + _("common.back"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_UNINSTALLING) {
        // 卸载中
        const std::string statusText = _("theme_detail.uninstalling");
        Gfx::Print(cardX + cardW / 2, cardY + 50, 40, Gfx::COLOR_TEXT, 
                   statusText.c_str(), Gfx::ALIGN_CENTER);
        
        // 旋转图标
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 120, 60, Gfx::COLOR_WARNING, 
                      0xf2ed, Gfx::ALIGN_CENTER, angle); // trash icon
        
        // 提示文本
        Gfx::Print(cardX + cardW / 2, cardY + 200, 28, Gfx::COLOR_ALT_TEXT, 
                   "Removing theme files...", Gfx::ALIGN_CENTER);
    } else if (mState == STATE_INSTALL_COMPLETE) {
        // 安装完成
        const std::string completeText = _("theme_detail.install_complete");
        Gfx::Print(cardX + cardW / 2, cardY + 80, 48, Gfx::COLOR_SUCCESS, 
                   completeText.c_str(), Gfx::ALIGN_CENTER);
        
        // 成功图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 160, 80, Gfx::COLOR_SUCCESS, 
                      0xf00c, Gfx::ALIGN_CENTER); // checkmark icon
        
        // 调试信息：显示线程状态
        std::string threadStatus = mInstallThreadRunning.load() ? "Thread running" : "Thread finished";
        Gfx::Print(cardX + cardW / 2, cardY + 220, 24, Gfx::COLOR_ALT_TEXT, 
                   threadStatus.c_str(), Gfx::ALIGN_CENTER);
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 250, 28, Gfx::COLOR_ALT_TEXT, 
                   "A/B: " + _("common.back"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_INSTALL_ERROR) {
        // 安装失败
        const std::string errorText = _("theme_detail.install_error");
        Gfx::Print(cardX + cardW / 2, cardY + 80, 48, Gfx::COLOR_ERROR, 
                   errorText.c_str(), Gfx::ALIGN_CENTER);
        
        // 错误图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 160, 80, Gfx::COLOR_ERROR, 
                      0xf071, Gfx::ALIGN_CENTER); // warning icon
        
        // 错误消息
        if (!mInstallError.empty()) {
            Gfx::Print(cardX + cardW / 2, cardY + 220, 24, Gfx::COLOR_ALT_TEXT, 
                       mInstallError.c_str(), Gfx::ALIGN_CENTER);
        }
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 250, 28, Gfx::COLOR_ALT_TEXT, 
                   "A/B: " + _("common.back"), Gfx::ALIGN_CENTER);
    } else if (mState == STATE_INSTALLING) {
        // 安装中
        const std::string statusText = _("theme_detail.installing");
        Gfx::Print(cardX + cardW / 2, cardY + 50, 40, Gfx::COLOR_TEXT, 
                   statusText.c_str(), Gfx::ALIGN_CENTER);
        
        // 旋转图标
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 120, 60, Gfx::COLOR_ACCENT, 
                      0xf1c6, Gfx::ALIGN_CENTER, angle); // file-archive icon
        
        // 进度条
        const int progressW = 600;
        const int progressH = 20;
        const int progressX = cardX + (cardW - progressW) / 2;
        const int progressY = cardY + 200;
        
        Gfx::DrawRectRounded(progressX, progressY, progressW, progressH, 10, Gfx::COLOR_ALT_BACKGROUND);
        
        int filledW = (int)(progressW * mInstallProgress);
        if (filledW > 0) {
            Gfx::DrawRectRounded(progressX, progressY, filledW, progressH, 10, Gfx::COLOR_ACCENT);
        }
        
        // 百分比
        char progressText[32];
        snprintf(progressText, sizeof(progressText), "%.0f%%", mInstallProgress * 100);
        Gfx::Print(cardX + cardW / 2, cardY + 245, 28, Gfx::COLOR_ALT_TEXT, 
                   progressText, Gfx::ALIGN_CENTER);
    } else if (mState == STATE_DOWNLOAD_COMPLETE) {
        // 下载完成（保留用于兼容）
        const std::string completeText = _("theme_detail.complete");
        Gfx::Print(cardX + cardW / 2, cardY + 80, 48, Gfx::COLOR_SUCCESS, 
                   completeText.c_str(), Gfx::ALIGN_CENTER);
        
        // 成功图标
        Gfx::DrawIcon(cardX + cardW / 2, cardY + 160, 80, Gfx::COLOR_SUCCESS, 
                      0xf00c, Gfx::ALIGN_CENTER); // checkmark icon
        
        // 提示信息
        Gfx::Print(cardX + cardW / 2, cardY + 250, 28, Gfx::COLOR_ALT_TEXT, 
                   "A/B: " + _("common.back"), Gfx::ALIGN_CENTER);
    } else {
        // 下载中或解压中
        int downloadState = mThemeManager->GetDownloadState();
        std::string statusText;
        
        if (downloadState == DOWNLOAD_EXTRACTING) {
            statusText = _("theme_detail.extracting");
        } else {
            statusText = _("theme_detail.downloading");
        }
        
        Gfx::Print(cardX + cardW / 2, cardY + 50, 40, Gfx::COLOR_TEXT, 
                   statusText.c_str(), Gfx::ALIGN_CENTER);
        
        // 旋转图标（使用 spinner 圆圈图标）
        double angle = (mFrameCount % 60) * 6.0;
        if (downloadState == DOWNLOAD_DOWNLOADING) {
            Gfx::DrawIcon(cardX + cardW / 2, cardY + 120, 60, Gfx::COLOR_ACCENT, 
                          0xf110, Gfx::ALIGN_CENTER, angle); // spinner icon
        } else {
            // 解压图标
            Gfx::DrawIcon(cardX + cardW / 2, cardY + 120, 60, Gfx::COLOR_ACCENT, 
                          0xf110, Gfx::ALIGN_CENTER, angle); // spinner icon (same for extracting)
        }
        
        // 进度条
        const int progressW = 600;
        const int progressH = 20;
        const int progressX = cardX + (cardW - progressW) / 2;
        const int progressY = cardY + 200;
        
        Gfx::DrawRectRounded(progressX, progressY, progressW, progressH, 10, Gfx::COLOR_ALT_BACKGROUND);
        
        int filledW = (int)(progressW * mDownloadProgress);
        if (filledW > 0) {
            Gfx::DrawRectRounded(progressX, progressY, filledW, progressH, 10, Gfx::COLOR_ACCENT);
        }
        
        // 百分比
        char progressText[32];
        snprintf(progressText, sizeof(progressText), "%.0f%%", mDownloadProgress * 100);
        Gfx::Print(cardX + cardW / 2, cardY + 245, 28, Gfx::COLOR_ALT_TEXT, 
                   progressText, Gfx::ALIGN_CENTER);
        
        // 取消提示
        Gfx::Print(cardX + cardW / 2, cardY + 270, 24, Gfx::COLOR_ALT_TEXT, 
                   "B: " + _("common.cancel"), Gfx::ALIGN_CENTER);
    }
}

bool ThemeDetailScreen::IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH) {
    return touchX >= rectX && touchX <= rectX + rectW &&
           touchY >= rectY && touchY <= rectY + rectH;
}

bool ThemeDetailScreen::UninstallTheme() {
    FileLogger::GetInstance().LogInfo("[UNINSTALL] Starting uninstall for theme: %s", mTheme->name.c_str());
    
    // 从图片路径提取主题文件夹路径
    // 新格式: fs:/vol/external01/wiiu/themes/ThemeName/images/collage.jpg -> fs:/vol/external01/wiiu/themes/ThemeName
    // 旧格式: fs:/vol/external01/wiiu/themes/ThemeName/collage.jpg -> fs:/vol/external01/wiiu/themes/ThemeName
    std::string themePath;
    if (!mTheme->collagePreview.hdUrl.empty()) {
        FileLogger::GetInstance().LogInfo("[UNINSTALL] Extracting path from: %s", mTheme->collagePreview.hdUrl.c_str());
        
        std::string fullPath = mTheme->collagePreview.hdUrl;
        size_t lastSlash = fullPath.rfind('/');
        
        if (lastSlash != std::string::npos) {
            std::string parentPath = fullPath.substr(0, lastSlash);
            
            // 检查父目录是否是 "images"
            size_t secondLastSlash = parentPath.rfind('/');
            if (secondLastSlash != std::string::npos) {
                std::string dirName = parentPath.substr(secondLastSlash + 1);
                if (dirName == "images") {
                    // 是新格式,再往上一级
                    themePath = parentPath.substr(0, secondLastSlash);
                    FileLogger::GetInstance().LogInfo("[UNINSTALL] Detected images/ subdirectory, using parent: %s", themePath.c_str());
                } else {
                    // 旧格式,直接使用父路径
                    themePath = parentPath;
                    FileLogger::GetInstance().LogInfo("[UNINSTALL] Using parent path (legacy): %s", themePath.c_str());
                }
            } else {
                themePath = parentPath;
            }
        }
    }
    
    if (themePath.empty()) {
        FileLogger::GetInstance().LogError("[UNINSTALL] Cannot determine theme path for uninstall");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("[UNINSTALL] About to call RemoveDirectory: %s", themePath.c_str());
    
    // 删除主题文件夹
    bool success = RemoveDirectory(themePath);
    
    FileLogger::GetInstance().LogInfo("[UNINSTALL] RemoveDirectory returned: %d", success);
    
    if (success) {
        FileLogger::GetInstance().LogInfo("[UNINSTALL] Theme uninstalled successfully: %s", mTheme->name.c_str());
        
        // 删除 installed 目录中的 JSON 文件
        std::string installedJsonPath = "fs:/vol/external01/UTheme/installed/" + mTheme->id + ".json";
        if (unlink(installedJsonPath.c_str()) == 0) {
            FileLogger::GetInstance().LogInfo("[UNINSTALL] Deleted installed JSON: %s", installedJsonPath.c_str());
        } else {
            FileLogger::GetInstance().LogWarning("[UNINSTALL] Failed to delete installed JSON (errno=%d): %s", 
                errno, installedJsonPath.c_str());
        }
    } else {
        FileLogger::GetInstance().LogError("[UNINSTALL] Failed to uninstall theme: %s", mTheme->name.c_str());
    }
    
    return success;
}

void ThemeDetailScreen::HandleTouchInput(const Input& input) {
    FileLogger::GetInstance().LogInfo("[HandleTouchInput] Entry - touched:%d valid:%d lastTouched:%d", 
                                      input.data.touched, input.data.validPointer, input.lastData.touched);
    
    if (!input.data.touched || !input.data.validPointer || input.lastData.touched) {
        FileLogger::GetInstance().LogInfo("[HandleTouchInput] Early return - check failed");
        return;
    }
    
    // 转换触摸坐标
    // input.data.x/y 是 1280x720 坐标系相对于中心的坐标（-640~+640, -360~+360）
    // 需要缩放到 1920x1080 屏幕坐标系
    float scaleX = 1920.0f / 1280.0f;  // 1.5
    float scaleY = 1080.0f / 720.0f;   // 1.5
    int touchX = (Gfx::SCREEN_WIDTH / 2) + (int)(input.data.x * scaleX);   // 960 + (x * 1.5)
    int touchY = (Gfx::SCREEN_HEIGHT / 2) - (int)(input.data.y * scaleY);  // 540 - (y * 1.5)
    
    FileLogger::GetInstance().LogInfo("[HandleTouchInput] Touch at screen coords: (%d, %d)", touchX, touchY);
    
    // 右侧信息面板的操作按钮位置（固定在底部）
    const int infoX = 1200;
    const int infoW = 660;
    const int actionBtnX = infoX + 60;
    const int actionBtnY = 890; // 固定位置
    const int actionBtnW = infoW - 120;
    const int actionBtnH = 70;
    
    FileLogger::GetInstance().LogInfo("[HandleTouchInput] Action button bounds: X[%d-%d] Y[%d-%d]", 
                                      actionBtnX, actionBtnX + actionBtnW, 
                                      actionBtnY, actionBtnY + actionBtnH);
    
    // 操作按钮（下载或卸载）
    if (IsTouchInRect(touchX, touchY, actionBtnX, actionBtnY, actionBtnW, actionBtnH)) {
        bool isLocalMode = (mThemeManager == nullptr);
        
        FileLogger::GetInstance().LogInfo("[HandleTouchInput] Action button hit! isLocal:%d state:%d", 
                                          isLocalMode, (int)mState.load());
        
        if (isLocalMode) {
            // 本地模式: 显示卸载确认对话框
            FileLogger::GetInstance().LogInfo("Uninstall button touched, showing confirmation");
            mUninstallRequested = true;
        } else {
            // 网络模式: 下载主题
            if (mState == STATE_VIEWING) {  // 只在浏览状态才响应
                mState = STATE_DOWNLOADING;
                mDownloadStartFrame = mFrameCount;
                mThemeManager->DownloadTheme(*mTheme); // 启动异步下载
                FileLogger::GetInstance().LogInfo("Download button touched, starting download");
            } else {
                FileLogger::GetInstance().LogInfo("[HandleTouchInput] Not in VIEWING state, ignoring");
            }
        }
        return;
    }
    
    // 左右箭头按钮
    const int previewX = 60;
    const int previewY = 60;
    const int previewW = 1100;
    const int previewH = (int)(previewW * 9.0 / 16.0);
    const int arrowSize = 50;
    const int arrowY = previewY + previewH / 2 - arrowSize / 2;
    
    FileLogger::GetInstance().LogInfo("[HandleTouchInput] Preview area: X[%d-%d] Y[%d-%d]", 
                                      previewX, previewX + previewW, previewY, previewY + previewH);
    
    // 点击预览图中心区域进入全屏预览 (排除箭头区域)
    const int centerMargin = 100; // 左右各留100px给箭头
    if (IsTouchInRect(touchX, touchY, 
                      previewX + centerMargin, previewY, 
                      previewW - centerMargin * 2, previewH)) {
        FileLogger::GetInstance().LogInfo("[HandleTouchInput] Preview center clicked - entering fullscreen");
        mState = STATE_FULLSCREEN_PREVIEW;
        return;
    }
    
    // 左箭头
    if (IsTouchInRect(touchX, touchY, previewX + 20, arrowY, arrowSize, arrowSize)) {
        FileLogger::GetInstance().LogInfo("[HandleTouchInput] Left arrow hit!");
        mPreviousPreview = mCurrentPreview;
        mCurrentPreview = (mCurrentPreview + 2) % 3;
        mSlideDirection = 1; // 向左
        mPreviewSlideAnim.SetImmediate(0.0f);
        mPreviewSlideAnim.SetTarget(1.0f, 350);
        FileLogger::GetInstance().LogInfo("Preview switched left to %d", mCurrentPreview);
        return;
    }
    
    // 右箭头
    if (IsTouchInRect(touchX, touchY, previewX + previewW - 70, arrowY, arrowSize, arrowSize)) {
        FileLogger::GetInstance().LogInfo("[HandleTouchInput] Right arrow hit!");
        mPreviousPreview = mCurrentPreview;
        mCurrentPreview = (mCurrentPreview + 1) % 3;
        mSlideDirection = -1; // 向右
        mPreviewSlideAnim.SetImmediate(0.0f);
        mPreviewSlideAnim.SetTarget(1.0f, 350);
        FileLogger::GetInstance().LogInfo("Preview switched right to %d", mCurrentPreview);
        return;
    }
    
    // 预览图指示器
    const int indicatorY = previewY + previewH - 50;
    const int indicatorW = 80;
    const int indicatorH = 8;
    const int indicatorSpacing = 15;
    const int totalIndicatorW = indicatorW * 3 + indicatorSpacing * 2;
    int indicatorX = previewX + (previewW - totalIndicatorW) / 2;
    
    for (int i = 0; i < 3; i++) {
        // 可点击区域稍大一 ?
        if (IsTouchInRect(touchX, touchY, indicatorX - 10, indicatorY - 10, indicatorW + 20, indicatorH + 20)) {
            if (i != mCurrentPreview) {
                mPreviousPreview = mCurrentPreview;
                mSlideDirection = (i > mCurrentPreview) ? -1 : 1;
                mCurrentPreview = i;
                mPreviewSlideAnim.SetImmediate(0.0f);
                mPreviewSlideAnim.SetTarget(1.0f, 350);
                FileLogger::GetInstance().LogInfo("Preview switched to %d", i);
            }
            return;
        }
        indicatorX += indicatorW + indicatorSpacing;
    }
}

bool ThemeDetailScreen::Update(Input &input) {
    // 检测返回按钮点击
    if (Screen::UpdateBackButton(input)) {
        return false;  // 返回上一级
    }
    
    // 保存输入状态用于调试显示
    mLastInput = input;
    
    // 全屏预览模式处理
    if (mState == STATE_FULLSCREEN_PREVIEW) {
        // 按B键或触摸退出全屏预览
        if (input.data.buttons_d & Input::BUTTON_B) {
            mState = STATE_VIEWING;
            return true;
        }
        
        // 支持左右键切换预览图
        if (input.data.buttons_d & Input::BUTTON_LEFT) {
            mFullscreenPrevPreview = mCurrentPreview;
            mCurrentPreview = (mCurrentPreview + 2) % 3;
            mFullscreenSlideDir = -1; // 向右滑动
            mFullscreenSlideAnim.Start(0.0f, 1.0f, 300); // 300ms动画
        } else if (input.data.buttons_d & Input::BUTTON_RIGHT) {
            mFullscreenPrevPreview = mCurrentPreview;
            mCurrentPreview = (mCurrentPreview + 1) % 3;
            mFullscreenSlideDir = 1; // 向左滑动
            mFullscreenSlideAnim.Start(0.0f, 1.0f, 300); // 300ms动画
        }
        
        // 触摸屏幕任意位置退出
        if (input.data.touched && !input.lastData.touched) {
            mState = STATE_VIEWING;
        }
        
        return true;
    }
    
    // 检查是否有卸载请求 (来自触摸)
    if (mUninstallRequested) {
        FileLogger::GetInstance().LogInfo("[UNINSTALL] Setting state to UNINSTALL_CONFIRM");
        mState = STATE_UNINSTALL_CONFIRM;
        mUninstallRequested = false;
    }
    
    // 如果正在卸载状态,直接同步执行卸载(不使用线程)
    if (mState == STATE_UNINSTALLING) {
        if (!mInstallThreadRunning.load()) {
            FileLogger::GetInstance().LogInfo("[UNINSTALL] Starting synchronous uninstall");
            
            bool success = UninstallTheme();
            FileLogger::GetInstance().LogInfo("[UNINSTALL] Uninstall completed with result: %d", success);
            
            if (success) {
                mState = STATE_UNINSTALL_COMPLETE;
                FileLogger::GetInstance().LogInfo("[UNINSTALL] Theme uninstalled successfully");
            } else {
                mState = STATE_UNINSTALL_COMPLETE;
                FileLogger::GetInstance().LogError("[UNINSTALL] Theme uninstall failed");
            }
        } else {
            FileLogger::GetInstance().LogWarning("[UNINSTALL] Install thread still running, waiting...");
        }
    }
    
    // 处理设置当前主题请求
    if (mState == STATE_SETTING_CURRENT) {
        FileLogger::GetInstance().LogInfo("[SET_CURRENT] Starting to set current theme");
        
        ThemePatcher patcher;
        bool success = patcher.SetCurrentTheme(mTheme->id);
        
        if (success) {
            mState = STATE_SET_CURRENT_COMPLETE;
            FileLogger::GetInstance().LogInfo("[SET_CURRENT] Theme set as current successfully");
            
            // 标记主题已更改（用于退出时软重启）
            Config::GetInstance().SetThemeChanged(true);
            FileLogger::GetInstance().LogInfo("[SET_CURRENT] Marked theme as changed for soft reboot on exit");
        } else {
            mState = STATE_SET_CURRENT_ERROR;
            FileLogger::GetInstance().LogError("[SET_CURRENT] Failed to set current theme");
        }
    }
    
    // 更新图片加载器 - 处理异步图片加载
    ImageLoader::Update();
    
    mTitleAnim.Update();
    mContentAnim.Update();
    mButtonHoverAnim.Update();
    mPreviewSwitchAnim.Update();
    mPreviewSlideAnim.Update();
    
    // 计算输入冷却（前30帧 = 0.5秒）
    const int INPUT_COOLDOWN_FRAMES = 30;
    bool inputAllowed = (mFrameCount - mEnterFrame) >= INPUT_COOLDOWN_FRAMES;
    
    // 滑动动画完成后重置方向
    if (mSlideDirection != 0 && mPreviewSlideAnim.GetValue() >= 1.0f) {
        mSlideDirection = 0;
    }
    
    // 下载和安装状态处理 - 即使在冷却期也要处理
    // 下载进度更新（检查 ThemeManager 中的下载器状态）
    if (mState == STATE_DOWNLOADING) {
        // 从 ThemeManager 获取实际下载进度
        mDownloadProgress = mThemeManager->GetDownloadProgress();
        
        // 检查下载是否完成或出错
        auto downloadState = mThemeManager->GetDownloadState();
        if (downloadState == DOWNLOAD_COMPLETE) {
            // 下载完成，开始安装
            mState = STATE_INSTALLING;
            mInstallProgress = 0.0f;
            
            // 异步安装主题
            std::string themeId = mTheme->id;
            std::string themeName = mTheme->name;
            std::string themeAuthor = mTheme->author;
            
            // 确保之前的安装线程已经结束
            if (mInstallThread.joinable()) {
                mInstallThread.join();
            }
            
            mInstallThreadRunning = true;
            mInstallThread = std::thread([this, themeId, themeName, themeAuthor]() {
                FileLogger::GetInstance().LogInfo("[INSTALL THREAD] Thread started");
                
                ThemePatcher patcher;
                
                // 获取解压后的主题文件夹路径
                std::string extractedPath = mThemeManager->GetExtractedPath();
                
                if (extractedPath.empty()) {
                    FileLogger::GetInstance().LogError("Extracted folder path is empty");
                    mInstallError = "Invalid theme path";
                    mErrorDisplayFrames = 0;
                    mState = STATE_INSTALL_ERROR;
                    mInstallThreadRunning = false;
                    FileLogger::GetInstance().LogInfo("[INSTALL THREAD] Thread exiting (empty path)");
                    return;
                }
                
                FileLogger::GetInstance().LogInfo("Installing theme from: %s", extractedPath.c_str());
                
                // 设置进度回调 - 使用 shared_ptr 或原子变量避免访问已销毁的对象
                // 注意: 不直接捕获 this,而是只访问原子变量
                patcher.SetProgressCallback([this](float progress, const std::string& message) {
                    // 只更新原子变量,不访问其他成员
                    mInstallProgress = progress;
                    // 日志输出是安全的
                    FileLogger::GetInstance().LogInfo("Install progress: %.1f%% - %s", progress * 100, message.c_str());
                });
                
                // 直接安装主题，不需要读取 metadata.json
                // 主题信息已经从 API 获取了
                FileLogger::GetInstance().LogInfo("[INSTALL THREAD] Calling InstallTheme");
                bool success = patcher.InstallTheme(extractedPath, themeId, themeName, themeAuthor);
                FileLogger::GetInstance().LogInfo("[INSTALL THREAD] InstallTheme returned: %d", success);
                
                if (success) {
                    FileLogger::GetInstance().LogInfo("Theme installed successfully: %s", themeName.c_str());
                    
                    // 更新 StyleMiiU 配置
                    if (patcher.SetCurrentTheme(themeId)) {
                        FileLogger::GetInstance().LogInfo("StyleMiiU config updated successfully");
                        
                        // 标记主题已更改（用于退出时软重启）
                        Config::GetInstance().SetThemeChanged(true);
                        FileLogger::GetInstance().LogInfo("[INSTALL] Marked theme as changed for soft reboot on exit");
                    } else {
                        FileLogger::GetInstance().LogWarning("Failed to update StyleMiiU config");
                    }
                    
                    mState = STATE_INSTALL_COMPLETE;
                } else {
                    FileLogger::GetInstance().LogError("Failed to install theme: %s", themeName.c_str());
                    mInstallError = "Installation failed";
                    mErrorDisplayFrames = 0;
                    mState = STATE_INSTALL_ERROR;
                }
                
                // 显式清理 patcher 对象
                FileLogger::GetInstance().LogInfo("[INSTALL THREAD] Cleaning up patcher");
                // patcher 会在这里自动析构
                
                mInstallThreadRunning = false;
                FileLogger::GetInstance().LogInfo("[INSTALL THREAD] Thread exiting");
            });
            
        } else if (downloadState == DOWNLOAD_ERROR || downloadState == DOWNLOAD_CANCELLED) {
            // 下载出错,显示错误信息
            if (downloadState == DOWNLOAD_ERROR) {
                mInstallError = "Download failed: " + mThemeManager->GetDownloadError();
                mErrorDisplayFrames = 0;
                mState = STATE_INSTALL_ERROR;
                FileLogger::GetInstance().LogError("Download failed: %s", mInstallError.c_str());
            } else {
                // 取消下载,直接返回浏览状态
                mState = STATE_VIEWING;
                mDownloadProgress = 0.0f;
            }
        }
        
        // 下载中允许按B取消
        if (input.data.buttons_d & Input::BUTTON_B) {
            mThemeManager->CancelDownload();
            mState = STATE_VIEWING;
            mDownloadProgress = 0.0f;
            return true;
        }
        return true;
    }
    
    // 安装中
    if (mState == STATE_INSTALLING) {
        // 禁止输入，只显示安装进度
        return true;
    }
    
    // 安装完成
    if (mState == STATE_INSTALL_COMPLETE) {
        if (input.data.buttons_d & (Input::BUTTON_A | Input::BUTTON_B)) {
            // 只有在线程真正完成时才允许退出
            if (!mInstallThreadRunning.load()) {
                // 确保线程已经join
                if (mInstallThread.joinable()) {
                    FileLogger::GetInstance().LogInfo("Joining install thread before exit...");
                    mInstallThread.join();
                }
                FileLogger::GetInstance().LogInfo("Install thread completed, returning to theme list");
                return false; // 返回主题列表
            } else {
                FileLogger::GetInstance().LogInfo("Install thread still running, please wait...");
            }
        }
        return true;
    }
    
    // 安装错误 - 自动3秒后关闭
    if (mState == STATE_INSTALL_ERROR) {
        mErrorDisplayFrames++;
        
        // 3秒后自动关闭 (60fps * 3 = 180帧)
        if (mErrorDisplayFrames >= 180 || 
            input.data.buttons_d & (Input::BUTTON_A | Input::BUTTON_B)) {
            // 只有在线程真正完成时才允许返回浏览
            if (!mInstallThreadRunning.load()) {
                // 确保线程已经join
                if (mInstallThread.joinable()) {
                    FileLogger::GetInstance().LogInfo("Joining install thread after error...");
                    mInstallThread.join();
                }
                mState = STATE_VIEWING;
                mInstallError.clear();
                mErrorDisplayFrames = 0;
            }
        }
        return true;
    }
    
    // 下载完成（保留用于兼容）
    if (mState == STATE_DOWNLOAD_COMPLETE) {
        if (input.data.buttons_d & (Input::BUTTON_A | Input::BUTTON_B)) {
            return false; // 返回主题列表
        }
        return true;
    }
    
    // 触摸滑动检测 - 参考MenuScreen实现，存储原始坐标
    if (input.data.touched && input.data.validPointer) {
        if (!mTouchStarted && !input.lastData.touched) {
            // 新触摸开始 - 存储原始1280x720坐标
            mTouchStarted = true;
            mTouchStartRawX = input.data.x;  // 存储原始坐标
            mTouchStartRawY = input.data.y;
            mTouchCurrentRawX = input.data.x;
            mTouchCurrentRawY = input.data.y;
            mTouchDragOffsetX = 0;
            mIsDragging = false;
            
            // 只用于日志显示的屏幕坐标
            float scaleX = 1920.0f / 1280.0f;
            float scaleY = 1080.0f / 720.0f;
            int screenX = (int)((input.data.x * scaleX) + 960);
            int screenY = (int)(540 - (input.data.y * scaleY));
            FileLogger::GetInstance().LogInfo("Touch started at screen(%d, %d) raw(%d, %d)", 
                                            screenX, screenY, input.data.x, input.data.y);
        } else if (mTouchStarted) {
            // 触摸移动 - 更新原始坐标并计算拖动偏移
            mTouchCurrentRawX = input.data.x;
            mTouchCurrentRawY = input.data.y;
            
            // 计算拖动偏移(屏幕坐标)
            float scaleX = 1920.0f / 1280.0f;
            int startScreenX = (int)((mTouchStartRawX * scaleX) + 960);
            int currentScreenX = (int)((mTouchCurrentRawX * scaleX) + 960);
            mTouchDragOffsetX = currentScreenX - startScreenX;
            
            // 只有水平滑动距离足够才算拖动
            if (abs(mTouchDragOffsetX) > 10) {
                mIsDragging = true;
            }
        }
    } else if (mTouchStarted) {
        // 触摸结束，判断是否为滑动手势
        // 转换为屏幕坐标用于计算
        float scaleX = 1920.0f / 1280.0f;
        float scaleY = 1080.0f / 720.0f;
        int startScreenX = (int)((mTouchStartRawX * scaleX) + 960);
        int startScreenY = (int)(540 - (mTouchStartRawY * scaleY));
        int endScreenX = (int)((mTouchCurrentRawX * scaleX) + 960);
        int endScreenY = (int)(540 - (mTouchCurrentRawY * scaleY));
        
        FileLogger::GetInstance().LogInfo("Touch ended at screen(%d, %d), started at screen(%d, %d), drag offset: %d", 
                                         endScreenX, endScreenY, startScreenX, startScreenY, mTouchDragOffsetX);
        
        if (inputAllowed) {
            int deltaX = endScreenX - startScreenX;
            int deltaY = endScreenY - startScreenY;
            
            // 预览区域(60, 60) 到 (1160, 678)
            const int SWIPE_THRESHOLD = 200; // 切换图片需要的最小滑动距离(增大到200)
            int previewX = 60;
            int previewW = 1100;
            bool inPreviewArea = (startScreenX >= previewX && startScreenX <= previewX + previewW);
            
            FileLogger::GetInstance().LogInfo("Delta: (%d, %d), inPreviewArea: %d", deltaX, deltaY, inPreviewArea);
            
            // 如果在预览区域且有拖动
            if (inPreviewArea && mIsDragging && abs(deltaX) > abs(deltaY)) {
                // 判断是否滑动距离足够切换图片
                if (abs(deltaX) > SWIPE_THRESHOLD) {
                    // 水平滑动足够远 - 切换图片
                    if (deltaX > 0) {
                        // 向右滑动 - 显示上一张
                        mPreviousPreview = mCurrentPreview;
                        mCurrentPreview = (mCurrentPreview + 2) % 3;
                        mSlideDirection = 1;
                        FileLogger::GetInstance().LogInfo("Swipe right detected - switching preview");
                    } else {
                        // 向左滑动 - 显示下一张
                        mPreviousPreview = mCurrentPreview;
                        mCurrentPreview = (mCurrentPreview + 1) % 3;
                        mSlideDirection = -1;
                        FileLogger::GetInstance().LogInfo("Swipe left detected - switching preview");
                    }
                    // 从当前拖动位置开始动画到目标位置
                    mPreviewSlideAnim.SetImmediate(0.0f);
                    mPreviewSlideAnim.SetTarget(1.0f, 250);
                } else {
                    // 滑动距离不够 - 回弹到原位
                    FileLogger::GetInstance().LogInfo("Swipe too short - bouncing back");
                    mSlideDirection = 0; // 不切换
                    mPreviewSlideAnim.SetImmediate(0.0f);
                    mPreviewSlideAnim.SetTarget(0.0f, 200); // 回弹动画
                }
                
                // 重置拖动偏移,让动画接管
                mTouchDragOffsetX = 0;
                mIsDragging = false;
            } else if (!mIsDragging) {
                // 如果不是滑动，当作点击处理 - 直接使用原始坐标
                FileLogger::GetInstance().LogInfo("Processing as tap at raw(%d, %d) screen(%d, %d)", 
                                                 mTouchCurrentRawX, mTouchCurrentRawY,
                                                 endScreenX, endScreenY);
                // 创建临时Input，使用原始坐标（无需转换！）
                Input tempInput = input;
                tempInput.data.x = mTouchCurrentRawX;
                tempInput.data.y = mTouchCurrentRawY;
                tempInput.data.touched = true;
                tempInput.lastData.touched = false;
                tempInput.data.validPointer = true;
                
                HandleTouchInput(tempInput);
            } else {
                // 在预览区域外拖动 - 直接取消
                mTouchDragOffsetX = 0;
                mIsDragging = false;
            }
        } else {
            FileLogger::GetInstance().LogInfo("Touch ignored (cooling period)");
            mTouchDragOffsetX = 0;
            mIsDragging = false;
        }
        
        // 无论是否在冷却期，都要重置触摸状态
        mTouchStarted = false;
    }
    
    // 输入冷却期内不处理按键输入
    if (!inputAllowed) {
        // 但允许按B键退出(紧急退出)
        if (input.data.buttons_d & Input::BUTTON_B) {
            FileLogger::GetInstance().LogInfo("Emergency exit during cooldown period");
            
            // 如果正在下载,先取消
            if (mState == STATE_DOWNLOADING) {
                mThemeManager->CancelDownload();
                mState = STATE_VIEWING;
            }
            
            // 如果正在安装,不允许退出
            if (mState == STATE_INSTALLING || mInstallThreadRunning.load()) {
                FileLogger::GetInstance().LogInfo("Installation in progress, cannot exit");
                return true;
            }
            
            return false;
        }
        return true;
    }
    
    // 预览切换（带滑动动画 ? 按键控制
    if (input.data.buttons_d & Input::BUTTON_LEFT) {
        mPreviousPreview = mCurrentPreview;
        mCurrentPreview = (mCurrentPreview + 2) % 3;
        mSlideDirection = 1; // 向左 ?
        mPreviewSlideAnim.SetImmediate(0.0f);
        mPreviewSlideAnim.SetTarget(1.0f, 350);
    } else if (input.data.buttons_d & Input::BUTTON_RIGHT) {
        mPreviousPreview = mCurrentPreview;
        mCurrentPreview = (mCurrentPreview + 1) % 3;
        mSlideDirection = -1; // 向右 ?
        mPreviewSlideAnim.SetImmediate(0.0f);
        mPreviewSlideAnim.SetTarget(1.0f, 350);
    }
    
    // A键操作（下载、卸载或确认）
    if (input.data.buttons_d & Input::BUTTON_A) {
        bool isLocalMode = (mThemeManager == nullptr);
        int currentState = mState.load();
        FileLogger::GetInstance().LogInfo("[INPUT] A button pressed, isLocalMode=%d, state=%d", isLocalMode, currentState);
        
        // 如果在设置当前主题确认对话框中按A,执行设置
        if (mState == STATE_SET_CURRENT_CONFIRM) {
            FileLogger::GetInstance().LogInfo("[SET_CURRENT] Confirmed, transitioning to SETTING_CURRENT");
            mState = STATE_SETTING_CURRENT;
            // 不要立即return,让下面的设置逻辑执行
        }
        
        // 如果在设置完成界面按A,返回
        else if (mState == STATE_SET_CURRENT_COMPLETE) {
            FileLogger::GetInstance().LogInfo("[SET_CURRENT] Complete screen, returning");
            mState = STATE_VIEWING;
            return true;
        }
        
        // 如果在设置失败界面按A,返回
        else if (mState == STATE_SET_CURRENT_ERROR) {
            FileLogger::GetInstance().LogInfo("[SET_CURRENT] Error screen, returning");
            mState = STATE_VIEWING;
            return true;
        }
        
        // 如果在确认对话框中按A,执行卸载
        else if (mState == STATE_UNINSTALL_CONFIRM) {
            FileLogger::GetInstance().LogInfo("[UNINSTALL] Confirmed, transitioning to UNINSTALLING");
            mState = STATE_UNINSTALLING;
            FileLogger::GetInstance().LogInfo("[UNINSTALL] State changed, continuing execution");
            // 不要立即return,让下面的卸载逻辑在同一帧执行
        }
        
        // 如果在卸载完成界面按A,返回
        else if (mState == STATE_UNINSTALL_COMPLETE) {
            FileLogger::GetInstance().LogInfo("[UNINSTALL] Complete screen, returning");
            return false;
        }
        
        // 正常浏览状态
        if (mState == STATE_VIEWING) {
            if (isLocalMode) {
                // 本地模式: 显示卸载确认对话框
                FileLogger::GetInstance().LogInfo("Showing uninstall confirmation for: %s", mTheme->name.c_str());
                mState = STATE_UNINSTALL_CONFIRM;
                return true;
            } else {
                // 网络模式: 下载主题
                mState = STATE_DOWNLOADING;
                mDownloadStartFrame = mFrameCount;
                mThemeManager->DownloadTheme(*mTheme); // 启动异步下载
                return true;
            }
        }
    }
    
    // Y键设置为当前主题（仅在本地模式下）
    if (input.data.buttons_d & Input::BUTTON_Y) {
        bool isLocalMode = (mThemeManager == nullptr);
        
        // 只在本地模式且浏览状态下允许设置当前主题
        if (isLocalMode && mState == STATE_VIEWING) {
            FileLogger::GetInstance().LogInfo("Showing set current theme confirmation for: %s", mTheme->name.c_str());
            mState = STATE_SET_CURRENT_CONFIRM;
            return true;
        }
    }
    
    // B键返回或取消
    if (input.data.buttons_d & Input::BUTTON_B) {
        // 如果在设置当前主题确认对话框中按B,取消
        if (mState == STATE_SET_CURRENT_CONFIRM) {
            mState = STATE_VIEWING;
            return true;
        }
        
        // 如果在设置完成界面按B,返回浏览状态
        if (mState == STATE_SET_CURRENT_COMPLETE) {
            mState = STATE_VIEWING;
            return true;
        }
        
        // 如果在设置失败界面按B,返回浏览状态
        if (mState == STATE_SET_CURRENT_ERROR) {
            mState = STATE_VIEWING;
            return true;
        }
        
        // 如果在确认对话框中按B,取消
        if (mState == STATE_UNINSTALL_CONFIRM) {
            mState = STATE_VIEWING;
            return true;
        }
        
        // 如果在卸载完成界面按B,返回
        if (mState == STATE_UNINSTALL_COMPLETE) {
            return false;
        }
        
        // 如果正在下载,先取消
        if (mState == STATE_DOWNLOADING) {
            FileLogger::GetInstance().LogInfo("Cancelling download before exit...");
            mThemeManager->CancelDownload();
            mState = STATE_VIEWING;
        }
        
        // 如果正在安装,不允许退出
        if (mState == STATE_INSTALLING) {
            FileLogger::GetInstance().LogInfo("Installation in progress, cannot exit");
            return true;
        }
        
        // 如果安装线程还在运行,等待完成
        if (mInstallThreadRunning.load()) {
            FileLogger::GetInstance().LogInfo("Install thread still running, waiting...");
            return true;
        }
        
        FileLogger::GetInstance().LogInfo("[UPDATE] B pressed, returning false (exit)");
        return false;
    }
    
    return true;
}

// 全屏预览绘制
void ThemeDetailScreen::DrawFullscreenPreview() {
    // 纯黑背景
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, {0, 0, 0, 255});
    
    // 辅助函数:根据索引获取预览图纹理
    auto getPreviewTexture = [this](int index) -> SDL_Texture* {
        switch (index) {
            case 0:
                return mTheme->collagePreview.hdTexture ? 
                       mTheme->collagePreview.hdTexture : 
                       mTheme->collagePreview.thumbTexture;
            case 1:
                return mTheme->launcherScreenshot.hdTexture ? 
                       mTheme->launcherScreenshot.hdTexture : 
                       mTheme->launcherScreenshot.thumbTexture;
            case 2:
                return mTheme->waraWaraScreenshot.hdTexture ? 
                       mTheme->waraWaraScreenshot.hdTexture : 
                       mTheme->waraWaraScreenshot.thumbTexture;
            default:
                return nullptr;
        }
    };
    
    // 辅助函数:绘制纹理到指定位置(保持比例,居中)
    auto drawTexture = [](SDL_Texture* texture, int offsetX, int alpha) {
        if (!texture) return;
        
        int texW, texH;
        SDL_QueryTexture(texture, nullptr, nullptr, &texW, &texH);
        
        // 适应整个屏幕(保持比例)
        float scaleW = (float)Gfx::SCREEN_WIDTH / texW;
        float scaleH = (float)Gfx::SCREEN_HEIGHT / texH;
        float scale = std::min(scaleW, scaleH); // 保持比例,不裁剪
        
        int scaledW = (int)(texW * scale);
        int scaledH = (int)(texH * scale);
        
        // 居中显示 + 偏移
        SDL_Rect dstRect;
        dstRect.x = (Gfx::SCREEN_WIDTH - scaledW) / 2 + offsetX;
        dstRect.y = (Gfx::SCREEN_HEIGHT - scaledH) / 2;
        dstRect.w = scaledW;
        dstRect.h = scaledH;
        
        // 设置透明度
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_RenderCopy(Gfx::GetRenderer(), texture, nullptr, &dstRect);
        SDL_SetTextureAlphaMod(texture, 255); // 恢复
    };
    
    // 获取动画进度
    float slideProgress = mFullscreenSlideAnim.GetValue();
    
    // 如果正在播放动画,同时绘制两张图片
    if (slideProgress < 1.0f && mFullscreenSlideDir != 0) {
        // 计算滑动偏移量
        int slideOffset = (int)(Gfx::SCREEN_WIDTH * slideProgress * mFullscreenSlideDir);
        
        // 绘制上一张图片(滑出)
        SDL_Texture* prevTexture = getPreviewTexture(mFullscreenPrevPreview);
        int prevAlpha = (int)(255 * (1.0f - slideProgress)); // 淡出
        drawTexture(prevTexture, slideOffset, prevAlpha);
        
        // 绘制当前图片(滑入)
        SDL_Texture* currTexture = getPreviewTexture(mCurrentPreview);
        int currOffset = slideOffset - (Gfx::SCREEN_WIDTH * mFullscreenSlideDir);
        int currAlpha = (int)(255 * slideProgress); // 淡入
        drawTexture(currTexture, currOffset, currAlpha);
    } else {
        // 没有动画,直接绘制当前图片
        SDL_Texture* texture = getPreviewTexture(mCurrentPreview);
        drawTexture(texture, 0, 255);
    }
    
    // 底部提示文字(半透明背景)
    const int tipHeight = 80;
    SDL_Color tipBg = {0, 0, 0, 180};
    Gfx::DrawRectFilled(0, Gfx::SCREEN_HEIGHT - tipHeight, Gfx::SCREEN_WIDTH, tipHeight, tipBg);
    
    // 获取当前预览图名称(多语言)
    const char* previewKeys[] = {
        "theme_detail.preview_collage",
        "theme_detail.preview_launcher", 
        "theme_detail.preview_wara_wara"
    };
    std::string previewName = _(previewKeys[mCurrentPreview]);
    
    // 获取操作提示(多语言)
    std::string hintSwitch = _("theme_detail.fullscreen_hint_switch");
    std::string hintExit = _("theme_detail.fullscreen_hint_exit");
    
    // 替换箭头占位符
    size_t pos = hintSwitch.find("<Arrow>");
    if (pos != std::string::npos) {
        hintSwitch.replace(pos, 7, "\ue07e");
    }
    
    // 左侧显示预览图名称(固定位置,避免跳动)
    const int leftMargin = 80;
    Gfx::Print(leftMargin, Gfx::SCREEN_HEIGHT - 40, 32, 
               Gfx::COLOR_TEXT, previewName.c_str(), Gfx::ALIGN_LEFT);
    
    // 右侧显示操作提示(固定位置)
    const int rightMargin = 80;
    std::string hints = hintSwitch + "  |  " + hintExit;
    Gfx::Print(Gfx::SCREEN_WIDTH - rightMargin, Gfx::SCREEN_HEIGHT - 40, 28, 
               Gfx::COLOR_TEXT, hints.c_str(), Gfx::ALIGN_RIGHT);
}
