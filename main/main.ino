#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "pin_config.h"
#include <WiFi.h>
#include "time.h"
#include "src/ui/ui.h"

/* ===== LVGL ===== */
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

/* ===== Flush ===== */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(
    area->x1, area->y1,
    (uint16_t *)&color_p->full,
    w, h
  );
  lv_disp_flush_ready(disp);
}

/* ===== LVGL tick ===== */
void lv_tick_cb(void *arg)
{
  lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ===== Wait for valid time ===== */
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

/* ===== SETUP ===== */
void setup()
{
  Serial.begin(115200);
  delay(200);

  /* ===== WiFi ===== */
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  /* ===== NTP / Timezone ===== */
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();
  if (waitForTime()) Serial.println("Time synced");
  else               Serial.println("Time sync timeout");

  /* ===== Display ===== */
  gfx->begin();
  gfx->setBrightness(255);

  /* ===== LVGL ===== */
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res   = LCD_WIDTH;
  disp_drv.ver_res   = LCD_HEIGHT;
  disp_drv.flush_cb  = my_disp_flush;
  disp_drv.draw_buf  = &draw_buf;
  disp_drv.sw_rotate = 1;               // software rotation enabled
  disp_drv.rotated   = LV_DISP_ROT_NONE; // must be NONE here (see below)
  lv_disp_drv_register(&disp_drv);

  // Apply rotation AFTER registering — setting it in disp_drv.rotated
  // alone garbles text; this two-step approach works correctly
  lv_disp_set_rotation(NULL, LV_DISP_ROT_90); // change to 90/180 if needed

  /* ===== SquareLine UI ===== */
  ui_init();

  /* ===== Initial placeholder text ===== */
  if (ui_Label1) lv_label_set_text(ui_Label1, "--:--");
  if (ui_Label2) lv_label_set_text(ui_Label2, "Waiting for time...");

  /* ===== LVGL tick timer ===== */
  const esp_timer_create_args_t timer_args = {
    .callback = &lv_tick_cb,
    .name     = "lvgl_tick"
  };
  esp_timer_handle_t timer;
  esp_timer_create(&timer_args, &timer);
  esp_timer_start_periodic(timer, LVGL_TICK_PERIOD_MS * 1000);
}

/* ===== LOOP ===== */
void loop()
{
  lv_timer_handler();
  delay(5);

  static int lastMinute = -1;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_min != lastMinute) {
      lastMinute = timeinfo.tm_min;
      char timeBuf[16];
      char dateBuf[40];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d",
               timeinfo.tm_hour, timeinfo.tm_min);
      strftime(dateBuf, sizeof(dateBuf), "%A, %B %d, %Y", &timeinfo);
      if (ui_Label1) lv_label_set_text(ui_Label1, timeBuf);
      if (ui_Label2) lv_label_set_text(ui_Label2, dateBuf);
      Serial.printf("Updated time: %s | %s\n", timeBuf, dateBuf);
    }
  } else {
    if (ui_Label1) lv_label_set_text(ui_Label1, "--:--");
    if (ui_Label2) lv_label_set_text(ui_Label2, "No time sync");
  }
  delay(20);
}