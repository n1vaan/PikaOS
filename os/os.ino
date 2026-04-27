#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <WiFi.h>
#include "time.h"
#include "src/ui/ui.h"
#include <CST816S.h> // Ensure you install the CST816S library

#define LVGL_TICK_PERIOD_MS 2

/* --- Global Buffers --- */
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];
static lv_indev_drv_t indev_drv;

/* --- Waveshare Touch Pins --- */
#define TOUCH_SDA 8
#define TOUCH_SCL 9
#define TOUCH_INT 5
#define TOUCH_RST 13
CST816S touch(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);

/* --- Display Driver --- */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *gfx = new Arduino_CO5300(bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

const char* ssid = "TANTRA";
const char* password = "SK0029101978";

/* --- Clock Objects --- */
lv_obj_t *analogHourLine = NULL;
lv_obj_t *pokeHourLine = NULL, *pokeMinuteLine = NULL, *pokeSecondLine = NULL;
static lv_point_t analogHourPts[2], pokeHourPts[2], pokeMinutePts[2], pokeSecondPts[2];

/* --- TOUCH READ CALLBACK --- */
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
    if (touch.available()) {
        data->state = LV_INDEV_STATE_PR;
        // Map touch coordinates (Adjust if swiping feels inverted)
        data->point.x = touch.data.x;
        data->point.y = touch.data.y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

/* --- CLOCK LOGIC: STANDARD ANALOG --- */
void updateAnalogClock(const struct tm& timeinfo) {
  if (!ui_Analog || lv_scr_act() != ui_Analog) return;
  
  int minuteAngle = ((timeinfo.tm_min * 60) + 420) % 3600;
  float hourRad = (((timeinfo.tm_hour % 12) * 30.0) + (timeinfo.tm_min * 0.5) - 90.0) * DEG_TO_RAD;

  if (ui_Image3) lv_img_set_angle(ui_Image3, minuteAngle);
  if (analogHourLine) {
    analogHourPts[0] = {LCD_WIDTH/2, LCD_HEIGHT/2};
    analogHourPts[1] = {(lv_coord_t)(LCD_WIDTH/2 + cos(hourRad)*90), (lv_coord_t)(LCD_HEIGHT/2 + sin(hourRad)*90)};
    lv_line_set_points(analogHourLine, analogHourPts, 2);
  }
}

/* --- CLOCK LOGIC: POKEBALL --- */
void updatePokeBallClock(const struct tm& timeinfo) {
  if (!ui_PokeBallAnalog || lv_scr_act() != ui_PokeBallAnalog) return;

  int cx = LCD_WIDTH/2, cy = LCD_HEIGHT/2;
  float hR = (((timeinfo.tm_hour % 12) * 30.0) + (timeinfo.tm_min * 0.5) - 90.0) * DEG_TO_RAD;
  float mR = (timeinfo.tm_min * 6.0 - 90.0) * DEG_TO_RAD;
  float sR = (timeinfo.tm_sec * 6.0 - 90.0) * DEG_TO_RAD;

  if (pokeHourLine) {
    pokeHourPts[0] = {cx, cy}; pokeHourPts[1] = {(lv_coord_t)(cx + cos(hR)*90), (lv_coord_t)(cy + sin(hR)*90)};
    lv_line_set_points(pokeHourLine, pokeHourPts, 2);
  }
  if (pokeMinuteLine) {
    pokeMinutePts[0] = {cx, cy}; pokeMinutePts[1] = {(lv_coord_t)(cx + cos(mR)*150), (lv_coord_t)(cy + sin(mR)*150)};
    lv_line_set_points(pokeMinuteLine, pokeMinutePts, 2);
  }
  if (pokeSecondLine) {
    pokeSecondPts[0] = {cx, cy}; pokeSecondPts[1] = {(lv_coord_t)(cx + cos(sR)*170), (lv_coord_t)(cy + sin(sR)*170)};
    lv_line_set_points(pokeSecondLine, pokeSecondPts, 2);
  }
}

/* --- FLUSH & TICK --- */
void my_disp_flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c) {
  gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&c->full, a->x2-a->x1+1, a->y2-a->y1+1);
  lv_disp_flush_ready(d);
}
void lv_tick_cb(void *arg) { lv_tick_inc(LVGL_TICK_PERIOD_MS); }

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  configTime(0, 0, "pool.ntp.org");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();

  touch.begin(); // Start CST816S
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

  // Register Touch
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  ui_init();

  // IMPORTANT: Disable scrolling so gestures work
  lv_obj_clear_flag(ui_Analog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_PokeBallAnalog, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);

  // Setup Lines
  analogHourLine = lv_line_create(ui_Analog);
  lv_obj_set_style_line_color(analogHourLine, lv_color_hex(0xD41800), 0);
  lv_obj_set_style_line_width(analogHourLine, 14, 0);
  lv_obj_set_style_line_rounded(analogHourLine, true, 0);
  lv_obj_clear_flag(analogHourLine, LV_OBJ_FLAG_CLICKABLE);

  pokeHourLine = lv_line_create(ui_PokeBallAnalog);
  lv_obj_set_style_line_color(pokeHourLine, lv_color_hex(0xFFCA00), 0);
  lv_obj_set_style_line_width(pokeHourLine, 14, 0);
  lv_obj_clear_flag(pokeHourLine, LV_OBJ_FLAG_CLICKABLE);

  // ... repeat similar setup for pokeMinute/pokeSecond if needed ...

  const esp_timer_create_args_t timer_args = { .callback = &lv_tick_cb, .name = "lvgl_tick" };
  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
}

void loop() {
  lv_timer_handler();
  
  static int lastSec = -1;
  struct tm ti;
  if (getLocalTime(&ti) && ti.tm_sec != lastSec) {
    lastSec = ti.tm_sec;
    updateAnalogClock(ti);
    updatePokeBallClock(ti);
    
    // Update Screen1 Labels
    if (lv_scr_act() == ui_Screen1) {
       char tb[10]; snprintf(tb, 10, "%02d:%02d", ti.tm_hour, ti.tm_min);
       lv_label_set_text(ui_Label1, tb);
    }
  }
  delay(5);
}