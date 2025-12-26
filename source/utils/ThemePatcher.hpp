#pragma once

#include <string>
#include <map>
#include <vector>
#include <functional>

// 系统区域
enum SystemRegion {
    REGION_JPN = 0,
    REGION_USA = 1,
    REGION_EUR = 2,
    REGION_UNIVERSAL = 3
};

// 主题元数据
struct ThemeMetadata {
    std::string themeID;
    std::string themeName;
    std::string themeAuthor;
    std::string themeVersion;
    SystemRegion themeRegion;
    std::map<std::string, std::string> patches; // patch文件名 -> 目标文件路径
};

// 主题补丁器
class ThemePatcher {
public:
    ThemePatcher();
    ~ThemePatcher();
    
    // 获取系统区域
    static SystemRegion GetSystemRegion();
    
    // 获取系统菜单路径
    static std::pair<std::string, std::string> GetMenuPaths();
    
    // 从 ZIP 中读取主题元数据（可选，用于查看主题信息）
    bool ReadThemeMetadata(const std::string& themePath, ThemeMetadata& metadata);
    
    // 安装主题（应用 BPS 补丁）
    // themePath: 解压后的主题文件夹路径
    // themeID, themeName, themeAuthor: 主题信息（用于保存安装记录）
    bool InstallTheme(const std::string& themePath, 
                     const std::string& themeID,
                     const std::string& themeName, 
                     const std::string& themeAuthor);
    
    // 卸载主题
    bool UninstallTheme(const std::string& themeID);
    
    // 检查主题是否已安装
    bool IsThemeInstalled(const std::string& themeID);
    
    // 获取已安装的主题列表
    std::vector<ThemeMetadata> GetInstalledThemes();
    
    // 设置进度回调
    void SetProgressCallback(std::function<void(float progress, const std::string& message)> callback);
    
private:
    std::function<void(float progress, const std::string& message)> mProgressCallback;
    
    // 内部方法
    bool CreateCacheFile(const std::string& sourcePath, const std::string& cachePath);
    bool ApplyBPSPatch(const std::vector<uint8_t>& sourceData, 
                      const std::vector<uint8_t>& patchData,
                      std::vector<uint8_t>& outputData);
    bool CreateDirectoryRecursive(const std::string& path);
    void ScanForBPSFiles(const std::string& basePath, const std::string& currentPath, 
                        std::vector<std::string>& bpsFiles);
};
