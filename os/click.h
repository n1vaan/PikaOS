// Synthetic UI click sound — ~10ms decaying impulse at 8 kHz mono 16-bit.
// Played whenever an LVGL widget receives LV_EVENT_CLICKED.
// Swap for a real recorded click later if you want (same format as pikachu1.h etc.).

const int16_t click_pcm[] = {
    20000, -20000, 18000, -18000, 16000, -16000, 14000, -14000,
    12000, -12000, 10000, -10000,  8000,  -8000,  6500,  -6500,
     5000,  -5000,  4000,  -4000,  3000,  -3000,  2200,  -2200,
     1600,  -1600,  1100,  -1100,   700,   -700,   450,   -450,
      280,   -280,   170,   -170,   100,   -100,    50,    -50,
       20,    -20,    10,    -10,     5,     -5,     0,      0,
};
const unsigned int click_pcm_len = sizeof(click_pcm);
