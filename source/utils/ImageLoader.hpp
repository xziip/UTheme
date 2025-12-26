#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>
#include <SDL2/SDL.h>

// 图片加载器 - 从 URL 下载并创建 SDL 纹理
class ImageLoader {
public:
    // 初始化 SDL_image
    static void Init();
    
    // 清理资源
    static void Cleanup();
    
    // 同步加载图片 (阻塞)
    static SDL_Texture* LoadFromUrl(const std::string& url);
    
    // 从内存数据加载图片
    static SDL_Texture* LoadFromMemory(const void* data, size_t size);
    
    // 异步加载图片 (非阻塞,使用回调)
    struct LoadRequest {
        std::string url;
        std::function<void(SDL_Texture*)> callback;
        bool highPriority = false;
    };
    static void LoadAsync(const LoadRequest& request);
    
    // 处理异步加载队列 (在主循环中调用)
    static void Update();
    
    // 缓存管理
    static SDL_Texture* GetCached(const std::string& url);
    static void CacheTexture(const std::string& url, SDL_Texture* texture);
    static void ClearCache();
    static void RemoveFromCache(const std::string& url);
    
    // 磁盘缓存
    static bool SaveToCache(const std::string& url, const void* data, size_t size);
    static std::vector<uint8_t> LoadFromCache(const std::string& url);
    static std::string GetCachePath(const std::string& url);
    static std::string UrlToFilename(const std::string& url);
    
    // 统计信息
    static size_t GetCacheSize() { return mTextureCache.size(); }
    static size_t GetQueueSize() { return mLoadQueue.size(); }
    
private:
    static std::map<std::string, SDL_Texture*> mTextureCache;
    static std::vector<LoadRequest> mLoadQueue;
    static bool mInitialized;
    
    // 内部辅助函数
    static std::vector<uint8_t> DownloadData(const std::string& url);
};
