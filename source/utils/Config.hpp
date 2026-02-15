#pragma once
#include <string>

class Config {
public:
    static Config& GetInstance();
    
    // 日志设置
    bool IsLoggingEnabled() const { return mLoggingEnabled; }
    void SetLoggingEnabled(bool enabled);
    
    bool IsVerboseLogging() const { return mVerboseLogging; }
    void SetVerboseLogging(bool verbose);
    
    // 语言设置
    std::string GetLanguage() const { return mLanguage; }
    void SetLanguage(const std::string& lang);
    
    // 下载路径设置
    std::string GetDownloadPath() const { return mDownloadPath; }
    void SetDownloadPath(const std::string& path);
    
    // 自动安装设置
    bool IsAutoInstallEnabled() const { return mAutoInstall; }
    void SetAutoInstallEnabled(bool enabled);
    
    // 背景音乐设置
    bool IsBgmEnabled() const { return mBgmEnabled; }
    void SetBgmEnabled(bool enabled);
    
    std::string GetBgmUrl() const { return mBgmUrl; }
    void SetBgmUrl(const std::string& url);
    
    // 触摸提示设置
    bool HasShownTouchHint() const { return mHasShownTouchHint; }
    void SetTouchHintShown(bool shown);
    
    // 语言切换提示设置
    bool HasShownLanguageSwitchHint() const { return mHasShownLanguageSwitchHint; }
    void SetLanguageSwitchHintShown(bool shown);
    
    // 主题更改标志（不保存到配置文件，仅运行时使用）
    bool IsThemeChanged() const { return mThemeChanged; }
    void SetThemeChanged(bool changed) { mThemeChanged = changed; }
    
    // 加载/保存配置
    bool Load();
    bool Save();
    
private:
    Config();
    ~Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    
    bool mLoggingEnabled;
    bool mVerboseLogging;
    std::string mLanguage;          // 语言代码: "zh-cn", "en-us", "ja-jp"
    std::string mDownloadPath;      // 主题下载路径
    bool mAutoInstall;              // 下载后自动安装
    bool mBgmEnabled;               // 背景音乐开关
    std::string mBgmUrl;            // BGM下载地址
    bool mHasShownTouchHint;        // 是否已显示触摸提示
    bool mHasShownLanguageSwitchHint; // 是否已显示语言切换提示
    bool mThemeChanged;             // 主题是否被更改（运行时标志）
    std::string mConfigPath;
};
