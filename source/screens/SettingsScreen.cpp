#include "SettingsScreen.hpp"
#include "Gfx.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/FileLogger.hpp"
#include "../utils/Config.hpp"

SettingsScreen::SettingsScreen()
    : mPrevSelectedItem(SETTINGS_LANGUAGE)
{
    mTitleAnim.Start(0, 1, 500);
    mSelectionAnim.Start(0, 1, 200);
    
    // 初始化所有选项动画进度
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        mItemAnimProgress[i] = 0.0f;
    }
    mItemAnimProgress[mSelectedItem] = 1.0f; // 当前选中项从完全显示开始
    
    // 找到当前语言在列表中的位置
    const auto& languages = Lang().GetAvailableLanguages();
    const std::string& currentLang = Lang().GetCurrentLanguage();
    
    for (size_t i = 0; i < languages.size(); i++) {
        if (languages[i].code == currentLang) {
            mSelectedLanguage = static_cast<int>(i);
            break;
        }
    }
}

SettingsScreen::~SettingsScreen() {
}

void SettingsScreen::Draw() {
    mFrameCount++;
    
    // 更新选项动画
    mSelectionAnim.Update();
    
    // 更新每个选项的动画进度
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        if (i == mSelectedItem) {
            // 当前选中项:逐渐增加到1.0
            mItemAnimProgress[i] += (1.0f - mItemAnimProgress[i]) * 0.2f;
        } else {
            // 非选中项:逐渐减少到0.0
            mItemAnimProgress[i] *= 0.8f;
        }
    }
    
    // Draw gradient background
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);
    
    DrawAnimatedTopBar(_("settings.title"), mTitleAnim, 0xf013);
    
    if (mLanguageDialogOpen) {
        DrawLanguageDialog();
        return;
    }
    
    // 设置项列表
    const int topBarHeight = 120;
    const int itemHeight = 120;
    const int itemSpacing = 20;
    const int listX = 200;
    const int listY = topBarHeight + 60;
    const int listW = Gfx::SCREEN_WIDTH - 400;
    
    // 获取当前语言名称
    const auto& languages = Lang().GetAvailableLanguages();
    std::string currentLanguageName = "Unknown";
    for (const auto& lang : languages) {
        if (lang.code == Lang().GetCurrentLanguage()) {
            currentLanguageName = lang.name;
            break;
        }
    }
    
    // 绘制设置项
    int currentY = listY;
    
    // 语言设置
    DrawSettingItem(listX, currentY, listW, 
                   _("settings.language"), 
                   _("settings.language_desc"), 
                   currentLanguageName,
                   mSelectedItem == SETTINGS_LANGUAGE,
                   mItemAnimProgress[SETTINGS_LANGUAGE]);
    currentY += itemHeight + itemSpacing;
    
    // 下载路径设置
    DrawSettingItem(listX, currentY, listW, 
                   _("settings.download_path"), 
                   _("settings.download_path_desc"), 
                   "SD:/themes/",
                   mSelectedItem == SETTINGS_DOWNLOAD_PATH,
                   mItemAnimProgress[SETTINGS_DOWNLOAD_PATH]);
    currentY += itemHeight + itemSpacing;
    
    // 自动安装设置
    DrawSettingItem(listX, currentY, listW, 
                   _("settings.auto_install"), 
                   _("settings.auto_install_desc"), 
                   _("common.yes"),
                   mSelectedItem == SETTINGS_AUTO_INSTALL,
                   mItemAnimProgress[SETTINGS_AUTO_INSTALL]);
    currentY += itemHeight + itemSpacing;
    
    // 背景音乐设置
    DrawSettingItem(listX, currentY, listW, 
                   _("settings.bgm_enabled"), 
                   _("settings.bgm_enabled_desc"), 
                   Config::GetInstance().IsBgmEnabled() ? _("common.yes") : _("common.no"),
                   mSelectedItem == SETTINGS_BGM_ENABLED,
                   mItemAnimProgress[SETTINGS_BGM_ENABLED]);
    currentY += itemHeight + itemSpacing;
    
    // 日志启用设置
    DrawSettingItem(listX, currentY, listW, 
                   _("settings.logging"), 
                   _("settings.logging_desc"), 
                   Config::GetInstance().IsLoggingEnabled() ? _("common.yes") : _("common.no"),
                   mSelectedItem == SETTINGS_LOGGING_ENABLED,
                   mItemAnimProgress[SETTINGS_LOGGING_ENABLED]);
    currentY += itemHeight + itemSpacing;
    
    // 详细日志设置
    DrawSettingItem(listX, currentY, listW, 
                   _("settings.verbose_logging"), 
                   _("settings.verbose_logging_desc"), 
                   Config::GetInstance().IsVerboseLogging() ? _("common.yes") : _("common.no"),
                   mSelectedItem == SETTINGS_LOGGING_VERBOSE,
                   mItemAnimProgress[SETTINGS_LOGGING_VERBOSE]);
    
    DrawBottomBar(nullptr, (std::string("\ue044 ") + _("input.exit")).c_str(), (std::string("\ue001 ") + _("input.back")).c_str());
}

void SettingsScreen::DrawSettingItem(int x, int y, int w, const std::string& title, 
                                     const std::string& description, const std::string& value, 
                                     bool selected, float animProgress) {
    const int itemH = 120; // 匹配Draw()中的itemHeight
    
    // 使用动画进度计算缩放效果
    float scale = 1.0f + (animProgress * 0.03f); // 选中时放大3%
    int scaledW = (int)(w * scale);
    int scaledH = (int)(itemH * scale);
    int offsetX = (w - scaledW) / 2;
    int offsetY = (itemH - scaledH) / 2;
    
    int drawX = x + offsetX;
    int drawY = y + offsetY;
    
    // 绘制背景和选中效果
    SDL_Color bgColor = Gfx::COLOR_CARD_BG;
    if (selected) {
        // 根据动画进度插值背景色
        SDL_Color hoverColor = Gfx::COLOR_CARD_HOVER;
        bgColor.r = (Uint8)(bgColor.r + (hoverColor.r - bgColor.r) * animProgress);
        bgColor.g = (Uint8)(bgColor.g + (hoverColor.g - bgColor.g) * animProgress);
        bgColor.b = (Uint8)(bgColor.b + (hoverColor.b - bgColor.b) * animProgress);
        
        // 绘制阴影
        SDL_Color shadowColor = Gfx::COLOR_SHADOW;
        shadowColor.a = (Uint8)(100 * animProgress);
        Gfx::DrawRectRounded(drawX + 4, drawY + 4, scaledW, scaledH, 12, shadowColor);
        
        // 绘制边框
        SDL_Color borderColor = Gfx::COLOR_ACCENT;
        borderColor.a = (Uint8)(180 * animProgress);
        Gfx::DrawRectRoundedOutline(drawX - 2, drawY - 2, scaledW + 4, scaledH + 4, 14, 3, borderColor);
    }
    
    Gfx::DrawRectRounded(drawX, drawY, scaledW, scaledH, 12, bgColor);
    
    // 绘制文本
    SDL_Color titleColor = Gfx::COLOR_TEXT;
    SDL_Color descColor = Gfx::COLOR_ALT_TEXT;
    SDL_Color valueColor = Gfx::COLOR_ICON;
    
    if (selected) {
        // 根据动画进度插值文本颜色
        SDL_Color whiteColor = Gfx::COLOR_WHITE;
        titleColor.r = (Uint8)(titleColor.r + (whiteColor.r - titleColor.r) * animProgress);
        titleColor.g = (Uint8)(titleColor.g + (whiteColor.g - titleColor.g) * animProgress);
        titleColor.b = (Uint8)(titleColor.b + (whiteColor.b - titleColor.b) * animProgress);
        
        SDL_Color accentColor = Gfx::COLOR_ACCENT;
        valueColor.r = (Uint8)(valueColor.r + (accentColor.r - valueColor.r) * animProgress);
        valueColor.g = (Uint8)(valueColor.g + (accentColor.g - valueColor.g) * animProgress);
        valueColor.b = (Uint8)(valueColor.b + (accentColor.b - valueColor.b) * animProgress);
    }
    
    int textX = drawX + 40;
    int valueX = drawX + scaledW - 40;
    
    // 计算垂直居中位置
    const int titleSize = 38;  // 从44减小到38
    const int descSize = 28;   // 从32减小到28
    const int titleHeight = Gfx::GetTextHeight(titleSize, title.c_str());
    const int descHeight = Gfx::GetTextHeight(descSize, description.c_str());
    const int totalTextHeight = titleHeight + descHeight + 8; // 8px间距
    const int textStartY = drawY + (scaledH - totalTextHeight) / 2;
    
    Gfx::Print(textX, textStartY, titleSize, titleColor, title, Gfx::ALIGN_VERTICAL);
    Gfx::Print(textX, textStartY + titleHeight + 8, descSize, descColor, description, Gfx::ALIGN_VERTICAL);
    
    // 计算值文本宽度,为箭头留出空间
    const int valueSize = 36;  // 从40减小到36
    int valueWidth = Gfx::GetTextWidth(valueSize, value.c_str());
    int arrowWidth = 28;       // 从32减小到28
    int spacing = 50; // 箭头和值文本之间的间距
    
    if (selected && animProgress > 0.1f) {
        // 箭头位置:在值文本左侧,带有动画淡入效果
        int arrowX = valueX - valueWidth - spacing;
        SDL_Color arrowColor = Gfx::COLOR_ACCENT;
        arrowColor.a = (Uint8)(255 * animProgress);
        Gfx::DrawIcon(arrowX, drawY + scaledH/2, arrowWidth, arrowColor, 0xf054, Gfx::ALIGN_VERTICAL);
    }
    
    Gfx::Print(valueX, drawY + scaledH/2, valueSize, valueColor, value, Gfx::ALIGN_VERTICAL | Gfx::ALIGN_RIGHT);
}

void SettingsScreen::DrawLanguageDialog() {
    // 半透明遮罩
    SDL_Color overlay = {0, 0, 0, 180};
    Gfx::DrawRectFilled(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, overlay);
    
    // 对话框
    const int dialogW = 800;
    const int dialogH = 500;
    const int dialogX = (Gfx::SCREEN_WIDTH - dialogW) / 2;
    const int dialogY = (Gfx::SCREEN_HEIGHT - dialogH) / 2;
    
    // 绘制对话框背景
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = 120;
    Gfx::DrawRectRounded(dialogX + 8, dialogY + 8, dialogW, dialogH, 20, shadowColor);
    Gfx::DrawRectRounded(dialogX, dialogY, dialogW, dialogH, 20, Gfx::COLOR_CARD_BG);
    
    // 标题
    Gfx::Print(dialogX + dialogW/2, dialogY + 50, 48, Gfx::COLOR_TEXT, 
              _("settings.language"), Gfx::ALIGN_CENTER);
    
    // 语言列表
    const auto& languages = Lang().GetAvailableLanguages();
    const int itemH = 80;
    const int listStartY = dialogY + 120;
    
    for (size_t i = 0; i < languages.size(); i++) {
        int itemY = listStartY + static_cast<int>(i) * itemH;
        bool isSelected = (static_cast<int>(i) == mSelectedLanguage);
        bool isCurrent = (languages[i].code == Lang().GetCurrentLanguage());
        
        // 绘制选中背景
        if (isSelected) {
            SDL_Color selectBg = Gfx::COLOR_ACCENT;
            selectBg.a = 60;
            Gfx::DrawRectRounded(dialogX + 40, itemY, dialogW - 80, itemH - 10, 8, selectBg);
        }
        
        // 绘制语言名称
        SDL_Color textColor = isSelected ? Gfx::COLOR_WHITE : Gfx::COLOR_TEXT;
        Gfx::Print(dialogX + 80, itemY + itemH/2, 42, textColor, 
                  languages[i].name, Gfx::ALIGN_VERTICAL);
        
        // 绘制当前语言标记
        if (isCurrent) {
            Gfx::DrawIcon(dialogX + dialogW - 100, itemY + itemH/2, 32, Gfx::COLOR_SUCCESS, 
                         0xf00c, Gfx::ALIGN_VERTICAL);
        }
    }
    
    // 底部按钮提示
    Gfx::Print(dialogX + dialogW/2, dialogY + dialogH - 60, 36, Gfx::COLOR_ALT_TEXT, 
              (std::string(_("input.confirm")) + "  " + _("input.back")).c_str(), Gfx::ALIGN_CENTER);
}

bool SettingsScreen::Update(Input &input) {
    if (mLanguageDialogOpen) {
        return UpdateLanguageDialog(input);
    }
    
    // 按B键返回
    if (input.data.buttons_d & Input::BUTTON_B) {
        return false;
    }
    
    // 检测上下按键(D-Pad + 摇杆)
    bool upPressed = (input.data.buttons_d & Input::BUTTON_UP) || (input.data.buttons_d & Input::STICK_L_UP);
    bool downPressed = (input.data.buttons_d & Input::BUTTON_DOWN) || (input.data.buttons_d & Input::STICK_L_DOWN);
    bool upHeld = (input.data.buttons_h & Input::BUTTON_UP) || (input.data.buttons_h & Input::STICK_L_UP);
    bool downHeld = (input.data.buttons_h & Input::BUTTON_DOWN) || (input.data.buttons_h & Input::STICK_L_DOWN);
    
    // 上下选择设置项(支持循环选择)
    bool shouldMoveUp = false;
    bool shouldMoveDown = false;
    
    if (upPressed) {
        shouldMoveUp = true;
        mHoldFrames = 0;
    } else if (upHeld) {
        mHoldFrames++;
        if (mHoldFrames >= mRepeatDelay) {
            if ((mHoldFrames - mRepeatDelay) % mRepeatRate == 0) {
                shouldMoveUp = true;
            }
        }
    } else if (downPressed) {
        shouldMoveDown = true;
        mHoldFrames = 0;
    } else if (downHeld) {
        mHoldFrames++;
        if (mHoldFrames >= mRepeatDelay) {
            if ((mHoldFrames - mRepeatDelay) % mRepeatRate == 0) {
                shouldMoveDown = true;
            }
        }
    } else {
        mHoldFrames = 0;
    }
    
    if (shouldMoveUp) {
        mSelectedItem = (mSelectedItem - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
    } else if (shouldMoveDown) {
        mSelectedItem = (mSelectedItem + 1) % SETTINGS_COUNT;
    }
    
    // A键进入设置项
    if (input.data.buttons_d & Input::BUTTON_A) {
        switch (mSelectedItem) {
            case SETTINGS_LANGUAGE:
                mLanguageDialogOpen = true;
                break;
            case SETTINGS_DOWNLOAD_PATH:
                // TODO: 打开路径设置对话框
                break;
            case SETTINGS_AUTO_INSTALL:
                // TODO: 切换自动安装选项
                break;
            case SETTINGS_BGM_ENABLED:
                // 切换背景音乐
                {
                    bool newState = !Config::GetInstance().IsBgmEnabled();
                    Config::GetInstance().SetBgmEnabled(newState);
                    // MusicPlayer会在下一帧检查配置并更新状态
                }
                break;
            case SETTINGS_LOGGING_ENABLED:
                // 切换日志启用状态
                {
                    bool newState = !Config::GetInstance().IsLoggingEnabled();
                    Config::GetInstance().SetLoggingEnabled(newState);
                    FileLogger::GetInstance().SetEnabled(newState);
                    if (newState) {
                        FileLogger::GetInstance().StartLog();
                    }
                }
                break;
            case SETTINGS_LOGGING_VERBOSE:
                // 切换详细日志模式
                {
                    bool newState = !Config::GetInstance().IsVerboseLogging();
                    Config::GetInstance().SetVerboseLogging(newState);
                    FileLogger::GetInstance().SetVerbose(newState);
                }
                break;
        }
    }
    
    return true;
}

bool SettingsScreen::UpdateLanguageDialog(Input &input) {
    const auto& languages = Lang().GetAvailableLanguages();
    
    // 按B键关闭对话框
    if (input.data.buttons_d & Input::BUTTON_B) {
        mLanguageDialogOpen = false;
        mDialogHoldFrames = 0;  // 重置计数器
        return true;
    }
    
    // 检测上下按键(D-Pad + 摇杆)
    bool upPressed = (input.data.buttons_d & Input::BUTTON_UP) || (input.data.buttons_d & Input::STICK_L_UP);
    bool downPressed = (input.data.buttons_d & Input::BUTTON_DOWN) || (input.data.buttons_d & Input::STICK_L_DOWN);
    bool upHeld = (input.data.buttons_h & Input::BUTTON_UP) || (input.data.buttons_h & Input::STICK_L_UP);
    bool downHeld = (input.data.buttons_h & Input::BUTTON_DOWN) || (input.data.buttons_h & Input::STICK_L_DOWN);
    
    // 上下选择语言(支持循环选择)
    bool shouldMoveUp = false;
    bool shouldMoveDown = false;
    
    if (upPressed) {
        shouldMoveUp = true;
        mDialogHoldFrames = 0;
    } else if (upHeld) {
        mDialogHoldFrames++;
        if (mDialogHoldFrames >= mRepeatDelay) {
            if ((mDialogHoldFrames - mRepeatDelay) % mRepeatRate == 0) {
                shouldMoveUp = true;
            }
        }
    } else if (downPressed) {
        shouldMoveDown = true;
        mDialogHoldFrames = 0;
    } else if (downHeld) {
        mDialogHoldFrames++;
        if (mDialogHoldFrames >= mRepeatDelay) {
            if ((mDialogHoldFrames - mRepeatDelay) % mRepeatRate == 0) {
                shouldMoveDown = true;
            }
        }
    } else {
        mDialogHoldFrames = 0;
    }
    
    if (shouldMoveUp) {
        mSelectedLanguage = (mSelectedLanguage - 1 + static_cast<int>(languages.size())) % static_cast<int>(languages.size());
    } else if (shouldMoveDown) {
        mSelectedLanguage = (mSelectedLanguage + 1) % static_cast<int>(languages.size());
    }
    
    // A键确认选择语言
    if (input.data.buttons_d & Input::BUTTON_A) {
        if (mSelectedLanguage >= 0 && mSelectedLanguage < static_cast<int>(languages.size())) {
            std::string newLang = languages[mSelectedLanguage].code;
            // SetCurrentLanguage内部会自动保存到Config
            Lang().SetCurrentLanguage(newLang);
            mLanguageDialogOpen = false;
        }
    }
    
    return true;
}
