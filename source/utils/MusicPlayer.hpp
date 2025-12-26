#pragma once
#include <SDL2/SDL_mixer.h>
#include <string>

class MusicPlayer {
public:
    static MusicPlayer& GetInstance();
    
    // 初始化和清理
    bool Init();
    void Shutdown();
    
    // 播放控制
    bool LoadMusic(const std::string& filepath);
    void Play();
    void Stop();
    void Pause();
    void Resume();
    
    // 音量控制 (0-128)
    void SetVolume(int volume);
    int GetVolume() const { return mVolume; }
    
    // 启用/禁用控制
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return mEnabled; }
    
    // 状态查询
    bool IsPlaying() const;
    bool IsPaused() const;
    
    // 获取当前音乐名称(从文件路径提取)
    std::string GetCurrentTrackName() const;
    
    // 每帧更新 - 根据配置自动控制播放
    void Update();
    
private:
    MusicPlayer();
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;
    
    Mix_Music* mMusic;
    int mVolume;
    bool mEnabled;
    bool mInitialized;
    bool mWasEnabled;  // 用于检测配置变化
    std::string mCurrentFilePath;  // 当前加载的音乐文件路径
};
