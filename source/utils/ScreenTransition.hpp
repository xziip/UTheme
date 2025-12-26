#pragma once

#include "Animation.hpp"
#include <memory>

class Screen;

class ScreenTransition {
public:
    enum Type {
        NONE,
        SLIDE_LEFT,   // New screen slides in from right, push old to left
        SLIDE_RIGHT   // New screen slides in from left, push old to right (for back)
    };

    ScreenTransition() 
        : mType(NONE)
        , mActive(false)
        , mOldScreen(nullptr)
        , mNewScreen(nullptr) {
        mAnimation.SetImmediate(0.0f);
    }

    // Start transition
    void Start(Type type, Screen* oldScreen, Screen* newScreen) {
        mType = type;
        mOldScreen = oldScreen;
        mNewScreen = newScreen;
        mActive = true;
        mAnimation.SetImmediate(0.0f);
        mAnimation.SetTarget(1.0f, 250);  // 250ms duration - optimized for smoother performance
    }

    // Update animation
    void Update() {
        if (mActive) {
            mAnimation.Update();
            if (mAnimation.GetValue() >= 0.99f) {
                mActive = false;
                mOldScreen = nullptr;
            }
        }
    }

    // Get current progress (0.0 to 1.0)
    float GetProgress() const {
        return mAnimation.GetValue();
    }

    bool IsActive() const {
        return mActive;
    }

    Type GetType() const {
        return mType;
    }

    Screen* GetOldScreen() const {
        return mOldScreen;
    }

    Screen* GetNewScreen() const {
        return mNewScreen;
    }

private:
    Type mType;
    bool mActive;
    Animation mAnimation;
    Screen* mOldScreen;
    Screen* mNewScreen;
};
