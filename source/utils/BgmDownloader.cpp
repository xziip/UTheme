#include "BgmDownloader.hpp"
#include "FileLogger.hpp"
#include "Config.hpp"
#include "MusicPlayer.hpp"
#include "../Screen.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// 每次下载的分段大小 (16KB)
#define DOWNLOAD_CHUNK_SIZE (16 * 1024)

BgmDownloader& BgmDownloader::GetInstance() {
    static BgmDownloader instance;
    return instance;
}

BgmDownloader::BgmDownloader() 
    : mState(BGM_IDLE)
    , mProgress(0.0f)
    , mDownloadedBytes(0)
    , mTotalBytes(0)
    , mCancelRequested(false)
    , mCurl(nullptr)
    , mFile(nullptr)
    , mBuffer(nullptr)
    , mBufferSize(0) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Initialized");
}

BgmDownloader::~BgmDownloader() {
    Cancel();
    
    if (mCurl) {
        curl_easy_cleanup(mCurl);
        mCurl = nullptr;
    }
    
    if (mFile) {
        fclose(mFile);
        mFile = nullptr;
    }
    
    if (mBuffer) {
        free(mBuffer);
        mBuffer = nullptr;
    }
    
    curl_global_cleanup();
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Destroyed");
}

void BgmDownloader::StartDownload(const std::string& url) {
    // 如果正在下载,先取消
    if (IsDownloading()) {
        FileLogger::GetInstance().LogInfo("[BgmDownloader] Already downloading, canceling previous download");
        Cancel();
    }
    
    mCurrentUrl = url;
    mCancelRequested.store(false);
    mState.store(BGM_DOWNLOADING);
    mProgress.store(0.0f);
    mDownloadedBytes.store(0);
    mTotalBytes.store(0);
    mErrorMessage = "";
    
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Starting download from: %s", url.c_str());
}

void BgmDownloader::Cancel() {
    if (IsDownloading()) {
        FileLogger::GetInstance().LogInfo("[BgmDownloader] Canceling download");
        mCancelRequested.store(true);
        mState.store(BGM_CANCELLED);
    }
}

void BgmDownloader::SetCompletionCallback(std::function<void(bool, const std::string&)> callback) {
    mCompletionCallback = callback;
}

void BgmDownloader::Update() {
    // 只在下载状态时处理
    if (mState.load() != BGM_DOWNLOADING) {
        return;
    }
    
    // 使用同步下载(在后台线程中调用时是非阻塞的)
    PerformDownload();
}

// CURL写入回调函数
size_t BgmDownloader::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    BgmDownloader* downloader = static_cast<BgmDownloader*>(userp);
    
    if (downloader->mCancelRequested.load()) {
        return 0; // 返回0会中止下载
    }
    
    if (downloader->mFile) {
        size_t written = fwrite(contents, 1, totalSize, downloader->mFile);
        downloader->mDownloadedBytes.fetch_add(written);
        
        // 更新进度
        size_t total = downloader->mTotalBytes.load();
        if (total > 0) {
            float progress = (float)downloader->mDownloadedBytes.load() / (float)total;
            downloader->mProgress.store(progress);
        }
        
        return written;
    }
    
    return totalSize;
}

// CURL进度回调函数
int BgmDownloader::ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                                    curl_off_t ultotal, curl_off_t ulnow) {
    BgmDownloader* downloader = static_cast<BgmDownloader*>(clientp);
    
    if (downloader->mCancelRequested.load()) {
        return 1; // 返回非0会中止下载
    }
    
    if (dltotal > 0) {
        downloader->mTotalBytes.store(dltotal);
        float progress = (float)dlnow / (float)dltotal;
        downloader->mProgress.store(progress);
    }
    
    return 0;
}

void BgmDownloader::PerformDownload() {
    const char* destPath = "fs:/vol/external01/UTheme/BGM.mp3";
    const char* tempPath = "fs:/vol/external01/UTheme/BGM.mp3.tmp";
    
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Starting download to: %s", destPath);
    
    // 确保目录存在
    const char* dirPath = "fs:/vol/external01/UTheme";
    struct stat st;
    if (stat(dirPath, &st) != 0) {
        mkdir(dirPath, 0777);
    }
    
    // 打开临时文件
    mFile = fopen(tempPath, "wb");
    if (!mFile) {
        mErrorMessage = "Failed to create temporary file";
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        mState.store(BGM_ERROR);
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 初始化CURL
    if (!mCurl) {
        mCurl = curl_easy_init();
    }
    
    if (!mCurl) {
        mErrorMessage = "Failed to initialize CURL";
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        fclose(mFile);
        mFile = nullptr;
        mState.store(BGM_ERROR);
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 配置CURL
    curl_easy_setopt(mCurl, CURLOPT_URL, mCurrentUrl.c_str());
    curl_easy_setopt(mCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(mCurl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(mCurl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(mCurl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(mCurl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(mCurl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(mCurl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(mCurl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(mCurl, CURLOPT_TIMEOUT, 300L); // 5分钟超时
    
    // 执行下载
    CURLcode res = curl_easy_perform(mCurl);
    
    // 关闭文件
    fclose(mFile);
    mFile = nullptr;
    
    // 检查结果
    if (res != CURLE_OK) {
        mErrorMessage = curl_easy_strerror(res);
        FileLogger::GetInstance().LogError("[BgmDownloader] Download failed: %s", mErrorMessage.c_str());
        remove(tempPath);
        mState.store(BGM_ERROR);
        
        // 显示错误通知
        Screen::GetBgmNotification().ShowError("Download failed: " + mErrorMessage);
        
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 获取HTTP状态码
    long httpCode = 0;
    curl_easy_getinfo(mCurl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    if (httpCode != 200) {
        mErrorMessage = "HTTP error: " + std::to_string(httpCode);
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        remove(tempPath);
        mState.store(BGM_ERROR);
        
        // 显示错误通知
        Screen::GetBgmNotification().ShowError(mErrorMessage);
        
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 重命名临时文件
    remove(destPath); // 先删除旧文件
    if (rename(tempPath, destPath) != 0) {
        mErrorMessage = "Failed to rename temporary file";
        FileLogger::GetInstance().LogError("[BgmDownloader] %s", mErrorMessage.c_str());
        remove(tempPath);
        mState.store(BGM_ERROR);
        if (mCompletionCallback) {
            mCompletionCallback(false, mErrorMessage);
        }
        return;
    }
    
    // 下载成功
    FileLogger::GetInstance().LogInfo("[BgmDownloader] Download completed successfully");
    mState.store(BGM_COMPLETE);
    mProgress.store(1.0f);
    
    // 显示成功通知
    Screen::GetBgmNotification().ShowNowPlaying("BGM.mp3");
    
    // 尝试加载音乐
    if (MusicPlayer::GetInstance().LoadMusic(destPath)) {
        MusicPlayer::GetInstance().SetEnabled(Config::GetInstance().IsBgmEnabled());
        MusicPlayer::GetInstance().SetVolume(64);
        FileLogger::GetInstance().LogInfo("[BgmDownloader] BGM loaded and playing");
    }
    
    if (mCompletionCallback) {
        mCompletionCallback(true, "");
    }
}

