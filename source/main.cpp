#include "Gfx.hpp"
#include "input/CombinedInput.h"
#include "input/VPADInput.h"
#include "input/WPADInput.h"
#include "screens/MainScreen.hpp"
#include "utils/logger.h"
#include "utils/LanguageManager.hpp"
#include "utils/FileLogger.hpp"
#include "utils/ImageLoader.hpp"
#include "utils/Config.hpp"
#include "utils/MusicPlayer.hpp"
#include "utils/BgmDownloader.hpp"
#include "utils/PluginDownloader.hpp"
#include <coreinit/title.h>
#include <memory>
#include <padscore/kpad.h>
#include <sndcore2/core.h>
#include <sysapp/launch.h>
#include <whb/proc.h>
#include <sys/stat.h>

inline bool RunningFromMiiMaker() {
    return (OSGetTitleID() & 0xFFFFFFFFFFFFF0FFull) == 0x000500101004A000ull;
}

int main(int argc, char const *argv[]) {
    initLogging();
    WHBProcInit();

    // Initialize audio system for SDL2_mixer
    AXInit();

    KPADInit();
    WPADEnableURCC(TRUE);

    // Initialize graphics
    Gfx::Init();
    
    // Initialize image loader
    ImageLoader::Init();
    
    // Load configuration
    Config::GetInstance().Load();
    
    // Initialize file logger with config settings
    FileLogger::GetInstance().SetEnabled(Config::GetInstance().IsLoggingEnabled());
    FileLogger::GetInstance().SetVerbose(Config::GetInstance().IsVerboseLogging());
    if (Config::GetInstance().IsLoggingEnabled()) {
        FileLogger::GetInstance().StartLog();
        FileLogger::GetInstance().LogInfo("UTheme started");
        FileLogger::GetInstance().LogInfo("Running from: %s", RunningFromMiiMaker() ? "MiiMaker" : "Homebrew Launcher");
    }
    
    // Initialize language system (will automatically load language from config)
    Lang().Initialize();
    
    // Initialize music player
    MusicPlayer::GetInstance().Init();
    
    // 检查BGM文件是否存在
    const char* bgmPath = "fs:/vol/external01/UTheme/BGM.mp3";
    struct stat st;
    bool bgmExists = (stat(bgmPath, &st) == 0);
    
    // 如果BGM文件不存在,自动下载
    if (!bgmExists) {
        FileLogger::GetInstance().LogInfo("BGM file not found, starting automatic download...");
        std::string bgmUrl = Config::GetInstance().GetBgmUrl();
        
        if (!bgmUrl.empty()) {
            FileLogger::GetInstance().LogInfo("Downloading BGM from: %s", bgmUrl.c_str());
            
            // 设置下载完成回调
            BgmDownloader::GetInstance().SetCompletionCallback([](bool success, const std::string& error) {
                if (success) {
                    FileLogger::GetInstance().LogInfo("BGM downloaded successfully");
                } else {
                    FileLogger::GetInstance().LogError("BGM download failed: %s", error.c_str());
                }
            });
            
            // 开始后台下载
            BgmDownloader::GetInstance().StartDownload(bgmUrl);
        } else {
            FileLogger::GetInstance().LogInfo("No BGM URL configured, skipping download");
        }
    } else {
        FileLogger::GetInstance().LogInfo("BGM file exists, loading...");
    }
    
    // 尝试加载BGM (可能已存在,或正在下载中)
    const char* bgmPaths[] = {
        "fs:/vol/external01/UTheme/BGM.mp3",
        "fs:/vol/external01/UTheme/BGM.ogg"
    };
    
    bool bgmLoaded = false;
    for (const char* path : bgmPaths) {
        if (MusicPlayer::GetInstance().LoadMusic(path)) {
            FileLogger::GetInstance().LogInfo("Background music loaded from: %s", path);
            bgmLoaded = true;
            
            // 显示音乐加载通知
            std::string musicName = MusicPlayer::GetInstance().GetCurrentTrackName();
            Screen::GetBgmNotification().ShowNowPlaying(musicName);
            
            break;
        }
    }
    
    if (bgmLoaded) {
        MusicPlayer::GetInstance().SetEnabled(Config::GetInstance().IsBgmEnabled());
        MusicPlayer::GetInstance().SetVolume(64);  // 50% volume
    } else {
        if (bgmExists) {
            FileLogger::GetInstance().LogError("Failed to load existing BGM file");
        } else {
            FileLogger::GetInstance().LogInfo("BGM downloading in background, will be available after completion");
        }
    }
    
    // 检查并下载 StyleMiiU 插件
    FileLogger::GetInstance().LogInfo("Checking for StyleMiiU plugin...");
    PluginDownloader::GetInstance().CheckAndDownloadStyleMiiU();

    std::unique_ptr<Screen> mainScreen = std::make_unique<MainScreen>();

    CombinedInput baseInput;
    VPadInput vpadInput;
    WPADInput wpadInputs[4] = {
            WPAD_CHAN_0,
            WPAD_CHAN_1,
            WPAD_CHAN_2,
            WPAD_CHAN_3};

    // 主循环 - 使用 try-catch 捕获异常
    bool shouldQuit = false;
    try {
        while (WHBProcIsRunning()) {
            baseInput.reset();
            if (vpadInput.update(1280, 720)) {
                baseInput.combine(vpadInput);
            }
            for (auto &wpadInput : wpadInputs) {
                if (wpadInput.update(1280, 720)) {
                    baseInput.combine(wpadInput);
                }
            }
            baseInput.process();

            if (!mainScreen->Update(baseInput)) {
                // screen requested quit
                shouldQuit = true;
                break;
            }
            
            // Update BGM downloader
            BgmDownloader::GetInstance().Update();
            
            // Update music player
            MusicPlayer::GetInstance().Update();
            
            // Update BGM notification
            Screen::UpdateBgmNotification();

            mainScreen->Draw();
            
            // Draw BGM notification on top
            Screen::DrawBgmNotification();
            
            Gfx::Render();
        }
    } catch (const std::exception& e) {
        FileLogger::GetInstance().LogError("Fatal exception in main loop: %s", e.what());
        FileLogger::GetInstance().LogError("Application will now exit");
        shouldQuit = true;
    } catch (...) {
        FileLogger::GetInstance().LogError("Unknown fatal exception in main loop");
        FileLogger::GetInstance().LogError("Application will now exit");
        shouldQuit = true;
    }

    // 清理
    FileLogger::GetInstance().LogInfo("Cleaning up resources...");
    mainScreen.reset();
    
    // Cleanup music player
    MusicPlayer::GetInstance().Shutdown();
    
    // Cleanup
    FileLogger::GetInstance().LogInfo("UTheme shutting down");
    FileLogger::GetInstance().EndLog();
    
    ImageLoader::Cleanup();
    Gfx::Shutdown();
    
    // Cleanup audio system
    AXQuit();

    WHBProcShutdown();
    deinitLogging();
    
    // 如果需要退出,决定退出方式
    if (shouldQuit) {
        if (RunningFromMiiMaker()) {
            // legacy way, just quit
            return 0;
        } else {
            // launch menu otherwise
            SYSLaunchMenu();
        }
    }
    
    return 0;
}
