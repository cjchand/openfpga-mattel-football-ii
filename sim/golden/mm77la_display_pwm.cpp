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
            // alpha = 1/2, matching MAME pwm_display_device's default
            // interpolation factor (src/devices/video/pwm.cpp:63,
            // set_interpolation(0.5); its per-frame update is
            // bri = bri*(1-f) + duty*f). The +1 rounding offset (divisor-1)
            // makes smooth == cnt an exact fixed point, so a steady cell
            // settles rather than creeping.
            st.smooth[cell] = (uint16_t)((st.smooth[cell] * 1 + st.cnt[cell] + 1) / 2);
            if (st.smooth[cell] >= kDisplayBrightMin) st.levels[cell] = 2;
            else if (st.smooth[cell] >= kDisplayDimMin) st.levels[cell] = 1;
            else st.levels[cell] = 0;
            st.cnt[cell] = 0;
        }
    } else {
        st.window_pos++;
    }
}
