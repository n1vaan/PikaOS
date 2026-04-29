#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include "TouchDrvCSTXXX.hpp"  // Official Waveshare Touch Library
#include <Wire.h>
#include <WiFi.h>
#include "time.h"
#include "src/ui/ui.h"

#include "ESP_I2S.h"
#include "esp_check.h"
#include "es8311.h"
#include "canon.h"

I2SClass i2s;
#define EXAMPLE_SAMPLE_RATE 8000
#define EXAMPLE_VOICE_VOLUME 80

volatile bool play_alarm = false; // Set to true to start, false to stop

/* ===== WiFi Settings ===== */
const char* ssid = "TANTRA";
const char* password = "SK0029101978";

/* ===== LVGL Settings ===== */
#define LVGL_TICK_PERIOD_MS 2
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];
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

// Helper to format the label (MM:SS)
void update_timer_label() {
    int minutes = timer_seconds / 60;
    int seconds = timer_seconds % 60;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    if (ui_Label14) lv_label_set_text(ui_Label14, buf);
}
/* ===== Touch Callbacks ===== */
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint8_t touched = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
  if (touched > 0) {
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
    snprintf(tB, sizeof(tB), "%02d:%02d", ti.tm_hour, ti.tm_min);
    strftime(dB, sizeof(dB), "%A, %B %d, %Y", &ti);
    if (ui_Label1) lv_label_set_text(ui_Label1, tB);
    if (ui_Label2) lv_label_set_text(ui_Label2, dB);
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
  es8311_handle_t es_handle = es8311_create(0, ES8311_ADDRRES_0);
  const es8311_clock_config_t es_clk = {
    .mclk_from_mclk_pin = true,
    .mclk_frequency = EXAMPLE_SAMPLE_RATE * 256,
    .sample_frequency = EXAMPLE_SAMPLE_RATE
  };
  es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL);
  return ESP_OK;
}

void audio_task(void *param) {
  i2s.setPins(BCLKPIN, WSPIN, DIPIN, DOPIN, MCLKPIN);
  i2s.begin(I2S_MODE_STD, EXAMPLE_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH);
  Wire.begin(15, 14); // Codec I2C
  es8311_codec_init();

  while (1) {
    if (play_alarm) {
      // This will loop the sound as long as play_alarm is true
      i2s.write((uint8_t *)canon_pcm, canon_pcm_len);
    } else {
      vTaskDelay(100 / portTICK_PERIOD_MS); // Idle wait
    }
    vTaskDelay(1); 
  }
}

/* ===== SETUP ===== */
void setup() {
  Serial.begin(115200);

  // 1. Touch Initialization (Official documented way)
  Wire.begin(IIC_SDA, IIC_SCL);
  pinMode(TP_INT, INPUT_PULLUP);
  digitalWrite(TP_RESET, LOW); delay(30); digitalWrite(TP_RESET, HIGH); delay(50);
  touch.setPins(TP_RESET, TP_INT);
  touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);

  // 2. WiFi & Time
  WiFi.begin(ssid, password);
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();

  // 3. Display & LVGL
  gfx->begin();
  gfx->setBrightness(255);
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_WIDTH; disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush; disp_drv.draw_buf = &draw_buf;
  disp_drv.sw_rotate = 1;
  lv_disp_drv_register(&disp_drv);
  lv_disp_set_rotation(NULL, LV_DISP_ROT_90);

  // 4. Input Driver Registration
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  indev_drv.long_press_time = 500;        // Wait 0.5s before repeating
  indev_drv.long_press_repeat_time = 100; // Repeat every 0.1s while held

  // 5. SquareLine UI
  ui_init();
  
  // Disable scrolling to prioritize gestures
  lv_obj_clear_flag(ui_Analog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_PokeBallAnalog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_Timer, LV_OBJ_FLAG_SCROLLABLE);

  // 6. Clock Face Setup
  setupRegularAnalog();
  setupPokeBall();

  lv_obj_add_event_cb(ui_Button5, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button1, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button2, handle_timer_buttons, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(ui_Button3, handle_timer_buttons, LV_EVENT_ALL, NULL);

  // Control buttons remain LV_EVENT_CLICKED
  lv_obj_add_event_cb(ui_Button6, start_timer_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Button9, end_timer_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(ui_Button10, pause_timer_cb, LV_EVENT_CLICKED, NULL);  
  update_timer_label(); // Set initial text

  // 7. Tick Timer
  const esp_timer_create_args_t timer_args = { .callback = &lv_tick_cb, .name = "tick" };
  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
  xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, NULL, 1, NULL, 1);
}

void loop() {
  lv_obj_invalidate(lv_scr_act()); // Add this: Forces a full screen refresh every cycle
  lv_timer_handler();
  
  static int lastSec = -1;
  struct tm ti;
  if (getLocalTime(&ti) && ti.tm_sec != lastSec) {
    lastSec = ti.tm_sec;
    updateAllLogic(ti);
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
}