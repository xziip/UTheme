#include "PluginDownloader.hpp"
#include "FileLogger.hpp"
#include "../Screen.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>

PluginDownloader& PluginDownloader::GetInstance() {
    static PluginDownloader instance;
    return instance;
}

bool PluginDownloader::CheckAndDownloadStyleMiiU() {
    const char* pluginPath = "fs:/vol/external01/wiiu/environments/aroma/plugins/stylemiiu.wps";
    const char* downloadUrl = "https://github.com/Themiify-hb/StyleMiiU-Plugin/releases/download/0.4.3/stylemiiu.wps";
    
    FileLogger::GetInstance().LogInfo("[PluginDownloader] Checking for StyleMiiU plugin at: %s", pluginPath);
    
    // 检查文件是否存在
    struct stat st;
    if (stat(pluginPath, &st) == 0) {
        FileLogger::GetInstance().LogInfo("[PluginDownloader] StyleMiiU plugin already exists");
        return true;
    }
    
    FileLogger::GetInstance().LogInfo("[PluginDownloader] StyleMiiU plugin not found, downloading...");
    
    // 下载插件
    bool success = DownloadFile(downloadUrl, pluginPath);
    
    if (success) {
        FileLogger::GetInstance().LogInfo("[PluginDownloader] StyleMiiU plugin downloaded successfully");
        Screen::GetBgmNotification().ShowNowPlaying("StyleMiiU Plugin Downloaded");
    } else {
        FileLogger::GetInstance().LogError("[PluginDownloader] Failed to download StyleMiiU plugin");
        Screen::GetBgmNotification().ShowError("Failed to download StyleMiiU plugin");
    }
    
    return success;
}

size_t PluginDownloader::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    FILE* file = static_cast<FILE*>(userp);
    
    if (file) {
        return fwrite(contents, 1, totalSize, file);
    }
    
    return totalSize;
}

bool PluginDownloader::DownloadFile(const std::string& url, const std::string& destPath) {
    FileLogger::GetInstance().LogInfo("[PluginDownloader] Downloading from: %s", url.c_str());
    FileLogger::GetInstance().LogInfo("[PluginDownloader] Destination: %s", destPath.c_str());
    
    // 确保目录存在
    std::string dirPath = destPath.substr(0, destPath.find_last_of('/'));
    FileLogger::GetInstance().LogInfo("[PluginDownloader] Creating directory: %s", dirPath.c_str());
    
    // 创建多层目录
    size_t pos = 0;
    while ((pos = dirPath.find('/', pos + 1)) != std::string::npos) {
        std::string subDir = dirPath.substr(0, pos);
        struct stat st;
        if (stat(subDir.c_str(), &st) != 0) {
            mkdir(subDir.c_str(), 0777);
        }
    }
    // 创建最后一层目录
    struct stat st;
    if (stat(dirPath.c_str(), &st) != 0) {
        mkdir(dirPath.c_str(), 0777);
    }
    
    // 使用临时文件
    std::string tempPath = destPath + ".tmp";
    
    // 打开文件
    FILE* file = fopen(tempPath.c_str(), "wb");
    if (!file) {
        FileLogger::GetInstance().LogError("[PluginDownloader] Failed to create file: %s", tempPath.c_str());
        return false;
    }
    
    // 初始化 CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        FileLogger::GetInstance().LogError("[PluginDownloader] Failed to initialize CURL");
        fclose(file);
        remove(tempPath.c_str());
        return false;
    }
    
    // 配置 CURL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // 5分钟超时
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "UTheme/1.0");
    
    // 执行下载
    CURLcode res = curl_easy_perform(curl);
    
    // 获取 HTTP 状态码
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    // 清理
    curl_easy_cleanup(curl);
    fclose(file);
    
    // 检查结果
    if (res != CURLE_OK) {
        FileLogger::GetInstance().LogError("[PluginDownloader] Download failed: %s", curl_easy_strerror(res));
        remove(tempPath.c_str());
        return false;
    }
    
    if (httpCode != 200) {
        FileLogger::GetInstance().LogError("[PluginDownloader] HTTP error: %ld", httpCode);
        remove(tempPath.c_str());
        return false;
    }
    
    // 重命名临时文件
    remove(destPath.c_str());
    if (rename(tempPath.c_str(), destPath.c_str()) != 0) {
        FileLogger::GetInstance().LogError("[PluginDownloader] Failed to rename file");
        remove(tempPath.c_str());
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("[PluginDownloader] Download completed successfully");
    return true;
}
