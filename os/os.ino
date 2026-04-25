#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <WiFi.h>
#include "time.h"
#include "src/ui/ui.h"

#define LVGL_TICK_PERIOD_MS 2

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

/* ===== WiFi ===== */
const char* ssid = "TANTRA";
const char* password = "SK0029101978";

/* ===== Display ===== */
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1,
  LCD_SDIO2, LCD_SDIO3
);

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0
);

/* ===== Regular Analog Overlay ===== */
lv_obj_t *analogHourLine = NULL;
static lv_point_t analogHourPts[2];

/* ===== PokeBall Analog Overlay ===== */
lv_obj_t *pokeHourLine = NULL;
lv_obj_t *pokeMinuteLine = NULL;
lv_obj_t *pokeSecondLine = NULL;

static lv_point_t pokeHourPts[2];
static lv_point_t pokeMinutePts[2];
static lv_point_t pokeSecondPts[2];

lv_obj_t *pokeTickLines[12];
static lv_point_t pokeTickPts[12][2];

/* ===== Flush ===== */
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

/* ===== LVGL Tick ===== */
void lv_tick_cb(void *arg)
{
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ===== Time Sync ===== */
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

/* =========================================================
   REGULAR ANALOG CLOCK
   Uses:
   - ui_Analog screen
   - ui_Image3 as minute hand
   - analogHourLine as hour hand
   ========================================================= */

void setupRegularAnalogOverlay()
{
  if (!ui_Analog) return;

  if (ui_Image3) {
    lv_img_set_pivot(ui_Image3, 120, 120);
    lv_img_set_zoom(ui_Image3, 256);
    lv_img_set_angle(ui_Image3, 0);

    lv_obj_set_style_opa(ui_Image3, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(ui_Image3, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Image3, LV_OBJ_FLAG_HIDDEN);
  }

  analogHourLine = lv_line_create(ui_Analog);

  analogHourPts[0].x = LCD_WIDTH / 2;
  analogHourPts[0].y = LCD_HEIGHT / 2;
  analogHourPts[1].x = LCD_WIDTH / 2 + 90;
  analogHourPts[1].y = LCD_HEIGHT / 2;

  lv_line_set_points(analogHourLine, analogHourPts, 2);

  lv_obj_set_style_line_width(analogHourLine, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(analogHourLine, lv_color_hex(0xAC0000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(analogHourLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(analogHourLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_move_foreground(analogHourLine);
}

void updateRegularAnalogClock(const struct tm& timeinfo)
{
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;

  const int MINUTE_OFFSET = 420;
  int minuteAngle = wrapAngle(minute * 60 + MINUTE_OFFSET);

  float hourDeg = ((hour % 12) * 30.0) + (minute * 0.5) - 90.0;
  float hourRad = hourDeg * DEG_TO_RAD;

  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;

  if (ui_Image3) {
    lv_img_set_angle(ui_Image3, minuteAngle);
  }

  if (analogHourLine) {
    analogHourPts[0].x = cx;
    analogHourPts[0].y = cy;
    analogHourPts[1].x = cx + cos(hourRad) * 90;
    analogHourPts[1].y = cy + sin(hourRad) * 90;

    lv_line_set_points(analogHourLine, analogHourPts, 2);
    lv_obj_move_foreground(analogHourLine);
  }
}

/* =========================================================
   POKEBALL ANALOG CLOCK
   Uses:
   - ui_PokeBallAnalog screen
   - line ticks
   - line hour/minute/second hands
   ========================================================= */

void hidePokeBallGeneratedParts()
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

void createPokeBallTickLines()
{
  if (!ui_PokeBallAnalog) return;

  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;

  for (int i = 0; i < 12; i++) {
    float deg = i * 30.0 - 90.0;
    float rad = deg * DEG_TO_RAD;

    int innerR = 190;
    int outerR = 220;

    pokeTickPts[i][0].x = cx + cos(rad) * innerR;
    pokeTickPts[i][0].y = cy + sin(rad) * innerR;
    pokeTickPts[i][1].x = cx + cos(rad) * outerR;
    pokeTickPts[i][1].y = cy + sin(rad) * outerR;

    pokeTickLines[i] = lv_line_create(ui_PokeBallAnalog);
    lv_line_set_points(pokeTickLines[i], pokeTickPts[i], 2);

    lv_obj_set_style_line_width(pokeTickLines[i], 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(pokeTickLines[i], lv_color_hex(0x303030), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(pokeTickLines[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(pokeTickLines[i], true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_move_foreground(pokeTickLines[i]);
  }
}

void setupPokeBallOverlay()
{
  if (!ui_PokeBallAnalog) return;

  hidePokeBallGeneratedParts();
  createPokeBallTickLines();

  pokeHourLine = lv_line_create(ui_PokeBallAnalog);
  lv_line_set_points(pokeHourLine, pokeHourPts, 2);
  lv_obj_set_style_line_width(pokeHourLine, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(pokeHourLine, lv_color_hex(0xFFCA00), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(pokeHourLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(pokeHourLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  pokeMinuteLine = lv_line_create(ui_PokeBallAnalog);
  lv_line_set_points(pokeMinuteLine, pokeMinutePts, 2);
  lv_obj_set_style_line_width(pokeMinuteLine, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(pokeMinuteLine, lv_color_hex(0xFFE066), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(pokeMinuteLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(pokeMinuteLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);

  pokeSecondLine = lv_line_create(ui_PokeBallAnalog);
  lv_line_set_points(pokeSecondLine, pokeSecondPts, 2);
  lv_obj_set_style_line_width(pokeSecondLine, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_color(pokeSecondLine, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_opa(pokeSecondLine, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_line_rounded(pokeSecondLine, true, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void updatePokeBallClock(const struct tm& timeinfo)
{
  int hour = timeinfo.tm_hour;
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

  if (pokeHourLine) {
    pokeHourPts[0].x = cx;
    pokeHourPts[0].y = cy;
    pokeHourPts[1].x = cx + cos(hourRad) * 90;
    pokeHourPts[1].y = cy + sin(hourRad) * 90;
    lv_line_set_points(pokeHourLine, pokeHourPts, 2);
    lv_obj_move_foreground(pokeHourLine);
  }

  if (pokeMinuteLine) {
    pokeMinutePts[0].x = cx;
    pokeMinutePts[0].y = cy;
    pokeMinutePts[1].x = cx + cos(minuteRad) * 150;
    pokeMinutePts[1].y = cy + sin(minuteRad) * 150;
    lv_line_set_points(pokeMinuteLine, pokeMinutePts, 2);
    lv_obj_move_foreground(pokeMinuteLine);
  }

  if (pokeSecondLine) {
    pokeSecondPts[0].x = cx;
    pokeSecondPts[0].y = cy;
    pokeSecondPts[1].x = cx + cos(secondRad) * 170;
    pokeSecondPts[1].y = cy + sin(secondRad) * 170;
    lv_line_set_points(pokeSecondLine, pokeSecondPts, 2);
    lv_obj_move_foreground(pokeSecondLine);
  }
}

/* =========================================================
   SETUP
   ========================================================= */

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

  setupRegularAnalogOverlay();
  setupPokeBallOverlay();

  Serial.println("WATCH UI READY");

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    updateRegularAnalogClock(timeinfo);
    updatePokeBallClock(timeinfo);
  }

  const esp_timer_create_args_t timer_args = {
    .callback = &lv_tick_cb,
    .name = "lvgl_tick"
  };

  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
}

/* =========================================================
   LOOP
   ========================================================= */

void loop()
{
  lv_timer_handler();
  delay(5);

  static int lastSecond = -1;

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_sec != lastSecond) {
      lastSecond = timeinfo.tm_sec;

      lv_obj_t *active = lv_scr_act();

      if (active == ui_Analog) {
        updateRegularAnalogClock(timeinfo);
      }

      if (active == ui_PokeBallAnalog) {
        updatePokeBallClock(timeinfo);
      }

      lv_obj_invalidate(active);

      Serial.printf("Active screen updated: %02d:%02d:%02d\n",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
  }
}
