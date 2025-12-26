#pragma once
#include <string>
#include <queue>
#include <functional>

class BackupManager {
public:
    struct FileEntry {
        std::string path;
        bool isDirectory;
        FileEntry(const std::string& p, bool isDir) : path(p), isDirectory(isDir) {}
    };

    // 备份回调函数类型
    using ProgressCallback = std::function<void(int current, int total, const std::string& currentFile)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    BackupManager();
    ~BackupManager();

    // 开始备份（扫描文件）
    bool StartBackup(const std::string& sourcePath, const std::string& backupPath);
    
    // 开始选择性备份（只备份将被SD卡文件覆盖的MLC文件）
    bool StartSelectiveBackup(const std::string& mlcPath, const std::string& sdSourcePath, const std::string& backupPath);
    
    // 更新备份进度（每帧调用一次）
    // 返回值：true = 继续进行中，false = 已完成
    bool UpdateBackup();
    
    // 取消备份
    void CancelBackup();
    
    // 获取备份状态
    bool IsBackupInProgress() const { return mIsBackupInProgress; }
    bool IsScanning() const { return mIsScanning; }
    int GetTotalItems() const { return mTotalItems; }
    int GetProcessedItems() const { return mProcessedItems; }
    int GetScannedDirs() const { return mScannedDirs; }  // 已扫描的目录数
    std::string GetCurrentFile() const { return mCurrentFile; }
    
    // 设置回调
    void SetProgressCallback(ProgressCallback callback) { mProgressCallback = callback; }
    void SetErrorCallback(ErrorCallback callback) { mErrorCallback = callback; }

private:
    // 异步扫描目录（每帧处理一个目录）
    bool UpdateScan();
    
    // 扫描目录
    void ScanDirectory(const std::string& srcPath, const std::string& dstPath);
    
    // 选择性扫描：只添加SD卡中存在的MLC文件
    void ScanDirectorySelective(const std::string& mlcPath, const std::string& sdSourcePath, const std::string& backupPath);
    
    // 开始文件复制
    bool StartFileCopy(const std::string& srcPath, const std::string& dstPath);
    
    // 继续文件复制（分块）
    bool ContinueFileCopy();
    
    // 结束文件复制
    void EndFileCopy();

    std::queue<FileEntry> mPendingFiles;
    std::string mCurrentFile;
    int mTotalItems;
    int mProcessedItems;
    int mScannedDirs;  // 已扫描的目录数量
    bool mIsBackupInProgress;
    bool mIsScanning;
    
    // 异步扫描状态
    struct ScanEntry {
        std::string srcPath;
        std::string dstPath;
        ScanEntry(const std::string& src, const std::string& dst) : srcPath(src), dstPath(dst) {}
    };
    std::queue<ScanEntry> mPendingScans;
    bool mIsSelectiveScan;
    std::string mSdSourceBasePath;  // 用于选择性扫描
    
    // 文件复制状态
    FILE* mCopySourceFile;
    FILE* mCopyDestFile;
    long mCopyFileSize;
    long mCopyFileCopied;
    bool mIsCopyingFile;
    
    std::string mSourcePath;
    std::string mBackupPath;
    
    // 回调函数
    ProgressCallback mProgressCallback;
    ErrorCallback mErrorCallback;
};
