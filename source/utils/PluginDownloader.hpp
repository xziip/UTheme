#pragma once

#include <string>
#include <functional>

// 插件下载器 - 检查并下载必需的插件
class PluginDownloader {
public:
    static PluginDownloader& GetInstance();
    
    // 检查并下载 StyleMiiU 插件
    // 返回: true=已存在或下载成功, false=下载失败
    bool CheckAndDownloadStyleMiiU();
    
    // 下载指定URL的文件到指定路径
    bool DownloadFile(const std::string& url, const std::string& destPath);
    
private:
    PluginDownloader() = default;
    ~PluginDownloader() = default;
    PluginDownloader(const PluginDownloader&) = delete;
    PluginDownloader& operator=(const PluginDownloader&) = delete;
    
    // CURL 写入回调
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
};
