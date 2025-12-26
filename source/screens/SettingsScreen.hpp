#pragma once
#include "Screen.hpp"
#include "../utils/LanguageManager.hpp"
#include "../utils/Animation.hpp"

class SettingsScreen : public Screen {
public:
    SettingsScreen();
    ~SettingsScreen() override;

    void Draw() override;
    bool Update(Input &input) override;

private:
    enum SettingsItem {
        SETTINGS_LANGUAGE,
        SETTINGS_DOWNLOAD_PATH,
        SETTINGS_AUTO_INSTALL,
        SETTINGS_BGM_ENABLED,
        SETTINGS_LOGGING_ENABLED,
        SETTINGS_LOGGING_VERBOSE,
        
        SETTINGS_COUNT
    };
    
    int mFrameCount = 0;
    int mSelectedItem = SETTINGS_LANGUAGE;
    bool mLanguageDialogOpen = false;
    int mSelectedLanguage = 0;
    int mPrevSelectedItem = SETTINGS_LANGUAGE; // 用于追踪上一个选中项
    Animation mTitleAnim;
    Animation mSelectionAnim; // 选项切换动画
    float mItemAnimProgress[SETTINGS_COUNT]; // 每个选项的动画进度
    
    // 长按连续选择
    int mHoldFrames = 0;
    int mRepeatDelay = 30;  // 初始延迟帧数 (约0.5秒)
    int mRepeatRate = 8;    // 重复间隔帧数 (约0.13秒)
    
    // 语言对话框的长按连续选择
    int mDialogHoldFrames = 0;
    
    void DrawSettingItem(int x, int y, int w, const std::string& title, 
                        const std::string& description, const std::string& value, 
                        bool selected, float animProgress);
    void DrawLanguageDialog();
    bool UpdateLanguageDialog(Input &input);
};
