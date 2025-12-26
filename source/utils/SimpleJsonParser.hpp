#pragma once

#include <string>
#include <map>
#include <vector>

// 简单的JSON值类型
enum JsonType {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
};

// 简单的JSON值
class JsonValue {
public:
    JsonType type = JSON_NULL;
    
    // 值存储
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;
    
    JsonValue() = default;
    
    // 类型检查
    bool isNull() const { return type == JSON_NULL; }
    bool isBool() const { return type == JSON_BOOL; }
    bool isNumber() const { return type == JSON_NUMBER; }
    bool isString() const { return type == JSON_STRING; }
    bool isArray() const { return type == JSON_ARRAY; }
    bool isObject() const { return type == JSON_OBJECT; }
    
    // 获取值
    bool asBool() const { return boolValue; }
    int asInt() const { return (int)numberValue; }
    double asDouble() const { return numberValue; }
    const std::string& asString() const { return stringValue; }
    
    // 数组访问
    size_t size() const { return arrayValue.size(); }
    const JsonValue& operator[](size_t index) const { 
        static JsonValue null;
        return index < arrayValue.size() ? arrayValue[index] : null;
    }
    
    // 对象访问
    bool has(const std::string& key) const {
        return objectValue.find(key) != objectValue.end();
    }
    
    const JsonValue& operator[](const std::string& key) const {
        static JsonValue null;
        auto it = objectValue.find(key);
        return it != objectValue.end() ? it->second : null;
    }
};

// 简单的JSON解析器
class SimpleJsonParser {
public:
    static JsonValue Parse(const std::string& json);
    
private:
    static JsonValue ParseValue(const char*& ptr);
    static JsonValue ParseObject(const char*& ptr);
    static JsonValue ParseArray(const char*& ptr);
    static JsonValue ParseString(const char*& ptr);
    static JsonValue ParseNumber(const char*& ptr);
    static JsonValue ParseBool(const char*& ptr);
    static JsonValue ParseNull(const char*& ptr);
    
    static void SkipWhitespace(const char*& ptr);
    static std::string UnescapeString(const std::string& str);
};
