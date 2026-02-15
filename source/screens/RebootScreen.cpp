#include "RebootScreen.hpp"
#include "Gfx.hpp"
#include "../utils/LanguageManager.hpp"
#include <coreinit/launch.h>   // OSLaunchTitle, OS_TITLE_ID_REBOOT

RebootScreen::RebootScreen(bool isSoftReboot) 
    : mIsSoftReboot(isSoftReboot) {
    mTitleAnim.Start(0, 1, 500);
}

RebootScreen::~RebootScreen() = default;

void RebootScreen::Draw() {
    // Draw gradient background
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);
    
    DrawAnimatedTopBar(_("reboot.title"), mTitleAnim, 0xf021);

    // Draw card in center
    int cardW = 900;
    int cardH = 400;
    int cardX = (Gfx::SCREEN_WIDTH - cardW) / 2;
    int cardY = (Gfx::SCREEN_HEIGHT - cardH) / 2;
    
    // Draw shadow and card
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = 80;
    Gfx::DrawRectRounded(cardX + 6, cardY + 6, cardW, cardH, 20, shadowColor);
    Gfx::DrawRectRounded(cardX, cardY, cardW, cardH, 20, Gfx::COLOR_CARD_BG);

    // Draw icon
    Gfx::DrawIcon(cardX + cardW/2, cardY + 100, 80, Gfx::COLOR_WARNING, 0xf021, Gfx::ALIGN_CENTER);
    
    // Draw text
    Gfx::Print(cardX + cardW/2, cardY + 210, 52, Gfx::COLOR_TEXT, _("reboot.confirm"), Gfx::ALIGN_CENTER);
    
    // 构造带图标的提示文本
    std::string pressAHint = std::string("\ue000 ") + _("reboot.press_a");
    std::string pressBHint = std::string("\ue001 ") + _("reboot.press_b");
    Gfx::Print(cardX + cardW/2, cardY + 280, 40, Gfx::COLOR_ALT_TEXT, pressAHint.c_str(), Gfx::ALIGN_CENTER);
    Gfx::Print(cardX + cardW/2, cardY + 330, 40, Gfx::COLOR_ALT_TEXT, pressBHint.c_str(), Gfx::ALIGN_CENTER);

    // 构造底部提示字符串
    std::string bottomHint = std::string("\ue000 ") + _("reboot.confirm_button") + " / \ue001 " + _("common.cancel");
    DrawBottomBar(nullptr, (std::string("\ue044 ") + _("input.exit")).c_str(), bottomHint.c_str());
    
    // 绘制圆形返回按钮
    Screen::DrawBackButton();
}

bool RebootScreen::Update(Input &input) {
    // 检测返回按钮点击
    if (Screen::UpdateBackButton(input)) {
        return false;  // 返回上一级
    }
    
    if (input.data.buttons_d & Input::BUTTON_A) {
        // 重启系统
        OSLaunchTitlel(OS_TITLE_ID_REBOOT, 0);
        return true; // 保持在当前界面，等待重启
    }
    
    if (input.data.buttons_d & Input::BUTTON_B) {
        // 取消，返回菜单
        return false;
    }

    return true;
}
