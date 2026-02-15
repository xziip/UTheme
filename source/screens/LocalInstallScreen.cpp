#include "LocalInstallScreen.hpp"
#include "Gfx.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/FileLogger.hpp"
#include "../utils/ThemePatcher.hpp"
#include "../utils/Utils.hpp"
#include "../utils/minizip/unzip.h"
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>

LocalInstallScreen::LocalInstallScreen() {
    mTitleAnim.Start(0, 1, 500);
    mContentAnim.Start(0, 1, 600);
    mListAnim.Start(0, 1, 700);
    
    FileLogger::GetInstance().LogInfo("LocalInstallScreen: Starting file scan");
    
    // 异步扫描.utheme文件
    std::thread([this]() {
        ScanThemeFiles();
        
        if (mThemeFiles.empty()) {
            mState = STATE_EMPTY;
        } else {
            mState = STATE_FILE_LIST;
            InitAnimations();
        }
    }).detach();
}

LocalInstallScreen::~LocalInstallScreen() {
    // 等待安装线程完成
    if (mInstallThreadRunning.load()) {
        FileLogger::GetInstance().LogInfo("LocalInstallScreen: Waiting for install thread to finish");
        if (mInstallThread.joinable()) {
            mInstallThread.join();
        }
    }
    
    FileLogger::GetInstance().LogInfo("LocalInstallScreen: Destructor completed");
}

void LocalInstallScreen::ScanThemeFiles() {
    mThemeFiles.clear();
    
    const char* searchPath = "fs:/vol/external01/wiiu/themes";
    FileLogger::GetInstance().LogInfo("Scanning for .utheme files in: %s", searchPath);
    
    DIR* dir = opendir(searchPath);
    if (!dir) {
        FileLogger::GetInstance().LogError("Failed to open directory: %s", searchPath);
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // 只处理普通文件
        if (entry->d_type != DT_REG) continue;
        
        // 检查是否是.utheme文件
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".utheme") != 0) continue;
        
        // 构建完整路径
        std::string fullPath = std::string(searchPath) + "/" + entry->d_name;
        
        // 获取文件大小
        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) {
            FileLogger::GetInstance().LogWarning("Failed to stat file: %s", fullPath.c_str());
            continue;
        }
        
        // 创建文件记录
        UThemeFile file;
        file.fileName = entry->d_name;
        file.fullPath = fullPath;
        file.fileSize = st.st_size;
        file.fileSizeStr = FormatFileSize(st.st_size);
        
        // 提取显示名称(去掉.utheme后缀)
        file.displayName = file.fileName.substr(0, file.fileName.length() - 7);
        
        mThemeFiles.push_back(file);
        FileLogger::GetInstance().LogInfo("Found .utheme file: %s (%s)", 
            file.fileName.c_str(), file.fileSizeStr.c_str());
    }
    
    closedir(dir);
    
    // 按文件名排序
    std::sort(mThemeFiles.begin(), mThemeFiles.end(), 
        [](const UThemeFile& a, const UThemeFile& b) {
            return a.fileName < b.fileName;
        });
    
    FileLogger::GetInstance().LogInfo("Found %zu .utheme files", mThemeFiles.size());
}

void LocalInstallScreen::InitAnimations() {
    mItemAnims.clear();
    mItemAnims.resize(mThemeFiles.size());
    
    // 初始化所有项目的动画
    for (size_t i = 0; i < mThemeFiles.size(); i++) {
        mItemAnims[i].scaleAnim.SetImmediate(1.0f);
        mItemAnims[i].highlightAnim.SetImmediate(0.0f);
    }
    
    // 选中第一个项目的动画 - 缩小放大比例,快速动画
    if (mThemeFiles.size() > 0) {
        mItemAnims[0].scaleAnim.SetTarget(1.02f, 350);
        mItemAnims[0].highlightAnim.SetTarget(1.0f, 350);
    }
}

void LocalInstallScreen::UpdateAnimations() {
    for (auto& anim : mItemAnims) {
        anim.scaleAnim.Update();
        anim.highlightAnim.Update();
    }
}

std::string LocalInstallScreen::FormatFileSize(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIndex = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unitIndex < 3) {
        size /= 1024.0;
        unitIndex++;
    }
    
    char buffer[32];
    if (unitIndex == 0) {
        snprintf(buffer, sizeof(buffer), "%.0f %s", size, units[unitIndex]);
    } else {
        snprintf(buffer, sizeof(buffer), "%.2f %s", size, units[unitIndex]);
    }
    
    return std::string(buffer);
}

void LocalInstallScreen::Draw() {
    mFrameCount++;
    
    // 更新动画
    mTitleAnim.Update();
    mContentAnim.Update();
    mListAnim.Update();
    UpdateAnimations();
    
    // 渐变背景
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);
    
    // 标题
    float titleAlpha = mTitleAnim.GetValue();
    SDL_Color titleColor = Gfx::COLOR_TEXT;
    titleColor.a = (uint8_t)(255 * titleAlpha);
    
    const std::string title = _("local_install.title");
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, 80, 52, titleColor, 
               title.c_str(), Gfx::ALIGN_CENTER);
    
    // 根据状态绘制内容
    switch (mState.load()) {
        case STATE_LOADING:
            {
                const std::string loadingText = _("local_install.scanning");
                Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 36, 
                           Gfx::COLOR_TEXT, loadingText.c_str(), Gfx::ALIGN_CENTER);
                
                // 旋转图标
                double angle = (mFrameCount % 60) * 6.0;
                Gfx::DrawIcon(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 + 80, 60, 
                              Gfx::COLOR_ACCENT, 0xf021, Gfx::ALIGN_CENTER, angle);
            }
            break;
            
        case STATE_FILE_LIST:
            DrawFileList();
            break;
            
        case STATE_CONFIRM_INSTALL:
            DrawFileList();
            DrawConfirmDialog();
            break;
            
        case STATE_INSTALLING:
            DrawInstallProgress();
            break;
            
        case STATE_INSTALL_COMPLETE:
        case STATE_INSTALL_ERROR:
            DrawInstallResult();
            break;
            
        case STATE_EMPTY:
            DrawEmptyState();
            break;
    }
    
    // 底部提示
    if (mState == STATE_FILE_LIST) {
        const std::string hint = _("local_install.hints");
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT - 40, 28, 
                   Gfx::COLOR_ALT_TEXT, hint.c_str(), Gfx::ALIGN_CENTER);
    }
    
    // 绘制圆形返回按钮
    Screen::DrawBackButton();
}

void LocalInstallScreen::DrawFileList() {
    float listAlpha = mListAnim.GetValue();
    
    // 列表容器
    const int listY = 160;
    const int listHeight = ITEMS_PER_PAGE * ITEM_HEIGHT;
    
    // 副标题
    std::string subtitle = _("local_install.found_files");
    char countStr[32];
    snprintf(countStr, sizeof(countStr), "%zu", mThemeFiles.size());
    size_t pos = subtitle.find("{count}");
    if (pos != std::string::npos) {
        subtitle.replace(pos, 7, countStr);
    }
    
    SDL_Color subtitleColor = Gfx::COLOR_ALT_TEXT;
    subtitleColor.a = (uint8_t)(255 * listAlpha);
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, 130, 28, subtitleColor, 
               subtitle.c_str(), Gfx::ALIGN_CENTER);
    
    // 绘制可见的文件项
    int visibleStart = mScrollOffset;
    int visibleEnd = std::min(visibleStart + ITEMS_PER_PAGE, (int)mThemeFiles.size());
    
    for (int i = visibleStart; i < visibleEnd; i++) {
        const auto& file = mThemeFiles[i];
        int itemY = listY + (i - visibleStart) * ITEM_HEIGHT;
        
        // 是否选中
        bool isSelected = (i == mSelectedIndex);
        
        // 从动画获取缩放和高亮值
        float scale = mItemAnims[i].scaleAnim.GetValue();
        float highlight = mItemAnims[i].highlightAnim.GetValue();
        
        // 背景卡片 - 使用圆角
        SDL_Color bgColor = isSelected ? Gfx::COLOR_ACCENT : SDL_Color{40, 40, 50, 200};
        bgColor.a = (uint8_t)(bgColor.a * listAlpha);
        
        const int cardX = 60;  // 减小左边距,让卡片更宽
        const int cardW = Gfx::SCREEN_WIDTH - 120;  // 增加卡片宽度
        const int cardH = ITEM_HEIGHT - 10;
        const int cardRadius = 16;  // 圆角半径
        
        // 应用缩放
        int scaledW = (int)(cardW * scale);
        int scaledH = (int)(cardH * scale);
        int scaledX = cardX - (scaledW - cardW) / 2;
        int scaledY = itemY - (scaledH - cardH) / 2;
        
        // 阴影效果(选中时)
        if (isSelected) {
            SDL_Color shadowColor = {0, 0, 0, (uint8_t)(100 * listAlpha)};
            Gfx::DrawRectRounded(scaledX + 6, scaledY + 6, scaledW, scaledH, cardRadius, shadowColor);
        }
        
        Gfx::DrawRectRounded(scaledX, scaledY, scaledW, scaledH, cardRadius, bgColor);
        
        // 边框 - 选中时高亮,未选中时显示淡边框
        if (isSelected && highlight > 0.01f) {
            // 选中高亮边框(使用动画值)
            SDL_Color borderColor = Gfx::COLOR_ACCENT;
            borderColor.a = (uint8_t)(200 * highlight * listAlpha);
            Gfx::DrawRectRounded(scaledX - 2, scaledY - 2, scaledW + 4, scaledH + 4, cardRadius + 2, borderColor);
            Gfx::DrawRectRounded(scaledX, scaledY, scaledW, scaledH, cardRadius, bgColor);
        } else if (!isSelected) {
            // 未选中项显示淡边框
            SDL_Color borderColor = {80, 80, 90, (uint8_t)(150 * listAlpha)};
            Gfx::DrawRectRounded(scaledX - 1, scaledY - 1, scaledW + 2, scaledH + 2, cardRadius + 1, borderColor);
            Gfx::DrawRectRounded(scaledX, scaledY, scaledW, scaledH, cardRadius, bgColor);
        }
        
        // 文件图标
        SDL_Color iconColor = isSelected ? Gfx::COLOR_TEXT : Gfx::COLOR_ACCENT;
        iconColor.a = (uint8_t)(255 * listAlpha);
        Gfx::DrawIcon(scaledX + 50, scaledY + scaledH / 2, 40, iconColor, 
                      0xf1c6, Gfx::ALIGN_CENTER); // file-archive icon
        
        // 文件名(左侧,垂直居中)
        SDL_Color nameColor = Gfx::COLOR_TEXT;
        nameColor.a = (uint8_t)(255 * listAlpha);
        Gfx::Print(scaledX + 120, scaledY + scaledH / 2, 32, nameColor, 
                   file.displayName.c_str(), Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
        
        // 文件大小(右侧,垂直居中)
        SDL_Color sizeColor = Gfx::COLOR_ALT_TEXT;
        sizeColor.a = (uint8_t)(255 * listAlpha);
        Gfx::Print(scaledX + scaledW - 120, scaledY + scaledH / 2, 28, sizeColor, 
                   file.fileSizeStr.c_str(), Gfx::ALIGN_RIGHT | Gfx::ALIGN_VERTICAL);
        
        // 选中指示器
        if (isSelected) {
            Gfx::DrawIcon(scaledX + scaledW - 40, scaledY + scaledH / 2, 30, 
                          Gfx::COLOR_TEXT, 0xf054, Gfx::ALIGN_CENTER); // chevron-right
        }
    }
    
    // 滚动指示器
    if (mThemeFiles.size() > ITEMS_PER_PAGE) {
        const int scrollBarX = Gfx::SCREEN_WIDTH - 50;
        const int scrollBarY = listY;
        const int scrollBarH = listHeight;
        const int scrollBarW = 8;
        
        // 滚动条背景
        SDL_Color scrollBg = {80, 80, 90, (uint8_t)(150 * listAlpha)};
        Gfx::DrawRectFilled(scrollBarX, scrollBarY, scrollBarW, scrollBarH, scrollBg);
        
        // 滚动条滑块
        float scrollRatio = (float)mScrollOffset / (mThemeFiles.size() - ITEMS_PER_PAGE);
        int thumbH = std::max(30, scrollBarH * ITEMS_PER_PAGE / (int)mThemeFiles.size());
        int thumbY = scrollBarY + (int)((scrollBarH - thumbH) * scrollRatio);
        
        SDL_Color scrollThumb = Gfx::COLOR_ACCENT;
        scrollThumb.a = (uint8_t)(200 * listAlpha);
        Gfx::DrawRectFilled(scrollBarX, thumbY, scrollBarW, thumbH, scrollThumb);
    }
}

void LocalInstallScreen::DrawConfirmDialog() {
    // 半透明遮罩
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, {0, 0, 0, 180});
    
    // 对话框 - 使用圆角
    const int dialogW = 900;
    const int dialogH = 500;
    const int dialogX = (Gfx::SCREEN_WIDTH - dialogW) / 2;
    const int dialogY = (Gfx::SCREEN_HEIGHT - dialogH) / 2;
    const int dialogRadius = 24;  // 圆角半径
    
    // 阴影效果
    SDL_Color shadowColor = {0, 0, 0, 100};
    Gfx::DrawRectRounded(dialogX + 8, dialogY + 8, dialogW, dialogH, dialogRadius, shadowColor);
    
    // 对话框背景
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, dialogRadius, {30, 30, 40, 255});
    
    // 对话框边框
    SDL_Color borderColor = Gfx::COLOR_ACCENT;
    borderColor.a = 150;
    Gfx::DrawRectRounded(dialogX - 2, dialogY - 2, dialogW + 4, dialogH + 4, dialogRadius + 2, borderColor);
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, dialogRadius, {30, 30, 40, 255});
    
    // 标题
    const std::string title = _("local_install.confirm_title");
    Gfx::Print(dialogX + dialogW / 2, dialogY + 70, 44, Gfx::COLOR_TEXT, 
               title.c_str(), Gfx::ALIGN_CENTER);
    
    // 图标
    Gfx::DrawIcon(dialogX + dialogW / 2, dialogY + 180, 80, Gfx::COLOR_ACCENT, 
                  0xf1c6, Gfx::ALIGN_CENTER); // file-archive icon
    
    // 文件名
    if (mSelectedIndex >= 0 && mSelectedIndex < (int)mThemeFiles.size()) {
        const auto& file = mThemeFiles[mSelectedIndex];
        Gfx::Print(dialogX + dialogW / 2, dialogY + 280, 32, Gfx::COLOR_TEXT, 
                   file.displayName.c_str(), Gfx::ALIGN_CENTER);
    }
    
    // 删除选项 - 完全居中显示,字体加大,右移80px
    const std::string deleteOption = _("local_install.delete_after_install");
    SDL_Color deleteColor = mDeleteAfterInstall ? Gfx::COLOR_ACCENT : Gfx::COLOR_ALT_TEXT;
    
    const int optionY = dialogY + 340;  // 选项区域的Y坐标(统一位置)
    const int checkboxSize = 36;
    const int checkboxRadius = 8;  // 复选框圆角
    
    // 计算文字宽度以实现完全居中,然后整体右移80px
    const int textWidth = deleteOption.length() * 18;
    const int totalWidth = checkboxSize + 15 + textWidth;  // 复选框 + 间距 + 文字
    const int centerX = dialogX + (dialogW / 2);  // 对话框中心
    const int startX = centerX - (totalWidth / 2) + 80;  // 居中后右移80px
    
    const int checkboxX = startX;
    const int checkboxY = optionY - checkboxSize / 2;
    
    // 复选框 - 使用圆角
    Gfx::DrawRectRounded(checkboxX, checkboxY, checkboxSize, checkboxSize, checkboxRadius, {50, 50, 60, 255});
    Gfx::DrawRectRounded(checkboxX - 2, checkboxY - 2, checkboxSize + 4, checkboxSize + 4, checkboxRadius + 1, deleteColor);
    Gfx::DrawRectRounded(checkboxX, checkboxY, checkboxSize, checkboxSize, checkboxRadius, {50, 50, 60, 255});
    
    if (mDeleteAfterInstall) {
        Gfx::DrawIcon(checkboxX + checkboxSize / 2, checkboxY + checkboxSize / 2, 28, 
                      deleteColor, 0xf00c, Gfx::ALIGN_CENTER); // checkmark
    }
    
    // 文字 - 垂直居中对齐,字体加大
    Gfx::Print(checkboxX + checkboxSize + 15, optionY, 32, deleteColor, 
               deleteOption.c_str(), Gfx::ALIGN_LEFT | Gfx::ALIGN_VERTICAL);
    
    // Install 按钮
    const int btnW = 400;
    const int btnH = 70;
    const int btnX = dialogX + (dialogW - btnW) / 2;
    const int btnY = dialogY + 400;  // 统一按钮位置
    const int btnRadius = 16;
    
    SDL_Color btnBgColor = Gfx::COLOR_ACCENT;
    SDL_Color btnBorderColor = Gfx::COLOR_ACCENT;
    btnBorderColor.a = 200;
    
    // 按钮阴影
    Gfx::DrawRectRounded(btnX + 4, btnY + 4, btnW, btnH, btnRadius, {0, 0, 0, 80});
    
    // 按钮背景
    Gfx::DrawRectRounded(btnX, btnY, btnW, btnH, btnRadius, btnBgColor);
    
    // 按钮图标
    Gfx::DrawIcon(btnX + btnW / 2 - 80, btnY + btnH / 2, 40, 
                  Gfx::COLOR_WHITE, 0xf019, Gfx::ALIGN_CENTER); // download icon
    
    // 按钮文字
    const std::string installText = _("local_install.install");
    Gfx::Print(btnX + btnW / 2 + 30, btnY + btnH / 2, 36, 
               Gfx::COLOR_WHITE, installText.c_str(), Gfx::ALIGN_CENTER);
    
}

void LocalInstallScreen::DrawInstallProgress() {
    // 半透明遮罩
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, {0, 0, 0, 180});
    
    // 对话框 - 使用圆角,统一大小为900x500
    const int dialogW = 900;
    const int dialogH = 500;
    const int dialogX = (Gfx::SCREEN_WIDTH - dialogW) / 2;
    const int dialogY = (Gfx::SCREEN_HEIGHT - dialogH) / 2;
    const int dialogRadius = 24;
    
    // 阴影效果
    SDL_Color shadowColor = {0, 0, 0, 100};
    Gfx::DrawRectRounded(dialogX + 8, dialogY + 8, dialogW, dialogH, dialogRadius, shadowColor);
    
    // 对话框背景
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, dialogRadius, {30, 30, 40, 255});
    
    // 对话框边框
    SDL_Color borderColor = Gfx::COLOR_ACCENT;
    borderColor.a = 150;
    Gfx::DrawRectRounded(dialogX - 2, dialogY - 2, dialogW + 4, dialogH + 4, dialogRadius + 2, borderColor);
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, dialogRadius, {30, 30, 40, 255});
    
    // 标题
    const std::string title = _("local_install.installing");
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, dialogY + 70, 44, Gfx::COLOR_TEXT, 
               title.c_str(), Gfx::ALIGN_CENTER);
    
    // 旋转图标
    double angle = (mFrameCount % 60) * 6.0;
    Gfx::DrawIcon(Gfx::SCREEN_WIDTH / 2, dialogY + 180, 80, Gfx::COLOR_ACCENT, 
                  0xf021, Gfx::ALIGN_CENTER, angle); // sync icon
    
    // 文件名
    if (!mInstalledThemeName.empty()) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, dialogY + 280, 32, Gfx::COLOR_TEXT, 
                   mInstalledThemeName.c_str(), Gfx::ALIGN_CENTER);
    }
    
    // 进度条 - 使用圆角
    float progress = mInstallProgress.load();
    const int barW = 700;
    const int barH = 40;
    const int barX = (Gfx::SCREEN_WIDTH - barW) / 2;
    const int barY = dialogY + 340;
    const int barRadius = 20;  // 圆角半径(半圆形)
    
    // 进度条背景(圆角)
    Gfx::DrawRectRounded(barX, barY, barW, barH, barRadius, {50, 50, 60, 255});
    
    // 进度条填充(圆角)
    if (progress > 0.0f) {
        int fillW = (int)(barW * progress);
        if (fillW > 0) {
            // 确保填充部分也是圆角的
            int fillRadius = (fillW < barRadius * 2) ? fillW / 2 : barRadius;
            Gfx::DrawRectRounded(barX, barY, fillW, barH, fillRadius, Gfx::COLOR_ACCENT);
        }
    }
    
    // 百分比
    char percentStr[16];
    snprintf(percentStr, sizeof(percentStr), "%.0f%%", progress * 100);
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, barY + barH / 2, 28, 
               Gfx::COLOR_TEXT, percentStr, Gfx::ALIGN_CENTER | Gfx::ALIGN_VERTICAL);
}

void LocalInstallScreen::DrawInstallResult() {
    bool isSuccess = (mState == STATE_INSTALL_COMPLETE);
    
    // 半透明遮罩
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, {0, 0, 0, 180});
    
    // 对话框 - 使用圆角,统一大小为900x500
    const int dialogW = 900;
    const int dialogH = 500;
    const int dialogX = (Gfx::SCREEN_WIDTH - dialogW) / 2;
    const int dialogY = (Gfx::SCREEN_HEIGHT - dialogH) / 2;
    const int dialogRadius = 24;
    
    // 阴影效果
    SDL_Color shadowColor = {0, 0, 0, 100};
    Gfx::DrawRectRounded(dialogX + 8, dialogY + 8, dialogW, dialogH, dialogRadius, shadowColor);
    
    // 对话框背景
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, dialogRadius, {30, 30, 40, 255});
    
    // 对话框边框 (根据成功/失败显示不同颜色)
    SDL_Color borderColor = isSuccess ? Gfx::COLOR_SUCCESS : Gfx::COLOR_ERROR;
    borderColor.a = 150;
    Gfx::DrawRectRounded(dialogX - 2, dialogY - 2, dialogW + 4, dialogH + 4, dialogRadius + 2, borderColor);
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, dialogRadius, {30, 30, 40, 255});
    
    // 标题
    const std::string title = isSuccess ? 
        _("local_install.install_complete") : 
        _("local_install.install_error");
    
    SDL_Color titleColor = isSuccess ? Gfx::COLOR_SUCCESS : Gfx::COLOR_ERROR;
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, dialogY + 70, 44, titleColor, 
               title.c_str(), Gfx::ALIGN_CENTER);
    
    // 图标
    int icon = isSuccess ? 0xf00c : 0xf071; // checkmark : warning
    Gfx::DrawIcon(Gfx::SCREEN_WIDTH / 2, dialogY + 180, 80, titleColor, 
                  icon, Gfx::ALIGN_CENTER);
    
    // 主题名称或错误信息
    if (isSuccess && !mInstalledThemeName.empty()) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, dialogY + 280, 32, Gfx::COLOR_TEXT, 
                   mInstalledThemeName.c_str(), Gfx::ALIGN_CENTER);
    } else if (!isSuccess && !mInstallError.empty()) {
        Gfx::Print(Gfx::SCREEN_WIDTH / 2, dialogY + 280, 28, Gfx::COLOR_ALT_TEXT, 
                   mInstallError.c_str(), Gfx::ALIGN_CENTER);
    }
    
    // 提示
    const std::string hint = _("common.back");
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, dialogY + 400, 28, 
               Gfx::COLOR_ALT_TEXT, ("A/B: " + hint).c_str(), Gfx::ALIGN_CENTER);
}

void LocalInstallScreen::DrawEmptyState() {
    // 图标
    Gfx::DrawIcon(Gfx::SCREEN_WIDTH / 2, 300, 100, 
                  Gfx::COLOR_ALT_TEXT, 0xf15c, Gfx::ALIGN_CENTER); // file icon
    
    // 消息
    const std::string message = _("local_install.no_files");
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, 430, 36, Gfx::COLOR_TEXT, 
               message.c_str(), Gfx::ALIGN_CENTER);
    
    // 路径提示
    const std::string pathHint = _("local_install.path_hint");
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, 490, 28, Gfx::COLOR_ALT_TEXT, 
               pathHint.c_str(), Gfx::ALIGN_CENTER);
    
    // 返回提示
    const std::string hint = _("common.back");
    Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT - 60, 28, 
               Gfx::COLOR_ALT_TEXT, ("B: " + hint).c_str(), Gfx::ALIGN_CENTER);
}

bool LocalInstallScreen::Update(Input &input) {
    // 检测返回按钮点击
    if (Screen::UpdateBackButton(input)) {
        return false;  // 返回上一级
    }
    
    State currentState = mState.load();
    
    // 处理文件列表导航
    if (currentState == STATE_FILE_LIST) {
        // 触摸处理 - 检测点击文件项
        if (input.data.touched && input.data.validPointer) {
            // 转换坐标
            float scaleX = 1920.0f / 1280.0f;
            float scaleY = 1080.0f / 720.0f;
            int touchX = (Gfx::SCREEN_WIDTH / 2) + (int)(input.data.x * scaleX);
            int touchY = (Gfx::SCREEN_HEIGHT / 2) - (int)(input.data.y * scaleY);
            
            // 文件列表区域
            const int listY = 160;
            const int cardX = 60;
            const int cardW = Gfx::SCREEN_WIDTH - 120;
            const int cardH = ITEM_HEIGHT - 10;
            
            // 检测点击的是哪个文件项
            for (int i = mScrollOffset; i < mScrollOffset + ITEMS_PER_PAGE && i < (int)mThemeFiles.size(); i++) {
                const int index = i - mScrollOffset;
                const int itemY = listY + index * ITEM_HEIGHT;
                
                // 应用缩放(从动画获取值)
                float scale = mItemAnims[i].scaleAnim.GetValue();
                int scaledW = (int)(cardW * scale);
                int scaledH = (int)(cardH * scale);
                int scaledX = cardX - (scaledW - cardW) / 2;
                int scaledY = itemY - (scaledH - cardH) / 2;
                
                if (IsTouchInRect(touchX, touchY, scaledX, scaledY, scaledW, scaledH)) {
                    if (!mTouchStarted) {
                        if (i == mSelectedIndex) {
                            // 双击效果: 再次点击选中项 = 确认
                            mState = STATE_CONFIRM_INSTALL;
                            FileLogger::GetInstance().LogInfo("File selected by touch: %s", 
                                mThemeFiles[i].fileName.c_str());
                        } else {
                            // 切换选中项 - 触发动画(快速动画)
                            int prevSelected = mSelectedIndex;
                            mSelectedIndex = i;
                            
                            // 触发选择动画
                            mItemAnims[prevSelected].scaleAnim.SetTarget(1.0f, 350);
                            mItemAnims[prevSelected].highlightAnim.SetTarget(0.0f, 350);
                            
                            mItemAnims[mSelectedIndex].scaleAnim.SetTarget(1.02f, 350);
                            mItemAnims[mSelectedIndex].highlightAnim.SetTarget(1.0f, 350);
                            
                            FileLogger::GetInstance().LogInfo("Changed selection to: %s", 
                                mThemeFiles[i].fileName.c_str());
                        }
                        mTouchStarted = true;
                    }
                    break;
                }
            }
        } else {
            mTouchStarted = false;
        }
        
        // 上下选择 - 支持循环和长按,带停顿控制
        bool selectionChanged = false;
        int prevSelected = mSelectedIndex;
        
        // 如果按键刚按下,立即响应
        if (input.data.buttons_d & Input::BUTTON_DOWN) {
            mSelectedIndex++;
            if (mSelectedIndex >= (int)mThemeFiles.size()) {
                mSelectedIndex = 0;  // 循环到第一个
                mScrollOffset = 0;
            } else if (mSelectedIndex >= mScrollOffset + ITEMS_PER_PAGE) {
                mScrollOffset = mSelectedIndex - ITEMS_PER_PAGE + 1;
            }
            selectionChanged = true;
            mInputRepeatDelay = INPUT_REPEAT_INITIAL;  // 设置初始延迟
        } else if (input.data.buttons_d & Input::BUTTON_UP) {
            mSelectedIndex--;
            if (mSelectedIndex < 0) {
                mSelectedIndex = (int)mThemeFiles.size() - 1;  // 循环到最后一个
                mScrollOffset = std::max(0, mSelectedIndex - ITEMS_PER_PAGE + 1);
            } else if (mSelectedIndex < mScrollOffset) {
                mScrollOffset = mSelectedIndex;
            }
            selectionChanged = true;
            mInputRepeatDelay = INPUT_REPEAT_INITIAL;  // 设置初始延迟
        }
        // 如果按键持续按住(不是刚按下),检查延迟计数器
        else if (input.data.buttons_h & Input::BUTTON_DOWN) {
            mInputRepeatDelay--;
            if (mInputRepeatDelay <= 0) {
                mSelectedIndex++;
                if (mSelectedIndex >= (int)mThemeFiles.size()) {
                    mSelectedIndex = 0;  // 循环到第一个
                    mScrollOffset = 0;
                } else if (mSelectedIndex >= mScrollOffset + ITEMS_PER_PAGE) {
                    mScrollOffset = mSelectedIndex - ITEMS_PER_PAGE + 1;
                }
                selectionChanged = true;
                mInputRepeatDelay = INPUT_REPEAT_RATE;  // 设置重复速率
            }
        } else if (input.data.buttons_h & Input::BUTTON_UP) {
            mInputRepeatDelay--;
            if (mInputRepeatDelay <= 0) {
                mSelectedIndex--;
                if (mSelectedIndex < 0) {
                    mSelectedIndex = (int)mThemeFiles.size() - 1;  // 循环到最后一个
                    mScrollOffset = std::max(0, mSelectedIndex - ITEMS_PER_PAGE + 1);
                } else if (mSelectedIndex < mScrollOffset) {
                    mScrollOffset = mSelectedIndex;
                }
                selectionChanged = true;
                mInputRepeatDelay = INPUT_REPEAT_RATE;  // 设置重复速率
            }
        } else {
            // 没有按键,重置延迟
            mInputRepeatDelay = 0;
        }
        
        // 触发选择动画
        if (selectionChanged) {
            mItemAnims[prevSelected].scaleAnim.SetTarget(1.0f, 350);
            mItemAnims[prevSelected].highlightAnim.SetTarget(0.0f, 350);
            
            mItemAnims[mSelectedIndex].scaleAnim.SetTarget(1.02f, 350);
            mItemAnims[mSelectedIndex].highlightAnim.SetTarget(1.0f, 350);
        }
        
        // A键确认选择
        if (input.data.buttons_d & Input::BUTTON_A) {
            if (mSelectedIndex >= 0 && mSelectedIndex < (int)mThemeFiles.size()) {
                mState = STATE_CONFIRM_INSTALL;
                FileLogger::GetInstance().LogInfo("Selected theme: %s", 
                    mThemeFiles[mSelectedIndex].fileName.c_str());
            }
        }
        
        // B键返回 - 清除按键状态后返回
        if (input.data.buttons_d & Input::BUTTON_B) {
            input.data.buttons_d &= ~Input::BUTTON_B;  // 清除 B 键按下状态
            input.data.buttons_h &= ~Input::BUTTON_B;  // 清除 B 键持续状态
            return false;
        }
    }
    // 处理确认对话框
    else if (currentState == STATE_CONFIRM_INSTALL) {
        // 触摸处理
        if (input.data.touched && input.data.validPointer) {
            // input.data.x/y 是 1280x720 坐标系相对于中心的坐标（-640~+640, -360~+360）
            // 需要缩放到 1920x1080 屏幕坐标系
            float scaleX = 1920.0f / 1280.0f;  // 1.5
            float scaleY = 1080.0f / 720.0f;   // 1.5
            int touchX = (Gfx::SCREEN_WIDTH / 2) + (int)(input.data.x * scaleX);   // 960 + (x * 1.5)
            int touchY = (Gfx::SCREEN_HEIGHT / 2) - (int)(input.data.y * scaleY);  // 540 - (y * 1.5)
            
            FileLogger::GetInstance().LogInfo("[LocalInstall Touch] Raw: (%d, %d) -> Screen: (%d, %d)", 
                input.data.x, input.data.y, touchX, touchY);
            
            // 计算对话框位置
            const int dialogW = 900;
            const int dialogH = 500;
            const int dialogX = (Gfx::SCREEN_WIDTH - dialogW) / 2;
            const int dialogY = (Gfx::SCREEN_HEIGHT - dialogH) / 2;
            
            // 复选框区域 - 计算与绘制时完全一致
            const int optionY = dialogY + 340;  // 与绘制代码一致
            const int checkboxSize = 36;
            const int checkboxRadius = 8;
            
            // 计算文字宽度以实现完全居中,然后整体右移80px
            const std::string deleteOption = _("local_install.delete_after_install");
            const int textWidth = deleteOption.length() * 18;
            const int totalWidth = checkboxSize + 15 + textWidth;  // 复选框 + 间距 + 文字
            const int centerX = dialogX + (dialogW / 2);  // 对话框中心
            const int startX = centerX - (totalWidth / 2) + 80;  // 居中后右移80px
            
            const int checkboxX = startX;
            const int checkboxY = optionY - checkboxSize / 2;
            
            // 可点击区域:扩大触摸范围,确保整个复选框+文字区域都可点击
            const int clickableX = checkboxX - 20;  // 左边扩展20px
            const int clickableY = optionY - 40;    // 垂直中心上下各40px(总高度80px)
            const int clickableW = totalWidth + 100; // 整个宽度 + 额外100px确保覆盖文字
            const int clickableH = 80; // 足够的垂直空间
            
            FileLogger::GetInstance().LogInfo("[Checkbox Area] x:%d y:%d w:%d h:%d, Touch:(%d,%d)", 
                clickableX, clickableY, clickableW, clickableH, touchX, touchY);
            
            // 检测复选框+文字整体区域的点击
            if (IsTouchInRect(touchX, touchY, clickableX, clickableY, clickableW, clickableH)) {
                if (!mTouchStarted) {
                    mDeleteAfterInstall = !mDeleteAfterInstall;
                    FileLogger::GetInstance().LogInfo("Checkbox toggled by touch: %d", mDeleteAfterInstall);
                    mTouchStarted = true;
                }
            }
            
            // Install 按钮区域
            const int btnW = 400;
            const int btnH = 70;
            const int btnX = dialogX + (dialogW - btnW) / 2;
            const int btnY = dialogY + 400;  // 与绘制代码一致
            
            if (IsTouchInRect(touchX, touchY, btnX, btnY, btnW, btnH)) {
                if (!mTouchStarted) {
                    StartInstall();
                    mTouchStarted = true;
                    FileLogger::GetInstance().LogInfo("Install button touched");
                }
            }
        } else {
            mTouchStarted = false;
        }
        
        // X键切换删除选项
        if (input.data.buttons_d & Input::BUTTON_X) {
            mDeleteAfterInstall = !mDeleteAfterInstall;
            FileLogger::GetInstance().LogInfo("Delete after install: %d", mDeleteAfterInstall);
        }
        
        // A键确认安装
        if (input.data.buttons_d & Input::BUTTON_A) {
            StartInstall();
        }
        
        // B键取消
        if (input.data.buttons_d & Input::BUTTON_B) {
            mState = STATE_FILE_LIST;
        }
    }
    // 处理安装结果
    else if (currentState == STATE_INSTALL_COMPLETE || currentState == STATE_INSTALL_ERROR) {
        // A或B键返回列表
        if ((input.data.buttons_d & Input::BUTTON_A) || (input.data.buttons_d & Input::BUTTON_B)) {
            // 重新扫描文件列表
            mState = STATE_LOADING;
            std::thread([this]() {
                ScanThemeFiles();
                mState = mThemeFiles.empty() ? STATE_EMPTY : STATE_FILE_LIST;
            }).detach();
        }
    }
    // 空状态
    else if (currentState == STATE_EMPTY) {
        if (input.data.buttons_d & Input::BUTTON_B) {
            return false;
        }
    }
    
    return true;
}

void LocalInstallScreen::StartInstall() {
    if (mSelectedIndex < 0 || mSelectedIndex >= (int)mThemeFiles.size()) {
        return;
    }
    
    mState = STATE_INSTALLING;
    mInstallProgress = 0.0f;
    mInstallError.clear();
    mInstalledThemeName = mThemeFiles[mSelectedIndex].displayName;
    
    FileLogger::GetInstance().LogInfo("Starting install of: %s", 
        mThemeFiles[mSelectedIndex].fullPath.c_str());
    
    // 启动安装线程
    mInstallThreadRunning = true;
    mInstallThread = std::thread([this]() {
        PerformInstall();
        mInstallThreadRunning = false;
    });
    mInstallThread.detach();
}

void LocalInstallScreen::PerformInstall() {
    const auto& file = mThemeFiles[mSelectedIndex];
    
    FileLogger::GetInstance().LogInfo("Installing theme from: %s", file.fullPath.c_str());
    
    mInstallProgress = 0.1f;
    
    // 使用文件名(不含.utheme后缀)作为主题ID
    std::string themeId = file.displayName;
    std::string themeName = file.displayName;
    std::string themeAuthor = "Unknown";
    
    // .utheme文件实际上是一个zip文件,需要先解压
    // 直接解压到 wiiu/themes/ 目录下
    std::string themesRoot = "fs:/vol/external01/wiiu/themes";
    std::string themeDir = themesRoot + "/" + themeId;
    
    // 确保themes目录存在
    struct stat st;
    if (stat(themesRoot.c_str(), &st) != 0) {
        if (mkdir(themesRoot.c_str(), 0755) != 0) {
            FileLogger::GetInstance().LogError("Failed to create themes directory");
            mInstallError = "Failed to create themes directory";
            mState = STATE_INSTALL_ERROR;
            return;
        }
    }
    
    // 如果主题目录已存在,先删除(重新安装)
    if (stat(themeDir.c_str(), &st) == 0) {
        FileLogger::GetInstance().LogInfo("Theme already exists, removing old version: %s", themeDir.c_str());
        
        if (!DeleteDirectoryRecursive(themeDir)) {
            FileLogger::GetInstance().LogError("Failed to delete existing theme directory");
            mInstallError = "Failed to remove old theme version";
            mState = STATE_INSTALL_ERROR;
            return;
        }
        
        FileLogger::GetInstance().LogInfo("Old theme version removed successfully");
    }
    
    // 创建主题目录
    if (mkdir(themeDir.c_str(), 0755) != 0) {
        FileLogger::GetInstance().LogError("Failed to create theme directory: %s", themeDir.c_str());
        mInstallError = "Failed to create theme directory";
        mState = STATE_INSTALL_ERROR;
        return;
    }
    
    mInstallProgress = 0.2f;
    
    // 解压.utheme文件到主题目录
    FileLogger::GetInstance().LogInfo("Extracting theme to: %s", themeDir.c_str());
    
    // 使用unzip解压(minizip库)
    unzFile zipFile = unzOpen(file.fullPath.c_str());
    if (!zipFile) {
        FileLogger::GetInstance().LogError("Failed to open .utheme file");
        mInstallError = "Failed to open theme file";
        mState = STATE_INSTALL_ERROR;
        return;
    }
    
    // 解压所有文件
    int ret = unzGoToFirstFile(zipFile);
    while (ret == UNZ_OK) {
        char filename[512];
        unz_file_info fileInfo;
        
        if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, sizeof(filename), 
                                  nullptr, 0, nullptr, 0) != UNZ_OK) {
            FileLogger::GetInstance().LogError("Failed to get file info");
            break;
        }
        
        std::string extractPath = themeDir + "/" + filename;
        
        // 如果是目录,创建它
        if (filename[strlen(filename) - 1] == '/') {
            mkdir(extractPath.c_str(), 0755);
        } else {
            // 解压文件
            if (unzOpenCurrentFile(zipFile) != UNZ_OK) {
                FileLogger::GetInstance().LogError("Failed to open file in zip: %s", filename);
                ret = unzGoToNextFile(zipFile);
                continue;
            }
            
            FILE* outFile = fopen(extractPath.c_str(), "wb");
            if (!outFile) {
                FileLogger::GetInstance().LogError("Failed to create file: %s", extractPath.c_str());
                unzCloseCurrentFile(zipFile);
                ret = unzGoToNextFile(zipFile);
                continue;
            }
            
            char buffer[8192];
            int bytesRead;
            while ((bytesRead = unzReadCurrentFile(zipFile, buffer, sizeof(buffer))) > 0) {
                fwrite(buffer, 1, bytesRead, outFile);
            }
            
            fclose(outFile);
            unzCloseCurrentFile(zipFile);
        }
        
        ret = unzGoToNextFile(zipFile);
    }
    
    unzClose(zipFile);
    
    mInstallProgress = 0.5f;
    
    // 创建或更新 theme_info.json,确保包含 id 字段
    std::string themeInfoPath = themeDir + "/theme_info.json";
    FILE* infoFile = fopen(themeInfoPath.c_str(), "r");
    
    if (infoFile) {
        // 如果已有 theme_info.json,读取并解析
        fseek(infoFile, 0, SEEK_END);
        long fileSize = ftell(infoFile);
        fseek(infoFile, 0, SEEK_SET);
        
        char* buffer = (char*)malloc(fileSize + 1);
        fread(buffer, 1, fileSize, infoFile);
        buffer[fileSize] = '\0';
        fclose(infoFile);
        
        // 简单解析获取 author
        std::string content(buffer);
        size_t authorPos = content.find("\"author\"");
        if (authorPos != std::string::npos) {
            size_t colonPos = content.find(":", authorPos);
            size_t quoteStart = content.find("\"", colonPos);
            size_t quoteEnd = content.find("\"", quoteStart + 1);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                themeAuthor = content.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            }
        }
        
        free(buffer);
    }
    
    // 保存完整的 theme_info.json,包含 id 字段
    FileLogger::GetInstance().LogInfo("Saving theme_info.json with id: %s", themeId.c_str());
    infoFile = fopen(themeInfoPath.c_str(), "w");
    if (infoFile) {
        std::string infoJson = "{\n";
        infoJson += "  \"id\": \"" + themeId + "\",\n";
        infoJson += "  \"author\": \"" + themeAuthor + "\",\n";
        infoJson += "  \"downloads\": 0,\n";
        infoJson += "  \"likes\": 0,\n";
        infoJson += "  \"updatedAt\": \"\"\n";
        infoJson += "}\n";
        
        fwrite(infoJson.c_str(), 1, infoJson.length(), infoFile);
        fclose(infoFile);
        FileLogger::GetInstance().LogInfo("theme_info.json saved successfully");
    } else {
        FileLogger::GetInstance().LogWarning("Failed to create theme_info.json (non-critical)");
    }
    
    mInstallProgress = 0.6f;
    
    // 现在安装解压后的主题(应用BPS补丁)
    FileLogger::GetInstance().LogInfo("Installing theme with ThemePatcher");
    
    ThemePatcher patcher;
    bool success = patcher.InstallTheme(themeDir, themeId, themeName, themeAuthor);
    
    mInstallProgress = 0.9f;
    
    if (success) {
        FileLogger::GetInstance().LogInfo("Theme installed successfully");
        
        // 更新 StyleMiiU 配置
        if (patcher.SetCurrentTheme(themeId)) {
            FileLogger::GetInstance().LogInfo("StyleMiiU config updated successfully");
        } else {
            FileLogger::GetInstance().LogWarning("Failed to update StyleMiiU config");
        }
        
        // 如果需要删除原文件
        if (mDeleteAfterInstall) {
            FileLogger::GetInstance().LogInfo("Deleting source file: %s", file.fullPath.c_str());
            if (remove(file.fullPath.c_str()) == 0) {
                FileLogger::GetInstance().LogInfo("Source file deleted");
            } else {
                FileLogger::GetInstance().LogError("Failed to delete source file");
            }
        }
        
        mInstallProgress = 1.0f;
        mState = STATE_INSTALL_COMPLETE;
    } else {
        FileLogger::GetInstance().LogError("Theme installation failed");
        mInstallError = "Failed to install theme";
        mState = STATE_INSTALL_ERROR;
    }
}

bool LocalInstallScreen::IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH) {
    return touchX >= rectX && touchX <= rectX + rectW &&
           touchY >= rectY && touchY <= rectY + rectH;
}

bool LocalInstallScreen::DeleteDirectoryRecursive(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        FileLogger::GetInstance().LogError("Failed to open directory for deletion: %s", path.c_str());
        return false;
    }
    
    struct dirent* entry;
    bool success = true;
    
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        std::string fullPath = path + "/" + entry->d_name;
        
        if (entry->d_type == DT_DIR) {
            // 递归删除子目录
            if (!DeleteDirectoryRecursive(fullPath)) {
                success = false;
            }
        } else {
            // 删除文件
            if (unlink(fullPath.c_str()) != 0) {
                FileLogger::GetInstance().LogError("Failed to delete file: %s", fullPath.c_str());
                success = false;
            }
        }
    }
    
    closedir(dir);
    
    // 删除空目录
    if (rmdir(path.c_str()) != 0) {
        FileLogger::GetInstance().LogError("Failed to delete directory: %s", path.c_str());
        return false;
    }
    
    return success;
}
