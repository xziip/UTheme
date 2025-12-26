#pragma once
#include "Input.h"
class CombinedInput : public Input {
public:
    void combine(const Input &b) {
        data.buttons_h |= b.data.buttons_h;
        
        // Copy touch data from the first valid input
        if (b.data.touched && b.data.validPointer) {
            data.touched = b.data.touched;
            data.validPointer = b.data.validPointer;
            data.x = b.data.x;
            data.y = b.data.y;
            data.pointerAngle = b.data.pointerAngle;
        }
    }

    void process() {
        data.buttons_d |= (data.buttons_h & (~lastData.buttons_h));
        data.buttons_r |= (lastData.buttons_h & (~data.buttons_h));
        lastData.buttons_h = data.buttons_h;
    }

    void reset() {
        // Save current touch state to lastData BEFORE clearing
        lastData.touched = data.touched;
        lastData.validPointer = data.validPointer;
        lastData.x = data.x;
        lastData.y = data.y;
        
        // Clear current frame data
        data.buttons_h = 0;
        data.buttons_d = 0;
        data.buttons_r = 0;
        data.touched = false;
        data.validPointer = false;
        data.x = 0;
        data.y = 0;
    }
};