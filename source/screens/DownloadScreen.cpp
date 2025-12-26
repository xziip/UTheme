#include "DownloadScreen.hpp"
#include "ThemeDetailScreen.hpp"
#include "Gfx.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/ImageLoader.hpp"
#include "../utils/DownloadQueue.hpp"
#include "../utils/Utils.hpp"
#include "../utils/logger.h"
#include "../utils/FileLogger.hpp"
#include "../input/CombinedInput.h"
#include "../input/VPADInput.h"
#include "../input/WPADInput.h"
#include <cmath>
#include <sys/stat.h>
#include <dirent.h>

DownloadScreen::DownloadScreen() {
    mTitleAnim.Start(0, 1, 500);
    mThemeManager = std::make_unique<ThemeManager>();
    mReturnFromDetailFrame = -1000;  // 初始化为负数,避免启动时触发冷却
    
    // 初始化图片加载器
    ImageLoader::Init();
    
    // 扫描已安装的主题(缓存,避免每帧检查)
    ScanInstalledThemes();
    
    // 设置回调
    mThemeManager->SetStateCallback([this](ThemeManager::FetchState state, const std::string& message) {
        switch (state) {
            case ThemeManager::FETCH_IN_PROGRESS:
                mState = STATE_LOADING;
                break;
            case ThemeManager::FETCH_SUCCESS:
                mState = STATE_SHOW_THEMES;
                mLoadedThemeCount = mThemeManager->GetThemes().size();
                InitAnimations(mLoadedThemeCount);  // 初始化动画
                break;
            case ThemeManager::FETCH_ERROR:
                mState = STATE_ERROR;
                mErrorMessage = message;
                break;
            default:
                break;
        }
    });
    
    // 先尝试从缓存加载
    FileLogger::GetInstance().LogInfo("DownloadScreen: Checking cache...");
    if (mThemeManager->LoadCache() && mThemeManager->IsCacheValid()) {
        // 缓存有效，直接显示主题列表
        FileLogger::GetInstance().LogInfo("DownloadScreen: Using cached themes");
        mState = STATE_SHOW_THEMES;
        mLoadedThemeCount = mThemeManager->GetThemes().size();
        
        // 初始化动画
        InitAnimations(mLoadedThemeCount);
        
        // 如果缓存主题数量少于50个,说明是旧版本缓存,自动刷新
        if (mLoadedThemeCount < 50) {
            FileLogger::GetInstance().LogInfo("DownloadScreen: Cache has only %zu themes (< 50), triggering full refresh", mLoadedThemeCount);
            mState = STATE_LOADING;
            mThemeManager->FetchThemes();
        } else {
            // 在后台检查更新
            mThemeManager->CheckForUpdates();
        }
    } else {
        // 缓存无效或不存在，需要从网络获取
        FileLogger::GetInstance().LogInfo("DownloadScreen: Cache invalid or missing, will fetch from network");
    }
}

// 初始化动画
void DownloadScreen::InitAnimations(size_t themeCount) {
    mThemeAnims.clear();
    mThemeAnims.resize(themeCount);
    
    for (size_t i = 0; i < themeCount; i++) {
        mThemeAnims[i].scaleAnim.SetImmediate(1.0f);
        mThemeAnims[i].highlightAnim.SetImmediate(0.0f);
    }
    
    // 选中第一个主题的动画
    if (themeCount > 0) {
        mThemeAnims[0].scaleAnim.SetTarget(1.05f, 300);
        mThemeAnims[0].highlightAnim.SetTarget(1.0f, 300);
    }
}

// 更新所有动画
void DownloadScreen::UpdateAnimations() {
    for (auto& anim : mThemeAnims) {
        anim.scaleAnim.Update();
        anim.highlightAnim.Update();
    }
}

// 检查触摸点是否在矩形内
bool DownloadScreen::IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH) {
    return touchX >= rectX && touchX <= rectX + rectW &&
           touchY >= rectY && touchY <= rectY + rectH;
}

DownloadScreen::~DownloadScreen() {
    FileLogger::GetInstance().LogInfo("DownloadScreen destructor called");
    
    // 强制处理所有待处理的下载,防止卡死
    if (DownloadQueue::GetInstance()) {
        FileLogger::GetInstance().LogInfo("Processing remaining downloads before cleanup");
        // 循环处理直到队列清空或超时
        int maxIterations = 100;
        while (ImageLoader::GetQueueSize() > 0 && maxIterations-- > 0) {
            DownloadQueue::GetInstance()->Process();
        }
        if (ImageLoader::GetQueueSize() > 0) {
            FileLogger::GetInstance().LogWarning("Still have %zu pending image loads after timeout", ImageLoader::GetQueueSize());
        }
    }
    
    // 清理 ThemeManager (会取消未完成的网络请求)
    if (mThemeManager) {
        FileLogger::GetInstance().LogInfo("Cleaning up ThemeManager");
        mThemeManager.reset();
        FileLogger::GetInstance().LogInfo("ThemeManager cleanup completed");
    }
    
    FileLogger::GetInstance().LogInfo("DownloadScreen destructor completed");
}

void DownloadScreen::Draw() {
    mFrameCount++;
    
    // Draw gradient background
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);
    
    DrawAnimatedTopBar(_("download.title"), mTitleAnim, 0xf019);
    
    switch (mState) {
        case STATE_INIT:
        case STATE_LOADING: {
            // 绘制加载卡片
            const int cardW = 900;
            const int cardH = 400;
            const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
            const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
            
            SDL_Color shadowColor = Gfx::COLOR_SHADOW;
            shadowColor.a = 80;
            Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
            Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
            
            // 绘制旋转图标
            double angle = (mFrameCount % 60) * 6.0; // 每帧旋转6度
            Gfx::DrawIcon(cardX + cardW/2, cardY + 120, 80, Gfx::COLOR_ACCENT, 0xf021, Gfx::ALIGN_CENTER, angle);
            
            // 显示加载状态
            Gfx::Print(cardX + cardW/2, cardY + 220, 44, Gfx::COLOR_TEXT, _("download.loading"), Gfx::ALIGN_CENTER);
            Gfx::Print(cardX + cardW/2, cardY + 280, 32, Gfx::COLOR_ALT_TEXT, _("download.loading_desc"), Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_SHOW_THEMES: {
            DrawThemeList();
            break;
        }
        case STATE_DOWNLOADING: {
            // 显示下载进度
            const int cardW = 900;
            const int cardH = 400;
            const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
            const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
            
            SDL_Color shadowColor = Gfx::COLOR_SHADOW;
            shadowColor.a = 80;
            Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
            Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
            
            // 绘制下载图标
            Gfx::DrawIcon(cardX + cardW/2, cardY + 100, 80, Gfx::COLOR_ACCENT, 0xf019, Gfx::ALIGN_CENTER);
            
            Gfx::Print(cardX + cardW/2, cardY + 200, 44, Gfx::COLOR_TEXT, _("download.downloading"), Gfx::ALIGN_CENTER);
            
            // 主题名称
            const auto& themes = mThemeManager->GetThemes();
            if (mSelectedTheme < (int)themes.size()) {
                std::string themeName = themes[mSelectedTheme].name;
                if (themeName.length() > 30) {
                    themeName = themeName.substr(0, 27) + "...";
                }
                Gfx::Print(cardX + cardW/2, cardY + 260, 32, Gfx::COLOR_ALT_TEXT, 
                          themeName.c_str(), Gfx::ALIGN_CENTER);
            }
            
            // 进度条
            const int barW = 700;
            const int barH = 40;
            const int barX = cardX + (cardW - barW) / 2;
            const int barY = cardY + 310;
            
            // 进度条背景
            Gfx::DrawRectRounded(barX, barY, barW, barH, 20, Gfx::COLOR_ALT_BACKGROUND);
            
            // 计算实际进度
            int elapsedFrames = mFrameCount - mDownloadStartFrame;
            float progress = std::min(elapsedFrames / 120.0f, 1.0f);
            int progressW = (int)(barW * progress);
            if (progressW > 0) {
                Gfx::DrawRectRounded(barX, barY, progressW, barH, 20, Gfx::COLOR_ACCENT);
            }
            
            // 进度百分比
            char progressText[16];
            snprintf(progressText, sizeof(progressText), "%.0f%%", progress * 100);
            Gfx::Print(cardX + cardW/2, barY + barH/2, 28, Gfx::COLOR_WHITE, 
                      progressText, Gfx::ALIGN_CENTER | Gfx::ALIGN_VERTICAL);
            
            break;
        }
        case STATE_DONE: {
            // 下载完成
            const int cardW = 800;
            const int cardH = 300;
            const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
            const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
            
            SDL_Color shadowColor = Gfx::COLOR_SHADOW;
            shadowColor.a = 80;
            Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
            Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
            
            Gfx::DrawIcon(cardX + cardW/2, cardY + 80, 70, Gfx::COLOR_SUCCESS, 0xf00c, Gfx::ALIGN_CENTER);
            Gfx::Print(cardX + cardW/2, cardY + 170, 48, Gfx::COLOR_SUCCESS, _("download.complete"), Gfx::ALIGN_CENTER);
            Gfx::Print(cardX + cardW/2, cardY + 230, 32, Gfx::COLOR_ALT_TEXT, (std::string("\ue001 ") + _("input.back")).c_str(), Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_ERROR: {
            // 错误状态
            const int cardW = 800;
            const int cardH = 300;
            const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
            const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
            
            SDL_Color shadowColor = Gfx::COLOR_SHADOW;
            shadowColor.a = 80;
            Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
            Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
            
            Gfx::DrawIcon(cardX + cardW/2, cardY + 70, 70, Gfx::COLOR_ERROR, 0xf071, Gfx::ALIGN_CENTER);
            Gfx::Print(cardX + cardW/2, cardY + 160, 48, Gfx::COLOR_ERROR, _("download.error"), Gfx::ALIGN_CENTER);
            Gfx::Print(cardX + cardW/2, cardY + 210, 32, Gfx::COLOR_ALT_TEXT, mErrorMessage.c_str(), Gfx::ALIGN_CENTER);
            break;
        }
    }
    
    // 底部栏 - 根据状态显示不同提示
    if (mState == STATE_SHOW_THEMES) {
        std::string middleHint = std::string("\ue000 ") + _("download.download") + " | \ue002 " + _("download.refresh");
        
        // 如果检测到更新,添加提示
        if (mThemeManager->HasUpdates()) {
            middleHint += " | " + std::string(_("download.update_available"));
        }
        
        DrawBottomBar((std::string("\ue07d ") + _("input.select")).c_str(), 
                     middleHint.c_str(), 
                     (std::string("\ue001 ") + _("input.back")).c_str());
    } else {
        DrawBottomBar(nullptr, 
                     (std::string("\ue044 ") + _("input.exit")).c_str(), 
                     (std::string("\ue001 ") + _("input.back")).c_str());
    }
}

bool DownloadScreen::Update(Input &input) {
    // 更新图片加载器
    ImageLoader::Update();
    // 更新动画
    UpdateAnimations();
    
    // 更新主题管理器
    if (mThemeManager) {
        mThemeManager->Update();
    }
    
    // 输入冷却 - 从详情页面返回后等待15帧再处理输入
    const int INPUT_COOLDOWN_FRAMES = 15;
    bool inputCooldown = (mFrameCount - mReturnFromDetailFrame) < INPUT_COOLDOWN_FRAMES;
    
    // 状态机
    if (mState == STATE_INIT) {
        // 只有在缓存无效时才从网络获取
        if (mThemeManager->GetThemes().empty()) {
            mState = STATE_LOADING;
            mThemeManager->FetchThemes();
        } else {
            // 已经从缓存加载了主题，直接显示
            mState = STATE_SHOW_THEMES;
        }
        return true;  // 立即返回,不处理输入
    }
    
    // 在加载状态下,只允许按B键返回,禁止其他输入
    if (mState == STATE_LOADING) {
        if (input.data.buttons_d & Input::BUTTON_B) {
            return false;
        }
        return true;  // 阻止其他按键处理
    }
    
    if (mState == STATE_SHOW_THEMES) {
        const auto& themes = mThemeManager->GetThemes();
        
        // 确保动画向量大小与主题数量匹配
        if (mThemeAnims.size() != themes.size()) {
            InitAnimations(themes.size());
        }
        
        // 如果在输入冷却期,不处理输入
        if (inputCooldown) {
            return true;
        }
        
        // 按B键返回菜单
        if (input.data.buttons_d & Input::BUTTON_B) {
            return false;
        }
        
        // X键刷新
        if (input.data.buttons_d & Input::BUTTON_X) {
            mState = STATE_LOADING;
            mThemeManager->ForceRefresh();
            return true;
        }
        
        // 触摸支持
        if (input.data.touched && input.data.validPointer && !input.lastData.touched) {
            // 转换触摸坐标 (DRC: 854x480 -> 1920x1080)
            int touchX = (int)((input.data.x * 1920.0f / 1280.0f) + 960);
            int touchY = (int)(540 - (input.data.y * 1080.0f / 720.0f));
            
            // 计算主题卡片的位置和大小（与 DrawThemeList 保持一致）
            const int cardW = 1720;
            const int cardH = 200;
            const int listX = 100;
            const int listY = 150;
            const int spacing = 20;
            const int visibleCount = 3;
            
            // 检查点击了哪个主题卡片
            for (int i = 0; i < visibleCount && (mScrollOffset + i) < (int)themes.size(); i++) {
                int themeIndex = mScrollOffset + i;
                int cardX = listX;
                int cardY = listY + i * (cardH + spacing);
                
                if (IsTouchInRect(touchX, touchY, cardX, cardY, cardW, cardH)) {
                    // 如果点击已选中的主题，打开详情页
                    if (themeIndex == mSelectedTheme) {
                        // 创建详情屏幕
                        mDetailScreen = new ThemeDetailScreen(&themes[mSelectedTheme], mThemeManager.get());
                        
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
                            
                            if (!mDetailScreen->Update(detailBaseInput)) {
                                break; // 返回主题列表
                            }
                            
                            mDetailScreen->Draw();
                            Gfx::Render();
                        }
                        
                        // 清理详情屏幕
                        delete mDetailScreen;
                        mDetailScreen = nullptr;
                        
                        FileLogger::GetInstance().LogInfo("Returned from detail screen (touch), theme count: %zu", themes.size());
                        
                        // 验证选中索引是否仍然有效
                        if (mSelectedTheme >= (int)themes.size()) {
                            FileLogger::GetInstance().LogError("Selected theme index out of bounds! Resetting to 0");
                            mSelectedTheme = 0;
                            mScrollOffset = 0;
                        }
                        
                        // 重新初始化动画以确保大小匹配
                        if (mThemeAnims.size() != themes.size()) {
                            FileLogger::GetInstance().LogInfo("Reinitializing animations after detail screen");
                            InitAnimations(themes.size());
                        }
                        
                        // 设置返回时间,启动输入冷却
                        mReturnFromDetailFrame = mFrameCount;
                        
                        // 从详情屏返回后，立即返回以避免本帧的输入被重复处理
                        return true;
                    } else {
                        // 否则选中该主题
                        mPrevSelectedTheme = mSelectedTheme;
                        mSelectedTheme = themeIndex;
                        
                        // 更新动画
                        if (mPrevSelectedTheme >= 0 && mPrevSelectedTheme < (int)mThemeAnims.size()) {
                            mThemeAnims[mPrevSelectedTheme].scaleAnim.SetTarget(1.0f, 300);
                            mThemeAnims[mPrevSelectedTheme].highlightAnim.SetTarget(0.0f, 300);
                        }
                        if (mSelectedTheme >= 0 && mSelectedTheme < (int)mThemeAnims.size()) {
                            mThemeAnims[mSelectedTheme].scaleAnim.SetTarget(1.05f, 300);
                            mThemeAnims[mSelectedTheme].highlightAnim.SetTarget(1.0f, 300);
                        }
                    }
                    break;
                }
            }
        }
        
        // 保存旧的选择
        mPrevSelectedTheme = mSelectedTheme;
        
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
        const int themeCount = (int)themes.size();
        if (shouldMoveUp) {
            if (mSelectedTheme > 0) {
                mSelectedTheme--;
            } else {
                // 循环到底部
                mSelectedTheme = themeCount - 1;
                mScrollOffset = std::max(0, themeCount - 3);
            }
            // 调整滚动
            if (mSelectedTheme < mScrollOffset) {
                mScrollOffset = mSelectedTheme;
            }
        } else if (shouldMoveDown) {
            if (mSelectedTheme < themeCount - 1) {
                mSelectedTheme++;
            } else {
                // 循环到顶部
                mSelectedTheme = 0;
                mScrollOffset = 0;
            }
            // 调整滚动 (假设显示3个)
            const int visibleCount = 3;
            if (mSelectedTheme >= mScrollOffset + visibleCount) {
                mScrollOffset = mSelectedTheme - visibleCount + 1;
            }
        }
        
        // 如果选择改变，更新动画
        if (mPrevSelectedTheme != mSelectedTheme) {
            // 重置旧的选择
            if (mPrevSelectedTheme >= 0 && mPrevSelectedTheme < (int)mThemeAnims.size()) {
                mThemeAnims[mPrevSelectedTheme].scaleAnim.SetTarget(1.0f, 300);
                mThemeAnims[mPrevSelectedTheme].highlightAnim.SetTarget(0.0f, 300);
            }
            
            // 高亮新的选择
            if (mSelectedTheme >= 0 && mSelectedTheme < (int)mThemeAnims.size()) {
                mThemeAnims[mSelectedTheme].scaleAnim.SetTarget(1.05f, 300);
                mThemeAnims[mSelectedTheme].highlightAnim.SetTarget(1.0f, 300);
            }
        }
        
        // A键打开主题详情
        if (input.data.buttons_d & Input::BUTTON_A) {
            if (mSelectedTheme < (int)themes.size()) {
                // 创建详情屏幕
                mDetailScreen = new ThemeDetailScreen(&themes[mSelectedTheme], mThemeManager.get());
                
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
                    
                    if (!mDetailScreen->Update(detailBaseInput)) {
                        break; // 返回主题列表
                    }
                    
                    mDetailScreen->Draw();
                    Gfx::Render();
                }
                
                // 清理详情屏幕
                delete mDetailScreen;
                mDetailScreen = nullptr;
                
                FileLogger::GetInstance().LogInfo("Returned from detail screen, theme count: %zu", themes.size());
                
                // 验证选中索引是否仍然有效
                if (mSelectedTheme >= (int)themes.size()) {
                    FileLogger::GetInstance().LogError("Selected theme index out of bounds! Resetting to 0");
                    mSelectedTheme = 0;
                    mScrollOffset = 0;
                }
                
                // 重新初始化动画以确保大小匹配
                if (mThemeAnims.size() != themes.size()) {
                    FileLogger::GetInstance().LogInfo("Reinitializing animations after detail screen");
                    InitAnimations(themes.size());
                }
                
                // 设置返回时间,启动输入冷却
                mReturnFromDetailFrame = mFrameCount;
                
                // 从详情屏返回后，立即返回以避免本帧的输入被重复处理
                return true;
            }
        }
    } else if (mState == STATE_DOWNLOADING) {
        // 模拟下载完成(120帧后,约2秒)
        if (mFrameCount - mDownloadStartFrame >= 120) {
            mState = STATE_DONE;
        }
    } else if (mState == STATE_DONE) {
        // 按B或A返回主题列表
        if (input.data.buttons_d & (Input::BUTTON_B | Input::BUTTON_A)) {
            mState = STATE_SHOW_THEMES;
        }
    } else if (mState == STATE_ERROR) {
        // 按B返回主题列表,按A重试
        if (input.data.buttons_d & Input::BUTTON_B) {
            mState = STATE_SHOW_THEMES;
        } else if (input.data.buttons_d & Input::BUTTON_A) {
            mState = STATE_LOADING;
            if (mThemeManager) {
                mThemeManager->FetchThemes();
            }
        }
    }
    
    return true;
}

void DownloadScreen::DrawThemeList() {
    const auto& themes = mThemeManager->GetThemes();
    
    if (themes.empty()) {
        // 没有主题
        const int cardW = 800;
        const int cardH = 300;
        const int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
        const int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
        
        SDL_Color shadowColor = Gfx::COLOR_SHADOW;
        shadowColor.a = 80;
        Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
        Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);
        
        Gfx::DrawIcon(cardX + cardW/2, cardY + 100, 70, Gfx::COLOR_WARNING, 0xf071, Gfx::ALIGN_CENTER);
        Gfx::Print(cardX + cardW/2, cardY + 190, 44, Gfx::COLOR_TEXT, "No themes found", Gfx::ALIGN_CENTER);
        return;
    }
    
    // 绘制主题列表
    const int listX = 100;
    const int listY = 150;
    const int cardW = 1720;
    const int cardH = 200;
    const int cardSpacing = 20;
    const int visibleCount = 3;
    
    int currentY = listY;
    int endIndex = std::min(mScrollOffset + visibleCount, (int)themes.size());
    
    for (int i = mScrollOffset; i < endIndex; i++) {
        bool selected = (i == mSelectedTheme);
        // 需要非 const 访问来修改缩略图状态
        auto& themesVec = const_cast<std::vector<Theme>&>(themes);
        DrawThemeCard(listX, currentY, cardW, cardH, themesVec[i], selected, i);
        currentY += cardH + cardSpacing;
    }
    
    // 绘制滚动指示器
    if (themes.size() > visibleCount) {
        char scrollInfo[32];
        snprintf(scrollInfo, sizeof(scrollInfo), "%d / %zu", mSelectedTheme + 1, themes.size());
        Gfx::Print(Gfx::SCREEN_WIDTH - 100, Gfx::SCREEN_HEIGHT - 150, 32, Gfx::COLOR_ALT_TEXT, 
                  scrollInfo, Gfx::ALIGN_VERTICAL | Gfx::ALIGN_RIGHT);
    }
}

void DownloadScreen::DrawThemeCard(int x, int y, int w, int h, Theme& theme, bool selected, int themeIndex) {
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
    const int thumbW = (int)(thumbH * 16.0f / 9.0f); // 16:9 宽高比
    const int thumbX = x + 20;
    const int thumbY = y + 20;
    
    // 绘制缩略图
    if (theme.collagePreview.thumbTexture) {
        // 已加载,绘制纹理
        SDL_Rect dstRect = {thumbX, thumbY, thumbW, thumbH};
        
        // 获取纹理尺寸
        int texW, texH;
        SDL_QueryTexture(theme.collagePreview.thumbTexture, nullptr, nullptr, &texW, &texH);
        
        // 计算缩放以保持纵横比
        float scale = std::min((float)thumbW / texW, (float)thumbH / texH);
        int scaledW = (int)(texW * scale);
        int scaledH = (int)(texH * scale);
        
        // 居中显示
        dstRect.x = thumbX + (thumbW - scaledW) / 2;
        dstRect.y = thumbY + (thumbH - scaledH) / 2;
        dstRect.w = scaledW;
        dstRect.h = scaledH;
        
        // 背景
        Gfx::DrawRectFilled(thumbX, thumbY, thumbW, thumbH, Gfx::COLOR_ALT_BACKGROUND);
        
        // 绘制纹理
        SDL_RenderCopy(Gfx::GetRenderer(), theme.collagePreview.thumbTexture, nullptr, &dstRect);
        
    } else if (!theme.collagePreview.thumbUrl.empty() && !theme.collagePreview.thumbLoaded) {
        // 还未加载,显示占位符并异步加载
        Gfx::DrawRectFilled(thumbX, thumbY, thumbW, thumbH, Gfx::COLOR_ALT_BACKGROUND);
        
        // 加载动画 (旋转圈)
        double angle = (mFrameCount % 60) * 6.0;
        Gfx::DrawIcon(thumbX + thumbW/2, thumbY + thumbH/2 - 15, 40, Gfx::COLOR_ICON, 0xf1ce, Gfx::ALIGN_CENTER, angle);
        
        // 加载文本
        Gfx::Print(thumbX + thumbW/2, thumbY + thumbH/2 + 30, 24, Gfx::COLOR_ALT_TEXT, 
                  _("download.loading_image"), Gfx::ALIGN_CENTER);
        
        // 标记为正在加载
        theme.collagePreview.thumbLoaded = true;
        
        // 异步加载 - 使用 ThemeManager 和索引来避免引用失效
        ImageLoader::LoadRequest request;
        request.url = theme.collagePreview.thumbUrl;
        request.highPriority = selected; // 选中的优先加载
        request.callback = [this, themeIndex](SDL_Texture* texture) {
            // 通过索引访问主题,避免引用失效
            if (!mThemeManager) {
                DEBUG_FUNCTION_LINE("ThemeManager is null in callback!");
                return;
            }
            
            auto& themes = mThemeManager->GetThemes();
            if (themeIndex >= 0 && themeIndex < (int)themes.size()) {
                themes[themeIndex].collagePreview.thumbTexture = texture;
                DEBUG_FUNCTION_LINE("Set texture for theme %d: %p", themeIndex, texture);
                
                if (texture) {
                    FileLogger::GetInstance().LogInfo("Image loaded for theme %d: %s", 
                        themeIndex, themes[themeIndex].name.c_str());
                } else {
                    FileLogger::GetInstance().LogError("Failed to load image for theme %d", themeIndex);
                }
            } else {
                DEBUG_FUNCTION_LINE("Invalid theme index in callback: %d (total: %zu)", themeIndex, themes.size());
            }
        };
        ImageLoader::LoadAsync(request);
        
    } else {
        // 没有缩略图URL,显示默认图标
        Gfx::DrawRectRounded(thumbX, thumbY, thumbW, thumbH, 12, Gfx::COLOR_ALT_BACKGROUND);
        Gfx::DrawIcon(thumbX + thumbW/2, thumbY + thumbH/2, 50, Gfx::COLOR_ICON, 0xf03e, Gfx::ALIGN_CENTER);
    }
    
    // 主题信息
    const int infoX = thumbX + thumbW + 30;
    const int infoY = y + 30;
    
    // 主题名称 - 清理特殊字符用于显示
    std::string displayName = Utils::SanitizeThemeNameForDisplay(theme.name);
    SDL_Color titleColor = selected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT;
    Gfx::Print(infoX, infoY, 42, titleColor, displayName.c_str(), Gfx::ALIGN_VERTICAL);
    
    // 作者
    SDL_Color authorColor = Gfx::COLOR_ALT_TEXT;
    Gfx::Print(infoX, infoY + 55, 32, authorColor, 
              (std::string("by ") + theme.author).c_str(), Gfx::ALIGN_VERTICAL);
    
    // 描述(截断) - 限制到一行
    std::string desc = theme.description;
    if (desc.empty()) {
        desc = "No description available";
    }
    
    // 移除换行符
    size_t newlinePos = desc.find('\n');
    if (newlinePos != std::string::npos) {
        desc = desc.substr(0, newlinePos);
    }
    
    // 限制描述长度到50字符以防止重叠
    if (desc.length() > 50) {
        desc = desc.substr(0, 47) + "...";
    }
    Gfx::Print(infoX, infoY + 100, 26, authorColor, desc.c_str(), Gfx::ALIGN_VERTICAL);
    
    // 统计信息 - 移到更靠下的位置
    const int statsY = y + h - 40;
    Gfx::DrawIcon(infoX, statsY, 24, Gfx::COLOR_ICON, 0xf019, Gfx::ALIGN_VERTICAL);
    Gfx::Print(infoX + 35, statsY, 28, authorColor, std::to_string(theme.downloads).c_str(), Gfx::ALIGN_VERTICAL);
    
    Gfx::DrawIcon(infoX + 150, statsY, 24, Gfx::COLOR_WARNING, 0xf004, Gfx::ALIGN_VERTICAL);
    Gfx::Print(infoX + 185, statsY, 28, authorColor, std::to_string(theme.likes).c_str(), Gfx::ALIGN_VERTICAL);
    
    // 检查是否已下载/已安装 (使用缓存,避免频繁磁盘IO)
    if (!theme.id.empty() && mInstalledThemeIds.find(theme.id) != mInstalledThemeIds.end()) {
        // 绘制"已下载"标签在右上角
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
                  _("download.installed"), Gfx::ALIGN_VERTICAL);
    }
}

// 扫描已安装的主题
void DownloadScreen::ScanInstalledThemes() {
    mInstalledThemeIds.clear();
    
    const char* installedDir = "fs:/vol/external01/UTheme/installed";
    DIR* dir = opendir(installedDir);
    if (!dir) {
        FileLogger::GetInstance().LogInfo("DownloadScreen: installed directory not found");
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        // 检查是否是 .json 文件
        if (filename.length() > 5 && filename.substr(filename.length() - 5) == ".json") {
            // 提取主题ID (去掉 .json 后缀)
            std::string themeId = filename.substr(0, filename.length() - 5);
            mInstalledThemeIds.insert(themeId);
        }
    }
    closedir(dir);
    
    FileLogger::GetInstance().LogInfo("DownloadScreen: Found %zu installed themes", mInstalledThemeIds.size());
}
