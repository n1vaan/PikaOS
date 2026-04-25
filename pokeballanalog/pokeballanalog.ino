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
lv_obj_t *minuteLine = NULL;
lv_obj_t *secondLine = NULL;

static lv_point_t hourPts[2];
static lv_point_t minutePts[2];
static lv_point_t secondPts[2];

lv_obj_t *tickLines[12];
static lv_point_t tickPts[12][2];

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

void hideGeneratedPillsAndImage()
{
  if (ui_Pil12) lv_obj_add_flag(ui_Pil12, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil1)  lv_obj_add_flag(ui_Pil1, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil2)  lv_obj_add_flag(ui_Pil2, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil4)  lv_obj_add_flag(ui_Pil4, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil5)  lv_obj_add_flag(ui_Pil5, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil6)  lv_obj_add_flag(ui_Pil6, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil7)  lv_obj_add_flag(ui_Pil7, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil8)  lv_obj_add_flag(ui_Pil8, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil10) lv_obj_add_flag(ui_Pil10, LV_OBJ_FLAG_HIDDEN);
  if (ui_Pil11) lv_obj_add_flag(ui_Pil11, LV_OBJ_FLAG_HIDDEN);

  if (ui_Image2) lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_HIDDEN);
}

void createTickLines()
{
  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;

  for (int i = 0; i < 12; i++) {
    float deg = i * 30.0 - 90.0;
    float rad = deg * DEG_TO_RAD;

    int innerR = 190;
    int outerR = 220;

    tickPts[i][0].x = cx + cos(rad) * innerR;
    tickPts[i][0].y = cy + sin(rad) * innerR;
    tickPts[i][1].x = cx + cos(rad) * outerR;
    tickPts[i][1].y = cy + sin(rad) * outerR;

    tickLines[i] = lv_line_create(ui_PokeBallAnalog);
    lv_line_set_points(tickLines[i], tickPts[i], 2);

    lv_obj_set_style_line_width(tickLines[i], 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(tickLines[i], lv_color_hex(0x303030), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(tickLines[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(tickLines[i], true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_move_foreground(tickLines[i]);
  }
}

void updateAnalogClock(const struct tm& timeinfo)
{
  int hour   = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int second = timeinfo.tm_sec;

  float hourDeg   = ((hour % 12) * 30.0) + (minute * 0.5) - 90.0;
  float minuteDeg = minute * 6.0 - 90.0;
  float secondDeg = second * 6.0 - 90.0;

  float hourRad   = hourDeg * DEG_TO_RAD;
  float minuteRad = minuteDeg * DEG_TO_RAD;
  float secondRad = secondDeg * DEG_TO_RAD;

  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;

  if (hourLine) {
    hourPts[0].x = cx;
    hourPts[0].y = cy;
    hourPts[1].x = cx + cos(hourRad) * 90;
    hourPts[1].y = cy + sin(hourRad) * 90;
    lv_line_set_points(hourLine, hourPts, 2);
  }

  if (minuteLine) {
    minutePts[0].x = cx;
    minutePts[0].y = cy;
    minutePts[1].x = cx + cos(minuteRad) * 150;
    minutePts[1].y = cy + sin(minuteRad) * 150;
    lv_line_set_points(minuteLine, minutePts, 2);
  }

  if (secondLine) {
    secondPts[0].x = cx;
    secondPts[0].y = cy;
    secondPts[1].x = cx + cos(secondRad) * 170;
    secondPts[1].y = cy + sin(secondRad) * 170;
    lv_line_set_points(secondLine, secondPts, 2);
  }

  lv_obj_move_foreground(hourLine);
  lv_obj_move_foreground(minuteLine);
  lv_obj_move_foreground(secondLine);

  lv_obj_invalidate(lv_scr_act());

  Serial.printf("Real time %02d:%02d:%02d\n", hour, minute, second);
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
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.sw_rotate = 1;
  disp_drv.rotated = LV_DISP_ROT_NONE;
  lv_disp_drv_register(&disp_drv);

  lv_disp_set_rotation(NULL, LV_DISP_ROT_90);

  ui_init();

  if (ui_PokeBallAnalog) {
    lv_scr_load(ui_PokeBallAnalog);
  }

  hideGeneratedPillsAndImage();
  createTickLines();

  hourLine = lv_line_create(ui_PokeBallAnalog);
  lv_line_set_points(hourLine, hourPts, 2);
  lv_obj_set_style_line_width(hourLine, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(hourLine, lv_color_hex(0xFFCA00), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(hourLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(hourLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  minuteLine = lv_line_create(ui_PokeBallAnalog);
  lv_line_set_points(minuteLine, minutePts, 2);
  lv_obj_set_style_line_width(minuteLine, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(minuteLine, lv_color_hex(0xFFCA00), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(minuteLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(minuteLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  secondLine = lv_line_create(ui_PokeBallAnalog);
  lv_line_set_points(secondLine, secondPts, 2);
  lv_obj_set_style_line_width(secondLine, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(secondLine, lv_color_hex(0xFFCA00), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(secondLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(secondLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  Serial.println("POKEBALL CLOCK READY");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    updateAnalogClock(timeinfo);
  } else {
    Serial.println("Could not get initial time");
  }

  const esp_timer_create_args_t timer_args = {
    .callback = &lv_tick_cb,
    .name = "lvgl_tick"
  };

  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
}

void loop()
{
  lv_timer_handler();
  delay(5);

  static int lastSecond = -1;

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_sec != lastSecond) {
      lastSecond = timeinfo.tm_sec;
      updateAnalogClock(timeinfo);
    }
  }
}