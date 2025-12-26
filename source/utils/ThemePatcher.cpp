#include "ThemePatcher.hpp"
#include "SimpleJsonParser.hpp"
#include "FileLogger.hpp"
#include "logger.h"
#include "hips.hpp"
#include "minizip/unzip.h"
#include <sysapp/title.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fstream>
#include <cstring>

#define WII_U_MENU_JPN_TID 0x0005001010040000ULL
#define WII_U_MENU_USA_TID 0x0005001010040100ULL
#define WII_U_MENU_EUR_TID 0x0005001010040200ULL

#define THEMES_ROOT "fs:/vol/external01/wiiu/themes"
#define CACHE_ROOT "fs:/vol/external01/UTheme/cache"
#define INSTALLED_THEMES_ROOT "fs:/vol/external01/UTheme/installed"

ThemePatcher::ThemePatcher() {
}

ThemePatcher::~ThemePatcher() {
}

void ThemePatcher::SetProgressCallback(std::function<void(float progress, const std::string& message)> callback) {
    mProgressCallback = callback;
}

SystemRegion ThemePatcher::GetSystemRegion() {
    uint64_t menuTitleID = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_WII_U_MENU);
    
    SystemRegion region = REGION_USA; // 默认美版
    
    switch (menuTitleID) {
        case WII_U_MENU_JPN_TID:
            region = REGION_JPN;
            break;
        case WII_U_MENU_USA_TID:
            region = REGION_USA;
            break;
        case WII_U_MENU_EUR_TID:
            region = REGION_EUR;
            break;
    }
    
    DEBUG_FUNCTION_LINE("System region: %d (TitleID: %016llx)", region, menuTitleID);
    return region;
}

std::pair<std::string, std::string> ThemePatcher::GetMenuPaths() {
    uint64_t menuTitleID = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_WII_U_MENU);
    
    // Title ID 格式: 0x0005001010040X00
    // 高32位: 0x00050010 (父目录)
    // 低32位: 0x10040X00 (子目录)
    uint32_t menuIDParentDir = (uint32_t)(menuTitleID >> 32);
    uint32_t menuIDChildDir = (uint32_t)menuTitleID;
    
    char splitMenuID[18];
    snprintf(splitMenuID, sizeof(splitMenuID), "%08x/%08x", menuIDParentDir, menuIDChildDir);
    
    // 使用 storage_mlc_UTheme 设备名（与 BackupManager 一致）
    std::string menuContentPath = "storage_mlc_UTheme:/sys/title/" + std::string(splitMenuID) + "/content/";
    
    DEBUG_FUNCTION_LINE("Menu Title ID: %016llx", menuTitleID);
    DEBUG_FUNCTION_LINE("Menu path components: %08x / %08x", menuIDParentDir, menuIDChildDir);
    DEBUG_FUNCTION_LINE("Menu content path: %s", menuContentPath.c_str());
    
    // 第二个参数不再使用，但为了保持接口兼容性返回空字符串
    return {menuContentPath, ""};
}

bool ThemePatcher::CreateDirectoryRecursive(const std::string& path) {
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
            current != "fs:/vol/external01" && current != "storage_mlc_UTheme:" &&
            current != "storage_mlc_UTheme:/sys") {
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

void ThemePatcher::ScanForBPSFiles(const std::string& basePath, const std::string& currentPath, 
                                   std::vector<std::string>& bpsFiles) {
    DIR* dir = opendir(currentPath.c_str());
    if (!dir) {
        FileLogger::GetInstance().LogError("Failed to open directory: %s", currentPath.c_str());
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        
        // 跳过 . 和 ..
        if (name == "." || name == "..") {
            continue;
        }
        
        std::string fullPath = currentPath + "/" + name;
        
        // 检查是否是目录
        struct stat st;
        if (stat(fullPath.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // 跳过 patched 目录
                if (name != "patched") {
                    // 递归扫描子目录
                    ScanForBPSFiles(basePath, fullPath, bpsFiles);
                }
            } else if (S_ISREG(st.st_mode)) {
                // 检查是否是 .bps 文件
                if (name.length() > 4 && name.substr(name.length() - 4) == ".bps") {
                    // 保存相对路径
                    std::string relPath = fullPath.substr(basePath.length());
                    if (relPath[0] == '/') {
                        relPath = relPath.substr(1);
                    }
                    bpsFiles.push_back(relPath);
                    FileLogger::GetInstance().LogInfo("Found BPS file: %s", relPath.c_str());
                }
            }
        }
    }
    
    closedir(dir);
}

bool ThemePatcher::ReadThemeMetadata(const std::string& themePath, ThemeMetadata& metadata) {
    FileLogger::GetInstance().LogInfo("Reading theme metadata from: %s", themePath.c_str());
    
    // themePath 现在是解压后的文件夹路径，不是 ZIP 文件
    std::string metadataPath = themePath + "/metadata.json";
    
    // 读取 metadata.json 文件
    FILE* file = fopen(metadataPath.c_str(), "r");
    if (!file) {
        FileLogger::GetInstance().LogError("Failed to open metadata.json: %s", metadataPath.c_str());
        return false;
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    if (fileSize <= 0) {
        FileLogger::GetInstance().LogError("metadata.json is empty");
        fclose(file);
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("metadata.json size: %zu bytes", fileSize);
    
    // 读取内容
    std::string jsonContent;
    jsonContent.resize(fileSize);
    size_t bytesRead = fread(&jsonContent[0], 1, fileSize, file);
    fclose(file);
    
    if (bytesRead != fileSize) {
        FileLogger::GetInstance().LogError("Failed to read metadata.json completely: read %zu of %zu bytes", 
            bytesRead, fileSize);
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("Read metadata.json content (first 200 chars): %.200s", jsonContent.c_str());
    
    // 解析 JSON
    try {
        JsonValue root = SimpleJsonParser::Parse(jsonContent);
        
        FileLogger::GetInstance().LogInfo("JSON parsed successfully");
        
        if (!root.has("Metadata")) {
            FileLogger::GetInstance().LogError("Invalid metadata.json: missing Metadata section");
            // 尝试直接从根节点读取（可能格式不同）
            if (root.has("themeID")) {
                FileLogger::GetInstance().LogInfo("Found flat metadata format, using direct fields");
                metadata.themeID = root.has("themeID") ? root["themeID"].asString() : "";
                metadata.themeName = root.has("themeName") ? root["themeName"].asString() : "";
                metadata.themeAuthor = root.has("themeAuthor") ? root["themeAuthor"].asString() : "";
                metadata.themeVersion = root.has("themeVersion") ? root["themeVersion"].asString() : "1.0";
                metadata.themeRegion = REGION_UNIVERSAL;
                
                FileLogger::GetInstance().LogInfo("Theme metadata loaded (flat): %s by %s",
                    metadata.themeName.c_str(), metadata.themeAuthor.c_str());
                return true;
            }
            return false;
        }
        
        const JsonValue& metadataNode = root["Metadata"];
        
        metadata.themeID = metadataNode.has("themeID") ? metadataNode["themeID"].asString() : "";
        metadata.themeName = metadataNode.has("themeName") ? metadataNode["themeName"].asString() : "";
        metadata.themeAuthor = metadataNode.has("themeAuthor") ? metadataNode["themeAuthor"].asString() : "";
        metadata.themeVersion = metadataNode.has("themeVersion") ? metadataNode["themeVersion"].asString() : "1.0";
        
        // 默认为 UNIVERSAL
        metadata.themeRegion = REGION_UNIVERSAL;
        
        FileLogger::GetInstance().LogInfo("Theme metadata loaded: %s by %s (ID: %s)",
            metadata.themeName.c_str(), metadata.themeAuthor.c_str(), metadata.themeID.c_str());
        
        return true;
    } catch (const std::exception& e) {
        FileLogger::GetInstance().LogError("Failed to parse metadata.json: %s", e.what());
        return false;
    } catch (...) {
        FileLogger::GetInstance().LogError("Failed to parse metadata.json: unknown error");
        return false;
    }
}

bool ThemePatcher::CreateCacheFile(const std::string& sourcePath, const std::string& cachePath) {
    FileLogger::GetInstance().LogInfo("Creating cache: %s -> %s", sourcePath.c_str(), cachePath.c_str());
    
    // 创建缓存目录
    std::string cacheDir = cachePath.substr(0, cachePath.find_last_of('/'));
    if (!CreateDirectoryRecursive(cacheDir)) {
        return false;
    }
    
    // 打开源文件
    FILE* sourceFile = fopen(sourcePath.c_str(), "rb");
    if (!sourceFile) {
        FileLogger::GetInstance().LogError("Failed to open source file: %s", sourcePath.c_str());
        return false;
    }
    
    // 获取文件大小
    fseek(sourceFile, 0, SEEK_END);
    size_t sourceSize = ftell(sourceFile);
    rewind(sourceFile);
    
    // 读取数据
    std::vector<uint8_t> buffer(sourceSize);
    size_t bytesRead = fread(buffer.data(), 1, sourceSize, sourceFile);
    fclose(sourceFile);
    
    if (bytesRead != sourceSize) {
        FileLogger::GetInstance().LogError("Failed to read source file completely");
        return false;
    }
    
    // 写入缓存
    FILE* cacheFile = fopen(cachePath.c_str(), "wb");
    if (!cacheFile) {
        FileLogger::GetInstance().LogError("Failed to create cache file: %s", cachePath.c_str());
        return false;
    }
    
    size_t bytesWritten = fwrite(buffer.data(), 1, sourceSize, cacheFile);
    fclose(cacheFile);
    
    if (bytesWritten != sourceSize) {
        FileLogger::GetInstance().LogError("Failed to write cache file completely");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("Cache created successfully (%zu bytes)", sourceSize);
    return true;
}

bool ThemePatcher::ApplyBPSPatch(const std::vector<uint8_t>& sourceData, 
                                 const std::vector<uint8_t>& patchData,
                                 std::vector<uint8_t>& outputData) {
    // 使用 libhips 应用 BPS 补丁
    auto [result, status] = Hips::patchBPS(
        sourceData.data(), sourceData.size(),
        patchData.data(), patchData.size()
    );
    
    if (status != Hips::Result::Success) {
        const char* errorMsg = "Unknown error";
        switch (status) {
            case Hips::Result::InvalidPatch: errorMsg = "Invalid patch"; break;
            case Hips::Result::SizeMismatch: errorMsg = "Size mismatch"; break;
            case Hips::Result::ChecksumMismatch: errorMsg = "Checksum mismatch"; break;
            default: break;
        }
        FileLogger::GetInstance().LogError("BPS patching failed: %s", errorMsg);
        return false;
    }
    
    outputData = std::move(result);
    return true;
}

bool ThemePatcher::InstallTheme(const std::string& themePath, 
                                const std::string& themeID,
                                const std::string& themeName, 
                                const std::string& themeAuthor) {
    FileLogger::GetInstance().LogInfo("Installing theme: %s from path: %s", themeName.c_str(), themePath.c_str());
    
    if (mProgressCallback) {
        mProgressCallback(0.0f, "Preparing installation...");
    }
    
    // themePath 现在是解压后的文件夹路径：wiiu/themes/主题名/
    // 补丁文件在这个文件夹里，修补后的文件输出到 patched/ 子目录
    std::string patchedPath = themePath + "/patched";
    
    FileLogger::GetInstance().LogInfo("Theme folder: %s", themePath.c_str());
    FileLogger::GetInstance().LogInfo("Patched output: %s", patchedPath.c_str());
    
    // 创建 patched 目录
    if (!CreateDirectoryRecursive(patchedPath)) {
        FileLogger::GetInstance().LogError("Failed to create patched directory");
        return false;
    }
    
    // 扫描主题文件夹，查找所有 .bps 文件
    std::vector<std::string> bpsFiles;
    ScanForBPSFiles(themePath, themePath, bpsFiles);
    
    if (bpsFiles.empty()) {
        FileLogger::GetInstance().LogError("No BPS patch files found in theme folder");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("Found %zu BPS patch files", bpsFiles.size());
    
    // 获取系统菜单路径
    auto menuPathsPair = GetMenuPaths();
    std::string menuContentPath = menuPathsPair.first;
    
    if (menuContentPath.empty()) {
        FileLogger::GetInstance().LogError("Failed to get system menu paths");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("System menu content: %s", menuContentPath.c_str());
    
    // 应用所有补丁
    int patchedCount = 0;
    for (size_t i = 0; i < bpsFiles.size(); i++) {
        const std::string& bpsRelPath = bpsFiles[i];
        std::string bpsFullPath = themePath + "/" + bpsRelPath;
        
        // BPS 文件名就是目标文件名（不含扩展名）
        // 例如: Men.bps -> 修补 Common/Package/Men.pack
        //       Men2.bps -> 修补 Common/Package/Men2.pack
        //       cafe_barista_men.bps -> 修补 Common/Sound/Men/cafe_barista_men (音频文件)
        // 先获取纯文件名（去掉路径和 .bps 后缀）
        std::string bpsFileName = bpsRelPath;
        size_t lastSlash = bpsFileName.find_last_of('/');
        if (lastSlash != std::string::npos) {
            bpsFileName = bpsFileName.substr(lastSlash + 1);
        }
        
        // 移除 .bps 后缀得到目标文件名（不含扩展名）
        std::string targetBaseName = bpsFileName.substr(0, bpsFileName.length() - 4);
        
        // 判断是否是音频文件(cafe_barista_men 等)
        bool isAudioFile = (targetBaseName.find("cafe_barista") != std::string::npos);
        
        std::string originalFilePath;
        std::string originalFileName;
        
        if (isAudioFile) {
            // 音频文件在 Common/Sound/Men 目录,扩展名为 .bfsar
            originalFileName = targetBaseName + ".bfsar";
            originalFilePath = menuContentPath + "Common/Sound/Men/" + originalFileName;
            FileLogger::GetInstance().LogInfo("Audio file detected: %s", originalFileName.c_str());
        } else {
            // 系统菜单界面文件都是 .pack 格式,在 Common/Package/ 下
            originalFileName = targetBaseName + ".pack";
            originalFilePath = menuContentPath + "Common/Package/" + originalFileName;
        }
        
        FileLogger::GetInstance().LogInfo("Patching [%zu/%zu]: %s", i + 1, bpsFiles.size(), originalFileName.c_str());
        
        // 读取原始文件
        FILE* origFile = fopen(originalFilePath.c_str(), "rb");
        if (!origFile) {
            FileLogger::GetInstance().LogError("Failed to open original file: %s", originalFilePath.c_str());
            continue;
        }
        
        fseek(origFile, 0, SEEK_END);
        size_t origSize = ftell(origFile);
        rewind(origFile);
        
        std::vector<uint8_t> originalData(origSize);
        fread(originalData.data(), 1, origSize, origFile);
        fclose(origFile);
        
        // 读取补丁文件
        FILE* patchF = fopen(bpsFullPath.c_str(), "rb");
        if (!patchF) {
            FileLogger::GetInstance().LogError("Failed to open patch: %s", bpsFullPath.c_str());
            continue;
        }
        
        fseek(patchF, 0, SEEK_END);
        size_t patchSize = ftell(patchF);
        rewind(patchF);
        
        std::vector<uint8_t> patchData(patchSize);
        fread(patchData.data(), 1, patchSize, patchF);
        fclose(patchF);
        
        // 应用 BPS 补丁
        std::vector<uint8_t> patchedData;
        if (!ApplyBPSPatch(originalData, patchData, patchedData)) {
            FileLogger::GetInstance().LogError("Failed to apply patch: %s", originalFileName.c_str());
            
            // 显式释放内存
            originalData.clear();
            originalData.shrink_to_fit();
            patchData.clear();
            patchData.shrink_to_fit();
            
            continue;
        }
        
        // 显式释放不再需要的内存
        originalData.clear();
        originalData.shrink_to_fit();
        patchData.clear();
        patchData.shrink_to_fit();
        
        // 保存修补后的文件到 patched/ 子目录（保持相同的目录结构）
        std::string patchedFilePath = patchedPath + "/Common/Package/" + originalFileName;
        
        // 创建父目录
        size_t slashPos = patchedFilePath.find_last_of('/');
        if (slashPos != std::string::npos) {
            CreateDirectoryRecursive(patchedFilePath.substr(0, slashPos));
        }
        
        FILE* outFile = fopen(patchedFilePath.c_str(), "wb");
        if (outFile) {
            fwrite(patchedData.data(), 1, patchedData.size(), outFile);
            fclose(outFile);
            patchedCount++;
            FileLogger::GetInstance().LogInfo("Patched successfully: %s (%zu bytes)", originalFileName.c_str(), patchedData.size());
        } else {
            FileLogger::GetInstance().LogError("Failed to write patched file: %s", patchedFilePath.c_str());
        }
        
        // 显式释放 patchedData 内存
        patchedData.clear();
        patchedData.shrink_to_fit();
        
        // 进度更新
        if (mProgressCallback) {
            float progress = (float)(i + 1) / bpsFiles.size();
            char msg[256];
            snprintf(msg, sizeof(msg), "Applying patch %zu/%zu", i + 1, bpsFiles.size());
            mProgressCallback(progress, msg);
        }
    }
    
    FileLogger::GetInstance().LogInfo("Successfully patched %d/%zu files", patchedCount, bpsFiles.size());
    
    // 保存安装信息
    std::string installedInfoPath = std::string(INSTALLED_THEMES_ROOT) + "/" + themeID + ".json";
    CreateDirectoryRecursive(INSTALLED_THEMES_ROOT);
    
    std::string installJson = "{\n";
    installJson += "  \"themeID\": \"" + themeID + "\",\n";
    installJson += "  \"themeName\": \"" + themeName + "\",\n";
    installJson += "  \"themeAuthor\": \"" + themeAuthor + "\",\n";
    installJson += "  \"installPath\": \"" + themePath + "\",\n";
    installJson += "  \"patchedFiles\": " + std::to_string(patchedCount) + "\n";
    installJson += "}\n";
    
    FILE* jsonFile = fopen(installedInfoPath.c_str(), "w");
    if (jsonFile) {
        fwrite(installJson.c_str(), 1, installJson.length(), jsonFile);
        fclose(jsonFile);
        FileLogger::GetInstance().LogInfo("Saved installation info to: %s", installedInfoPath.c_str());
    }
    
    if (mProgressCallback) {
        mProgressCallback(1.0f, "Installation complete");
    }
    
    return true;
}

bool DeleteDirectoryRecursive(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        return false;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        std::string fullPath = path + "/" + entry->d_name;
        
        if (entry->d_type == DT_DIR) {
            DeleteDirectoryRecursive(fullPath);
        } else {
            unlink(fullPath.c_str());
        }
    }
    
    closedir(dir);
    rmdir(path.c_str());
    
    return true;
}

bool ThemePatcher::UninstallTheme(const std::string& themeID) {
    FileLogger::GetInstance().LogInfo("Uninstalling theme: %s", themeID.c_str());
    
    // 读取安装信息获取主题名称
    std::string installedInfoPath = std::string(INSTALLED_THEMES_ROOT) + "/" + themeID + ".json";
    
    FILE* jsonFile = fopen(installedInfoPath.c_str(), "r");
    if (!jsonFile) {
        FileLogger::GetInstance().LogError("Theme not installed or info file missing");
        return false;
    }
    
    fseek(jsonFile, 0, SEEK_END);
    size_t fileSize = ftell(jsonFile);
    rewind(jsonFile);
    
    std::string jsonContent;
    jsonContent.resize(fileSize);
    fread(&jsonContent[0], 1, fileSize, jsonFile);
    fclose(jsonFile);
    
    // 简单解析获取 themeName
    std::string themeName;
    size_t namePos = jsonContent.find("\"themeName\"");
    if (namePos != std::string::npos) {
        size_t colonPos = jsonContent.find(":", namePos);
        size_t quoteStart = jsonContent.find("\"", colonPos);
        size_t quoteEnd = jsonContent.find("\"", quoteStart + 1);
        themeName = jsonContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }
    
    if (themeName.empty()) {
        FileLogger::GetInstance().LogError("Failed to parse theme name");
        return false;
    }
    
    // 删除主题目录
    std::string themeBasePath = std::string(THEMES_ROOT) + "/" + themeName;
    DeleteDirectoryRecursive(themeBasePath);
    
    // 删除安装信息
    unlink(installedInfoPath.c_str());
    
    FileLogger::GetInstance().LogInfo("Theme uninstalled successfully");
    
    return true;
}

bool ThemePatcher::IsThemeInstalled(const std::string& themeID) {
    std::string installedInfoPath = std::string(INSTALLED_THEMES_ROOT) + "/" + themeID + ".json";
    
    struct stat st;
    return (stat(installedInfoPath.c_str(), &st) == 0);
}

std::vector<ThemeMetadata> ThemePatcher::GetInstalledThemes() {
    std::vector<ThemeMetadata> themes;
    
    DIR* dir = opendir(INSTALLED_THEMES_ROOT);
    if (!dir) {
        FileLogger::GetInstance().LogInfo("No installed themes directory");
        return themes;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) {
            continue;
        }
        
        std::string filename = entry->d_name;
        if (filename.length() < 5 || filename.substr(filename.length() - 5) != ".json") {
            continue;
        }
        
        std::string fullPath = std::string(INSTALLED_THEMES_ROOT) + "/" + filename;
        
        FILE* jsonFile = fopen(fullPath.c_str(), "r");
        if (!jsonFile) {
            continue;
        }
        
        fseek(jsonFile, 0, SEEK_END);
        size_t fileSize = ftell(jsonFile);
        rewind(jsonFile);
        
        std::string jsonContent;
        jsonContent.resize(fileSize);
        fread(&jsonContent[0], 1, fileSize, jsonFile);
        fclose(jsonFile);
        
        // 简单解析
        ThemeMetadata metadata;
        
        // 解析 themeID
        size_t pos = jsonContent.find("\"themeID\"");
        if (pos != std::string::npos) {
            size_t colonPos = jsonContent.find(":", pos);
            size_t quoteStart = jsonContent.find("\"", colonPos);
            size_t quoteEnd = jsonContent.find("\"", quoteStart + 1);
            metadata.themeID = jsonContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
        
        // 解析 themeName
        pos = jsonContent.find("\"themeName\"");
        if (pos != std::string::npos) {
            size_t colonPos = jsonContent.find(":", pos);
            size_t quoteStart = jsonContent.find("\"", colonPos);
            size_t quoteEnd = jsonContent.find("\"", quoteStart + 1);
            metadata.themeName = jsonContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
        
        // 解析 themeAuthor
        pos = jsonContent.find("\"themeAuthor\"");
        if (pos != std::string::npos) {
            size_t colonPos = jsonContent.find(":", pos);
            size_t quoteStart = jsonContent.find("\"", colonPos);
            size_t quoteEnd = jsonContent.find("\"", quoteStart + 1);
            metadata.themeAuthor = jsonContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
        
        // 解析 themeVersion
        pos = jsonContent.find("\"themeVersion\"");
        if (pos != std::string::npos) {
            size_t colonPos = jsonContent.find(":", pos);
            size_t quoteStart = jsonContent.find("\"", colonPos);
            size_t quoteEnd = jsonContent.find("\"", quoteStart + 1);
            metadata.themeVersion = jsonContent.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
        
        metadata.themeRegion = REGION_UNIVERSAL;
        
        themes.push_back(metadata);
    }
    
    closedir(dir);
    
    FileLogger::GetInstance().LogInfo("Found %zu installed themes", themes.size());
    
    return themes;
}
