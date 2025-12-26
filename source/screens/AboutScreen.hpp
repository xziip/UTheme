#pragma once

#include "Screen.hpp"
#include "../utils/Animation.hpp"
#include "../utils/LanguageManager.hpp"
#include <vector>

class AboutScreen : public Screen {
public:
    AboutScreen();

    ~AboutScreen() override;

    void Draw() override;

    bool Update(Input &input) override;

private:
    Animation mFadeInAnim;
    Animation mTitleAnim;
    uint64_t mFrameCount = 0;
};
