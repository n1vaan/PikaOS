#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "TouchDrvCSTXXX.hpp"  // Official Waveshare Touch Library
#include "SensorQMI8658.hpp"  // 6-axis IMU for shake detection
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include "time.h"
#include "src/ui/ui.h"

#include "ESP_I2S.h"
#include "esp_check.h"
#include "es8311.h"
#include "canon.h"
#include "pikachu.h"
#include "click.h"

es8311_handle_t es_handle = NULL; // This is the actual global variable
I2SClass i2s;
#define EXAMPLE_SAMPLE_RATE 16000
#define EXAMPLE_VOICE_VOLUME 80   // codec max we'll ever set; slider 100 -> this value

volatile bool play_alarm = false; // Set to true to start, false to stop. Off at boot — fires when timer hits 0.
volatile bool play_chime = false;     // One-shot pikachu chime; cleared automatically after playing.
volatile int chime_clip_idx = 0;      // which pikachu_clips[] entry to play
bool chimes_enabled = false;          // toggled by ui_Switch1 ("Chimes")
volatile bool play_click = false;     // One-shot UI click; cleared automatically.

/* ===== WiFi Settings ===== */
const char* ssid = "J&J FutureNet-WiFi";
const char* password = "Weare1family#";

/* ===== LVGL Settings ===== */
#define LVGL_TICK_PERIOD_MS 2
static lv_disp_draw_buf_t draw_buf;

// CHANGE THIS: Allocate the buffer in PSRAM
static lv_color_t *buf;
static lv_indev_drv_t indev_drv;

/* ===== Touch Driver (CST9217) ===== */
TouchDrvCST92xx touch;
int16_t tx[5], ty[5];
volatile bool isPressed = false;

/* ===== Display Driver ===== */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

/* ===== Clock Object Pointers ===== */
// Regular Analog
lv_obj_t *analogHourLine = NULL;
static lv_point_t analogHourPts[2];

// PokeBall Analog
lv_obj_t *pokeHourLine = NULL;
lv_obj_t *pokeMinuteLine = NULL;
lv_obj_t *pokeSecondLine = NULL;
static lv_point_t pokeHourPts[2], pokeMinutePts[2], pokeSecondPts[2];
lv_obj_t *pokeTickLines[12];
static lv_point_t pokeTickPts[12][2];

int timer_seconds = 1500; // Default 25:00 (25 * 60)
bool timer_running = false;
unsigned long last_timer_update = 0;

/* ===== StopWatch State ===== */
unsigned long stopwatch_accum = 0;       // accumulated ms while paused
unsigned long stopwatch_start_ms = 0;    // millis() when last started
bool stopwatch_running = false;

/* ===== Theme / Auto-dim State ===== */
bool is_dark = true;            // matches the previous static initial value
bool auto_dim_enabled = false;  // toggled by ui_Switch2 ("Dim")
// Dark mode active hours: [DARK_START_HOUR, DARK_END_HOUR) -- 8 PM to 6 AM
#define DARK_START_HOUR 20
#define DARK_END_HOUR    6

/* ===== NVS persistence ===== */
Preferences prefs;

/* ===== Idle dimming ===== */
unsigned long last_touch_ms = 0;
unsigned long last_fade_step_ms = 0;
int user_brightness_raw = 100;   // 0..100 (slider value); user's chosen "awake" brightness
int current_brightness_raw = 100;// what the display is actually showing right now
bool is_dimmed = false;
#define IDLE_DIM_MS 30000UL      // start fading after 30s with no touch
#define DIM_BRIGHTNESS_RAW 30    // dim target (~30%)
#define DIM_FADE_INTERVAL_MS 30  // ms between brightness steps -> ~2.1s full fade

/* ===== Pending volume (audio_task applies post-codec-init) ===== */
volatile int pending_codec_vol = -1;  // -1 = none; otherwise 0..80 (codec scale)

/* ===== Global press-feedback style ===== */
static lv_style_t style_pressed;

/* ===== Shake detection (QMI8658 accelerometer) ===== */
SensorQMI8658 qmi;
bool qmi_ready = false;
unsigned long last_shake_ms = 0;
// SensorLib's QMI8658.getAccelerometer actually returns values in g (gravity ≈ 1.0 at rest).
// We measure deviation from that baseline to detect motion.
#define GRAVITY_G            1.0f
#define SHAKE_DELTA_G        0.5f    // ~0.5g of motion above 1g rest — clear deliberate shake
#define SHAKE_DEBOUNCE_MS    2000UL

// Pull the PCM byte length out of a WAV's "data" sub-chunk header (offset 40, little-endian u32).
// Trims off both the 44-byte RIFF/fmt header and any trailing metadata chunks (LIST/INFO/ID3).
static inline unsigned int wav_pcm_size(const unsigned char *wav, unsigned int total_len) {
    if (total_len < 44) return 0;
    unsigned int size = (unsigned int)wav[40]
                      | ((unsigned int)wav[41] << 8)
                      | ((unsigned int)wav[42] << 16)
                      | ((unsigned int)wav[43] << 24);
    if (size > total_len - 44) size = total_len - 44;
    return size;
}

/* ===== Press feedback ===== */
// Walk a screen and attach the global pressed style only to lv_btn descendants.
// Limiting to buttons keeps the style well-scoped and skips containers/labels that
// don't visually benefit from a scale-on-press.
void apply_press_feedback_recursive(lv_obj_t *root) {
    if (!root) return;
    if (lv_obj_check_type(root, &lv_btn_class)) {
        lv_obj_add_style(root, &style_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    }
    uint32_t cnt = lv_obj_get_child_cnt(root);
    for (uint32_t i = 0; i < cnt; i++) {
        apply_press_feedback_recursive(lv_obj_get_child(root, i));
    }
}

void updateAllLogic(const struct tm& ti);  // forward decl; defined below

/* ===== Boot splash + random face =====
 * Holds a "PikaOS / Connecting..." screen until WiFi connects AND NTP gives us
 * a real local time. Falls through after MAX_BOOT_WAIT_MS so a bad/missing
 * network doesn't brick the boot — clocks will just show their default text
 * until time syncs later.
 */
#define MIN_SPLASH_MS       800UL    // always show splash at least this long
#define MAX_BOOT_WAIT_MS    15000UL  // give up waiting for WiFi+NTP after this

void show_splash_and_load_random_face() {
    // Pick a random face up front.
    lv_obj_t *face_list[] = {
        ui_PokeBallAnalog, ui_Screen1, ui_Analog, ui_Photo, ui_Photo2, ui_Pet
    };
    const int n_faces = sizeof(face_list) / sizeof(face_list[0]);
    int pick = random(n_faces);
    lv_obj_t *target_face = face_list[pick];
    if (!target_face) target_face = ui_PokeBallAnalog;

    // Build splash: yellow bg, "PikaOS" title, status label underneath.
    lv_obj_t *splash = lv_obj_create(NULL);
    lv_obj_clear_flag(splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(splash, lv_color_hex(0xFFB900), 0);
    lv_obj_set_style_bg_opa(splash, 255, 0);

    lv_obj_t *status = lv_label_create(splash);
    lv_label_set_text(status, "Connecting...");
    lv_obj_set_style_text_color(status, lv_color_hex(0x000000), 0);
    lv_obj_center(status);

    lv_scr_load(splash);

    // Wait for WiFi connection + NTP sync (or timeout). Pump LVGL the whole time.
    unsigned long start = millis();
    bool announced_wifi = false;
    bool ready = false;
    struct tm ti;

    while (millis() - start < MAX_BOOT_WAIT_MS) {
        lv_timer_handler();
        delay(50);

        if (WiFi.status() == WL_CONNECTED) {
            if (!announced_wifi) {
                lv_label_set_text(status, "Syncing time...");
                announced_wifi = true;
            }
            if (getLocalTime(&ti, 100)) {  // 100ms internal poll
                // Require a sane post-2024 year to confirm NTP actually replied
                if (ti.tm_year + 1900 >= 2024) {
                    lv_label_set_text(status, "Ready");
                    ready = true;
                    break;
                }
            }
        }
    }

    if (!ready) {
        lv_label_set_text(status, "Offline mode");
    }

    // Always show the final splash state for at least MIN_SPLASH_MS — feels less abrupt.
    unsigned long min_end = start + MIN_SPLASH_MS;
    while (millis() < min_end) {
        lv_timer_handler();
        delay(5);
    }
    // Brief extra moment to read the final status line.
    unsigned long linger = millis();
    while (millis() - linger < 400) {
        lv_timer_handler();
        delay(5);
    }

    lv_scr_load(target_face);
    lv_obj_del_async(splash);

    // Paint the right time/date immediately on the new face so it doesn't
    // briefly show the SquareLine default ("00:25 PM", "No Timer", etc.)
    if (ready) {
        lv_timer_handler();           // process the screen swap first
        updateAllLogic(ti);           // fill the freshly-active face
    }
}

/* ===== Pet Screen ===== */
void pet_screen_clicked(lv_event_t * e) {
    if (PIKACHU_CLIP_COUNT == 0) return;
    chime_clip_idx = random(PIKACHU_CLIP_COUNT);
    play_chime = true;
}

void update_stopwatch_label() {
    unsigned long elapsed = stopwatch_accum;
    if (stopwatch_running) elapsed += millis() - stopwatch_start_ms;
    int hours = elapsed / 3600000UL;
    int minutes = (elapsed / 60000UL) % 60;
    int seconds = (elapsed / 1000UL) % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);
    if (ui_Time) lv_label_set_text(ui_Time, buf);
}

void update_pauseplay_icon() {
    if (ui_Label42) lv_label_set_text(ui_Label42, stopwatch_running ? "g" : "h");
}

void handle_stopwatch_events(lv_event_t * e) {
    lv_obj_t * target = lv_event_get_target(e);
    if (target == ui_PausePlay) {
        if (stopwatch_running) {
            stopwatch_accum += millis() - stopwatch_start_ms;
            stopwatch_running = false;
        } else {
            stopwatch_start_ms = millis();
            stopwatch_running = true;
        }
        update_stopwatch_label();
        update_pauseplay_icon();
    } else if (target == ui_Reset) {
        stopwatch_running = false;
        stopwatch_accum = 0;
        update_stopwatch_label();
        update_pauseplay_icon();
    }
}

// Helper to format the label (MM:SS)
void update_timer_label() {
    int minutes = timer_seconds / 60;
    int seconds = timer_seconds % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    if (ui_Label14) lv_label_set_text(ui_Label14, buf);
}
/* ===== Touch Callbacks ===== */
// LVGL input feedback — fires for every event from the touch device.
// We arm play_click on LV_EVENT_CLICKED so the audio task plays the click pcm.
void my_indev_feedback(lv_indev_drv_t *drv, uint8_t event_code) {
    if (event_code == LV_EVENT_CLICKED) {
        lv_obj_t * obj = lv_indev_get_obj_act();
        // Only chirp on clickable widgets (avoid clicks on bare screen backgrounds).
        if (obj && lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
            play_click = true;
        }
    }
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint8_t touched = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
  if (touched > 0) {
    // Wake from idle-dim immediately on any touch (instant ramp-up; gradual ramp-down)
    last_touch_ms = millis();
    if (current_brightness_raw != user_brightness_raw) {
        current_brightness_raw = user_brightness_raw;
        gfx->setBrightness(map(current_brightness_raw, 0, 100, 0, 255));
    }
    is_dimmed = false;
    data->state = LV_INDEV_STATE_PR;
    data->point.x = tx[0];
    data->point.y = ty[0];
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  lv_disp_flush_ready(disp);
}

void lv_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

/* ===== Clock Logic - Regular Analog ===== */
void setupRegularAnalog() {
  if (ui_Image3) {
    lv_img_set_pivot(ui_Image3, 120, 120);
    lv_img_set_zoom(ui_Image3, 256);
    lv_obj_clear_flag(ui_Image3, LV_OBJ_FLAG_CLICKABLE);
  }
  analogHourLine = lv_line_create(ui_Analog);
  lv_obj_set_style_line_width(analogHourLine, 14, 0);
  lv_obj_set_style_line_color(analogHourLine, lv_color_hex(0xD41800), 0);
  lv_obj_set_style_line_rounded(analogHourLine, true, 0);
  lv_obj_clear_flag(analogHourLine, LV_OBJ_FLAG_CLICKABLE);
}

/* ===== Clock Logic - PokeBall Analog ===== */
void setupPokeBall() {
  // Hide generated UI elements to draw custom ones  
  int cx = LCD_WIDTH / 2, cy = LCD_HEIGHT / 2;
  for (int i = 0; i < 12; i++) {
    float rad = (i * 30.0 - 90.0) * DEG_TO_RAD;
    pokeTickPts[i][0] = {(lv_coord_t)(cx + cos(rad) * 190), (lv_coord_t)(cy + sin(rad) * 190)};
    pokeTickPts[i][1] = {(lv_coord_t)(cx + cos(rad) * 220), (lv_coord_t)(cy + sin(rad) * 220)};
    pokeTickLines[i] = lv_line_create(ui_PokeBallAnalog);
    lv_line_set_points(pokeTickLines[i], pokeTickPts[i], 2);
    lv_obj_set_style_line_width(pokeTickLines[i], 14, 0);
    lv_obj_set_style_line_color(pokeTickLines[i], lv_color_hex(0x303030), 0);
    lv_obj_set_style_line_rounded(pokeTickLines[i], true, 0);
  }

  pokeHourLine = lv_line_create(ui_PokeBallAnalog);
  lv_obj_set_style_line_width(pokeHourLine, 14, 0);
  lv_obj_set_style_line_color(pokeHourLine, lv_color_hex(0xFFCA00), 0);
  lv_obj_set_style_line_rounded(pokeHourLine, true, 0);

  pokeMinuteLine = lv_line_create(ui_PokeBallAnalog);
  lv_obj_set_style_line_width(pokeMinuteLine, 14, 0);
  lv_obj_set_style_line_color(pokeMinuteLine, lv_color_hex(0xFFCA00), 0);
  lv_obj_set_style_line_rounded(pokeMinuteLine, true, 0);

  pokeSecondLine = lv_line_create(ui_PokeBallAnalog);
  lv_obj_set_style_line_width(pokeSecondLine, 6, 0);
  lv_obj_set_style_line_color(pokeSecondLine, lv_color_hex(0xFFCA00), 0);
  lv_obj_set_style_line_rounded(pokeSecondLine, true, 0);
}

/* ===== Universal Update Function ===== */
void updateAllLogic(const struct tm& ti) {
  lv_obj_t *scr = lv_scr_act();
  int cx = LCD_WIDTH / 2, cy = LCD_HEIGHT / 2;

  /* Existing Clock Logics */
  if (scr == ui_Analog) {
    int mAngle = ((ti.tm_min * 60) + 420) % 3600;
    float hRad = (((ti.tm_hour % 12) * 30.0) + (ti.tm_min * 0.5) - 90.0) * DEG_TO_RAD;
    if (ui_Image3) lv_img_set_angle(ui_Image3, mAngle);
    analogHourPts[0] = {cx, cy};
    analogHourPts[1] = {(lv_coord_t)(cx + cos(hRad) * 90), (lv_coord_t)(cy + sin(hRad) * 90)};
    lv_line_set_points(analogHourLine, analogHourPts, 2);
  } 
  else if (scr == ui_PokeBallAnalog) {
    float hR = (((ti.tm_hour % 12) * 30.0) + (ti.tm_min * 0.5) - 90.0) * DEG_TO_RAD;
    float mR = (ti.tm_min * 6.0 - 90.0) * DEG_TO_RAD;
    float sR = (ti.tm_sec * 6.0 - 90.0) * DEG_TO_RAD;
    pokeHourPts[0] = {cx, cy}; pokeHourPts[1] = {(lv_coord_t)(cx + cos(hR)*90), (lv_coord_t)(cy + sin(hR)*90)};
    pokeMinutePts[0] = {cx, cy}; pokeMinutePts[1] = {(lv_coord_t)(cx + cos(mR)*150), (lv_coord_t)(cy + sin(mR)*150)};
    pokeSecondPts[0] = {cx, cy}; pokeSecondPts[1] = {(lv_coord_t)(cx + cos(sR)*170), (lv_coord_t)(cy + sin(sR)*170)};
    lv_line_set_points(pokeHourLine, pokeHourPts, 2);
    lv_line_set_points(pokeMinuteLine, pokeMinutePts, 2);
    lv_line_set_points(pokeSecondLine, pokeSecondPts, 2);
  }
  else if (scr == ui_Screen1) {
    char tB[16], dB[40];
    int h12 = ti.tm_hour % 12; if (h12 == 0) h12 = 12;
    snprintf(tB, sizeof(tB), "%d:%02d", h12, ti.tm_min);
    strftime(dB, sizeof(dB), "%A, %B %d, %Y", &ti);
    if (ui_Label1) lv_label_set_text(ui_Label1, tB);
    if (ui_Label2) lv_label_set_text(ui_Label2, dB);
  }

  /* --- NEW PHOTO SCREEN LOGIC --- */
  else if (scr == ui_Photo || scr == ui_Photo2) {
    char tB[16], dB[40];
    int h12 = ti.tm_hour % 12; if (h12 == 0) h12 = 12;
    snprintf(tB, sizeof(tB), "%d:%02d", h12, ti.tm_min);
    
    // Update Photo 1 (Allyellow)
    if (ui_Label25) lv_label_set_text(ui_Label25, tB);
    
    // Update Photo 2 (Pokeoutfits)
    if (ui_Label27) lv_label_set_text(ui_Label27, tB);
    
    // Update the Date on Photo 2
    strftime(dB, sizeof(dB), "%A, %B %d, %Y", &ti);
    if (ui_Label21) lv_label_set_text(ui_Label21, dB);
  }
  else if (scr == ui_StopWatch) {
    update_stopwatch_label();
  }
}
void handle_timer_buttons(lv_event_t * e) {
    lv_obj_t * target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    // Triggers on single click OR repeatedly while holding
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        if (target == ui_Button5)      timer_seconds += 60; // +1 Min
        else if (target == ui_Button1) timer_seconds -= 60; // -1 Min
        else if (target == ui_Button2) timer_seconds += 1;  // +1 Sec
        else if (target == ui_Button3) timer_seconds -= 1;  // -1 Sec

        // Bounds checking
        if (timer_seconds < 0) timer_seconds = 0;
        if (timer_seconds > 5999) timer_seconds = 5999; // Max 99:59

        update_timer_label();
    }
}

// Add a global pointer for the codec handle if you haven't yet
extern es8311_handle_t es_handle; 

void handle_settings_events(lv_event_t * e) {
    lv_obj_t * target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    // --- 1. Sliders (Brightness & Volume) ---
    if(code == LV_EVENT_VALUE_CHANGED) {
        if (target == ui_Slider1) {
            int val = lv_slider_get_value(ui_Slider1);
            user_brightness_raw = val;
            current_brightness_raw = val;
            gfx->setBrightness(map(val, 0, 100, 0, 255));
            is_dimmed = false;
            last_touch_ms = millis();
            lv_label_set_text_fmt(ui_Label30, "%d", val);
        }
        else if (target == ui_Slider2) {
            int val = lv_slider_get_value(ui_Slider2);
            int codec_vol = map(val, 0, 100, 0, 80);
            if(es_handle) es8311_voice_volume_set(es_handle, codec_vol, NULL);
            lv_label_set_text_fmt(ui_Label31, "%d", val);
        }
    }
    // Persist on release (slider drag fires VALUE_CHANGED constantly — don't hammer NVS)
    if (code == LV_EVENT_RELEASED) {
        if (target == ui_Slider1) prefs.putInt("bright", lv_slider_get_value(ui_Slider1));
        else if (target == ui_Slider2) prefs.putInt("vol", lv_slider_get_value(ui_Slider2));
    }

    // --- 2. Keyboard Control (The "Enter" and "X" keys) ---
    if (target == ui_Keyboard) {
        // LV_EVENT_READY is the Checkmark (Enter), LV_EVENT_CANCEL is the X
        if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
            lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
            // Unfocus the text area so the cursor stops blinking
            lv_obj_t * ta = lv_keyboard_get_textarea(ui_Keyboard);
            if(ta) lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        }
    }

    // --- 3. Button Clicks (WiFi Popup) ---
    if(code == LV_EVENT_CLICKED) {
        if (target == ui_WifiButton) {
            lv_obj_clear_flag(ui_WifiConnectPopup, LV_OBJ_FLAG_HIDDEN);
        }
        else if (target == ui_Cancel) {
            lv_obj_add_flag(ui_WifiConnectPopup, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN); // Close keyboard too
        }
        else if (target == ui_Connect) {
            const char * ssid_target = lv_textarea_get_text(ui_NetworkBox);
            const char * pass_target = lv_textarea_get_text(ui_PasswordBox);
            
            lv_label_set_text(ui_Label36, "Status: Connecting...");
            WiFi.begin(ssid_target, pass_target);
            
            // Hide keyboard automatically when starting connection
            lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // --- 4. Focus Handling ---
    if(code == LV_EVENT_FOCUSED) {
        if(target == ui_NetworkBox || target == ui_PasswordBox) {
            lv_keyboard_set_textarea(ui_Keyboard, target);
            lv_obj_clear_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (target == ui_DarkMode && code == LV_EVENT_CLICKED) {
        is_dark = !is_dark;
        apply_dark_mode(is_dark);
        lv_obj_set_style_img_recolor(ui_Label26, is_dark ? lv_color_hex(0x2095F6) : lv_color_hex(0xFFFFFF), 0);
        prefs.putBool("is_dark", is_dark);
    }
    if (target == ui_Switch2 && code == LV_EVENT_VALUE_CHANGED) {
        auto_dim_enabled = lv_obj_has_state(ui_Switch2, LV_STATE_CHECKED);
        prefs.putBool("autodim", auto_dim_enabled);
        Serial.printf("Auto-dim %s\n", auto_dim_enabled ? "ON" : "OFF");
    }
    if (target == ui_Switch1 && code == LV_EVENT_VALUE_CHANGED) {
        chimes_enabled = lv_obj_has_state(ui_Switch1, LV_STATE_CHECKED);
        prefs.putBool("chimes", chimes_enabled);
        Serial.printf("Chimes %s\n", chimes_enabled ? "ON" : "OFF");
    }
    // Inside handle_settings_events(lv_event_t * e)
    if(code == LV_EVENT_CLICKED) {
        // ... your existing WiFi buttons ...

        // 1. Show the Info Page
        if (target == ui_Info) {
            lv_obj_clear_flag(ui_Container1, LV_OBJ_FLAG_HIDDEN);
        }
        
        // 2. Hide the Info Page
        else if (target == ui_InfoExit) {
            lv_obj_add_flag(ui_Container1, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void setup_settings_controls() {
    // Hide popup and keyboard by default
    lv_obj_add_flag(ui_WifiConnectPopup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_Keyboard, LV_OBJ_FLAG_HIDDEN);

    // Attach Brightness & Volume Sliders
    lv_obj_add_event_cb(ui_Slider1, handle_settings_events, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_Slider2, handle_settings_events, LV_EVENT_VALUE_CHANGED, NULL);

    // Attach Buttons
    lv_obj_add_event_cb(ui_WifiButton, handle_settings_events, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Cancel, handle_settings_events, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Connect, handle_settings_events, LV_EVENT_CLICKED, NULL);

    // Attach TextBoxes for Keyboard
    lv_obj_add_event_cb(ui_NetworkBox, handle_settings_events, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ui_PasswordBox, handle_settings_events, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ui_Keyboard, handle_settings_events, LV_EVENT_ALL, NULL);
    // Slider positions are restored from NVS in setup() — don't overwrite them here.
    lv_obj_add_event_cb(ui_DarkMode, handle_settings_events, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_Switch1, handle_settings_events, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_Switch2, handle_settings_events, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui_Info, handle_settings_events, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_InfoExit, handle_settings_events, LV_EVENT_CLICKED, NULL);
}

void start_timer_cb(lv_event_t * e) {
    if (timer_seconds > 0) {
        timer_running = true;
        play_alarm = false; // Ensure sound is off
        last_timer_update = millis();
        lv_obj_add_flag(ui_SetTimerContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_ActiveTimerContainer, LV_OBJ_FLAG_HIDDEN);
    }
}

void end_timer_cb(lv_event_t * e) {
    timer_running = false;
    play_alarm = false; // <--- ADD THIS: Stops the sound
    lv_obj_clear_flag(ui_SetTimerContainer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_ActiveTimerContainer, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_Label23, "Pause"); 
}

void pause_timer_cb(lv_event_t * e) {
    timer_running = !timer_running;
    lv_label_set_text(ui_Label23, timer_running ? "Pause" : "Resume");
}

esp_err_t es8311_codec_init(void) {
  es_handle = es8311_create(0, ES8311_ADDRRES_0);
  ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, "ES8311", "create failed");

  const es8311_clock_config_t es_clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = EXAMPLE_SAMPLE_RATE * 256,
    .sample_frequency = EXAMPLE_SAMPLE_RATE
  };

  ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
  ESP_ERROR_CHECK(es8311_sample_frequency_config(es_handle, es_clk.mclk_frequency, es_clk.sample_frequency));
  ESP_ERROR_CHECK(es8311_microphone_config(es_handle, false));
  ESP_ERROR_CHECK(es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL));
  return ESP_OK;
}

void audio_task(void *param) {
  // 1. Set I2S Pins first
  i2s.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN);
  
  // 2. Start I2S
  if (!i2s.begin(I2S_MODE_STD, EXAMPLE_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_BOTH)) {
    Serial.println("I2S init failed!");
    vTaskDelete(NULL);
  }

  // 3. Initialize Codec (Using the ALREADY STARTED Wire bus from setup)
  // We do NOT call Wire.begin again here.
  if (es8311_codec_init() != ESP_OK) {
    Serial.println("ES8311 init failed!");
    vTaskDelete(NULL);
  }
  
  Serial.println("Audio Task: Codec and I2S Ready.");

  // Apply persisted volume now that the codec is alive
  if (pending_codec_vol >= 0 && es_handle) {
    es8311_voice_volume_set(es_handle, pending_codec_vol, NULL);
    pending_codec_vol = -1;
  }

  while (1) {
    if (play_click) {
      i2s.write((uint8_t *)click_pcm, click_pcm_len);
      play_click = false; // one-shot
    } else if (play_chime) {
      int idx = chime_clip_idx;
      if (idx < 0 || idx >= (int)PIKACHU_CLIP_COUNT) idx = 0;
      i2s.write((uint8_t *)pikachu_clips[idx] + 44, wav_pcm_size(pikachu_clips[idx], pikachu_clip_lens[idx]));
      play_chime = false; // one-shot
    } else if (play_alarm) {
      i2s.write((uint8_t *)canon_pcm + 44, wav_pcm_size(canon_pcm, canon_pcm_len));
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(1);
  }
}

void apply_dark_mode(bool active) {
    lv_color_t backgrounds = active ? lv_color_hex(0xFFB900) : lv_color_hex(0x000000);
    lv_color_t text_color = active ? lv_color_hex(0x000000) : lv_color_hex(0xFFB900);
    lv_color_t datesubtitle = active ? lv_color_hex(0x000000) : lv_color_hex(0xFFB900);

    if (ui_Screen1) lv_obj_set_style_bg_color(ui_Screen1, backgrounds, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_Analog) lv_obj_set_style_bg_color(ui_Analog, backgrounds, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_Timer) lv_obj_set_style_bg_color(ui_Timer, backgrounds, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (ui_StopWatch) lv_obj_set_style_bg_color(ui_StopWatch, backgrounds, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (ui_Label2) lv_obj_set_style_text_color(ui_Label2, datesubtitle, 0);


    lv_obj_t * labels[] = {
        ui_Label3, ui_Label4, ui_Label5, ui_Label6, ui_Label7, 
        ui_Label8, ui_Label9, ui_Label10, ui_Label11, ui_Label12, 
        ui_Label13, ui_Label15
    };

    for (int i = 0; i < 12; i++) {
        if (labels[i] == NULL) continue;

        // Get the text to check if it's "2" or "5"
        const char * text = lv_label_get_text(labels[i]);
        
        if (strcmp(text, "2") != 0 && strcmp(text, "5") != 0) {
            lv_obj_set_style_text_color(labels[i], text_color, 0);
        } 
    }


    Serial.printf("Dark Mode %s\n", active ? "Enabled" : "Disabled");
}






/* ===== SETUP ===== */
void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to stabilize
  randomSeed(esp_random()); // hardware RNG so random() varies across boots

  // Load persisted settings BEFORE display init so we can apply brightness immediately.
  prefs.begin("pika", false);
  user_brightness_raw = prefs.getInt("bright", 100);
  int saved_volume     = prefs.getInt("vol",    100);
  is_dark              = prefs.getBool("is_dark", true);
  auto_dim_enabled     = prefs.getBool("autodim", false);
  chimes_enabled       = prefs.getBool("chimes",  false);
  pending_codec_vol    = map(saved_volume, 0, 100, 0, 80); // applied once codec is ready
  buf = (lv_color_t *)heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  // Check if allocation worked
  if (buf == NULL) {
      Serial.println("PSRAM Allocation Failed! System will freeze.");
  }
  // 1. Display Hardware Init (Get the screen on early)

  Wire.begin(15, 14);
  pinMode(PA, OUTPUT); // Speaker amp enable (GPIO 46 — pin 10 is DOPIN!)
  digitalWrite(PA, HIGH);

  gfx->begin();
  current_brightness_raw = user_brightness_raw;
  gfx->setBrightness(map(current_brightness_raw, 0, 100, 0, 255));
  last_touch_ms = millis(); // start the idle-dim timer fresh

  // 3. LVGL Core Init
  lv_init();
  lv_img_cache_set_size(4); // Vital for AMOLED performance
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_WIDTH; 
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush; 
  disp_drv.draw_buf = &draw_buf;
  disp_drv.sw_rotate = 1;
  lv_disp_drv_register(&disp_drv);
  lv_disp_set_rotation(NULL, LV_DISP_ROT_90);

  // 4. Input (Touch) Init
  pinMode(TP_INT, INPUT_PULLUP);
  digitalWrite(TP_RESET, LOW); delay(30); digitalWrite(TP_RESET, HIGH); delay(50);
  touch.setPins(TP_RESET, TP_INT);
  touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  indev_drv.feedback_cb = my_indev_feedback;
  lv_indev_drv_register(&indev_drv);

  // 4b. IMU (QMI8658) for shake-to-pika
  qmi_ready = qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (qmi_ready) {
      qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                              SensorQMI8658::ACC_ODR_125Hz,
                              SensorQMI8658::LPF_MODE_0);
      qmi.enableAccelerometer();
      Serial.println("QMI8658 ready");
  } else {
      Serial.println("QMI8658 init failed (shake-to-pika disabled)");
  }

  // 6. SquareLine UI Start
  ui_init();

  // Disable scrolling for clocks
  lv_obj_clear_flag(ui_Analog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_PokeBallAnalog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_Timer, LV_OBJ_FLAG_SCROLLABLE);

  // 6a. Sync Settings UI to persisted state BEFORE the splash so it's right when user opens Settings
  lv_slider_set_value(ui_Slider1, user_brightness_raw, LV_ANIM_OFF);
  lv_slider_set_value(ui_Slider2, prefs.getInt("vol", 100), LV_ANIM_OFF);
  if (ui_Label30) lv_label_set_text_fmt(ui_Label30, "%d", user_brightness_raw);
  if (ui_Label31) lv_label_set_text_fmt(ui_Label31, "%d", prefs.getInt("vol", 100));
  if (chimes_enabled)   lv_obj_add_state(ui_Switch1, LV_STATE_CHECKED);
  if (auto_dim_enabled) lv_obj_add_state(ui_Switch2, LV_STATE_CHECKED);
  apply_dark_mode(is_dark);
  if (ui_Label26) lv_obj_set_style_img_recolor(ui_Label26, is_dark ? lv_color_hex(0x2095F6) : lv_color_hex(0xFFFFFF), 0);

  // (Press-feedback removed — buttons no longer shrink on press.)

  // 7. Custom Overlays & Events
  setupRegularAnalog();
  setupPokeBall();

// --- ADD THIS TO SETUP ---
  setup_settings_controls(); // Initialize flags and default values

  // Link Settings Sliders
  lv_obj_add_event_cb(ui_Slider1, handle_settings_events, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(ui_Slider2, handle_settings_events, LV_EVENT_VALUE_CHANGED, NULL);

  // Link WiFi Popup & Connect Buttons
  lv_obj_add_event_cb(ui_WifiButton, handle_settings_events, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Cancel, handle_settings_events, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Connect, handle_settings_events, LV_EVENT_CLICKED, NULL);

  // Link TextBoxes to Keyboard
  lv_obj_add_event_cb(ui_NetworkBox, handle_settings_events, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(ui_PasswordBox, handle_settings_events, LV_EVENT_FOCUSED, NULL);

  lv_obj_add_event_cb(ui_Button5, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button1, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button2, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button3, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button6, start_timer_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Button9, end_timer_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Button10, pause_timer_cb, LV_EVENT_CLICKED, NULL);
  update_timer_label();

  // Pet screen: tap anywhere plays a random pikachu clip
  lv_obj_add_event_cb(ui_Pet, pet_screen_clicked, LV_EVENT_CLICKED, NULL);

  // StopWatch wiring
  lv_obj_add_event_cb(ui_PausePlay, handle_stopwatch_events, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Reset,     handle_stopwatch_events, LV_EVENT_CLICKED, NULL);
  lv_obj_clear_flag(ui_StopWatch, LV_OBJ_FLAG_SCROLLABLE);
  update_stopwatch_label();
  update_pauseplay_icon();

  // 8. WiFi, Time & Audio
  WiFi.begin(ssid, password);
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();

  const esp_timer_create_args_t timer_args = { .callback = &lv_tick_cb, .name = "tick" };
  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
  xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 1, NULL, 1);

  // Persist slider end-positions to NVS (debounced — fires on release, not every drag step)
  lv_obj_add_event_cb(ui_Slider1, handle_settings_events, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(ui_Slider2, handle_settings_events, LV_EVENT_RELEASED, NULL);

  // Boot splash + transition to a random face
  show_splash_and_load_random_face();

  // Greet the user with a random pikachu sound. audio_task may still be
  // initializing the codec — once it's up, it'll see this flag and play.
  if (PIKACHU_CLIP_COUNT > 0) {
      chime_clip_idx = random(PIKACHU_CLIP_COUNT);
      play_chime = true;
  }
}

void loop() {
  static lv_obj_t * last_scr = NULL;
  lv_obj_t * active_scr = lv_scr_act();
  
  // If the screen just changed, kill the image cache immediately
  if (active_scr != last_scr) {
      lv_img_cache_invalidate_src(NULL); 
      last_scr = active_scr;
      Serial.println("Screen Changed: Cache Cleared");
  }

  lv_timer_handler();  
  
  static int lastSec = -1;
  struct tm ti;
  if (getLocalTime(&ti) && ti.tm_sec != lastSec) {
    lastSec = ti.tm_sec;
    lv_obj_invalidate(lv_scr_act()); // Add this: Forces a full screen refresh every cycle
    updateAllLogic(ti);

    // Hour chime: trigger random pikachu clip on hour rollover (skip the first observation at boot)
    {
        static int last_seen_hour = -1;
        if (last_seen_hour != ti.tm_hour) {
            if (chimes_enabled && last_seen_hour != -1 && PIKACHU_CLIP_COUNT > 0) {
                chime_clip_idx = random(PIKACHU_CLIP_COUNT);
                play_chime = true;
            }
            last_seen_hour = ti.tm_hour;
        }
    }

    // Auto-dim: when enabled, force dark theme during DARK_START_HOUR..DARK_END_HOUR.
    // Note: apply_dark_mode(true)  -> yellow bg (light)
    //       apply_dark_mode(false) -> black  bg (dark)
    // is_dark mirrors that arg, so is_dark==false means visually-dark.
    if (auto_dim_enabled) {
        bool in_night_window =
            (DARK_START_HOUR < DARK_END_HOUR)
              ? (ti.tm_hour >= DARK_START_HOUR && ti.tm_hour < DARK_END_HOUR)
              : (ti.tm_hour >= DARK_START_HOUR || ti.tm_hour < DARK_END_HOUR);
        bool target_light = !in_night_window;
        if (target_light != is_dark) {
            is_dark = target_light;
            apply_dark_mode(is_dark);
            if (ui_Label26) lv_obj_set_style_img_recolor(ui_Label26, is_dark ? lv_color_hex(0x2095F6) : lv_color_hex(0xFFFFFF), 0);
        }
    }
  }
  // Timer Countdown Logic
  if (timer_running && timer_seconds > 0) {
      if (millis() - last_timer_update >= 1000) {
          last_timer_update = millis();
          timer_seconds--;
          
          update_timer_label();
          
          // Trigger sound effect on finish
          if (timer_seconds == 0) {
              timer_running = false;
              play_alarm = true; // <--- ADD THIS: Starts the sound 
              lv_label_set_text(ui_Label23, "Finish");
          }
      }
  }
  delay(5);

  // Shake-to-pika: poll accelerometer, fire random chime when motion exceeds threshold
  if (qmi_ready) {
      float ax, ay, az;
      if (qmi.getAccelerometer(ax, ay, az)) {
          float mag   = sqrtf(ax * ax + ay * ay + az * az);
          float delta = fabsf(mag - GRAVITY_G);

          // Debug: print mag once every ~500ms so you can tune SHAKE_DELTA_MS2 to taste.
          static unsigned long last_print_ms = 0;
          if (millis() - last_print_ms > 500) {
              last_print_ms = millis();
              Serial.printf("accel mag=%.2f delta=%.2f\n", mag, delta);
          }

          if (delta > SHAKE_DELTA_G &&
              (millis() - last_shake_ms) > SHAKE_DEBOUNCE_MS &&
              PIKACHU_CLIP_COUNT > 0 && !play_chime) {
              last_shake_ms = millis();
              chime_clip_idx = random(PIKACHU_CLIP_COUNT);
              play_chime = true;
          }
      }
  }

  // Idle dim: gradual fade once IDLE_DIM_MS of inactivity has passed
  if (!is_dimmed && (millis() - last_touch_ms) > IDLE_DIM_MS) {
      if (millis() - last_fade_step_ms >= DIM_FADE_INTERVAL_MS) {
          last_fade_step_ms = millis();
          if (current_brightness_raw > DIM_BRIGHTNESS_RAW) {
              current_brightness_raw--;
              gfx->setBrightness(map(current_brightness_raw, 0, 100, 0, 255));
          }
          if (current_brightness_raw <= DIM_BRIGHTNESS_RAW) is_dimmed = true;
      }
  }

  static unsigned long last_wifi_check = 0;
  if (millis() - last_wifi_check > 10000) {
      last_wifi_check = millis();
      if (lv_obj_is_visible(ui_WifiConnectPopup)) {
          if (WiFi.status() == WL_CONNECTED) {
              lv_label_set_text(ui_Label36, "Status: Connected!");
          } else if (WiFi.status() == WL_CONNECT_FAILED) {
              lv_label_set_text(ui_Label36, "Status: Failed");
          }
      }
  }
}