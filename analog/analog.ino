#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <WiFi.h>
#include "time.h"
#include "src/ui/ui.h"

#define LVGL_TICK_PERIOD_MS 2

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

lv_obj_t *hourLine = NULL;
static lv_point_t hourPts[2];

const char* ssid = "TANTRA";
const char* password = "SK0029101978";

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1,
  LCD_SDIO2, LCD_SDIO3
);

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0
);

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  gfx->draw16bitRGBBitmap(
    area->x1, area->y1,
    (uint16_t *)&color_p->full,
    w, h
  );

  lv_disp_flush_ready(disp);
}

void lv_tick_cb(void *arg)
{
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

bool waitForTime(int timeoutMs = 10000)
{
  struct tm timeinfo;
  unsigned long start = millis();

  while (millis() - start < (unsigned long)timeoutMs) {
    if (getLocalTime(&timeinfo)) return true;
    delay(200);
  }

  return false;
}

int wrapAngle(int angle)
{
  while (angle < 0) angle += 3600;
  while (angle >= 3600) angle -= 3600;
  return angle;
}

void updateAnalogClock(const struct tm& timeinfo)
{
  int hour   = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;

  const int MINUTE_OFFSET = 420;

  int minuteAngle = wrapAngle(minute * 60 + MINUTE_OFFSET);

  float hourDeg = ((hour % 12) * 30.0) + (minute * 0.5) - 90.0;
  float hourRad = hourDeg * DEG_TO_RAD;

  if (ui_Image3) {
    lv_img_set_angle(ui_Image3, minuteAngle);
  }

  if (hourLine) {
    hourPts[0].x = LCD_WIDTH / 2;
    hourPts[0].y = LCD_HEIGHT / 2;

    hourPts[1].x = LCD_WIDTH / 2 + cos(hourRad) * 90;
    hourPts[1].y = LCD_HEIGHT / 2 + sin(hourRad) * 90;

    lv_line_set_points(hourLine, hourPts, 2);
    lv_obj_move_foreground(hourLine);
  }

  lv_obj_invalidate(lv_scr_act());

  Serial.printf("Real time %02d:%02d | minuteAngle=%d hourDeg=%.1f\n",
                hour, minute, minuteAngle, hourDeg);
}

void setup()
{
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("===== BOOT STARTED =====");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  unsigned long wifiStart = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(300);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
  } else {
    Serial.println("\nWiFi timeout, continuing anyway");
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();

  if (waitForTime()) {
    Serial.println("Time synced");
  } else {
    Serial.println("Time sync timeout");
  }

  gfx->begin();
  gfx->setBrightness(255);

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res   = LCD_WIDTH;
  disp_drv.ver_res   = LCD_HEIGHT;
  disp_drv.flush_cb  = my_disp_flush;
  disp_drv.draw_buf  = &draw_buf;
  disp_drv.sw_rotate = 1;
  disp_drv.rotated   = LV_DISP_ROT_NONE;
  lv_disp_drv_register(&disp_drv);

  lv_disp_set_rotation(NULL, LV_DISP_ROT_90);

  ui_init();

  if (ui_Analog) {
    lv_scr_load(ui_Analog);
  }

  /* ===== Minute hand setup ===== */
  if (ui_Image3) {
    lv_img_set_pivot(ui_Image3, 120, 120);
    lv_img_set_zoom(ui_Image3, 256);
    lv_img_set_angle(ui_Image3, 0);

    lv_obj_set_style_opa(ui_Image3, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_Image3, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
  }

  /* ===== Manual red hour line ===== */
  hourLine = lv_line_create(ui_Analog);

  hourPts[0].x = LCD_WIDTH / 2;
  hourPts[0].y = LCD_HEIGHT / 2;
  hourPts[1].x = LCD_WIDTH / 2 + 90;
  hourPts[1].y = LCD_HEIGHT / 2;

  lv_line_set_points(hourLine, hourPts, 2);

  lv_obj_set_style_line_width(hourLine, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(hourLine, lv_color_hex(0xAC0000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(hourLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(hourLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_move_foreground(hourLine);

  Serial.println("HOUR LINE CREATED");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    updateAnalogClock(timeinfo);
  } else {
    Serial.println("Could not get initial time");
  }

  const esp_timer_create_args_t timer_args = {
    .callback = &lv_tick_cb,
    .name     = "lvgl_tick"
  };

  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
}

void loop()
{
  lv_timer_handler();
  delay(5);

  static int lastMinute = -1;

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_min != lastMinute) {
      lastMinute = timeinfo.tm_min;
      updateAnalogClock(timeinfo);
    }
  }
}