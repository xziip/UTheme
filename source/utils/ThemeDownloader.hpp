#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <curl/curl.h>

// 下载状态
enum DownloadState {
    DOWNLOAD_IDLE,
    DOWNLOAD_DOWNLOADING,
    DOWNLOAD_EXTRACTING,
    DOWNLOAD_COMPLETE,
    DOWNLOAD_ERROR,
    DOWNLOAD_CANCELLED
};

// 主题下载器
class ThemeDownloader {
public:
    ThemeDownloader();
    ~ThemeDownloader();
    
    // 下载并安装主题（异步）
    void DownloadThemeAsync(const std::string& downloadUrl, const std::string& themeName);
    
    // 状态查询
    DownloadState GetState() const { return mState; }
    float GetProgress() const { return mProgress; }
    const std::string& GetError() const { return mErrorMessage; }
    bool IsDownloading() const { return mState == DOWNLOAD_DOWNLOADING || mState == DOWNLOAD_EXTRACTING; }
    std::string GetDownloadedFilePath() const { return mTempFilePath; }
    std::string GetExtractedPath() const { return mExtractPath; }
    
    // 回调设置
    void SetProgressCallback(std::function<void(float progress, long downloaded, long total)> callback);
    void SetStateCallback(std::function<void(DownloadState state, const std::string& message)> callback);
    
    // 取消下载
    void Cancel();
    
private:
    std::atomic<DownloadState> mState;
    std::atomic<float> mProgress;
    std::atomic<bool> mCancelRequested;
    std::string mErrorMessage;
    
    std::string mTempFilePath;
    std::string mExtractPath;
    
    std::thread mDownloadThread;
    
    // 回调
    std::function<void(float progress, long downloaded, long total)> mProgressCallback;
    std::function<void(DownloadState state, const std::string& message)> mStateCallback;
    
    // 内部方法
    void DownloadThreadFunc(const std::string& url, const std::string& themeName);
    std::string SanitizeFileName(const std::string& fileName); // 清理文件名
    bool DownloadFile(const std::string& url, const std::string& outputPath);
    bool ExtractZip(const std::string& zipPath, const std::string& extractPath);
    bool CreateDirectoryRecursive(const std::string& path);
    
    // CURL 回调
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static int ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                               curl_off_t ultotal, curl_off_t ulnow);
};
