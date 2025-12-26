#include "DownloadQueue.hpp"
#include "logger.h"
#include "FileLogger.hpp"
#include <cstring>

// 全局单例
DownloadQueue* DownloadQueue::sDownloadQueue = nullptr;

// CURL 写入回调
static size_t WriteCallback(char* data, size_t n, size_t l, void* userp) {
    DownloadOperation* download = (DownloadOperation*)userp;
    download->buffer.append(data, n * l);
    return n * l;
}

void DownloadQueue::Init() {
    if (sDownloadQueue == nullptr) {
        sDownloadQueue = new DownloadQueue();
        DEBUG_FUNCTION_LINE("DownloadQueue initialized");
        FileLogger::GetInstance().LogInfo("DownloadQueue initialized");
    }
}

void DownloadQueue::Quit() {
    if (sDownloadQueue != nullptr) {
        delete sDownloadQueue;
        sDownloadQueue = nullptr;
        DEBUG_FUNCTION_LINE("DownloadQueue cleaned up");
        FileLogger::GetInstance().LogInfo("DownloadQueue cleaned up");
    }
}

DownloadQueue::DownloadQueue() {
    mCurlMulti = curl_multi_init();
    if (mCurlMulti) {
        curl_multi_setopt(mCurlMulti, CURLMOPT_MAXCONNECTS, MAX_PARALLEL_DOWNLOADS);
        DEBUG_FUNCTION_LINE("CURLM initialized with max %d parallel downloads", MAX_PARALLEL_DOWNLOADS);
        FileLogger::GetInstance().LogInfo("CURLM initialized with max %d parallel downloads", MAX_PARALLEL_DOWNLOADS);
    } else {
        DEBUG_FUNCTION_LINE("Failed to initialize CURLM!");
        FileLogger::GetInstance().LogError("Failed to initialize CURLM!");
    }
}

DownloadQueue::~DownloadQueue() {
    // 清理所有队列中的任务
    for (auto* download : mQueue) {
        if (download->status == DownloadStatus::DOWNLOADING) {
            TransferFinish(download);
        }
    }
    mQueue.clear();
    
    if (mCurlMulti) {
        curl_multi_cleanup(mCurlMulti);
        mCurlMulti = nullptr;
    }
}

void DownloadQueue::DownloadAdd(DownloadOperation* download) {
    download->status = DownloadStatus::QUEUED;
    mQueue.push_back(download);
    if (FileLogger::GetInstance().IsVerbose()) {
        FileLogger::GetInstance().LogDebug("[DOWNLOAD] Added to queue: %s", download->url.c_str());
    }
}

void DownloadQueue::DownloadCancel(DownloadOperation* download) {
    if (download->status == DownloadStatus::DOWNLOADING) {
        TransferFinish(download);
        if (FileLogger::GetInstance().IsVerbose()) {
            FileLogger::GetInstance().LogDebug("[DOWNLOAD] Cancelled active transfer: %s", download->url.c_str());
        }
    } else if (download->status == DownloadStatus::QUEUED && !mQueue.empty()) {
        mQueue.remove(download);
        if (FileLogger::GetInstance().IsVerbose()) {
            FileLogger::GetInstance().LogDebug("[DOWNLOAD] Removed from queue: %s", download->url.c_str());
        }
    }
}

void DownloadQueue::TransferStart(DownloadOperation* download) {
    if (!mCurlMulti) {
        DEBUG_FUNCTION_LINE("[DOWNLOAD] ERROR: CURLM not initialized!");
        FileLogger::GetInstance().LogError("[DOWNLOAD] ERROR: CURLM not initialized!");
        return;
    }
    
    download->eh = curl_easy_init();
    if (!download->eh) {
        DEBUG_FUNCTION_LINE("[DOWNLOAD] Failed to create easy handle for: %s", download->url.c_str());
        FileLogger::GetInstance().LogError("[DOWNLOAD] Failed to create easy handle for: %s", download->url.c_str());
        return;
    }
    
    // 配置 CURL
    curl_easy_setopt(download->eh, CURLOPT_URL, download->url.c_str());
    curl_easy_setopt(download->eh, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(download->eh, CURLOPT_WRITEDATA, download);
    curl_easy_setopt(download->eh, CURLOPT_PRIVATE, download);
    curl_easy_setopt(download->eh, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(download->eh, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(download->eh, CURLOPT_CONNECTTIMEOUT, 10L);
    
    // SSL 设置
    curl_easy_setopt(download->eh, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(download->eh, CURLOPT_SSL_VERIFYHOST, 0L);
    
    // POST 数据支持
    struct curl_slist* headers = nullptr;
    if (!download->postData.empty()) {
        curl_easy_setopt(download->eh, CURLOPT_POST, 1L);
        curl_easy_setopt(download->eh, CURLOPT_POSTFIELDS, download->postData.c_str());
        curl_easy_setopt(download->eh, CURLOPT_POSTFIELDSIZE, download->postData.size());
        
        // 设置 Content-Type 为 JSON
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(download->eh, CURLOPT_HTTPHEADER, headers);
        
        if (FileLogger::GetInstance().IsVerbose()) {
            FileLogger::GetInstance().LogDebug("[DOWNLOAD] POST request with %zu bytes data", download->postData.size());
        }
    }
    
    // 添加到 multi handle
    curl_multi_add_handle(mCurlMulti, download->eh);
    mActiveTransfers++;
    
    if (FileLogger::GetInstance().IsVerbose()) {
        FileLogger::GetInstance().LogDebug("[DOWNLOAD] Started transfer (%d active): %s", mActiveTransfers, download->url.c_str());
    }
    
    // 注意: headers 会在 TransferFinish 时被清理
    if (headers) {
        // 将 headers 存储到 cbdata 中以便后续清理
        // 暂时不清理,让 CURL 在传输时使用
    }
}

void DownloadQueue::TransferFinish(DownloadOperation* download) {
    if (!download->eh) {
        return;
    }
    
    if (mCurlMulti) {
        curl_multi_remove_handle(mCurlMulti, download->eh);
    }
    curl_easy_cleanup(download->eh);
    download->eh = nullptr;
    mActiveTransfers--;
    
    if (FileLogger::GetInstance().IsVerbose()) {
        FileLogger::GetInstance().LogDebug("[DOWNLOAD] Finished transfer (%d active): %s", mActiveTransfers, download->url.c_str());
    }
}

void DownloadQueue::StartTransfersFromQueue() {
    while (mActiveTransfers < MAX_PARALLEL_DOWNLOADS && !mQueue.empty()) {
        DownloadOperation* download = mQueue.front();
        mQueue.pop_front();
        
        download->status = DownloadStatus::DOWNLOADING;
        TransferStart(download);
    }
}

int DownloadQueue::Process() {
    if (!mCurlMulti) {
        return 0;
    }
    
    int still_alive = 1;
    int msgs_left = -1;
    
    // 执行传输
    curl_multi_perform(mCurlMulti, &still_alive);
    
    // 处理完成的下载
    CURLMsg* msg;
    while ((msg = curl_multi_info_read(mCurlMulti, &msgs_left))) {
        if (msg->msg != CURLMSG_DONE) {
            continue;
        }
        
        DownloadOperation* download = nullptr;
        curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &download);
        curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &download->response_code);
        
        TransferFinish(download);
        StartTransfersFromQueue();
        
        // 设置状态
        if (download->response_code == 200) {
            download->status = DownloadStatus::COMPLETE;
            if (FileLogger::GetInstance().IsVerbose()) {
                FileLogger::GetInstance().LogDebug("[DOWNLOAD] Complete (HTTP %ld): %s (%zu bytes)", 
                           download->response_code, download->url.c_str(), download->buffer.size());
            }
        } else {
            download->status = DownloadStatus::FAILED;
            FileLogger::GetInstance().LogError("[DOWNLOAD] Failed (HTTP %ld): %s", 
                       download->response_code, download->url.c_str());
        }
        
        // 调用回调
        if (download->cb) {
            download->cb(download);
        }
    }
    
    // 启动队列中的新传输
    StartTransfersFromQueue();
    
    // 返回是否还有活动的下载
    return (still_alive || msgs_left > 0 || !mQueue.empty());
}
