#pragma once

#include "Screen.hpp"
#include "../utils/Animation.hpp"

class RebootScreen : public Screen {
public:
    RebootScreen();
    ~RebootScreen() override;

    void Draw() override;

    bool Update(Input &input) override;

private:
    Animation mTitleAnim;
};
