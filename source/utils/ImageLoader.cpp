#include "ImageLoader.hpp"
#include "DownloadQueue.hpp"
#include "logger.h"
#include "FileLogger.hpp"
#include "../Gfx.hpp"
#include <SDL2/SDL_image.h>
#include <curl/curl.h>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

// libwebp 解码器
#include "src/webp/decode.h"

// 静态成员初始化
std::map<std::string, SDL_Texture*> ImageLoader::mTextureCache;
std::vector<ImageLoader::LoadRequest> ImageLoader::mLoadQueue;
bool ImageLoader::mInitialized = false;

// 缓存目录
static const char* CACHE_DIR = "fs:/vol/external01/UTheme/temp/images/";

// 辅助结构:异步下载上下文
struct AsyncDownloadContext {
    std::string url;
    std::function<void(SDL_Texture*)> callback;
    DownloadOperation* download;
};

void ImageLoader::Init() {
    if (mInitialized) {
        return;
    }
    
    // 初始化 SDL_image - 尝试所有可用格式
    int imgFlags = IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_WEBP;
    int initted = IMG_Init(imgFlags);
    
    FileLogger::GetInstance().LogInfo("SDL_image IMG_Init called with flags: 0x%X", imgFlags);
    FileLogger::GetInstance().LogInfo("SDL_image IMG_Init returned: 0x%X", initted);
    
    // 检查每个格式是否成功初始化
    if (initted & IMG_INIT_JPG) {
        FileLogger::GetInstance().LogInfo("[OK] JPEG support loaded");
    } else {
        FileLogger::GetInstance().LogWarning("[WARN] JPEG support not available, will try fallback");
    }
    
    if (initted & IMG_INIT_PNG) {
        FileLogger::GetInstance().LogInfo("[OK] PNG support loaded");
    } else {
        FileLogger::GetInstance().LogWarning("[WARN] PNG support not available");
    }
    
    if (initted & IMG_INIT_WEBP) {
        FileLogger::GetInstance().LogInfo("[OK] WEBP support loaded");
    } else {
        FileLogger::GetInstance().LogWarning("[WARN] WEBP support not available");
    }
    
    // 即使部分格式失败,也继续运行(SDL_image 可能有内置的 stb_image 后备)
    if (initted == 0) {
        FileLogger::GetInstance().LogWarning("SDL_image initialization returned 0, but will try generic loading");
    }
    
    DEBUG_FUNCTION_LINE("SDL_image initialized with flags: 0x%X", initted);
    
    // 初始化 CURL
    curl_global_init(CURL_GLOBAL_ALL);
    
    // 初始化下载队列
    DownloadQueue::Init();
    
    // 创建缓存目录
    const char* paths[] = {
        "fs:/vol/external01/UTheme",
        "fs:/vol/external01/UTheme/temp",
        "fs:/vol/external01/UTheme/temp/images"
    };
    
    for (const char* path : paths) {
        struct stat st;
        if (stat(path, &st) != 0) {
            if (mkdir(path, 0777) != 0) {
                FileLogger::GetInstance().LogError("Failed to create directory: %s", path);
            }
        }
    }
    
    mInitialized = true;
    FileLogger::GetInstance().LogInfo("ImageLoader initialized (Async CURLM)");
}

void ImageLoader::Cleanup() {
    if (!mInitialized) {
        return;
    }
    
    // 清理纹理缓存
    ClearCache();
    
    // 清理加载队列
    mLoadQueue.clear();
    
    // 清理下载队列
    DownloadQueue::Quit();
    
    // 清理 CURL
    curl_global_cleanup();
    
    // 清理 SDL_image
    IMG_Quit();
    
    mInitialized = false;
    FileLogger::GetInstance().LogInfo("ImageLoader cleaned up");
}

void ImageLoader::Update() {
    // 处理下载队列 (非阻塞,异步)
    if (DownloadQueue::GetInstance()) {
        DownloadQueue::GetInstance()->Process();
    }
}

SDL_Texture* ImageLoader::GetCached(const std::string& url) {
    auto it = mTextureCache.find(url);
    return (it != mTextureCache.end()) ? it->second : nullptr;
}

void ImageLoader::CacheTexture(const std::string& url, SDL_Texture* texture) {
    if (!texture) return;
    
    // 如果已缓存,先释放旧的
    auto it = mTextureCache.find(url);
    if (it != mTextureCache.end() && it->second) {
        SDL_DestroyTexture(it->second);
    }
    
    mTextureCache[url] = texture;
    
    // 限制缓存大小 (最多 100 张)
    if (mTextureCache.size() > 100) {
        auto oldest = mTextureCache.begin();
        if (oldest->second) {
            SDL_DestroyTexture(oldest->second);
        }
        mTextureCache.erase(oldest);
    }
    
    if (FileLogger::GetInstance().IsVerbose()) {
        FileLogger::GetInstance().LogDebug("[CACHE] Texture cached: %s", url.c_str());
    }
}

void ImageLoader::ClearCache() {
    for (auto& pair : mTextureCache) {
        if (pair.second) {
            SDL_DestroyTexture(pair.second);
        }
    }
    mTextureCache.clear();
    DEBUG_FUNCTION_LINE("Image cache cleared");
    FileLogger::GetInstance().LogInfo("Texture cache cleared");
}

void ImageLoader::RemoveFromCache(const std::string& url) {
    auto it = mTextureCache.find(url);
    if (it != mTextureCache.end()) {
        if (it->second) {
            SDL_DestroyTexture(it->second);
        }
        mTextureCache.erase(it);
        
        if (FileLogger::GetInstance().IsVerbose()) {
            FileLogger::GetInstance().LogDebug("[CACHE] Removed: %s", url.c_str());
        }
    }
}

std::string ImageLoader::UrlToFilename(const std::string& url) {
    // URL -> 文件名: 使用简单的哈希
    std::hash<std::string> hasher;
    size_t hash = hasher(url);
    
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << hash;
    
    // 检测文件扩展名
    std::string ext = ".jpg";
    if (url.find(".png") != std::string::npos) {
        ext = ".png";
    } else if (url.find(".webp") != std::string::npos) {
        ext = ".webp";
    }
    
    return oss.str() + ext;
}

std::string ImageLoader::GetCachePath(const std::string& url) {
    return std::string(CACHE_DIR) + UrlToFilename(url);
}

bool ImageLoader::SaveToCache(const std::string& url, const void* data, size_t size) {
    if (!data || size == 0) return false;
    
    std::string path = GetCachePath(url);
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        FileLogger::GetInstance().LogWarning("Failed to open cache file for writing: %s", path.c_str());
        return false;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if (written != size) {
        FileLogger::GetInstance().LogError("Failed to write complete cache file: %s", path.c_str());
        return false;
    }
    
    if (FileLogger::GetInstance().IsVerbose()) {
        FileLogger::GetInstance().LogDebug("[CACHE SAVED] %s -> %s (%zu bytes)", url.c_str(), path.c_str(), size);
    }
    
    return true;
}

std::vector<uint8_t> ImageLoader::LoadFromCache(const std::string& url) {
    std::vector<uint8_t> data;
    std::string path = GetCachePath(url);
    
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return data;
    }
    
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (fileSize <= 0 || fileSize > 10 * 1024 * 1024) {
        fclose(file);
        FileLogger::GetInstance().LogWarning("Invalid cache file size: %ld", fileSize);
        return data;
    }
    
    data.resize(fileSize);
    size_t bytesRead = fread(data.data(), 1, fileSize, file);
    fclose(file);
    
    if (bytesRead != (size_t)fileSize) {
        FileLogger::GetInstance().LogError("Failed to read complete cache file");
        data.clear();
        return data;
    }
    
    if (FileLogger::GetInstance().IsVerbose()) {
        FileLogger::GetInstance().LogDebug("[CACHE HIT - DISK] %s (%zu bytes)", path.c_str(), data.size());
    }
    
    return data;
}

SDL_Texture* ImageLoader::LoadFromMemory(const void* data, size_t size) {
    if (!data || size == 0) {
        FileLogger::GetInstance().LogError("[LoadFromMemory] Invalid data: data=%p, size=%zu", data, size);
        return nullptr;
    }
    
    FileLogger::GetInstance().LogInfo("[LoadFromMemory] Attempting to load %zu bytes", size);
    
    // 检测图片格式
    const unsigned char* bytes = (const unsigned char*)data;
    std::string format = "unknown";
    
    if (size >= 4) {
        if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
            format = "JPEG";
        } else if (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47) {
            format = "PNG";
        } else if (bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F') {
            // 检查是否是 WEBP (RIFF....WEBP)
            if (size >= 12 && bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
                format = "WEBP";
            }
        } else if (bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F') {
            format = "GIF";
        }
    }
    FileLogger::GetInstance().LogInfo("[LoadFromMemory] Image format: %s (header: %02X %02X %02X %02X)", 
        format.c_str(), bytes[0], bytes[1], bytes[2], bytes[3]);
    
    // 如果是 WEBP 格式，使用 libwebp 解码
    if (format == "WEBP") {
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] Using libwebp decoder for WEBP image");
        
        // 获取图片尺寸
        int width = 0, height = 0;
        if (!WebPGetInfo(bytes, size, &width, &height)) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] WebPGetInfo failed - invalid WEBP data");
            return nullptr;
        }
        
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] WEBP dimensions: %dx%d", width, height);
        
        // 解码为 RGBA
        uint8_t* rgba_data = WebPDecodeRGBA(bytes, size, &width, &height);
        if (!rgba_data) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] WebPDecodeRGBA failed");
            return nullptr;
        }
        
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] WEBP decoded successfully to RGBA buffer");
        
        // 创建临时 SDL Surface 使用 RGBA 格式
        // 注意：WebPDecodeRGBA 返回的是 R,G,B,A 字节顺序（与字节序无关）
        SDL_Surface* temp_surface = SDL_CreateRGBSurfaceFrom(
            rgba_data,
            width, height,
            32,                    // 32 bits per pixel
            width * 4,             // pitch (bytes per row)
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
            0xFF000000,            // R mask (大端序)
            0x00FF0000,            // G mask
            0x0000FF00,            // B mask
            0x000000FF             // A mask
#else
            0x000000FF,            // R mask (小端序)
            0x0000FF00,            // G mask
            0x00FF0000,            // B mask
            0xFF000000             // A mask
#endif
        );
        
        if (!temp_surface) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError());
            WebPFree(rgba_data);
            return nullptr;
        }
        
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] Temp surface created: format=%s", 
            SDL_GetPixelFormatName(temp_surface->format->format));
        
        // 获取渲染器
        SDL_Renderer* renderer = Gfx::GetRenderer();
        if (!renderer) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] Renderer is null!");
            SDL_FreeSurface(temp_surface);
            WebPFree(rgba_data);
            return nullptr;
        }
        
        // 转换为渲染器原生格式以获得最佳性能和正确颜色
        Uint32 renderer_format = SDL_PIXELFORMAT_RGBA8888; // 默认格式
        SDL_RendererInfo renderer_info;
        if (SDL_GetRendererInfo(renderer, &renderer_info) == 0) {
            if (renderer_info.num_texture_formats > 0) {
                renderer_format = renderer_info.texture_formats[0];
                FileLogger::GetInstance().LogInfo("[LoadFromMemory] Using renderer format: %s", 
                    SDL_GetPixelFormatName(renderer_format));
            }
        }
        
        // 转换 surface 到渲染器格式
        SDL_Surface* converted_surface = SDL_ConvertSurfaceFormat(temp_surface, renderer_format, 0);
        SDL_FreeSurface(temp_surface);
        WebPFree(rgba_data);
        
        if (!converted_surface) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] SDL_ConvertSurfaceFormat failed: %s", SDL_GetError());
            return nullptr;
        }
        
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] Surface converted to: %s", 
            SDL_GetPixelFormatName(converted_surface->format->format));
        
        // 创建纹理
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, converted_surface);
        SDL_FreeSurface(converted_surface);
        
        if (!texture) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
            return nullptr;
        }
        
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] [SUCCESS] WEBP texture created successfully");
        return texture;
    }
    
    // 对于其他格式，使用 SDL_image
    SDL_RWops* rw = SDL_RWFromConstMem(data, size);
    if (!rw) {
        FileLogger::GetInstance().LogError("[LoadFromMemory] Failed to create RWops: %s", SDL_GetError());
        return nullptr;
    }
    
    SDL_Surface* surface = nullptr;
    if (format == "JPEG") {
        FileLogger::GetInstance().LogInfo("[LoadFromMemory] Trying IMG_LoadTyped_RW with 'JPG' hint");
        surface = IMG_LoadTyped_RW(rw, 1, "JPG");
        if (!surface) {
            FileLogger::GetInstance().LogError("[LoadFromMemory] IMG_LoadTyped_RW('JPG') failed: %s", IMG_GetError());
            return nullptr;
        }
    } else {
        surface = IMG_Load_RW(rw, 1);
    }
    
    if (!surface) {
        FileLogger::GetInstance().LogError("[LoadFromMemory] All decoders failed");
        return nullptr;
    }
    
    FileLogger::GetInstance().LogInfo("[LoadFromMemory] Surface created: %dx%d, format=%d", 
        surface->w, surface->h, surface->format->format);
    
    SDL_Renderer* renderer = Gfx::GetRenderer();
    if (!renderer) {
        FileLogger::GetInstance().LogError("[LoadFromMemory] Renderer is null!");
        SDL_FreeSurface(surface);
        return nullptr;
    }
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    
    if (!texture) {
        FileLogger::GetInstance().LogError("[LoadFromMemory] SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        return nullptr;
    }
    
    FileLogger::GetInstance().LogInfo("[LoadFromMemory] [SUCCESS] Texture created successfully (%s format)", format.c_str());
    return texture;
}

SDL_Texture* ImageLoader::LoadFromUrl(const std::string& url) {
    if (url.empty()) return nullptr;
    
    SDL_Texture* cached = GetCached(url);
    if (cached) {
        DEBUG_FUNCTION_LINE("Image loaded from memory cache: %s", url.c_str());
        if (FileLogger::GetInstance().IsVerbose()) {
            FileLogger::GetInstance().LogDebug("[CACHE HIT - MEMORY] %s", url.c_str());
        }
        return cached;
    }
    
    std::vector<uint8_t> diskData = LoadFromCache(url);
    if (!diskData.empty()) {
        SDL_Texture* texture = LoadFromMemory(diskData.data(), diskData.size());
        if (texture) {
            CacheTexture(url, texture);
            FileLogger::GetInstance().LogInfo("[CACHE HIT - DISK] %s", url.c_str());
            return texture;
        } else {
            FileLogger::GetInstance().LogWarning("[CACHE CORRUPT] Failed to load texture from cache: %s", GetCachePath(url).c_str());
        }
    } else {
        FileLogger::GetInstance().LogInfo("[CACHE MISS] Not found in disk cache: %s", url.c_str());
    }
    
    DEBUG_FUNCTION_LINE("Downloading image synchronously: %s", url.c_str());
    FileLogger::GetInstance().LogInfo("[DOWNLOADING - SYNC] %s", url.c_str());
    
    std::vector<uint8_t> data = DownloadData(url);
    if (data.empty()) {
        FileLogger::GetInstance().LogError("[DOWNLOAD FAILED] %s", url.c_str());
        return nullptr;
    }
    
    SaveToCache(url, data.data(), data.size());
    
    SDL_Texture* texture = LoadFromMemory(data.data(), data.size());
    if (texture) {
        CacheTexture(url, texture);
    }
    
    return texture;
}

std::vector<uint8_t> ImageLoader::DownloadData(const std::string& url) {
    std::vector<uint8_t> data;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        DEBUG_FUNCTION_LINE("Failed to initialize CURL");
        return data;
    }
    
    auto writeCallback = [](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
        std::vector<uint8_t>* buffer = (std::vector<uint8_t>*)userdata;
        size_t total = size * nmemb;
        buffer->insert(buffer->end(), ptr, ptr + total);
        return total;
    };
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);  // 增加到60秒（HD图片较大）
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);  // 增加连接超时
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "UTheme/1.0 (Wii U)");
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);  // 启用详细日志
    
    FileLogger::GetInstance().LogInfo("[CURL] Starting download: %s", url.c_str());
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        DEBUG_FUNCTION_LINE("CURL error: %s", curl_easy_strerror(res));
        FileLogger::GetInstance().LogError("CURL error [%d]: %s - %s", res, curl_easy_strerror(res), url.c_str());
        data.clear();
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            DEBUG_FUNCTION_LINE("HTTP error %ld", http_code);
            FileLogger::GetInstance().LogError("HTTP error %ld: %s", http_code, url.c_str());
            data.clear();
        } else {
            FileLogger::GetInstance().LogInfo("[CURL] Successfully downloaded %zu bytes", data.size());
        }
    }
    
    curl_easy_cleanup(curl);
    return data;
}

void ImageLoader::LoadAsync(const LoadRequest& request) {
    if (request.url.empty()) return;
    
    // 检查是否是本地文件 (以 fs:/ 开头的路径)
    bool isLocalFile = (request.url.find("fs:/") == 0);
    std::string localPath;
    
    FileLogger::GetInstance().LogInfo("[LoadAsync] URL: %s, isLocal: %d", request.url.c_str(), isLocalFile);
    
    if (isLocalFile) {
        // 直接使用本地文件路径
        localPath = request.url;
        FileLogger::GetInstance().LogInfo("[LOCAL FILE] Loading: %s", localPath.c_str());
        
        // 检查文件是否存在
        struct stat st;
        int statResult = stat(localPath.c_str(), &st);
        int statErrno = errno;
        FileLogger::GetInstance().LogInfo("[STAT CALL] path='%s', result=%d, errno=%d", localPath.c_str(), statResult, statErrno);
        
        if (statResult != 0) {
            FileLogger::GetInstance().LogError("[LOCAL FILE NOT FOUND] %s (errno: %d)", localPath.c_str(), statErrno);
            if (request.callback) {
                request.callback(nullptr);
            }
            return;
        }
        
        FileLogger::GetInstance().LogInfo("[LOCAL FILE EXISTS] Size: %lld bytes, mode: 0x%x", (long long)st.st_size, st.st_mode);
        
        // 检查是否真的是文件
        if (S_ISDIR(st.st_mode)) {
            FileLogger::GetInstance().LogError("[ERROR] Path is a directory, not a file: %s", localPath.c_str());
            if (request.callback) {
                request.callback(nullptr);
            }
            return;
        }
        
        if (!S_ISREG(st.st_mode)) {
            FileLogger::GetInstance().LogWarning("[WARNING] Path is not a regular file (mode: 0x%x): %s", st.st_mode, localPath.c_str());
        }
        
        // 检查文件大小 - 即使显示0字节也尝试加载(可能是stat的问题)
        if (st.st_size == 0) {
            FileLogger::GetInstance().LogWarning("[WARNING] File appears empty (0 bytes), but will try to load anyway: %s", localPath.c_str());
        }
        
        // 尝试加载本地文件 - 先用 IMG_Load 让它自动检测格式
        SDL_Surface* surface = IMG_Load(localPath.c_str());
        
        // 如果失败,尝试强制作为 WEBP 加载
        if (!surface) {
            FileLogger::GetInstance().LogWarning("[IMG_Load FAILED] %s, trying as WEBP...", IMG_GetError());
            
            SDL_RWops* rwops = SDL_RWFromFile(localPath.c_str(), "rb");
            if (rwops) {
                FileLogger::GetInstance().LogInfo("[SDL_RWFromFile OK] Trying IMG_LoadTyped_RW with WEBP");
                surface = IMG_LoadTyped_RW(rwops, 1, "WEBP");
                if (!surface) {
                    FileLogger::GetInstance().LogError("[WEBP Load FAILED via SDL_image] %s", IMG_GetError());
                }
            } else {
                FileLogger::GetInstance().LogError("[SDL_RWFromFile FAILED] %s", SDL_GetError());
            }
        } else {
            FileLogger::GetInstance().LogInfo("[IMG_Load SUCCESS] Surface created directly");
        }
        
        // 如果 SDL_image 失败,尝试直接使用 libwebp 解码
        if (!surface) {
            FileLogger::GetInstance().LogInfo("[Trying libwebp directly]");
            
            FILE* fp = fopen(localPath.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long fileSize = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                
                uint8_t* fileData = (uint8_t*)malloc(fileSize);
                if (fileData) {
                    size_t readSize = fread(fileData, 1, fileSize, fp);
                    fclose(fp);
                    
                    if (readSize == (size_t)fileSize) {
                        int width, height;
                        uint8_t* rgbaData = WebPDecodeRGBA(fileData, fileSize, &width, &height);
                        
                        if (rgbaData) {
                            FileLogger::GetInstance().LogInfo("[libwebp SUCCESS] Decoded %dx%d WEBP image", width, height);
                            
                            // 创建 SDL_Surface - 使用正确的像素格式
                            // WebPDecodeRGBA 返回 RGBA 字节序: R, G, B, A
                            #if SDL_BYTEORDER == SDL_BIG_ENDIAN
                                Uint32 rmask = 0xff000000;
                                Uint32 gmask = 0x00ff0000;
                                Uint32 bmask = 0x0000ff00;
                                Uint32 amask = 0x000000ff;
                            #else
                                Uint32 rmask = 0x000000ff;
                                Uint32 gmask = 0x0000ff00;
                                Uint32 bmask = 0x00ff0000;
                                Uint32 amask = 0xff000000;
                            #endif
                            
                            surface = SDL_CreateRGBSurfaceFrom(
                                rgbaData,
                                width, height,
                                32, // bits per pixel
                                width * 4, // pitch
                                rmask, gmask, bmask, amask
                            );
                            
                            if (surface) {
                                // 复制数据因为 WebPFree 会释放 rgbaData
                                SDL_Surface* copiedSurface = SDL_ConvertSurface(surface, surface->format, 0);
                                SDL_FreeSurface(surface);
                                surface = copiedSurface;
                                FileLogger::GetInstance().LogInfo("[libwebp] Surface created and copied");
                            }
                            
                            WebPFree(rgbaData);
                        } else {
                            FileLogger::GetInstance().LogError("[libwebp FAILED] Could not decode WEBP data");
                        }
                    } else {
                        FileLogger::GetInstance().LogError("[FILE READ FAILED] Expected %ld bytes, read %zu bytes", fileSize, readSize);
                    }
                    
                    free(fileData);
                } else {
                    FileLogger::GetInstance().LogError("[MALLOC FAILED] Could not allocate %ld bytes", fileSize);
                    fclose(fp);
                }
            } else {
                FileLogger::GetInstance().LogError("[FILE OPEN FAILED] Could not open: %s", localPath.c_str());
            }
        }
        
        if (surface) {
            FileLogger::GetInstance().LogInfo("[SURFACE CREATED] %dx%d, format: %s", 
                surface->w, surface->h, SDL_GetPixelFormatName(surface->format->format));
                
            SDL_Texture* texture = SDL_CreateTextureFromSurface(Gfx::GetRenderer(), surface);
            SDL_FreeSurface(surface);
            
            if (texture) {
                FileLogger::GetInstance().LogInfo("[LOCAL FILE LOADED] %s -> texture: %p", localPath.c_str(), texture);
                // 缓存纹理
                CacheTexture(request.url, texture);
                if (request.callback) {
                    request.callback(texture);
                }
            } else {
                FileLogger::GetInstance().LogError("[LOCAL FILE TEXTURE FAILED] %s: %s", localPath.c_str(), SDL_GetError());
                if (request.callback) {
                    request.callback(nullptr);
                }
            }
        } else {
            FileLogger::GetInstance().LogError("[LOCAL FILE LOAD FAILED] %s: All methods failed", localPath.c_str());
            if (request.callback) {
                request.callback(nullptr);
            }
        }
        return;
    }
    
    // 网络URL - 原有逻辑
    SDL_Texture* cached = GetCached(request.url);
    if (cached) {
        if (FileLogger::GetInstance().IsVerbose()) {
            FileLogger::GetInstance().LogDebug("[CACHE HIT - MEMORY] Async: %s", request.url.c_str());
        }
        if (request.callback) {
            request.callback(cached);
        }
        return;
    }
    
    std::vector<uint8_t> diskData = LoadFromCache(request.url);
    if (!diskData.empty()) {
        SDL_Texture* texture = LoadFromMemory(diskData.data(), diskData.size());
        if (texture) {
            CacheTexture(request.url, texture);
            FileLogger::GetInstance().LogInfo("[CACHE HIT - DISK] Async: %s", request.url.c_str());
            if (request.callback) {
                request.callback(texture);
            }
            return;
        }
    }
    
    FileLogger::GetInstance().LogInfo("[DOWNLOADING - ASYNC] %s", request.url.c_str());
    
    AsyncDownloadContext* context = new AsyncDownloadContext();
    context->url = request.url;
    context->callback = request.callback;
    context->download = new DownloadOperation();
    context->download->url = request.url;
    
    context->download->cb = [](DownloadOperation* download) {
        AsyncDownloadContext* ctx = (AsyncDownloadContext*)download->cbdata;
        
        SDL_Texture* texture = nullptr;
        
        if (download->status == DownloadStatus::COMPLETE && !download->buffer.empty()) {
            // 先记录下载的数据信息
            FileLogger::GetInstance().LogInfo("[DOWNLOAD COMPLETE] %s (%zu bytes)", ctx->url.c_str(), download->buffer.size());
            
            // 检查前几个字节
            if (download->buffer.size() >= 4) {
                const unsigned char* bytes = (const unsigned char*)download->buffer.data();
                FileLogger::GetInstance().LogInfo("[DOWNLOAD DATA] First 4 bytes: %02X %02X %02X %02X", 
                    bytes[0], bytes[1], bytes[2], bytes[3]);
                    
                // 检查前16字节看是否是HTML错误
                if (download->buffer.size() >= 16) {
                    char preview[17] = {0};
                    memcpy(preview, bytes, 16);
                    for (int i = 0; i < 16; i++) {
                        if (preview[i] < 32 || preview[i] > 126) preview[i] = '.';
                    }
                    FileLogger::GetInstance().LogInfo("[DOWNLOAD DATA] First 16 chars: %s", preview);
                }
            }
            
            SaveToCache(ctx->url, download->buffer.data(), download->buffer.size());
            
            texture = LoadFromMemory(download->buffer.data(), download->buffer.size());
            if (texture) {
                CacheTexture(ctx->url, texture);
            } else {
                FileLogger::GetInstance().LogError("[TEXTURE CREATION FAILED] %s", ctx->url.c_str());
            }
        } else if (download->status == DownloadStatus::FAILED) {
            FileLogger::GetInstance().LogError("[DOWNLOAD FAILED] %s (HTTP %ld)", ctx->url.c_str(), download->response_code);
        }
        
        if (ctx->callback) {
            ctx->callback(texture);
        }
        
        delete ctx->download;
        delete ctx;
    };
    
    context->download->cbdata = context;
    
    if (DownloadQueue::GetInstance()) {
        DownloadQueue::GetInstance()->DownloadAdd(context->download);
    } else {
        FileLogger::GetInstance().LogError("DownloadQueue not initialized!");
        delete context->download;
        delete context;
    }
}
