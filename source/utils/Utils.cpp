#include "Utils.hpp"
#include "logger.h"
#include <cstring>

#include <coreinit/debug.h>
#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#include <dirent.h>
#include <fstream>
#include <malloc.h>
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>

namespace Utils {
    bool CheckFile(const std::string &fullpath) {
        struct stat filestat {};

        char dirnoslash[strlen(fullpath.c_str()) + 2];
        snprintf(dirnoslash, sizeof(dirnoslash), "%s", fullpath.c_str());

        while (dirnoslash[strlen(dirnoslash) - 1] == '/') {
            dirnoslash[strlen(dirnoslash) - 1] = '\0';
        }

        char *notRoot = strrchr(dirnoslash, '/');
        if (!notRoot) {
            strcat(dirnoslash, "/");
        }

        return stat(dirnoslash, &filestat) == 0;
    }

    bool CreateSubfolder(const std::string &fullpath) {
        if (fullpath.empty()) {
            return false;
        }

        char dirnoslash[fullpath.length() + 1];
        strcpy(dirnoslash, fullpath.c_str());

        auto pos = strlen(dirnoslash) - 1;
        while (dirnoslash[pos] == '/') {
            dirnoslash[pos] = '\0';
            pos--;
        }

        if (CheckFile(dirnoslash))
            return true;

        char parentpath[strlen(dirnoslash) + 2];
        strcpy(parentpath, dirnoslash);
        char *ptr = strrchr(parentpath, '/');

        if (!ptr) {
            //! Device root directory (must be with '/')
            strcat(parentpath, "/");
            struct stat filestat {};
            return stat(parentpath, &filestat) == 0;
        }

        ptr++;
        ptr[0] = '\0';

        if (CreateSubfolder(parentpath) == 0)
            return false;

        return mkdir(dirnoslash, 0777) >= 0;
    }

    bool CopyFile(const std::string &in, const std::string &out) {
        try {
            std::ifstream src(in.c_str(), std::ios::binary);
            std::ofstream dst(out.c_str(), std::ios::binary);

            dst << src.rdbuf();
            return true;
        } catch (std::exception &ex) {
            DEBUG_FUNCTION_LINE_ERR("Exception: (Tried to copy %s -> %s): %s", in.c_str(), out.c_str(), ex.what());
        }
        return false;
    }

    bool CopyFolder(const std::string &in, const std::string &out, CopyProgressCallback progressCallback) {
        // First pass: count total files for progress tracking
        int totalFiles = 0;
        if (progressCallback) {
            DIR *countDir = opendir(in.c_str());
            if (countDir) {
                struct dirent *dp;
                while ((dp = readdir(countDir)) != nullptr) {
                    std::string name = dp->d_name;
                    if (name == "." || name == "..") continue;
                    
                    std::string srcPath = in + "/" + name;
                    struct stat filestat {};
                    if (stat(srcPath.c_str(), &filestat) == 0) {
                        if ((filestat.st_mode & S_IFMT) != S_IFDIR) {
                            totalFiles++;
                        }
                    }
                }
                closedir(countDir);
            }
        }

        DIR *srcDir = opendir(in.c_str());
        if (!srcDir) {
            DEBUG_FUNCTION_LINE_ERR("Failed to open source directory %s", in.c_str());
            return false;
        }

        // ensure destination folder exists
        if (!CreateSubfolder(out)) {
            DEBUG_FUNCTION_LINE_ERR("Failed to create destination directory %s", out.c_str());
            closedir(srcDir);
            return false;
        }

        // Report directory creation progress
        if (progressCallback) {
            progressCallback(out, true);
            usleep(1000); // Give UI a chance to update
        }

        struct dirent *dp;
        while ((dp = readdir(srcDir)) != nullptr) {
            std::string name = dp->d_name;
            if (name == "." || name == "..")
                continue;

            std::string srcPath = in + "/" + name;
            std::string dstPath = out + "/" + name;

            struct stat filestat {};
            if (stat(srcPath.c_str(), &filestat) < 0) {
                DEBUG_FUNCTION_LINE_ERR("Failed to stat %s", srcPath.c_str());
                closedir(srcDir);
                return false;
            }

            if ((filestat.st_mode & S_IFMT) == S_IFDIR) {
                // Report directory progress before recursing
                if (progressCallback) {
                    progressCallback(srcPath, true);
                    usleep(1000); // Give UI a chance to update
                }
                // recurse into directory
                if (!CopyFolder(srcPath, dstPath, progressCallback)) {
                    closedir(srcDir);
                    return false;
                }
            } else {
                // Report file progress
                if (progressCallback) {
                    progressCallback(srcPath, false);
                    usleep(1000); // Give UI a chance to update
                }
                // copy file
                if (!CopyFile(srcPath, dstPath)) {
                    DEBUG_FUNCTION_LINE_ERR("Failed to copy file %s -> %s", srcPath.c_str(), dstPath.c_str());
                    closedir(srcDir);
                    return false;
                }
            }
        }

        closedir(srcDir);
        return true;
    }

    // 清理主题名称中的特殊Unicode字符用于显示
    std::string SanitizeThemeNameForDisplay(const std::string& themeName) {
        std::string safe = themeName;
        
        // 处理可能导致显示问题的Unicode字符
        // 这些字符在某些字体中可能显示不正确或导致文件系统问题
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
                safe.replace(pos, patLen, "");  // 直接移除,不替换为下划线
                // pos 保持不变,继续从当前位置查找
            }
        }
        
        // 移除ASCII非法字符(显示为下划线)
        for (size_t i = 0; i < safe.length(); ++i) {
            unsigned char ch = (unsigned char)safe[i];
            if (ch == '?' || ch == '<' || ch == '>' || ch == ':' || 
                ch == '*' || ch == '|' || ch == '"' || ch == '/' || ch == '\\') {
                safe[i] = '_';
            }
            // 移除控制字符
            else if (ch < 32 || ch == 127) {
                safe[i] = ' ';
            }
        }
        
        // 移除多余的空格
        size_t pos = 0;
        while ((pos = safe.find("  ", pos)) != std::string::npos) {
            safe.replace(pos, 2, " ");
        }
        
        // 移除首尾空格
        while (!safe.empty() && safe[0] == ' ') {
            safe.erase(0, 1);
        }
        while (!safe.empty() && safe[safe.length()-1] == ' ') {
            safe.erase(safe.length()-1);
        }
        
        // 如果清理后为空，返回原名称
        if (safe.empty()) {
            return themeName;
        }
        
        return safe;
    }

} // namespace Utils
