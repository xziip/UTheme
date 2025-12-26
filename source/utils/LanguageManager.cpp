#include "LanguageManager.hpp"
#include "Config.hpp"
#include "Utils.hpp"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>

// 包含嵌入的语言文件
#include <zh-cn_json.h>
#include <en-us_json.h>
#include <ja-jp_json.h>

LanguageManager* LanguageManager::mInstance = nullptr;

// 简单的JSON解析器（只处理字符串值）
class SimpleJsonParser {
public:
    static std::map<std::string, std::string> ParseFlat(const std::string& jsonStr) {
        std::map<std::string, std::string> result;
        std::string content = jsonStr;
        
        // 移除空白字符和换行
        content.erase(std::remove_if(content.begin(), content.end(), 
                     [](char c) { return c == '\n' || c == '\r' || c == '\t'; }), content.end());
        
        size_t pos = 0;
        std::string currentPath = "";
        
        ParseObject(content, pos, result, currentPath);
        
        return result;
    }

private:
    static void ParseObject(const std::string& content, size_t& pos, 
                           std::map<std::string, std::string>& result, 
                           const std::string& path) {
        // 跳过空白
        SkipWhitespace(content, pos);
        
        if (pos >= content.length() || content[pos] != '{') {
            return;
        }
        
        pos++; // 跳过 '{'
        
        while (pos < content.length()) {
            SkipWhitespace(content, pos);
            
            if (pos >= content.length()) break;
            if (content[pos] == '}') {
                pos++;
                break;
            }
            
            // 解析键
            if (content[pos] != '"') {
                pos++;
                continue;
            }
            
            std::string key = ParseString(content, pos);
            
            SkipWhitespace(content, pos);
            if (pos >= content.length() || content[pos] != ':') {
                continue;
            }
            pos++; // 跳过 ':'
            
            SkipWhitespace(content, pos);
            
            std::string fullKey = path.empty() ? key : path + "." + key;
            
            if (pos < content.length()) {
                if (content[pos] == '"') {
                    // 字符串值
                    std::string value = ParseString(content, pos);
                    result[fullKey] = value;
                } else if (content[pos] == '{') {
                    // 嵌套对象
                    ParseObject(content, pos, result, fullKey);
                } else {
                    // 跳过其他类型的值
                    SkipValue(content, pos);
                }
            }
            
            SkipWhitespace(content, pos);
            if (pos < content.length() && content[pos] == ',') {
                pos++;
            }
        }
    }
    
    static std::string ParseString(const std::string& content, size_t& pos) {
        if (pos >= content.length() || content[pos] != '"') {
            return "";
        }
        
        pos++; // 跳过开始的引号
        size_t start = pos;
        
        while (pos < content.length() && content[pos] != '"') {
            if (content[pos] == '\\') {
                pos += 2; // 跳过转义字符
            } else {
                pos++;
            }
        }
        
        std::string result = content.substr(start, pos - start);
        
        if (pos < content.length() && content[pos] == '"') {
            pos++; // 跳过结束的引号
        }
        
        return result;
    }
    
    static void SkipWhitespace(const std::string& content, size_t& pos) {
        while (pos < content.length() && 
               (content[pos] == ' ' || content[pos] == '\t' || 
                content[pos] == '\n' || content[pos] == '\r')) {
            pos++;
        }
    }
    
    static void SkipValue(const std::string& content, size_t& pos) {
        SkipWhitespace(content, pos);
        if (pos >= content.length()) return;
        
        if (content[pos] == '"') {
            ParseString(content, pos);
        } else if (content[pos] == '{') {
            int braceCount = 1;
            pos++;
            while (pos < content.length() && braceCount > 0) {
                if (content[pos] == '{') braceCount++;
                else if (content[pos] == '}') braceCount--;
                pos++;
            }
        } else if (content[pos] == '[') {
            int bracketCount = 1;
            pos++;
            while (pos < content.length() && bracketCount > 0) {
                if (content[pos] == '[') bracketCount++;
                else if (content[pos] == ']') bracketCount--;
                pos++;
            }
        } else {
            // 跳过数字、布尔值等
            while (pos < content.length() && 
                   content[pos] != ',' && content[pos] != '}' && content[pos] != ']') {
                pos++;
            }
        }
    }
};

LanguageManager& LanguageManager::getInstance() {
    if (!mInstance) {
        mInstance = new LanguageManager();
    }
    return *mInstance;
}

bool LanguageManager::Initialize() {
    DEBUG_FUNCTION_LINE("Initializing LanguageManager");
    
    // 设置可用语言
    mAvailableLanguages = {
        {"zh-cn", "简体中文", "zh-cn.json"},
        {"en-us", "English", "en-us.json"},
        {"ja-jp", "日本語", "ja-jp.json"}
    };
    
    // 加载设置
    LoadSettings();
    
    // 加载默认语言
    if (!LoadLanguage(mCurrentLanguage)) {
        // 如果加载失败，尝试加载英语
        if (!LoadLanguage("en-us")) {
            // 如果还是失败，尝试加载中文
            LoadLanguage("zh-cn");
        }
    }
    
    return true;
}

bool LanguageManager::LoadLanguage(const std::string& languageCode) {
    DEBUG_FUNCTION_LINE("Loading language: %s", languageCode.c_str());
    
    // 获取嵌入的语言数据
    const uint8_t* langData = nullptr;
    size_t langSize = 0;
    
    if (languageCode == "zh-cn") {
        langData = zh_cn_json;
        langSize = zh_cn_json_size;
    } else if (languageCode == "en-us") {
        langData = en_us_json;
        langSize = en_us_json_size;
    } else if (languageCode == "ja-jp") {
        langData = ja_jp_json;
        langSize = ja_jp_json_size;
    }
    
    if (!langData || langSize == 0) {
        DEBUG_FUNCTION_LINE("Language data not found: %s", languageCode.c_str());
        return false;
    }
    
    // 转换为字符串
    std::string content(reinterpret_cast<const char*>(langData), langSize);
    
    if (content.empty()) {
        DEBUG_FUNCTION_LINE("Language content is empty: %s", languageCode.c_str());
        return false;
    }
    
    // 解析JSON
    mTexts = SimpleJsonParser::ParseFlat(content);
    
    if (mTexts.empty()) {
        DEBUG_FUNCTION_LINE("Failed to parse language data: %s", languageCode.c_str());
        return false;
    }
    
    mCurrentLanguage = languageCode;
    DEBUG_FUNCTION_LINE("Successfully loaded language: %s (%d texts)", 
                       languageCode.c_str(), (int)mTexts.size());
    
    // 测试几个关键键是否存在
    DEBUG_FUNCTION_LINE("Test key 'app_name': %s", GetText("app_name").c_str());
    DEBUG_FUNCTION_LINE("Test key 'theme_detail.by': %s", GetText("theme_detail.by").c_str());
    
    return true;
}

std::string LanguageManager::GetText(const std::string& key) const {
    auto it = mTexts.find(key);
    if (it != mTexts.end()) {
        return it->second;
    }
    
    // 如果没找到，返回键本身
    // DEBUG_FUNCTION_LINE("Language key not found: %s (total keys: %d)", key.c_str(), (int)mTexts.size());
    return key;
}

void LanguageManager::SetCurrentLanguage(const std::string& languageCode) {
    if (LoadLanguage(languageCode)) {
        mCurrentLanguage = languageCode;
        SaveLanguageSettings();
    }
}

void LanguageManager::LoadSettings() {
    // 从Config加载语言设置
    std::string configLang = Config::GetInstance().GetLanguage();
    
    // 验证语言代码是否有效
    bool validLanguage = false;
    for (const auto& lang : mAvailableLanguages) {
        if (lang.code == configLang) {
            validLanguage = true;
            break;
        }
    }
    
    if (validLanguage) {
        mCurrentLanguage = configLang;
        DEBUG_FUNCTION_LINE("Loaded language setting from config: %s", mCurrentLanguage.c_str());
    } else {
        // 如果配置中的语言无效,使用默认中文
        mCurrentLanguage = "zh-cn";
        DEBUG_FUNCTION_LINE("Invalid language in config, using default: zh-cn");
    }
}

void LanguageManager::SaveLanguageSettings() {
    // 保存语言设置到Config
    Config::GetInstance().SetLanguage(mCurrentLanguage);
    DEBUG_FUNCTION_LINE("Saved language setting to config: %s", mCurrentLanguage.c_str());
}
