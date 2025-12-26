#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <SDL2/SDL.h>

// 前向声明
struct DownloadOperation;
class ThemeDownloader;

// 主题图片数据
struct ThemeImage {
    std::string thumbUrl;  // 缩略图 URL
    std::string hdUrl;     // 高清图 URL
    
    // 本地缓存
    bool thumbLoaded = false;
    bool hdLoaded = false;
    SDL_Texture* thumbTexture = nullptr;
    SDL_Texture* hdTexture = nullptr;
};

// 主题数据结构
struct Theme {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string downloadUrl;
    int downloads = 0;
    int likes = 0;
    std::string version;
    std::string updatedAt;
    std::vector<std::string> tags;
    
    // 图片资源
    ThemeImage collagePreview;      // 组合预览图(列表缩略图)
    ThemeImage launcherScreenshot;  // Launcher 截图
    ThemeImage waraWaraScreenshot;  // Wara Wara Plaza 截图
    
    std::string launcherBgUrl;      // Launcher 背景 URL
    std::string waraWaraBgUrl;      // Wara Wara 背景 URL
};

// 主题管理器
class ThemeManager {
public:
    enum FetchState {
        FETCH_IDLE,
        FETCH_IN_PROGRESS,
        FETCH_SUCCESS,
        FETCH_ERROR
    };
    
    ThemeManager();
    ~ThemeManager();
    
    // 获取主题列表
    void FetchThemes();
    
    // 下载主题
    void DownloadTheme(const Theme& theme);
    float GetDownloadProgress() const;
    int GetDownloadState() const;  // 返回 DownloadState 枚举
    std::string GetDownloadError() const;
    std::string GetDownloadedFilePath() const;
    std::string GetExtractedPath() const;
    void CancelDownload();
    
    // 获取状态
    FetchState GetState() const { return mState; }
    const std::string& GetError() const { return mErrorMessage; }
    
    // 获取主题列表
    const std::vector<Theme>& GetThemes() const { return mThemes; }
    std::vector<Theme>& GetThemes() { return mThemes; }  // 非 const 版本
    
    // 检查是否有缓存数据
    bool HasCachedThemes() const { return !mThemes.empty(); }
    
    // 强制刷新(清除缓存)
    void ForceRefresh() {
        mThemes.clear();
        FetchThemes();
    }
    
    // 缓存管理
    bool SaveCache();           // 保存缓存到文件
    bool LoadCache();           // 从文件加载缓存
    bool IsCacheValid() const;  // 检查缓存是否有效
    void CheckForUpdates();     // 后台检测更新
    bool HasUpdates() const { return mHasUpdates; }
    
    // 更新(在主循环中调用)
    void Update();
    
    // 设置回调
    void SetProgressCallback(std::function<void(float progress, long downloaded, long total)> callback);
    void SetStateCallback(std::function<void(FetchState state, const std::string& message)> callback);
    
private:
    std::vector<Theme> mThemes;
    FetchState mState = FETCH_IDLE;
    std::string mErrorMessage;
    bool mHasUpdates = false;
    bool mCheckingUpdates = false;
    DownloadOperation* mFetchOp = nullptr;  // 异步网络请求操作
    ThemeDownloader* mDownloader = nullptr; // 主题下载器
    bool mDownloaderNeedsCleanup = false;   // 标记下载器需要清理
    
    // 回调
    std::function<void(float progress, long downloaded, long total)> mProgressCallback;
    std::function<void(FetchState state, const std::string& message)> mStateCallback;
    
    // 内部方法
    bool ParseThemezerResponse(const std::string& jsonData);
    std::string FetchUrl(const std::string& url, const std::string& postData = "");
    std::string GetCachePath() const;
    std::string SerializeThemes() const;
    bool DeserializeThemes(const std::string& data);
    void SaveThemeMetadata(const Theme& theme, const std::string& themePath);
    bool DownloadImageToFile(const std::string& url, const std::string& filePath);
    
    // 静态方法供异步线程使用
    static bool DownloadImageToFileStatic(const std::string& url, const std::string& filePath);
};
