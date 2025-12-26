#include "FileLogger.hpp"
#include <cstdarg>
#include <ctime>
#include <sys/stat.h>
#include <dirent.h>
#include <cstring>

FileLogger::FileLogger() 
    : mLogFile(nullptr)
    , mEnabled(true)      // 默认启用
    , mVerbose(false)     // 默认不详细
    , mLogLevel(LOG_INFO) // 默认INFO级别
{
}

FileLogger::~FileLogger() {
    EndLog();
}

FileLogger& FileLogger::GetInstance() {
    static FileLogger instance;
    return instance;
}

void FileLogger::SetEnabled(bool enabled) {
    mEnabled = enabled;
    if (!enabled && mLogFile) {
        EndLog();
    }
}

bool FileLogger::IsEnabled() const {
    return mEnabled;
}

void FileLogger::SetVerbose(bool verbose) {
    mVerbose = verbose;
}

bool FileLogger::IsVerbose() const {
    return mVerbose;
}

void FileLogger::SetLogLevel(LogLevel level) {
    mLogLevel = level;
}

FileLogger::LogLevel FileLogger::GetLogLevel() const {
    return mLogLevel;
}

int FileLogger::GetNextLogNumber() {
    const char* logDir = "fs:/vol/external01/log/UTheme";
    DIR* dir = opendir(logDir);
    if (!dir) {
        return 0;
    }
    
    int maxNumber = -1;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        
        // 检查文件名格式: utheme##.log
        if (name.length() >= 11 && // "utheme##.log"
            name.substr(0, 6) == "utheme" &&
            name.substr(name.length() - 4) == ".log") {
            
            // 提取数字部分
            std::string numStr = name.substr(6, name.length() - 10);
            if (numStr.length() == 2 && isdigit(numStr[0]) && isdigit(numStr[1])) {
                int num = (numStr[0] - '0') * 10 + (numStr[1] - '0');
                if (num > maxNumber) {
                    maxNumber = num;
                }
            }
        }
    }
    
    closedir(dir);
    return maxNumber + 1;
}

bool FileLogger::StartLog() {
    if (!mEnabled) {
        return false;
    }
    
    // 结束之前的日志
    EndLog();
    
    // 创建日志目录
    const char* logDir = "fs:/vol/external01/log/UTheme";
    
    // 递归创建目录
    struct stat st;
    if (stat(logDir, &st) != 0) {
        // 目录不存在,创建
        const char* paths[] = {
            "fs:/vol/external01/log",
            "fs:/vol/external01/log/UTheme"
        };
        
        for (const char* path : paths) {
            if (stat(path, &st) != 0) {
                if (mkdir(path, 0777) != 0) {
                    return false;
                }
            }
        }
    }
    
    // 获取下一个日志编号
    int logNum = GetNextLogNumber();
    if (logNum > 99) {
        logNum = 0; // 重置到 00
    }
    
    // 创建日志文件路径
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/utheme%02d.log", logDir, logNum);
    mCurrentLogPath = filename;
    
    // 打开日志文件
    mLogFile = fopen(filename, "w");
    if (!mLogFile) {
        return false;
    }
    
    // 写入日志头
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    fprintf(mLogFile, "========================================\n");
    fprintf(mLogFile, "UTheme Log File\n");
    fprintf(mLogFile, "Time: %s\n", timebuf);
    fprintf(mLogFile, "Log Level: %s\n", 
            mLogLevel == LOG_DEBUG ? "DEBUG" :
            mLogLevel == LOG_INFO ? "INFO" :
            mLogLevel == LOG_WARNING ? "WARNING" : "ERROR");
    fprintf(mLogFile, "Verbose: %s\n", mVerbose ? "Yes" : "No");
    fprintf(mLogFile, "========================================\n\n");
    fflush(mLogFile);
    
    return true;
}

void FileLogger::WriteLog(const char* level, const char* format, va_list args) {
    if (!mLogFile) {
        return;
    }
    
    // 获取时间戳
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", timeinfo);
    
    // 写入级别和时间
    fprintf(mLogFile, "[%s][%s] ", timebuf, level);
    
    // 写入消息
    vfprintf(mLogFile, format, args);
    fprintf(mLogFile, "\n");
    fflush(mLogFile);
}

void FileLogger::LogDebug(const char* format, ...) {
    if (!mLogFile || mLogLevel > LOG_DEBUG) return;
    
    va_list args;
    va_start(args, format);
    WriteLog("DEBUG", format, args);
    va_end(args);
}

void FileLogger::LogInfo(const char* format, ...) {
    if (!mLogFile || mLogLevel > LOG_INFO) return;
    
    va_list args;
    va_start(args, format);
    WriteLog("INFO", format, args);
    va_end(args);
}

void FileLogger::Log(const char* format, ...) {
    if (!mLogFile) return;
    
    va_list args;
    va_start(args, format);
    WriteLog("INFO", format, args);
    va_end(args);
}

void FileLogger::LogWarning(const char* format, ...) {
    if (!mLogFile || mLogLevel > LOG_WARNING) return;
    
    va_list args;
    va_start(args, format);
    WriteLog("WARN", format, args);
    va_end(args);
}

void FileLogger::LogError(const char* format, ...) {
    if (!mLogFile || mLogLevel > LOG_ERROR) return;
    
    va_list args;
    va_start(args, format);
    WriteLog("ERROR", format, args);
    va_end(args);
}

void FileLogger::EndLog() {
    if (mLogFile) {
        fprintf(mLogFile, "\n========================================\n");
        fprintf(mLogFile, "Log End\n");
        fprintf(mLogFile, "========================================\n");
        fclose(mLogFile);
        mLogFile = nullptr;
    }
}
