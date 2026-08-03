#include "mm77la_display_pwm.h"

void display_pwm_step(DisplayPwmState& st, uint16_t rowsel, uint16_t rowdata) {
    st.window_tick = false;

    for (int row = 0; row < 10; row++) {
        if (!((rowsel >> row) & 1)) continue;
        for (int col = 0; col < 11; col++) {
            if ((rowdata >> col) & 1) {
                st.cnt[row * 11 + col]++;
            }
        }
    }

    if (st.window_pos == kDisplayWindow - 1) {
        st.window_pos = 0;
        st.window_tick = true;
        for (int cell = 0; cell < kDisplayCells; cell++) {
            if (st.cnt[cell] >= kDisplayBrightMin) st.levels[cell] = 2;
            else if (st.cnt[cell] >= kDisplayDimMin) st.levels[cell] = 1;
            else st.levels[cell] = 0;
            st.cnt[cell] = 0;
        }
    } else {
        st.window_pos++;
    }
}
