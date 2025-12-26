#pragma once

#include <string>
#include <list>
#include <functional>
#include <curl/curl.h>

// 下载状态
enum class DownloadStatus {
    QUEUED,      // 队列中等待
    DOWNLOADING, // 正在下载
    COMPLETE,    // 下载完成
    FAILED       // 下载失败
};

// 下载操作
struct DownloadOperation {
    std::string url;                                     // URL
    std::string postData;                                // POST 数据 (空表示 GET 请求)
    std::string buffer;                                  // 下载数据缓冲区
    DownloadStatus status = DownloadStatus::QUEUED;     // 状态
    CURL* eh = nullptr;                                  // Easy handle
    std::function<void(DownloadOperation*)> cb;          // 完成回调
    void* cbdata = nullptr;                              // 回调数据
    long response_code = 0;                              // HTTP 响应码
};

// 下载队列管理器 (单例)
class DownloadQueue {
public:
    static void Init();
    static void Quit();
    
    // 添加下载任务
    void DownloadAdd(DownloadOperation* download);
    
    // 取消下载任务
    void DownloadCancel(DownloadOperation* download);
    
    // 处理下载队列 (在主循环中调用)
    // 返回值: 是否还有活动的下载
    int Process();
    
    // 获取全局实例
    static DownloadQueue* GetInstance() { return sDownloadQueue; }
    
private:
    DownloadQueue();
    ~DownloadQueue();
    
    void TransferStart(DownloadOperation* download);
    void TransferFinish(DownloadOperation* download);
    void StartTransfersFromQueue();
    
    CURLM* mCurlMulti = nullptr;           // CURL multi handle
    std::list<DownloadOperation*> mQueue;  // 等待队列
    int mActiveTransfers = 0;              // 活动的传输数量
    
    static DownloadQueue* sDownloadQueue;  // 全局单例
    static constexpr int MAX_PARALLEL_DOWNLOADS = 4; // 最大并发下载数
};
