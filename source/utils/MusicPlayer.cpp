#include "MusicPlayer.hpp"
#include "Config.hpp"
#include "FileLogger.hpp"
#include <SDL2/SDL.h>

MusicPlayer::MusicPlayer() 
    : mMusic(nullptr)
    , mVolume(64)  // 默认50%音量
    , mEnabled(true)
    , mInitialized(false)
    , mWasEnabled(true)
    , mCurrentFilePath("") {
}

MusicPlayer::~MusicPlayer() {
    Shutdown();
}

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

bool MusicPlayer::Init() {
    if (mInitialized) {
        return true;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Initializing SDL2_mixer...");
    
    // 初始化SDL音频子系统(如果还没初始化)
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            FileLogger::GetInstance().LogError("MusicPlayer: Failed to init SDL audio: %s", SDL_GetError());
            return false;
        }
    }
    
    // 初始化SDL2_mixer
    // 参数: 频率, 格式, 声道数, 块大小
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        FileLogger::GetInstance().LogError("MusicPlayer: Mix_OpenAudio failed: %s", Mix_GetError());
        return false;
    }
    
    // 分配混音通道数
    Mix_AllocateChannels(16);
    
    mInitialized = true;
    FileLogger::GetInstance().LogInfo("MusicPlayer: Initialized successfully");
    return true;
}

void MusicPlayer::Shutdown() {
    if (!mInitialized) {
        return;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Shutting down...");
    
    Stop();
    
    if (mMusic) {
        Mix_FreeMusic(mMusic);
        mMusic = nullptr;
    }
    
    Mix_CloseAudio();
    
    mInitialized = false;
    FileLogger::GetInstance().LogInfo("MusicPlayer: Shutdown complete");
}

bool MusicPlayer::LoadMusic(const std::string& filepath) {
    if (!mInitialized) {
        FileLogger::GetInstance().LogError("MusicPlayer: Not initialized");
        return false;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Loading music from %s", filepath.c_str());
    
    // 释放之前的音乐
    if (mMusic) {
        Mix_FreeMusic(mMusic);
        mMusic = nullptr;
    }
    
    // 加载新音乐
    mMusic = Mix_LoadMUS(filepath.c_str());
    if (!mMusic) {
        FileLogger::GetInstance().LogError("MusicPlayer: Failed to load music: %s", Mix_GetError());
        mCurrentFilePath = "";
        return false;
    }
    
    // 保存文件路径
    mCurrentFilePath = filepath;
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Music loaded successfully");
    return true;
}

void MusicPlayer::Play() {
    if (!mInitialized || !mMusic || !mEnabled) {
        return;
    }
    
    if (!IsPlaying()) {
        FileLogger::GetInstance().LogInfo("MusicPlayer: Starting music playback");
        Mix_PlayMusic(mMusic, -1);  // -1 = 循环播放
        Mix_VolumeMusic(mVolume);
    }
}

void MusicPlayer::Stop() {
    if (!mInitialized) {
        return;
    }
    
    if (IsPlaying()) {
        FileLogger::GetInstance().LogInfo("MusicPlayer: Stopping music");
        Mix_HaltMusic();
    }
}

void MusicPlayer::Pause() {
    if (!mInitialized || !IsPlaying()) {
        return;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Pausing music");
    Mix_PauseMusic();
}

void MusicPlayer::Resume() {
    if (!mInitialized || !IsPaused()) {
        return;
    }
    
    FileLogger::GetInstance().LogInfo("MusicPlayer: Resuming music");
    Mix_ResumeMusic();
}

void MusicPlayer::SetVolume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 128) volume = 128;
    
    mVolume = volume;
    
    if (mInitialized) {
        Mix_VolumeMusic(mVolume);
    }
}

void MusicPlayer::SetEnabled(bool enabled) {
    if (mEnabled != enabled) {
        FileLogger::GetInstance().LogInfo("MusicPlayer: %s", enabled ? "Enabled" : "Disabled");
        mEnabled = enabled;
        
        if (enabled) {
            Play();
        } else {
            Stop();
        }
    }
}

bool MusicPlayer::IsPlaying() const {
    if (!mInitialized) {
        return false;
    }
    return Mix_PlayingMusic() == 1 && !IsPaused();
}

bool MusicPlayer::IsPaused() const {
    if (!mInitialized) {
        return false;
    }
    return Mix_PausedMusic() == 1;
}

std::string MusicPlayer::GetCurrentTrackName() const {
    if (mCurrentFilePath.empty()) {
        return "No Music";
    }
    
    // 从路径中提取文件名
    size_t lastSlash = mCurrentFilePath.find_last_of("/\\");
    std::string filename;
    if (lastSlash != std::string::npos) {
        filename = mCurrentFilePath.substr(lastSlash + 1);
    } else {
        filename = mCurrentFilePath;
    }
    
    // 去掉扩展名
    size_t lastDot = filename.find_last_of('.');
    if (lastDot != std::string::npos) {
        filename = filename.substr(0, lastDot);
    }
    
    return filename;
}

void MusicPlayer::Update() {
    if (!mInitialized) {
        return;
    }
    
    // 检查配置是否改变
    bool configEnabled = Config::GetInstance().IsBgmEnabled();
    if (configEnabled != mWasEnabled) {
        mWasEnabled = configEnabled;
        SetEnabled(configEnabled);
    }
    
    // 如果启用但没有播放,则开始播放
    if (mEnabled && mMusic && !IsPlaying() && !IsPaused()) {
        Play();
    }
}
