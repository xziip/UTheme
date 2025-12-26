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
    std::string mConfigPath;
};
