#pragma once

#include <cmath>
#include <coreinit/time.h>

class Animation {
public:
    Animation() : mStartValue(0), mTargetValue(0), mCurrentValue(0), mDuration(0), mStartTime(0), mIsAnimating(false) {}

    void Start(float from, float to, float durationMs) {
        mStartValue = from;
        mTargetValue = to;
        mCurrentValue = from;
        mDuration = durationMs;
        mStartTime = OSTicksToMilliseconds(OSGetSystemTime());
        mIsAnimating = true;
    }

    void SetTarget(float target, float durationMs) {
        if (!mIsAnimating) {
            Start(mCurrentValue, target, durationMs);
        } else {
            mStartValue = mCurrentValue;
            mTargetValue = target;
            mDuration = durationMs;
            mStartTime = OSTicksToMilliseconds(OSGetSystemTime());
        }
    }

    void Update() {
        if (!mIsAnimating) return;

        uint64_t currentTime = OSTicksToMilliseconds(OSGetSystemTime());
        float elapsed = (float)(currentTime - mStartTime);

        if (elapsed >= mDuration) {
            mCurrentValue = mTargetValue;
            mIsAnimating = false;
        } else {
            float progress = elapsed / mDuration;
            // Ease out cubic
            progress = 1 - pow(1 - progress, 3);
            mCurrentValue = mStartValue + (mTargetValue - mStartValue) * progress;
        }
    }

    float GetValue() const { return mCurrentValue; }
    bool IsAnimating() const { return mIsAnimating; }
    float GetTarget() const { return mTargetValue; }
    void SetImmediate(float value) {
        mCurrentValue = value;
        mTargetValue = value;
        mIsAnimating = false;
    }

private:
    float mStartValue;
    float mTargetValue;
    float mCurrentValue;
    float mDuration;
    uint64_t mStartTime;
    bool mIsAnimating;
};

// Easing functions
namespace Easing {
    inline float EaseInOutCubic(float t) {
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    }

    inline float EaseOutCubic(float t) {
        return 1.0f - pow(1.0f - t, 3.0f);
    }

    inline float EaseInCubic(float t) {
        return t * t * t;
    }

    inline float EaseOutElastic(float t) {
        const float c4 = (2.0f * M_PI) / 3.0f;
        return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : pow(2.0f, -10.0f * t) * sin((t * 10.0f - 0.75f) * c4) + 1.0f;
    }

    inline float EaseOutBack(float t) {
        const float c1 = 1.70158f;
        const float c3 = c1 + 1.0f;
        return 1.0f + c3 * pow(t - 1.0f, 3.0f) + c1 * pow(t - 1.0f, 2.0f);
    }
}
