// Backlight brightness, battery voltage and deep sleep for the T-Display.

#include "common.h"
#include "esp_sleep.h"

#define ADC_EN_PIN  14                // switches the battery voltage divider on
#define BATT_PIN    34                // battery voltage / 2
#define BL_CHANNEL  0                 // LEDC (PWM) channel for the backlight

static const uint8_t BRIGHT_DUTY[NUM_BRIGHTNESS] = {8, 25, 60, 130, 255};   // roughly even steps to the eye
static int brightness = NUM_BRIGHTNESS - 1;

void initPower() {
  // undo the pin holds from the previous deep sleep
  gpio_hold_dis((gpio_num_t)TFT_BL);
  gpio_hold_dis((gpio_num_t)ADC_EN_PIN);
  gpio_deep_sleep_hold_dis();
  pinMode(ADC_EN_PIN, OUTPUT);
  digitalWrite(ADC_EN_PIN, LOW);       // divider only on while measuring
}

void initBacklight() {
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  setBrightness(prefs.getUChar("bright", NUM_BRIGHTNESS - 1), false);
}

void setBrightness(int level, bool save) {
  brightness = constrain(level, 0, NUM_BRIGHTNESS - 1);
  ledcWrite(BL_CHANNEL, BRIGHT_DUTY[brightness]);
  if (save) prefs.putUChar("bright", brightness);
}

int brightnessLevel() { return brightness; }

int batteryMilliVolts() {
  digitalWrite(ADC_EN_PIN, HIGH);
  delay(5);                            // let the divider settle
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(BATT_PIN);   // calibrated reading
  digitalWrite(ADC_EN_PIN, LOW);
  return sum / 16 * 2;
}

int batteryPercent(int mv) {
  // typical LiPo discharge curve under light load
  static const int16_t MV[]  = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int8_t  PCT[] = {   0,    5,   10,   20,   35,   50,   65,   80,   90,  100};
  if (mv <= MV[0]) return 0;
  for (int i = 1; i < 10; i++)
    if (mv < MV[i]) return PCT[i - 1] + (PCT[i] - PCT[i - 1]) * (mv - MV[i - 1]) / (MV[i] - MV[i - 1]);
  return 100;
}

// with USB connected the pin sees the ~5 V supply instead of the battery
bool onUsbPower(int mv) { return mv >= 4350; }

void goToSleep() {
  dbg("going to sleep\n");
  if (usbLogOn()) Serial.flush();
  spr.fillSprite(TFT_BLACK);
  spr.pushSprite(0, 0);
  tft.writecommand(0x28);              // DISPOFF
  tft.writecommand(0x10);              // SLPIN: panel controller into its own sleep mode
  delay(5);

  // backlight and battery divider firmly off, held through deep sleep
  ledcDetachPin(TFT_BL);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  digitalWrite(ADC_EN_PIN, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL);
  gpio_hold_en((gpio_num_t)ADC_EN_PIN);
  gpio_deep_sleep_hold_en();

  while (digitalRead(WAKE_PIN) == LOW) delay(10);   // don't wake instantly

  // ext1 (unlike ext0) lets the RTC peripherals power down; the button has
  // an external pull-up on the board, so no internal pull-up is needed
  esp_sleep_enable_ext1_wakeup(1ULL << WAKE_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
  esp_deep_sleep_start();
}
