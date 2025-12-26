#pragma once

#include "Gfx.hpp"
#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include <memory>

class MainScreen : public Screen {
public:
    MainScreen() {
        mSpinnerAnim.SetImmediate(0.0f);
        mLoadingAnim.SetImmediate(0.0f);
        mLoadingAnim.SetTarget(1.0f, 800);
    }

    ~MainScreen() override;

    void Draw() override;

    bool Update(Input &input) override;
    
    // 静态方法检查Mocha是否可用
    static bool IsMochaAvailable() { return sMochaAvailable; }

protected:
    static void DrawStatus(std::string status, SDL_Color color = Gfx::COLOR_TEXT);
    void DrawLoadingSpinner(int x, int y, int size, float progress);

private:
    enum {
        STATE_INIT,
        STATE_INIT_MOCHA,
        STATE_INIT_FS,
        STATE_LOAD_MENU,
        STATE_IN_MENU,
    } mState           = STATE_INIT;
    bool mStateFailure = false;
    
    // 静态变量,全局标记Mocha是否可用
    static bool sMochaAvailable;

    std::unique_ptr<Screen> mMenuScreen;
    
    Animation mSpinnerAnim;
    Animation mLoadingAnim;
    uint64_t mFrameCount = 0;
    
    static bool mMochaAvailableGlobal;  // 全局标记
};
