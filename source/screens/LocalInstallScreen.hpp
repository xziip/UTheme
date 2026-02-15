#pragma once
#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>

// 本地.utheme文件结构
struct UThemeFile {
    std::string fileName;      // 文件名
    std::string fullPath;      // 完整路径
    std::string displayName;   // 显示名称(不含.utheme后缀)
    uint64_t fileSize;         // 文件大小(字节)
    std::string fileSizeStr;   // 文件大小(格式化字符串)
};

// 动画状态
struct ItemAnimation {
    Animation scaleAnim;
    Animation highlightAnim;
};

class LocalInstallScreen : public Screen {
public:
    LocalInstallScreen();
    ~LocalInstallScreen() override;

    void Draw() override;
    bool Update(Input &input) override;

private:
    enum State {
        STATE_LOADING,           // 正在扫描文件
        STATE_FILE_LIST,         // 显示文件列表
        STATE_CONFIRM_INSTALL,   // 确认安装对话框
        STATE_INSTALLING,        // 正在安装
        STATE_NUS_CONFIRM,       // NUS下载确认对话框
        STATE_INSTALL_COMPLETE,  // 安装完成
        STATE_INSTALL_ERROR,     // 安装失败
        STATE_EMPTY              // 没有找到.utheme文件
    };
    
    std::atomic<State> mState{STATE_LOADING};
    int mFrameCount = 0;
    
    // 文件列表
    std::vector<UThemeFile> mThemeFiles;
    int mSelectedIndex = 0;
    int mScrollOffset = 0;
    
    // UI配置
    static constexpr int ITEMS_PER_PAGE = 6;
    static constexpr int ITEM_HEIGHT = 100;
    
    // 动画
    Animation mTitleAnim;
    Animation mContentAnim;
    Animation mListAnim;
    std::vector<ItemAnimation> mItemAnims;  // 每个文件项的动画状态
    
    // 安装相关
    bool mDeleteAfterInstall = false;  // 是否在安装后删除原文件
    std::atomic<float> mInstallProgress{0.0f};
    std::string mInstallError;
    std::string mInstalledThemeName;
    
    // NUS下载确认
    std::atomic<bool> mNUSConfirmPending{false};
    std::atomic<bool> mNUSConfirmResult{false};
    std::string mNUSConfirmTitle;
    std::string mNUSConfirmMessage;
    
    // 安装线程
    std::thread mInstallThread;
    std::atomic<bool> mInstallThreadRunning{false};
    
    // 触摸输入
    bool mTouchStarted = false;
    int mTouchStartY = 0;
    int mTouchCurrentY = 0;
    
    // 按键重复控制
    int mInputRepeatDelay = 0;
    static constexpr int INPUT_REPEAT_INITIAL = 30;  // 初始延迟(约0.5秒)
    static constexpr int INPUT_REPEAT_RATE = 5;      // 重复速率(约0.08秒)
    
    // 辅助函数
    void ScanThemeFiles();
    void InitAnimations();
    void UpdateAnimations();
    void DrawFileList();
    void DrawConfirmDialog();
    void DrawInstallProgress();
    void DrawInstallResult();
    void DrawEmptyState();
    void DrawNUSConfirmDialog();
    
    void StartInstall();
    void PerformInstall(); // 在线程中执行
    
    std::string FormatFileSize(uint64_t bytes);
    bool IsTouchInRect(int touchX, int touchY, int rectX, int rectY, int rectW, int rectH);
    bool DeleteDirectoryRecursive(const std::string& path); // 递归删除目录
};
