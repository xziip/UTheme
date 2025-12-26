#include "ThemeManager.hpp"
#include "ThemeDownloader.hpp"
#include "SimpleJsonParser.hpp"
#include "DownloadQueue.hpp"
#include "logger.h"
#include "FileLogger.hpp"
#include <curl/curl.h>
#include <nn/ac.h>
#include <coreinit/thread.h>
#include <cstring>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// Themezer GraphQL API URL
#define THEMEZER_GRAPHQL_URL "https://api.themezer.net/graphql"
#define THEMEZER_CDN_URL "https://cdn.themezer.net"
#define CACHE_DIR "fs:/vol/external01/UTheme/temp"
#define CACHE_FILE "fs:/vol/external01/UTheme/temp/themes_cache.json"

// CURL回调函数
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* str = (std::string*)userp;
    str->append((char*)contents, realsize);
    return realsize;
}

// 用于文件下载的回调函数
struct FileDownloadData {
    FILE* fp;
    size_t totalWritten;
};

static size_t WriteFileCallback(char* ptr, size_t size, size_t nmemb, void* userp) {
    FileDownloadData* data = (FileDownloadData*)userp;
    size_t totalSize = size * nmemb;
    size_t written = fwrite(ptr, 1, totalSize, data->fp);  // 以字节为单位写入
    data->totalWritten += written;
    
    // 每 100KB 输出一次进度
    if (data->totalWritten % (100 * 1024) < totalSize) {
        FileLogger::GetInstance().LogInfo("[DOWNLOAD PROGRESS] Written: %zu bytes", data->totalWritten);
    }
    
    return written;  // 返回实际写入的字节数
}

ThemeManager::ThemeManager() {
    // 初始化网络
    nn::ac::Initialize();
    nn::ac::Connect();
}

ThemeManager::~ThemeManager() {
    FileLogger::GetInstance().LogInfo("[ThemeManager] Destructor called");
    
    // 取消未完成的下载操作
    if (mFetchOp && DownloadQueue::GetInstance()) {
        FileLogger::GetInstance().LogInfo("[ThemeManager] Cancelling fetch operation");
        DownloadQueue::GetInstance()->DownloadCancel(mFetchOp);
        FileLogger::GetInstance().LogInfo("[ThemeManager] Fetch operation cancelled");
        // 注意: 不要 delete mFetchOp,因为可能导致阻塞
        // delete mFetchOp;
        mFetchOp = nullptr;
        FileLogger::GetInstance().LogInfo("[ThemeManager] Fetch operation cleanup complete");
    } else if (mFetchOp) {
        FileLogger::GetInstance().LogInfo("[ThemeManager] Fetch operation exists but DownloadQueue is null");
    }
    
    FileLogger::GetInstance().LogInfo("[ThemeManager] About to clean up downloader");
    
    // 清理主题下载器
    if (mDownloader) {
        FileLogger::GetInstance().LogInfo("[ThemeManager] Cancelling downloader");
        mDownloader->Cancel();
        FileLogger::GetInstance().LogInfo("[ThemeManager] Cancel completed, skipping delete");
        // 注意: 不要 delete mDownloader,因为在 Wii U 上 delete 操作会阻塞
        // 让对象泄漏,进程退出时会自动清理
        // delete mDownloader;  // ← 这行会导致卡死!
        mDownloader = nullptr;
        FileLogger::GetInstance().LogInfo("[ThemeManager] Downloader cleanup complete");
    }
    
    // 注意: 不要调用 nn::ac::Finalize(),因为其他地方可能还在使用网络
    // nn::ac 是全局的,应该在程序退出时由 ImageLoader::Cleanup 统一清理
    FileLogger::GetInstance().LogInfo("[ThemeManager] Destructor completed");
}

std::string ThemeManager::FetchUrl(const std::string& url, const std::string& postData) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        DEBUG_FUNCTION_LINE("Failed to initialize CURL");
        return "";
    }
    
    std::string response;
    struct curl_slist* headers = nullptr;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "UTheme/1.0 (Wii U)");
    
    // 如果有 POST 数据,设置 POST 请求
    if (!postData.empty()) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        
        // 设置 Content-Type 为 JSON
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    // 清理 headers
    if (headers) {
        curl_slist_free_all(headers);
    }
    
    if (res != CURLE_OK) {
        DEBUG_FUNCTION_LINE("CURL error: %s", curl_easy_strerror(res));
        mErrorMessage = std::string("Network error: ") + curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return "";
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (http_code != 200) {
        DEBUG_FUNCTION_LINE("HTTP error: %ld", http_code);
        mErrorMessage = "HTTP error: " + std::to_string(http_code);
        return "";
    }
    
    return response;
}

// 解析 Themezer GraphQL 响应
bool ThemeManager::ParseThemezerResponse(const std::string& jsonData) {
    mThemes.clear();
    
    DEBUG_FUNCTION_LINE("Parsing JSON response (%zu bytes)", jsonData.size());
    
    try {
        JsonValue root = SimpleJsonParser::Parse(jsonData);
        
        if (!root.isObject()) {
            DEBUG_FUNCTION_LINE("Expected object at root");
            return false;
        }
        
        // GraphQL 响应格式: { "data": { "wiiuThemes": { "nodes": [...] } } }
        if (!root.has("data") || !root["data"].isObject()) {
            DEBUG_FUNCTION_LINE("Missing 'data' field");
            return false;
        }
        
        const JsonValue& data = root["data"];
        if (!data.has("wiiuThemes") || !data["wiiuThemes"].isObject()) {
            DEBUG_FUNCTION_LINE("Missing 'wiiuThemes' field");
            return false;
        }
        
        const JsonValue& wiiuThemes = data["wiiuThemes"];
        if (!wiiuThemes.has("nodes") || !wiiuThemes["nodes"].isArray()) {
            DEBUG_FUNCTION_LINE("Missing 'nodes' array");
            return false;
        }
        
        const JsonValue& nodes = wiiuThemes["nodes"];
        
        // 辅助函数: 解析 ImageSizes 对象
        auto parseImageSizes = [](const JsonValue& imgObj) -> ThemeImage {
            ThemeImage img;
            if (imgObj.has("thumbUrl") && imgObj["thumbUrl"].isString()) {
                img.thumbUrl = imgObj["thumbUrl"].asString();
            }
            if (imgObj.has("hdUrl") && imgObj["hdUrl"].isString()) {
                img.hdUrl = imgObj["hdUrl"].asString();
            }
            return img;
        };
        
        // 遍历主题数组
        for (size_t i = 0; i < nodes.size(); i++) {
            const JsonValue& themeJson = nodes[i];
            
            if (!themeJson.isObject()) {
                continue;
            }
            
            Theme theme;
            
            // 解析主题数据
            if (themeJson.has("uuid") && themeJson["uuid"].isString()) {
                theme.id = themeJson["uuid"].asString();
            }
            
            if (themeJson.has("name") && themeJson["name"].isString()) {
                theme.name = themeJson["name"].asString();
            }
            
            if (themeJson.has("description") && themeJson["description"].isString()) {
                theme.description = themeJson["description"].asString();
            } else {
                theme.description = "";
            }
            
            // 作者信息
            if (themeJson.has("creator") && themeJson["creator"].isObject()) {
                const JsonValue& creator = themeJson["creator"];
                if (creator.has("username") && creator["username"].isString()) {
                    theme.author = creator["username"].asString();
                }
            }
            
            // 统计信息 (GraphQL 使用 downloadCount 和 saveCount)
            if (themeJson.has("downloadCount") && themeJson["downloadCount"].isNumber()) {
                theme.downloads = themeJson["downloadCount"].asInt();
            }
            
            if (themeJson.has("saveCount") && themeJson["saveCount"].isNumber()) {
                theme.likes = themeJson["saveCount"].asInt();
            }
            
            // 版本 (GraphQL 没有 version 字段,使用空字符串)
            theme.version = "1.0";
            
            // 更新时间
            if (themeJson.has("updatedAt") && themeJson["updatedAt"].isString()) {
                theme.updatedAt = themeJson["updatedAt"].asString();
            }
            
            // 解析图片 URLs
            if (themeJson.has("collagePreview") && themeJson["collagePreview"].isObject()) {
                theme.collagePreview = parseImageSizes(themeJson["collagePreview"]);
            }
            
            if (themeJson.has("launcherScreenshot") && themeJson["launcherScreenshot"].isObject()) {
                theme.launcherScreenshot = parseImageSizes(themeJson["launcherScreenshot"]);
            }
            
            if (themeJson.has("waraWaraPlazaScreenshot") && themeJson["waraWaraPlazaScreenshot"].isObject()) {
                theme.waraWaraScreenshot = parseImageSizes(themeJson["waraWaraPlazaScreenshot"]);
            }
            
            // 背景图 URLs
            if (themeJson.has("launcherBgUrl") && themeJson["launcherBgUrl"].isString()) {
                theme.launcherBgUrl = themeJson["launcherBgUrl"].asString();
            }
            
            if (themeJson.has("waraWaraPlazaBgUrl") && themeJson["waraWaraPlazaBgUrl"].isString()) {
                theme.waraWaraBgUrl = themeJson["waraWaraPlazaBgUrl"].asString();
            }
            
            // 下载 URL
            if (themeJson.has("downloadUrl") && themeJson["downloadUrl"].isString()) {
                theme.downloadUrl = themeJson["downloadUrl"].asString();
            }
            
            // 标签
            if (themeJson.has("tags") && themeJson["tags"].isArray()) {
                const JsonValue& tagsArray = themeJson["tags"];
                for (size_t j = 0; j < tagsArray.size(); j++) {
                    const JsonValue& tagObj = tagsArray[j];
                    if (tagObj.isObject() && tagObj.has("name") && tagObj["name"].isString()) {
                        theme.tags.push_back(tagObj["name"].asString());
                    }
                }
            }
            
            // 只添加有效的主题
            if (!theme.id.empty() && !theme.name.empty()) {
                mThemes.push_back(theme);
                DEBUG_FUNCTION_LINE("Loaded theme: %s by %s", theme.name.c_str(), theme.author.c_str());
            }
        }
        
        return !mThemes.empty();
        
    } catch (...) {
        DEBUG_FUNCTION_LINE("Exception while parsing JSON");
        return false;
    }
}

void ThemeManager::FetchThemes() {
    if (mState == FETCH_IN_PROGRESS) {
        return;
    }
    
    mState = FETCH_IN_PROGRESS;
    mErrorMessage.clear();
    
    if (mStateCallback) {
        mStateCallback(FETCH_IN_PROGRESS, "Fetching themes...");
    }
    
    DEBUG_FUNCTION_LINE("Fetching themes from Themezer GraphQL API (ASYNC)");
    FileLogger::GetInstance().LogInfo("Starting async FetchThemes");
    
    // 构造 GraphQL 查询 (包含图片URL) - 设置limit为200以获取更多主题
    std::string query = R"({
        "query": "{ wiiuThemes(limit: 200) { nodes { uuid name description downloadCount saveCount updatedAt creator { username } downloadUrl collagePreview { thumbUrl hdUrl } launcherScreenshot { thumbUrl hdUrl } waraWaraPlazaScreenshot { thumbUrl hdUrl } launcherBgUrl waraWaraPlazaBgUrl tags { name } } } }"
    })";
    
    // 使用 DownloadQueue 进行异步请求
    if (!DownloadQueue::GetInstance()) {
        mState = FETCH_ERROR;
        mErrorMessage = "DownloadQueue not initialized";
        if (mStateCallback) {
            mStateCallback(FETCH_ERROR, mErrorMessage);
        }
        return;
    }
    
    // 创建下载操作
    mFetchOp = new DownloadOperation();
    mFetchOp->url = THEMEZER_GRAPHQL_URL;
    mFetchOp->postData = query;  // GraphQL 查询作为 POST 数据
    
    // 设置回调
    mFetchOp->cb = [this](DownloadOperation* op) {
        if (op->status == DownloadStatus::COMPLETE && !op->buffer.empty()) {
            FileLogger::GetInstance().LogInfo("Async FetchThemes COMPLETE: %zu bytes", op->buffer.size());
            
            // 解析响应
            if (ParseThemezerResponse(op->buffer)) {
                mState = FETCH_SUCCESS;
                if (mStateCallback) {
                    mStateCallback(FETCH_SUCCESS, "Themes loaded successfully");
                }
                DEBUG_FUNCTION_LINE("Successfully loaded %zu themes", mThemes.size());
                FileLogger::GetInstance().LogInfo("FetchThemes SUCCESS: %zu themes loaded", mThemes.size());
                
                // 保存到缓存
                if (SaveCache()) {
                    FileLogger::GetInstance().LogInfo("Cache saved successfully after FetchThemes");
                } else {
                    FileLogger::GetInstance().LogError("Failed to save cache after FetchThemes");
                }
            } else {
                mState = FETCH_ERROR;
                mErrorMessage = "Failed to parse theme data";
                if (mStateCallback) {
                    mStateCallback(FETCH_ERROR, mErrorMessage);
                }
                FileLogger::GetInstance().LogError("Failed to parse theme response");
            }
        } else {
            mState = FETCH_ERROR;
            mErrorMessage = "Network request failed";
            if (mStateCallback) {
                mStateCallback(FETCH_ERROR, mErrorMessage);
            }
            FileLogger::GetInstance().LogError("Async FetchThemes FAILED: HTTP %ld", op->response_code);
        }
        
        // 清理
        delete mFetchOp;
        mFetchOp = nullptr;
    };
    
    mFetchOp->cbdata = this;
    
    //  添加到异步下载队列 (不阻塞!)
    DownloadQueue::GetInstance()->DownloadAdd(mFetchOp);
    FileLogger::GetInstance().LogInfo("FetchThemes request added to DownloadQueue");
}

void ThemeManager::DownloadTheme(const Theme& theme) {
    FileLogger::GetInstance().LogInfo("Starting async theme download: %s", theme.name.c_str());
    FileLogger::GetInstance().LogInfo("Download URL: %s", theme.downloadUrl.c_str());
    
    if (theme.downloadUrl.empty()) {
        FileLogger::GetInstance().LogError("Download URL is empty!");
        if (mStateCallback) {
            mStateCallback(FETCH_ERROR, "Download URL is empty");
        }
        return;
    }
    
    // 如果已有下载器，清理 (包括待清理的)
    if (mDownloader) {
        FileLogger::GetInstance().LogInfo("[DownloadTheme] Cleaning up existing downloader");
        mDownloader->Cancel();
        delete mDownloader;
        mDownloader = nullptr;
        mDownloaderNeedsCleanup = false;  // 重置标志
    }
    
    // 创建新的下载器
    mDownloader = new ThemeDownloader();
    
    // 设置回调
    mDownloader->SetProgressCallback([this](float progress, long downloaded, long total) {
        if (mProgressCallback) {
            mProgressCallback(progress, downloaded, total);
        }
        // 只在重要节点打印日志，避免刷屏
        if (progress == 1.0f || (int)(progress * 10) != (int)((progress - 0.01f) * 10)) {
            FileLogger::GetInstance().LogInfo("Download progress: %.1f%% (%ld / %ld bytes)", 
                progress * 100, downloaded, total);
        }
    });
    
    mDownloader->SetStateCallback([this, theme](DownloadState state, const std::string& message) {
        FileLogger::GetInstance().LogInfo("Download state: %d - %s", state, message.c_str());
        
        // 转换状态到 FetchState
        if (state == DOWNLOAD_COMPLETE) {
            // 下载完成后，保存主题元数据和图片
            std::string extractedPath = mDownloader->GetExtractedPath();
            SaveThemeMetadata(theme, extractedPath);
            
            if (mStateCallback) mStateCallback(FETCH_SUCCESS, message);
            
            // 标记下载器需要清理(不能在回调中直接删除,因为回调可能还在下载器对象的方法中执行)
            FileLogger::GetInstance().LogInfo("[ThemeManager] Marking downloader for cleanup after success");
            mDownloaderNeedsCleanup = true;
        } else if (state == DOWNLOAD_ERROR || state == DOWNLOAD_CANCELLED) {
            if (mStateCallback) mStateCallback(FETCH_ERROR, message);
            
            // 标记下载器需要清理
            FileLogger::GetInstance().LogInfo("[ThemeManager] Marking downloader for cleanup after error/cancel");
            mDownloaderNeedsCleanup = true;
        }
    });
    
    // 启动异步下载
    mDownloader->DownloadThemeAsync(theme.downloadUrl, theme.name);
    
    FileLogger::GetInstance().LogInfo("Async download started");
}

float ThemeManager::GetDownloadProgress() const {
    if (mDownloader) {
        return mDownloader->GetProgress();
    }
    return 0.0f;
}

int ThemeManager::GetDownloadState() const {
    // 先检查是否需要清理下载器(在主线程中安全清理)
    if (mDownloaderNeedsCleanup && mDownloader) {
        FileLogger::GetInstance().LogInfo("[ThemeManager] Performing deferred downloader cleanup");
        ThemeDownloader* downloaderToDelete = mDownloader;
        const_cast<ThemeManager*>(this)->mDownloader = nullptr;
        const_cast<ThemeManager*>(this)->mDownloaderNeedsCleanup = false;
        delete downloaderToDelete;
        FileLogger::GetInstance().LogInfo("[ThemeManager] Deferred cleanup completed");
        return (int)DOWNLOAD_IDLE;
    }
    
    if (mDownloader) {
        return (int)mDownloader->GetState();
    }
    return (int)DOWNLOAD_IDLE;
}

std::string ThemeManager::GetDownloadError() const {
    if (mDownloader) {
        return mDownloader->GetError();
    }
    return "";
}

std::string ThemeManager::GetDownloadedFilePath() const {
    if (mDownloader) {
        return mDownloader->GetDownloadedFilePath();
    }
    return "";
}

std::string ThemeManager::GetExtractedPath() const {
    if (mDownloader) {
        return mDownloader->GetExtractedPath();
    }
    return "";
}

void ThemeManager::CancelDownload() {
    if (mDownloader) {
        mDownloader->Cancel();
    }
}

void ThemeManager::Update() {
    // 无需处理缓存加载
}

void ThemeManager::SetProgressCallback(std::function<void(float progress, long downloaded, long total)> callback) {
    mProgressCallback = callback;
}

void ThemeManager::SetStateCallback(std::function<void(FetchState state, const std::string& message)> callback) {
    mStateCallback = callback;
}

std::string ThemeManager::GetCachePath() const {
    return CACHE_FILE;
}

// 序列化主题列表为 JSON 字符串
std::string ThemeManager::SerializeThemes() const {
    FileLogger::GetInstance().LogInfo("SerializeThemes: Serializing %zu themes", mThemes.size());
    
    // 使用格式化的JSON输出,提高可读性
    std::string json = "{\n  \"themes\": [\n";
    
    for (size_t i = 0; i < mThemes.size(); i++) {
        const Theme& theme = mThemes[i];
        
        if (i > 0) json += ",\n";
        json += "    {\n";
        json += "      \"id\": \"" + theme.id + "\",\n";
        json += "      \"name\": \"" + theme.name + "\",\n";
        json += "      \"author\": \"" + theme.author + "\",\n";
        json += "      \"description\": \"" + theme.description + "\",\n";
        json += "      \"downloads\": " + std::to_string(theme.downloads) + ",\n";
        json += "      \"likes\": " + std::to_string(theme.likes) + ",\n";
        json += "      \"version\": \"" + theme.version + "\",\n";
        json += "      \"updatedAt\": \"" + theme.updatedAt + "\",\n";
        json += "      \"downloadUrl\": \"" + theme.downloadUrl + "\",\n";
        
        // 图片 URLs
        json += "      \"collageThumbUrl\": \"" + theme.collagePreview.thumbUrl + "\",\n";
        json += "      \"collageHdUrl\": \"" + theme.collagePreview.hdUrl + "\",\n";
        json += "      \"launcherThumbUrl\": \"" + theme.launcherScreenshot.thumbUrl + "\",\n";
        json += "      \"launcherHdUrl\": \"" + theme.launcherScreenshot.hdUrl + "\",\n";
        json += "      \"waraWaraThumbUrl\": \"" + theme.waraWaraScreenshot.thumbUrl + "\",\n";
        json += "      \"waraWaraHdUrl\": \"" + theme.waraWaraScreenshot.hdUrl + "\",\n";
        json += "      \"launcherBgUrl\": \"" + theme.launcherBgUrl + "\",\n";
        json += "      \"waraWaraBgUrl\": \"" + theme.waraWaraBgUrl + "\"\n";
        
        json += "    }";
        
        // 每隔10个主题记录一次进度和大小
        if ((i + 1) % 10 == 0 || i == mThemes.size() - 1) {
            FileLogger::GetInstance().LogInfo("SerializeThemes: Processed %zu/%zu themes, JSON size: %zu bytes", 
                i + 1, mThemes.size(), json.size());
        }
    }
    
    json += "\n  ]\n}\n";
    
    FileLogger::GetInstance().LogInfo("SerializeThemes: Complete, final JSON size: %zu bytes", json.size());
    return json;
}

// 从 JSON 字符串反序列化主题列表
bool ThemeManager::DeserializeThemes(const std::string& data) {
    try {
        FileLogger::GetInstance().LogInfo("DeserializeThemes: Parsing %zu bytes of JSON", data.size());
        
        JsonValue root = SimpleJsonParser::Parse(data);
        
        if (!root.isObject() || !root.has("themes") || !root["themes"].isArray()) {
            FileLogger::GetInstance().LogError("DeserializeThemes: Invalid root structure");
            return false;
        }
        
        const JsonValue& themesArray = root["themes"];
        FileLogger::GetInstance().LogInfo("DeserializeThemes: Found themes array with %zu items", themesArray.size());
        
        mThemes.clear();
        
        for (size_t i = 0; i < themesArray.size(); i++) {
            const JsonValue& themeJson = themesArray[i];
            
            if (!themeJson.isObject()) {
                FileLogger::GetInstance().LogWarning("DeserializeThemes: Theme %zu is not an object", i);
                continue;
            }
            
            Theme theme;
            
            if (themeJson.has("id")) theme.id = themeJson["id"].asString();
            if (themeJson.has("name")) theme.name = themeJson["name"].asString();
            if (themeJson.has("author")) theme.author = themeJson["author"].asString();
            if (themeJson.has("description")) theme.description = themeJson["description"].asString();
            if (themeJson.has("downloads")) theme.downloads = themeJson["downloads"].asInt();
            if (themeJson.has("likes")) theme.likes = themeJson["likes"].asInt();
            if (themeJson.has("version")) theme.version = themeJson["version"].asString();
            if (themeJson.has("updatedAt")) theme.updatedAt = themeJson["updatedAt"].asString();
            if (themeJson.has("downloadUrl")) theme.downloadUrl = themeJson["downloadUrl"].asString();
            
            // 图片 URLs
            if (themeJson.has("collageThumbUrl")) theme.collagePreview.thumbUrl = themeJson["collageThumbUrl"].asString();
            if (themeJson.has("collageHdUrl")) theme.collagePreview.hdUrl = themeJson["collageHdUrl"].asString();
            if (themeJson.has("launcherThumbUrl")) theme.launcherScreenshot.thumbUrl = themeJson["launcherThumbUrl"].asString();
            if (themeJson.has("launcherHdUrl")) theme.launcherScreenshot.hdUrl = themeJson["launcherHdUrl"].asString();
            if (themeJson.has("waraWaraThumbUrl")) theme.waraWaraScreenshot.thumbUrl = themeJson["waraWaraThumbUrl"].asString();
            if (themeJson.has("waraWaraHdUrl")) theme.waraWaraScreenshot.hdUrl = themeJson["waraWaraHdUrl"].asString();
            if (themeJson.has("launcherBgUrl")) theme.launcherBgUrl = themeJson["launcherBgUrl"].asString();
            if (themeJson.has("waraWaraBgUrl")) theme.waraWaraBgUrl = themeJson["waraWaraBgUrl"].asString();
            
            if (!theme.id.empty() && !theme.name.empty()) {
                mThemes.push_back(theme);
                
                // 每隔5个主题记录一次进度
                if ((i + 1) % 5 == 0 || i == themesArray.size() - 1) {
                    FileLogger::GetInstance().LogInfo("DeserializeThemes: Loaded %zu/%zu themes", i + 1, themesArray.size());
                }
            } else {
                FileLogger::GetInstance().LogWarning("DeserializeThemes: Theme %zu has empty id or name", i);
            }
        }
        
        FileLogger::GetInstance().LogInfo("DeserializeThemes: Successfully loaded %zu themes", mThemes.size());
        return !mThemes.empty();
        
    } catch (...) {
        FileLogger::GetInstance().LogError("DeserializeThemes: Exception during parsing");
        return false;
    }
}

// 保存主题到缓存文件
bool ThemeManager::SaveCache() {
    FileLogger::GetInstance().LogInfo("Saving theme cache to: %s", CACHE_FILE);
    
    // 创建目录
    struct stat st;
    if (stat(CACHE_DIR, &st) != 0) {
        const char* paths[] = {
            "fs:/vol/external01/UTheme",
            "fs:/vol/external01/UTheme/temp"
        };
        
        for (const char* path : paths) {
            if (stat(path, &st) != 0) {
                if (mkdir(path, 0777) != 0) {
                    FileLogger::GetInstance().LogError("Failed to create directory: %s", path);
                    return false;
                }
            }
        }
    }
    
    // 序列化主题数据
    std::string json = SerializeThemes();
    
    FileLogger::GetInstance().LogInfo("SaveCache: JSON length before write: %zu bytes", json.length());
    
    // 删除旧文件（Wii U 文件系统可能需要这样做）
    unlink(CACHE_FILE);
    
    // 写入文件
    FILE* file = fopen(CACHE_FILE, "wb");  // 使用二进制模式
    if (!file) {
        FileLogger::GetInstance().LogError("Failed to open cache file for writing: errno=%d", errno);
        return false;
    }
    
    size_t written = fwrite(json.c_str(), 1, json.length(), file);
    
    // 获取文件描述符进行同步
    int fd = fileno(file);
    
    // 刷新缓冲区到内核
    if (fflush(file) != 0) {
        FileLogger::GetInstance().LogError("Failed to flush cache file: errno=%d", errno);
        fclose(file);
        return false;
    }
    
    // 强制同步到磁盘 (Wii U 必需)
    if (fsync(fd) != 0) {
        FileLogger::GetInstance().LogError("Failed to fsync cache file: errno=%d", errno);
        fclose(file);
        return false;
    }
    
    fclose(file);
    
    FileLogger::GetInstance().LogInfo("SaveCache: Attempted to write %zu bytes, actually wrote %zu bytes", 
        json.length(), written);
    
    if (written != json.length()) {
        FileLogger::GetInstance().LogError("Failed to write cache file (partial write: %zu/%zu)", written, json.length());
        return false;
    }
    
    // 验证文件大小
    struct stat verifySt;
    if (stat(CACHE_FILE, &verifySt) == 0) {
        FileLogger::GetInstance().LogInfo("SaveCache: File size on disk: %ld bytes", verifySt.st_size);
        
        if (verifySt.st_size == 0) {
            FileLogger::GetInstance().LogError("SaveCache: WARNING - File size is 0 despite successful write!");
        }
    } else {
        FileLogger::GetInstance().LogError("SaveCache: Failed to stat file after write: errno=%d", errno);
    }
    
    FileLogger::GetInstance().LogInfo("Saved %zu themes to cache (%zu bytes)", mThemes.size(), json.length());
    return true;
}

// 从缓存文件加载主题
bool ThemeManager::LoadCache() {
    FileLogger::GetInstance().LogInfo("Loading theme cache from: %s", CACHE_FILE);
    
    // 检查文件是否存在
    struct stat st;
    if (stat(CACHE_FILE, &st) != 0) {
        FileLogger::GetInstance().LogInfo("Cache file does not exist");
        return false;
    }
    
    // 读取文件内容
    FILE* file = fopen(CACHE_FILE, "r");
    if (!file) {
        FileLogger::GetInstance().LogError("Failed to open cache file for reading");
        return false;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (fileSize <= 0) {
        fclose(file);
        FileLogger::GetInstance().LogError("Cache file is empty");
        return false;
    }
    
    // 读取内容
    std::string json;
    json.resize(fileSize);
    size_t read = fread(&json[0], 1, fileSize, file);
    fclose(file);
    
    if (read != (size_t)fileSize) {
        FileLogger::GetInstance().LogError("Failed to read cache file");
        return false;
    }
    
    // 反序列化主题数据
    if (!DeserializeThemes(json)) {
        FileLogger::GetInstance().LogError("Failed to deserialize cache");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("Loaded %zu themes from cache (%zu bytes)", mThemes.size(), fileSize);
    return true;
}

// 检查缓存是否有效 (24小时内)
bool ThemeManager::IsCacheValid() const {
    struct stat st;
    if (stat(CACHE_FILE, &st) != 0) {
        return false; // 文件不存在
    }
    
    // 获取当前时间
    time_t now = time(NULL);
    time_t fileTime = st.st_mtime;
    
    // 检查是否在24小时内
    const time_t CACHE_VALIDITY_SECONDS = 24 * 60 * 60; // 24小时
    time_t age = now - fileTime;
    
    bool valid = (age >= 0 && age < CACHE_VALIDITY_SECONDS);
    
    if (valid) {
        FileLogger::GetInstance().LogInfo("Cache is valid (age: %ld seconds)", age);
    } else {
        FileLogger::GetInstance().LogInfo("Cache is invalid (age: %ld seconds, max: %ld)", age, CACHE_VALIDITY_SECONDS);
    }
    
    return valid;
}

// 后台检测更新
void ThemeManager::CheckForUpdates() {
    if (mCheckingUpdates || mThemes.empty()) {
        return;
    }
    
    mCheckingUpdates = true;
    mHasUpdates = false;
    
    FileLogger::GetInstance().LogInfo("Checking for theme updates...");
    
    // 构造 GraphQL 查询 (只获取基本信息用于对比)
    std::string query = R"({
        "query": "{ wiiuThemes(limit: 50) { nodes { uuid updatedAt } } }"
    })";
    
    std::string response = FetchUrl(THEMEZER_GRAPHQL_URL, query);
    
    if (response.empty()) {
        mCheckingUpdates = false;
        FileLogger::GetInstance().LogWarning("Failed to check for updates");
        return;
    }
    
    try {
        JsonValue root = SimpleJsonParser::Parse(response);
        
        if (!root.isObject() || !root.has("data")) {
            mCheckingUpdates = false;
            return;
        }
        
        const JsonValue& data = root["data"];
        if (!data.has("wiiuThemes") || !data["wiiuThemes"].has("nodes")) {
            mCheckingUpdates = false;
            return;
        }
        
        const JsonValue& nodes = data["wiiuThemes"]["nodes"];
        
        // 检查是否有新主题或更新
        for (size_t i = 0; i < nodes.size(); i++) {
            const JsonValue& node = nodes[i];
            
            if (!node.has("uuid") || !node.has("updatedAt")) continue;
            
            std::string id = node["uuid"].asString();
            std::string updatedAt = node["updatedAt"].asString();
            
            // 查找本地缓存中的主题
            bool found = false;
            bool isNewer = false;
            
            for (const Theme& theme : mThemes) {
                if (theme.id == id) {
                    found = true;
                    if (theme.updatedAt != updatedAt) {
                        isNewer = true;
                    }
                    break;
                }
            }
            
            // 如果是新主题或有更新
            if (!found || isNewer) {
                mHasUpdates = true;
                FileLogger::GetInstance().LogInfo("Found updates!");
                break;
            }
        }
        
    } catch (...) {
        FileLogger::GetInstance().LogError("Error parsing update check response");
    }
    
    mCheckingUpdates = false;
}

void ThemeManager::SaveThemeMetadata(const Theme& theme, const std::string& themePath) {
    FileLogger::GetInstance().LogInfo("Saving theme metadata to: %s", themePath.c_str());
    
    // 创建 JSON 元数据
    std::string metadataPath = themePath + "/theme_info.json";
    FILE* fp = fopen(metadataPath.c_str(), "w");
    if (!fp) {
        FileLogger::GetInstance().LogError("Failed to create metadata file: %s", metadataPath.c_str());
        return;
    }
    
    // 写入元数据
    fprintf(fp, "{\n");
    fprintf(fp, "  \"id\": \"%s\",\n", theme.id.c_str());
    fprintf(fp, "  \"name\": \"%s\",\n", theme.name.c_str());
    fprintf(fp, "  \"author\": \"%s\",\n", theme.author.c_str());
    fprintf(fp, "  \"description\": \"%s\",\n", theme.description.c_str());
    fprintf(fp, "  \"downloads\": %d,\n", theme.downloads);
    fprintf(fp, "  \"likes\": %d,\n", theme.likes);
    fprintf(fp, "  \"updatedAt\": \"%s\",\n", theme.updatedAt.c_str());
    fprintf(fp, "  \"tags\": [");
    for (size_t i = 0; i < theme.tags.size(); i++) {
        fprintf(fp, "\"%s\"", theme.tags[i].c_str());
        if (i < theme.tags.size() - 1) fprintf(fp, ", ");
    }
    fprintf(fp, "]\n");
    fprintf(fp, "}\n");
    fclose(fp);
    
    FileLogger::GetInstance().LogInfo("Metadata saved successfully");
    
    // 启动异步图片下载线程
    std::thread imageDownloadThread([theme, themePath]() {
        FileLogger::GetInstance().LogInfo("Starting async image downloads");
        
        // 创建 images 子目录
        std::string imagesDir = themePath + "/images";
        mkdir(imagesDir.c_str(), 0777);
        FileLogger::GetInstance().LogInfo("Created images directory: %s", imagesDir.c_str());
        
        int successCount = 0;
        int totalImages = 6;
        
        // 下载图片到 images/ 子目录 - 先删除旧文件以确保重新下载
        if (!theme.collagePreview.thumbUrl.empty()) {
            std::string thumbPath = imagesDir + "/collage_thumb.jpg";
            unlink(thumbPath.c_str());
            FileLogger::GetInstance().LogInfo("Downloading collage thumb: %s", theme.collagePreview.thumbUrl.c_str());
            if (ThemeManager::DownloadImageToFileStatic(theme.collagePreview.thumbUrl, thumbPath)) {
                successCount++;
            }
        }
        
        if (!theme.collagePreview.hdUrl.empty()) {
            std::string hdPath = imagesDir + "/collage.jpg";
            unlink(hdPath.c_str());
            FileLogger::GetInstance().LogInfo("Downloading collage HD: %s", theme.collagePreview.hdUrl.c_str());
            if (ThemeManager::DownloadImageToFileStatic(theme.collagePreview.hdUrl, hdPath)) {
                successCount++;
            }
        }
        
        if (!theme.launcherScreenshot.thumbUrl.empty()) {
            std::string thumbPath = imagesDir + "/launcher_thumb.jpg";
            unlink(thumbPath.c_str());
            FileLogger::GetInstance().LogInfo("Downloading launcher thumb: %s", theme.launcherScreenshot.thumbUrl.c_str());
            if (ThemeManager::DownloadImageToFileStatic(theme.launcherScreenshot.thumbUrl, thumbPath)) {
                successCount++;
            }
        }
        
        if (!theme.launcherScreenshot.hdUrl.empty()) {
            std::string hdPath = imagesDir + "/launcher.jpg";
            unlink(hdPath.c_str());
            FileLogger::GetInstance().LogInfo("Downloading launcher HD: %s", theme.launcherScreenshot.hdUrl.c_str());
            if (ThemeManager::DownloadImageToFileStatic(theme.launcherScreenshot.hdUrl, hdPath)) {
                successCount++;
            }
        }
        
        if (!theme.waraWaraScreenshot.thumbUrl.empty()) {
            std::string thumbPath = imagesDir + "/warawara_thumb.jpg";
            unlink(thumbPath.c_str());
            FileLogger::GetInstance().LogInfo("Downloading warawara thumb: %s", theme.waraWaraScreenshot.thumbUrl.c_str());
            if (ThemeManager::DownloadImageToFileStatic(theme.waraWaraScreenshot.thumbUrl, thumbPath)) {
                successCount++;
            }
        }
        
        if (!theme.waraWaraScreenshot.hdUrl.empty()) {
            std::string hdPath = imagesDir + "/warawara.jpg";
            unlink(hdPath.c_str());
            FileLogger::GetInstance().LogInfo("Downloading warawara HD: %s", theme.waraWaraScreenshot.hdUrl.c_str());
            if (ThemeManager::DownloadImageToFileStatic(theme.waraWaraScreenshot.hdUrl, hdPath)) {
                successCount++;
            }
        }
        
        FileLogger::GetInstance().LogInfo("Async image downloads complete: %d/%d successful", successCount, totalImages);
    });
    
    // 分离线程,让它在后台运行
    imageDownloadThread.detach();
    
    FileLogger::GetInstance().LogInfo("Metadata saved, image downloads started in background");
}

bool ThemeManager::DownloadImageToFile(const std::string& url, const std::string& filePath) {
    FileLogger::GetInstance().LogInfo("[START DOWNLOAD] URL: %s -> %s", url.c_str(), filePath.c_str());
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        FileLogger::GetInstance().LogError("Failed to initialize curl for image download");
        return false;
    }
    
    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        FileLogger::GetInstance().LogError("Failed to open file for writing: %s", filePath.c_str());
        curl_easy_cleanup(curl);
        return false;
    }
    
    FileDownloadData downloadData = { fp, 0 };
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &downloadData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    FileLogger::GetInstance().LogInfo("[DOWNLOAD COMPLETE] Total written: %zu bytes, CURL result: %d", downloadData.totalWritten, res);
    
    // 确保数据写入磁盘
    fflush(fp);
    fclose(fp);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        FileLogger::GetInstance().LogError("Failed to download image: %s", curl_easy_strerror(res));
        unlink(filePath.c_str());  // 删除失败的文件
        return false;
    }
    
    // 验证文件大小
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0) {
        FileLogger::GetInstance().LogInfo("Image downloaded successfully: %s (Size: %ld bytes)", filePath.c_str(), st.st_size);
        if (st.st_size == 0) {
            FileLogger::GetInstance().LogError("Downloaded file is empty: %s", filePath.c_str());
            unlink(filePath.c_str());
            return false;
        }
    } else {
        FileLogger::GetInstance().LogError("Failed to stat downloaded file: %s", filePath.c_str());
        return false;
    }
    
    return true;
}

// 静态版本供异步线程使用
bool ThemeManager::DownloadImageToFileStatic(const std::string& url, const std::string& filePath) {
    FileLogger::GetInstance().LogInfo("[ASYNC START DOWNLOAD] URL: %s -> %s", url.c_str(), filePath.c_str());
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        FileLogger::GetInstance().LogError("Failed to initialize curl for async image download");
        return false;
    }
    
    FILE* fp = fopen(filePath.c_str(), "wb");
    if (!fp) {
        FileLogger::GetInstance().LogError("Failed to open file for async writing: %s", filePath.c_str());
        curl_easy_cleanup(curl);
        return false;
    }
    
    FileDownloadData downloadData = { fp, 0 };
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &downloadData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    
    FileLogger::GetInstance().LogInfo("[ASYNC DOWNLOAD COMPLETE] Total written: %zu bytes, CURL result: %d", downloadData.totalWritten, res);
    
    // 确保数据写入磁盘
    fflush(fp);
    fclose(fp);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        FileLogger::GetInstance().LogError("Failed to async download image: %s", curl_easy_strerror(res));
        unlink(filePath.c_str());  // 删除失败的文件
        return false;
    }
    
    // 验证文件大小
    struct stat st;
    if (stat(filePath.c_str(), &st) == 0) {
        FileLogger::GetInstance().LogInfo("Async image downloaded successfully: %s (Size: %ld bytes)", filePath.c_str(), st.st_size);
        if (st.st_size == 0) {
            FileLogger::GetInstance().LogError("Async downloaded file is empty: %s", filePath.c_str());
            unlink(filePath.c_str());
            return false;
        }
    } else {
        FileLogger::GetInstance().LogError("Failed to stat async downloaded file: %s", filePath.c_str());
        return false;
    }
    
    return true;
}
