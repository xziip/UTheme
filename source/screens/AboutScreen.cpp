#include "AboutScreen.hpp"
#include "Gfx.hpp"
#include "common.h"
#include "../utils/LanguageManager.hpp"
#include <cmath>

AboutScreen::AboutScreen() {
    // 初始化动画
    mFadeInAnim.SetImmediate(0.0f);
    mFadeInAnim.SetTarget(1.0f, 600);
    
    mTitleAnim.Start(0, 1, 800);
}

AboutScreen::~AboutScreen() = default;

void AboutScreen::Draw() {
    mFrameCount++;
    mFadeInAnim.Update();
    mTitleAnim.Update();
    
    float fadeAlpha = mFadeInAnim.GetValue();
    
    // Draw gradient background
    Gfx::DrawGradientV(0, 0, Gfx::SCREEN_WIDTH, Gfx::SCREEN_HEIGHT, 
                       Gfx::COLOR_BACKGROUND, Gfx::COLOR_ALT_BACKGROUND);
    
    // 使用基类的动画顶部栏 (使用info图标)
    DrawAnimatedTopBar(_("about.title"), mTitleAnim, 0xf05a);

    // 使用现代卡片布局
    const int topBarHeight = 120;
    const int cardSpacing = 30;
    const int startY = topBarHeight + 50;
    
    // 左侧大卡片 - 制作人员和字体
    int leftCardX = 100;
    int leftCardY = startY;
    int leftCardW = 850;
    int leftCardH = 600;
    
    // 应用淡入透明度
    Gfx::SetGlobalAlpha(fadeAlpha);
    
    // 绘制阴影
    SDL_Color shadowColor = Gfx::COLOR_SHADOW;
    shadowColor.a = (Uint8)(80 * fadeAlpha);
    Gfx::DrawRectRounded(leftCardX + 8, leftCardY + 8, leftCardW, leftCardH, 24, shadowColor);
    
    // 绘制卡片
    Gfx::DrawRectRounded(leftCardX, leftCardY, leftCardW, leftCardH, 24, Gfx::COLOR_CARD_BG);
    
    // 卡片内容 - 制作人员
    int yOff = leftCardY + 40;
    Gfx::DrawIcon(leftCardX + 40, yOff, 48, Gfx::COLOR_ACCENT, 0xf007, Gfx::ALIGN_VERTICAL);
    Gfx::Print(leftCardX + 110, yOff, 44, Gfx::COLOR_TEXT, _("about.credits"), Gfx::ALIGN_VERTICAL);
    
    yOff += 80;
    Gfx::Print(leftCardX + 60, yOff, 38, Gfx::COLOR_WHITE, "UTheme", Gfx::ALIGN_VERTICAL);
    
    yOff += 60;
    SDL_Color noteColor = Gfx::COLOR_ALT_TEXT;
    Gfx::Print(leftCardX + 60, yOff, 28, noteColor, _("about.based_on"), Gfx::ALIGN_VERTICAL);
    yOff += 40;
    Gfx::Print(leftCardX + 80, yOff, 26, noteColor, "  WiiUCrashLogDumper by Maschell", Gfx::ALIGN_VERTICAL);
    yOff += 35;
    Gfx::Print(leftCardX + 80, yOff, 26, noteColor, "  WiiUIdent by GaryOderNichts", Gfx::ALIGN_VERTICAL);
    yOff += 35;
    Gfx::Print(leftCardX + 80, yOff, 26, noteColor, "  Haxcopy by YveltalGriffin", Gfx::ALIGN_VERTICAL);
    
    // 字体信息
    yOff += 80;
    Gfx::DrawIcon(leftCardX + 40, yOff, 44, Gfx::COLOR_ICON, 0xf031, Gfx::ALIGN_VERTICAL);
    Gfx::Print(leftCardX + 110, yOff, 40, Gfx::COLOR_TEXT, _("about.fonts"), Gfx::ALIGN_VERTICAL);
    
    yOff += 60;
    Gfx::Print(leftCardX + 60, yOff, 28, noteColor, std::string("  ") + _("about.system_font"), Gfx::ALIGN_VERTICAL);
    yOff += 35;
    Gfx::Print(leftCardX + 60, yOff, 28, noteColor, std::string("  ") + _("about.fontawesome"), Gfx::ALIGN_VERTICAL);
    yOff += 35;
    Gfx::Print(leftCardX + 60, yOff, 28, noteColor, std::string("  ") + _("about.terminus"), Gfx::ALIGN_VERTICAL);
    
    // 右侧卡片 - 源代码
    int rightCardX = leftCardX + leftCardW + cardSpacing;
    int rightCardY = startY;
    int rightCardW = 850;
    int rightCardH = 300;
    
    // 绘制阴影
    Gfx::DrawRectRounded(rightCardX + 8, rightCardY + 8, rightCardW, rightCardH, 24, shadowColor);
    
    // 绘制卡片
    Gfx::DrawRectRounded(rightCardX, rightCardY, rightCardW, rightCardH, 24, Gfx::COLOR_CARD_BG);
    
    // 卡片内容
    yOff = rightCardY + 40;
    Gfx::DrawIcon(rightCardX + 40, yOff, 48, Gfx::COLOR_SUCCESS, 0xf121, Gfx::ALIGN_VERTICAL);
    Gfx::Print(rightCardX + 110, yOff, 44, Gfx::COLOR_TEXT, _("about.source_code"), Gfx::ALIGN_VERTICAL);
    
    yOff += 100;
    Gfx::DrawIcon(rightCardX + rightCardW/2, yOff, 64, Gfx::COLOR_ACCENT, 0xf08e, Gfx::ALIGN_CENTER);
    
    yOff += 90;
    Gfx::Print(rightCardX + rightCardW/2, yOff, 32, Gfx::COLOR_ICON, "github.com/xziip/utheme", Gfx::ALIGN_CENTER);
    
    // 特性卡片
    rightCardY += rightCardH + cardSpacing;
    rightCardH = 270;
    
    Gfx::DrawRectRounded(rightCardX + 8, rightCardY + 8, rightCardW, rightCardH, 24, shadowColor);
    Gfx::DrawRectRounded(rightCardX, rightCardY, rightCardW, rightCardH, 24, Gfx::COLOR_CARD_BG);
    
    yOff = rightCardY + 40;
    Gfx::DrawIcon(rightCardX + 40, yOff, 48, Gfx::COLOR_WARNING, 0xf0ad, Gfx::ALIGN_VERTICAL);
    Gfx::Print(rightCardX + 110, yOff, 44, Gfx::COLOR_TEXT, _("about.features"), Gfx::ALIGN_VERTICAL);
    
    yOff += 70;
    Gfx::DrawIcon(rightCardX + 60, yOff, 32, Gfx::COLOR_SUCCESS, 0xf00c, Gfx::ALIGN_VERTICAL);
    Gfx::Print(rightCardX + 110, yOff, 30, Gfx::COLOR_TEXT, _("about.multilang"), Gfx::ALIGN_VERTICAL);
    
    yOff += 50;
    Gfx::DrawIcon(rightCardX + 60, yOff, 32, Gfx::COLOR_SUCCESS, 0xf00c, Gfx::ALIGN_VERTICAL);
    Gfx::Print(rightCardX + 110, yOff, 30, Gfx::COLOR_TEXT, _("about.modern_ui"), Gfx::ALIGN_VERTICAL);
    
    yOff += 50;
    Gfx::DrawIcon(rightCardX + 60, yOff, 32, Gfx::COLOR_SUCCESS, 0xf00c, Gfx::ALIGN_VERTICAL);
    Gfx::Print(rightCardX + 110, yOff, 30, Gfx::COLOR_TEXT, _("about.smooth_anim"), Gfx::ALIGN_VERTICAL);
    
    // 恢复全局透明度
    Gfx::SetGlobalAlpha(1.0f);

    DrawBottomBar(nullptr, (std::string("\ue044 ") + _("input.exit")).c_str(), (std::string("\ue001 ") + _("input.back")).c_str());
}

bool AboutScreen::Update(Input &input) {
    return !(input.data.buttons_d & Input::BUTTON_B);
}
