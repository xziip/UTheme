#include "SimpleJsonParser.hpp"
#include <cstring>
#include <cctype>

void SimpleJsonParser::SkipWhitespace(const char*& ptr) {
    while (*ptr && std::isspace(*ptr)) {
        ptr++;
    }
}

std::string SimpleJsonParser::UnescapeString(const std::string& str) {
    std::string result;
    result.reserve(str.length());
    
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            switch (str[i + 1]) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                default: result += str[i + 1]; break;
            }
            i++;
        } else {
            result += str[i];
        }
    }
    
    return result;
}

JsonValue SimpleJsonParser::ParseString(const char*& ptr) {
    JsonValue value;
    value.type = JSON_STRING;
    
    ptr++; // Skip opening quote
    
    std::string str;
    while (*ptr && *ptr != '"') {
        if (*ptr == '\\' && *(ptr + 1)) {
            str += *ptr++;
            str += *ptr++;
        } else {
            str += *ptr++;
        }
    }
    
    if (*ptr == '"') ptr++; // Skip closing quote
    
    value.stringValue = UnescapeString(str);
    return value;
}

JsonValue SimpleJsonParser::ParseNumber(const char*& ptr) {
    JsonValue value;
    value.type = JSON_NUMBER;
    
    char* end;
    value.numberValue = std::strtod(ptr, &end);
    ptr = end;
    
    return value;
}

JsonValue SimpleJsonParser::ParseBool(const char*& ptr) {
    JsonValue value;
    value.type = JSON_BOOL;
    
    if (std::strncmp(ptr, "true", 4) == 0) {
        value.boolValue = true;
        ptr += 4;
    } else if (std::strncmp(ptr, "false", 5) == 0) {
        value.boolValue = false;
        ptr += 5;
    }
    
    return value;
}

JsonValue SimpleJsonParser::ParseNull(const char*& ptr) {
    JsonValue value;
    value.type = JSON_NULL;
    ptr += 4; // Skip "null"
    return value;
}

JsonValue SimpleJsonParser::ParseArray(const char*& ptr) {
    JsonValue value;
    value.type = JSON_ARRAY;
    
    ptr++; // Skip '['
    SkipWhitespace(ptr);
    
    if (*ptr == ']') {
        ptr++;
        return value;
    }
    
    while (*ptr) {
        value.arrayValue.push_back(ParseValue(ptr));
        SkipWhitespace(ptr);
        
        if (*ptr == ',') {
            ptr++;
            SkipWhitespace(ptr);
        } else if (*ptr == ']') {
            ptr++;
            break;
        } else {
            break;
        }
    }
    
    return value;
}

JsonValue SimpleJsonParser::ParseObject(const char*& ptr) {
    JsonValue value;
    value.type = JSON_OBJECT;
    
    ptr++; // Skip '{'
    SkipWhitespace(ptr);
    
    if (*ptr == '}') {
        ptr++;
        return value;
    }
    
    while (*ptr) {
        SkipWhitespace(ptr);
        
        // Parse key
        if (*ptr != '"') break;
        JsonValue key = ParseString(ptr);
        
        SkipWhitespace(ptr);
        if (*ptr != ':') break;
        ptr++; // Skip ':'
        
        // Parse value
        SkipWhitespace(ptr);
        value.objectValue[key.stringValue] = ParseValue(ptr);
        
        SkipWhitespace(ptr);
        if (*ptr == ',') {
            ptr++;
        } else if (*ptr == '}') {
            ptr++;
            break;
        } else {
            break;
        }
    }
    
    return value;
}

JsonValue SimpleJsonParser::ParseValue(const char*& ptr) {
    SkipWhitespace(ptr);
    
    if (*ptr == '"') {
        return ParseString(ptr);
    } else if (*ptr == '{') {
        return ParseObject(ptr);
    } else if (*ptr == '[') {
        return ParseArray(ptr);
    } else if (*ptr == 't' || *ptr == 'f') {
        return ParseBool(ptr);
    } else if (*ptr == 'n') {
        return ParseNull(ptr);
    } else if (*ptr == '-' || std::isdigit(*ptr)) {
        return ParseNumber(ptr);
    }
    
    return JsonValue();
}

JsonValue SimpleJsonParser::Parse(const std::string& json) {
    const char* ptr = json.c_str();
    return ParseValue(ptr);
}
