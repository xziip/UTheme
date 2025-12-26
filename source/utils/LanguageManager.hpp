#pragma once
#include <string>
#include <map>
#include <vector>

class LanguageManager {
public:
    struct LanguageInfo {
        std::string code;
        std::string name;
        std::string filename;
    };

    static LanguageManager& getInstance();
    
    // 初始化语言系统
    bool Initialize();
    
    // 加载指定语言
    bool LoadLanguage(const std::string& languageCode);
    
    // 获取文本
    std::string GetText(const std::string& key) const;
    
    // 获取当前语言代码
    const std::string& GetCurrentLanguage() const { return mCurrentLanguage; }
    
    // 获取可用语言列表
    const std::vector<LanguageInfo>& GetAvailableLanguages() const { return mAvailableLanguages; }
    
    // 设置当前语言
    void SetCurrentLanguage(const std::string& languageCode);
    
    // 保存语言设置
    void SaveLanguageSettings();

private:
    LanguageManager() = default;
    ~LanguageManager() = default;
    LanguageManager(const LanguageManager&) = delete;
    LanguageManager& operator=(const LanguageManager&) = delete;
    
    // 解析嵌套的JSON键（如 "menu.download_themes"）
    std::string GetNestedValue(const std::string& key) const;
    
    // 加载设置
    void LoadSettings();
    
    std::string mCurrentLanguage = "zh-cn";
    std::map<std::string, std::string> mTexts;
    std::vector<LanguageInfo> mAvailableLanguages;
    
    static LanguageManager* mInstance;
};

// 便捷宏定义
#define Lang() LanguageManager::getInstance()
#define _(key) Lang().GetText(key)
