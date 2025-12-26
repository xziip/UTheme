#pragma once
#include <string>
#include <cstdio>

class FileLogger {
public:
    static FileLogger& GetInstance();
    
    // 日志级别
    enum LogLevel {
        LOG_DEBUG = 0,
        LOG_INFO = 1,
        LOG_WARNING = 2,
        LOG_ERROR = 3
    };
    
    // 开始一个新的日志会话
    bool StartLog();
    
    // 记录日志
    void Log(const char* format, ...);
    void LogDebug(const char* format, ...);
    void LogInfo(const char* format, ...);
    void LogWarning(const char* format, ...);
    void LogError(const char* format, ...);
    
    // 结束日志会话
    void EndLog();
    
    // 设置和获取
    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    
    void SetVerbose(bool verbose);
    bool IsVerbose() const;
    
    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const;
    
    // 获取当前日志文件路径
    const std::string& GetCurrentLogPath() const { return mCurrentLogPath; }
    
private:
    FileLogger();
    ~FileLogger();
    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;
    
    void WriteLog(const char* level, const char* format, va_list args);
    int GetNextLogNumber();
    
    FILE* mLogFile;
    std::string mCurrentLogPath;
    bool mEnabled;
    bool mVerbose;
    LogLevel mLogLevel;
};
