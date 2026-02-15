#include "ThemeDownloader.hpp"
#include "FileLogger.hpp"
#include "logger.h"
#include "minizip/unzip.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <coreinit/filesystem.h>

#define THEMES_BASE_PATH "fs:/vol/external01/wiiu/themes"

// 静态初始化 CURL (只执行一次)
static bool curl_initialized = false;
static void EnsureCurlInitialized() {
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
        FileLogger::GetInstance().LogInfo("[CURL] Global initialization completed");
    }
}

ThemeDownloader::ThemeDownloader() 
    : mState(DOWNLOAD_IDLE), mProgress(0.0f), mCancelRequested(false) {
    EnsureCurlInitialized();
    FileLogger::GetInstance().LogInfo("[ThemeDownloader] Constructor called");
}

ThemeDownloader::~ThemeDownloader() {
    FileLogger::GetInstance().LogInfo("[ThemeDownloader] Destructor called");
    
    // 请求取消并等待线程结束
    if (IsDownloading()) {
        FileLogger::GetInstance().LogInfo("[ThemeDownloader] Requesting download cancellation and waiting...");
        mCancelRequested.store(true);
        
        // 必须等待线程结束，否则线程可能还在访问对象成员
        if (mDownloadThread.joinable()) {
            FileLogger::GetInstance().LogInfo("[ThemeDownloader] Joining download thread...");
            mDownloadThread.join();  // 使用 join 等待线程结束
            FileLogger::GetInstance().LogInfo("[ThemeDownloader] Download thread joined successfully");
        }
    } else {
        // 即使不在下载状态，也要确保线程结束
        if (mDownloadThread.joinable()) {
            FileLogger::GetInstance().LogInfo("[ThemeDownloader] Joining idle thread...");
            mDownloadThread.join();
            FileLogger::GetInstance().LogInfo("[ThemeDownloader] Idle thread joined");
        }
    }
    
    // 清理临时文件
    CleanupDownload();
    
    FileLogger::GetInstance().LogInfo("[ThemeDownloader] Destructor completed");
}

void ThemeDownloader::SetProgressCallback(std::function<void(float progress, long downloaded, long total)> callback) {
    mProgressCallback = callback;
}

void ThemeDownloader::SetStateCallback(std::function<void(DownloadState state, const std::string& message)> callback) {
    mStateCallback = callback;
}

void ThemeDownloader::Cancel() {
    if (IsDownloading()) {
        DEBUG_FUNCTION_LINE("Cancelling download...");
        FileLogger::GetInstance().LogInfo("[ThemeDownloader] Cancel requested, waiting for thread");
        mCancelRequested.store(true);
        
        // 等待线程结束以确保安全
        if (mDownloadThread.joinable()) {
            DEBUG_FUNCTION_LINE("Joining download thread");
            FileLogger::GetInstance().LogInfo("[ThemeDownloader] Joining thread in Cancel()");
            mDownloadThread.join();
            FileLogger::GetInstance().LogInfo("[ThemeDownloader] Thread joined in Cancel()");
        }
        
        mState.store(DOWNLOAD_CANCELLED);
        if (mStateCallback) {
            mStateCallback(DOWNLOAD_CANCELLED, "Download cancelled");
        }
        FileLogger::GetInstance().LogInfo("[ThemeDownloader] Cancel completed");
    }
}

void ThemeDownloader::DownloadThemeAsync(const std::string& downloadUrl, const std::string& themeName, const std::string& themeId) {
    // 如果已经在下载或有之前的线程,先等待结束
    if (mDownloadThread.joinable()) {
        FileLogger::GetInstance().LogInfo("[DownloadThemeAsync] Waiting for previous thread to finish");
        mCancelRequested.store(true);
        mDownloadThread.join();  // 等待线程结束
        FileLogger::GetInstance().LogInfo("[DownloadThemeAsync] Previous thread finished");
    }
    
    // 保存 themeId
    mThemeId = themeId;
    
    // 重置状态
    mState.store(DOWNLOAD_IDLE);
    mProgress.store(0.0f);
    mCancelRequested.store(false);
    mErrorMessage.clear();
    
    // 启动下载线程
    mDownloadThread = std::thread(&ThemeDownloader::DownloadThreadFunc, this, downloadUrl, themeName);
}

size_t ThemeDownloader::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    FILE* fp = (FILE*)userp;
    return fwrite(contents, size, nmemb, fp);
}

int ThemeDownloader::ProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                                     curl_off_t ultotal, curl_off_t ulnow) {
    ThemeDownloader* downloader = (ThemeDownloader*)clientp;
    
    // 检查是否需要取消
    if (downloader->mCancelRequested.load()) {
        DEBUG_FUNCTION_LINE("Download cancelled by user");
        return 1; // 非0返回值会让CURL中止
    }
    
    if (dltotal > 0) {
        float progress = (float)dlnow / (float)dltotal;
        downloader->mProgress.store(progress * 0.9f); // 下载占90%，解压占10%
        
        // 调用进度回调
        if (downloader->mProgressCallback) {
            downloader->mProgressCallback(progress, (long)dlnow, (long)dltotal);
        }
        
        DEBUG_FUNCTION_LINE("Download progress: %.1f%% (%lld / %lld bytes)", 
                          progress * 100.0f, dlnow, dltotal);
    }
    
    return 0;
}

// 清理文件名中的非法字符
std::string ThemeDownloader::SanitizeFileName(const std::string& fileName) {
    std::string safe = fileName;
    bool modified = false;
    
    // 替换文件系统非法字符 (单个字符逐一检查替换)
    for (size_t i = 0; i < safe.length(); ++i) {
        unsigned char ch = (unsigned char)safe[i];
        char original = safe[i];
        
        // 检查是否为ASCII非法字符
        if (ch == '?' || ch == '<' || ch == '>' || ch == ':' || 
            ch == '*' || ch == '|' || ch == '"' || ch == '/' || ch == '\\') {
            safe[i] = '_';
            modified = true;
            FileLogger::GetInstance().LogInfo("[SanitizeFileName] Replaced ASCII char 0x%02X ('%c') at position %zu", 
                (unsigned char)original, (original >= 32 && original < 127) ? original : '?', i);
        }
        // 移除ASCII控制字符
        else if (ch < 32 || ch == 127) {
            safe[i] = '_';
            modified = true;
            FileLogger::GetInstance().LogInfo("[SanitizeFileName] Replaced control char 0x%02X at position %zu", ch, i);
        }
    }
    
    // 处理可能导致文件系统问题的Unicode字符
    // 这些字符在某些FAT32实现中可能不被支持
    const char* problematicSequences[] = {
        "\xE0\xA3\xAA",  // ࣪ (Arabic combining mark - U+08EA)
        "\xCB\x96",      // ˖ (modifier letter - U+02D6)
        "\xE2\x9F\xA1",  // ⟡ (white star - U+27E1)
        "\xEF\xB8\x8F",  // ️ (variation selector)
        NULL
    };
    
    for (int seq = 0; problematicSequences[seq] != NULL; ++seq) {
        const char* pattern = problematicSequences[seq];
        size_t patLen = strlen(pattern);
        size_t pos = 0;
        while ((pos = safe.find(pattern, pos)) != std::string::npos) {
            safe.replace(pos, patLen, "_");
            modified = true;
            FileLogger::GetInstance().LogInfo("[SanitizeFileName] Replaced Unicode sequence at position %zu", pos);
            pos += 1;  // 移动到下一个位置
        }
    }
    
    // 移除所有下划线的连续重复
    auto new_end = std::unique(safe.begin(), safe.end(), 
        [](char a, char b) { return a == '_' && b == '_'; });
    if (new_end != safe.end()) {
        safe.erase(new_end, safe.end());
        modified = true;
    }
    
    // 移除首尾空格和下划线
    while (!safe.empty() && (safe[0] == ' ' || safe[0] == '_')) {
        safe.erase(0, 1);
        modified = true;
    }
    while (!safe.empty() && (safe[safe.length()-1] == ' ' || safe[safe.length()-1] == '_')) {
        safe.erase(safe.length()-1);
        modified = true;
    }
    
    // 限制长度 (避免路径过长)
    if (safe.length() > 100) {
        safe = safe.substr(0, 100);
        modified = true;
    }
    
    // 如果清理后为空，使用默认名称
    if (safe.empty()) {
        safe = "theme";
        modified = true;
    }
    
    if (!modified) {
        FileLogger::GetInstance().LogInfo("[SanitizeFileName] No changes needed for: %s", fileName.c_str());
    } else {
        FileLogger::GetInstance().LogInfo("[SanitizeFileName] Sanitized: %s -> %s", fileName.c_str(), safe.c_str());
    }
    
    return safe;
}

void ThemeDownloader::DownloadThreadFunc(const std::string& url, const std::string& themeName) {
    DEBUG_FUNCTION_LINE("Download thread started for theme: %s", themeName.c_str());
    FileLogger::GetInstance().LogInfo("Starting async download for: %s", themeName.c_str());
    
    // 检查磁盘空间
    long long availableMB = GetAvailableDiskSpaceMB();
    
    if (availableMB < 0) {
        // 无法检测磁盘空间
        mErrorMessage = "[[disk_space_check_failed]]";
        FileLogger::GetInstance().LogError("Failed to check disk space");
        mState.store(DOWNLOAD_ERROR);
        if (mStateCallback) {
            mStateCallback(DOWNLOAD_ERROR, mErrorMessage);
        }
        return;
    }
    
    FileLogger::GetInstance().LogInfo("Available disk space: %lld MB", availableMB);
    
    if (availableMB < 100) {
        // 磁盘空间不足
        mErrorMessage = "[[disk_space_low:" + std::to_string(availableMB) + "]]";
        FileLogger::GetInstance().LogWarning("Disk space low: %lld MB", availableMB);
        mState.store(DOWNLOAD_ERROR);
        if (mStateCallback) {
            mStateCallback(DOWNLOAD_ERROR, mErrorMessage);
        }
        return;
    }
    
    // 调试: 打印主题名的十六进制
    std::string hexDump;
    for (size_t i = 0; i < themeName.length() && i < 200; ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", (unsigned char)themeName[i]);
        hexDump += buf;
    }
    FileLogger::GetInstance().LogInfo("[HEX DUMP] Theme name bytes: %s", hexDump.c_str());
    
    // 设置路径
    std::string cacheDir = "fs:/vol/external01/UTheme/cache";
    CreateDirectoryRecursive(cacheDir);
    
    // 清理文件名中的非法字符
    std::string safeThemeName = SanitizeFileName(themeName);
    FileLogger::GetInstance().LogInfo("Sanitized theme name: %s -> %s", themeName.c_str(), safeThemeName.c_str());
    
    // 构建文件夹名称: "Theme Name ([themeID])"
    std::string folderName;
    if (!mThemeId.empty()) {
        folderName = safeThemeName + " ([" + mThemeId + "])";
        FileLogger::GetInstance().LogInfo("Using folder name with ID: %s", folderName.c_str());
    } else {
        folderName = safeThemeName;
        FileLogger::GetInstance().LogInfo("No theme ID provided, using theme name only: %s", folderName.c_str());
    }
    
    mTempFilePath = cacheDir + "/" + safeThemeName + ".zip";
    mExtractPath = std::string(THEMES_BASE_PATH) + "/" + folderName;
    
    FileLogger::GetInstance().LogInfo("Download paths - ZIP: %s, extract: %s", 
        mTempFilePath.c_str(), mExtractPath.c_str());
    
    // 状态：开始下载
    mState.store(DOWNLOAD_DOWNLOADING);
    if (mStateCallback) {
        mStateCallback(DOWNLOAD_DOWNLOADING, "Downloading theme...");
    }
    
    // 下载文件
    if (!DownloadFile(url, mTempFilePath)) {
        if (!mCancelRequested.load()) {
            mState.store(DOWNLOAD_ERROR);
            if (mStateCallback) {
                mStateCallback(DOWNLOAD_ERROR, mErrorMessage);
            }
            // 清理失败的下载文件
            CleanupDownload();
        }
        return;
    }
    
    // 检查是否取消
    if (mCancelRequested.load()) {
        return;
    }
    
    // 状态：解压缩
    mState.store(DOWNLOAD_EXTRACTING);
    mProgress.store(0.9f); // 显示90%
    if (mStateCallback) {
        mStateCallback(DOWNLOAD_EXTRACTING, "Extracting theme files...");
    }
    
    // 解压文件到 wiiu/themes/themeName/ （包含 BPS 补丁文件和 metadata.json）
    if (!ExtractZip(mTempFilePath, mExtractPath)) {
        if (!mCancelRequested.load()) {
            mState.store(DOWNLOAD_ERROR);
            if (mStateCallback) {
                mStateCallback(DOWNLOAD_ERROR, mErrorMessage);
            }
            // 清理失败的下载和解压文件
            CleanupDownload();
        }
        return;
    }
    
    // 完成下载和解压，ZIP 文件和解压后的文件都保留
    mState.store(DOWNLOAD_COMPLETE);
    mProgress.store(1.0f);
    if (mStateCallback) {
        mStateCallback(DOWNLOAD_COMPLETE, "Download complete!");
    }
    
    DEBUG_FUNCTION_LINE("Download thread finished, extracted to: %s", mExtractPath.c_str());
    FileLogger::GetInstance().LogInfo("Download and extraction completed, path: %s", mExtractPath.c_str());
}

bool ThemeDownloader::CreateDirectoryRecursive(const std::string& path) {
    std::string current;
    size_t pos = 0;
    
    while (pos < path.length()) {
        size_t nextSlash = path.find('/', pos);
        if (nextSlash == std::string::npos) {
            current = path;
            pos = path.length();
        } else {
            current = path.substr(0, nextSlash);
            pos = nextSlash + 1;
        }
        
        if (!current.empty() && current != "fs:" && current != "fs:/vol" && 
            current != "fs:/vol/external01") {
            struct stat st;
            if (stat(current.c_str(), &st) != 0) {
                if (mkdir(current.c_str(), 0777) != 0) {
                    FileLogger::GetInstance().LogError("Failed to create directory: %s", current.c_str());
                    return false;
                }
            }
        }
    }
    
    return true;
}

bool ThemeDownloader::DownloadFile(const std::string& url, const std::string& outputPath) {
    FileLogger::GetInstance().LogInfo("Downloading: %s -> %s", url.c_str(), outputPath.c_str());
    
    // 创建输出目录
    std::string dir = outputPath.substr(0, outputPath.find_last_of('/'));
    CreateDirectoryRecursive(dir);
    
    // 打开输出文件
    FILE* fp = fopen(outputPath.c_str(), "wb");
    if (!fp) {
        mErrorMessage = "Failed to create temp file";
        FileLogger::GetInstance().LogError("Failed to create file: %s", outputPath.c_str());
        return false;
    }
    
    // 初始化 CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        mErrorMessage = "Failed to initialize CURL";
        return false;
    }
    
    // 设置 CURL 选项
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // 5分钟超时
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "UTheme/1.0 (Wii U)");
    
    // 性能优化设置
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);        // TCP keepalive
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);         // 允许连接复用
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);        // 优先复用现有连接
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 524288L);      // 512KB大缓冲区(主题文件大)
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0); // HTTP/2
    
    // 设置进度回调
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    
    // 执行下载
    CURLcode res = curl_easy_perform(curl);
    
    // 清理
    fclose(fp);
    
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl);
        
        // 如果是用户取消，不报告错误
        if (mCancelRequested.load()) {
            FileLogger::GetInstance().LogInfo("Download cancelled by user");
            return false;
        }
        
        mErrorMessage = std::string("Download failed: ") + curl_easy_strerror(res);
        FileLogger::GetInstance().LogError("CURL error [%d]: %s", res, curl_easy_strerror(res));
        return false;
    }
    
    // 检查 HTTP 状态码
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);
    
    if (httpCode != 200) {
        mErrorMessage = "HTTP error: " + std::to_string(httpCode);
        FileLogger::GetInstance().LogError("HTTP error: %ld", httpCode);
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("Download completed successfully");
    return true;
}

bool ThemeDownloader::ExtractZip(const std::string& zipPath, const std::string& extractPath) {
    FileLogger::GetInstance().LogInfo("Extracting: %s -> %s", zipPath.c_str(), extractPath.c_str());
    
    // 创建目标目录
    CreateDirectoryRecursive(extractPath);
    
    // 打开 ZIP 文件
    unzFile zipFile = unzOpen(zipPath.c_str());
    if (!zipFile) {
        mErrorMessage = "Failed to open ZIP file";
        FileLogger::GetInstance().LogError("Failed to open ZIP: %s", zipPath.c_str());
        return false;
    }
    
    // 获取文件信息
    unz_global_info globalInfo;
    if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK) {
        unzClose(zipFile);
        mErrorMessage = "Failed to get ZIP info";
        return false;
    }
    
    // 解压每个文件
    char filename[256];
    unz_file_info fileInfo;
    
    for (uLong i = 0; i < globalInfo.number_entry; i++) {
        // 检查是否取消
        if (mCancelRequested.load()) {
            unzClose(zipFile);
            return false;
        }
        
        if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, sizeof(filename), 
                                 nullptr, 0, nullptr, 0) != UNZ_OK) {
            break;
        }
        
        std::string fullPath = extractPath + "/" + filename;
        
        // 如果是目录
        if (filename[strlen(filename) - 1] == '/') {
            CreateDirectoryRecursive(fullPath);
        } else {
            // 创建父目录
            std::string dir = fullPath.substr(0, fullPath.find_last_of('/'));
            CreateDirectoryRecursive(dir);
            
            // 打开 ZIP 中的文件
            if (unzOpenCurrentFile(zipFile) != UNZ_OK) {
                break;
            }
            
            // 创建输出文件
            FILE* outFile = fopen(fullPath.c_str(), "wb");
            if (outFile) {
                char buffer[8192];
                int bytesRead;
                
                while ((bytesRead = unzReadCurrentFile(zipFile, buffer, sizeof(buffer))) > 0) {
                    fwrite(buffer, 1, bytesRead, outFile);
                }
                
                fclose(outFile);
            }
            
            unzCloseCurrentFile(zipFile);
        }
        
        // 移动到下一个文件
        if (i + 1 < globalInfo.number_entry) {
            if (unzGoToNextFile(zipFile) != UNZ_OK) {
                break;
            }
        }
        
        // 更新进度
        float extractProgress = 0.9f + (0.1f * (float)(i + 1) / (float)globalInfo.number_entry);
        mProgress.store(extractProgress);
    }
    
    unzClose(zipFile);
    
    FileLogger::GetInstance().LogInfo("Extraction completed");
    return true;
}

void ThemeDownloader::CleanupDownload() {
    FileLogger::GetInstance().LogInfo("[CleanupDownload] Cleaning up failed download...");
    
    // 删除临时 ZIP 文件
    if (!mTempFilePath.empty()) {
        if (unlink(mTempFilePath.c_str()) == 0) {
            FileLogger::GetInstance().LogInfo("[CleanupDownload] Deleted ZIP: %s", mTempFilePath.c_str());
        } else {
            FileLogger::GetInstance().LogWarning("[CleanupDownload] Failed to delete ZIP: %s", mTempFilePath.c_str());
        }
    }
    
    // 删除不完整的解压目录（递归删除）
    if (!mExtractPath.empty()) {
        std::function<bool(const std::string&)> removeDirectory = [&](const std::string& path) -> bool {
            DIR* dir = opendir(path.c_str());
            if (!dir) {
                return false;
            }
            
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name == "." || name == "..") continue;
                
                std::string fullPath = path + "/" + name;
                struct stat st;
                if (stat(fullPath.c_str(), &st) == 0) {
                    if (S_ISDIR(st.st_mode)) {
                        removeDirectory(fullPath);
                    } else {
                        unlink(fullPath.c_str());
                    }
                }
            }
            closedir(dir);
            
            return rmdir(path.c_str()) == 0;
        };
        
        if (removeDirectory(mExtractPath)) {
            FileLogger::GetInstance().LogInfo("[CleanupDownload] Deleted directory: %s", mExtractPath.c_str());
        } else {
            FileLogger::GetInstance().LogWarning("[CleanupDownload] Failed to delete directory: %s", mExtractPath.c_str());
        }
    }
    
    FileLogger::GetInstance().LogInfo("[CleanupDownload] Cleanup completed");
}

long long ThemeDownloader::GetAvailableDiskSpaceMB() {
    FSClient* fsClient = (FSClient*)malloc(sizeof(FSClient));
    if (!fsClient) {
        FileLogger::GetInstance().LogError("[GetAvailableDiskSpaceMB] Failed to allocate FSClient");
        return -1;
    }
    
    FSStatus addClientStatus = FSAddClient(fsClient, FS_ERROR_FLAG_NONE);
    if (addClientStatus != FS_STATUS_OK) {
        FileLogger::GetInstance().LogError("[GetAvailableDiskSpaceMB] FSAddClient failed with status %d", addClientStatus);
        free(fsClient);
        return -1;
    }
    
    FSCmdBlock* cmdBlock = (FSCmdBlock*)malloc(sizeof(FSCmdBlock));
    if (!cmdBlock) {
        FileLogger::GetInstance().LogError("[GetAvailableDiskSpaceMB] Failed to allocate FSCmdBlock");
        FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
        free(fsClient);
        return -1;
    }
    
    FSInitCmdBlock(cmdBlock);
    
    uint64_t freeSpace = 0;
    FSStatus fsStatus = FSGetFreeSpaceSize(fsClient, 
                                           cmdBlock,
                                           "/vol/external01", 
                                           &freeSpace,
                                           FS_ERROR_FLAG_ALL);
    
    free(cmdBlock);
    FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
    free(fsClient);
    
    if (fsStatus >= 0) {
        long long availableMB = (long long)(freeSpace / (1024 * 1024));
        FileLogger::GetInstance().LogInfo("[GetAvailableDiskSpaceMB] ✓ FSGetFreeSpaceSize: %llu bytes (%lld MB)",
                                         (unsigned long long)freeSpace, availableMB);
        return availableMB;
    }
    
    FileLogger::GetInstance().LogError("[GetAvailableDiskSpaceMB] FSGetFreeSpaceSize failed with status %d (0x%X)", 
                                      fsStatus, (unsigned int)fsStatus);
    return -1;
}
