// USB logging (switchable in Settings) and a record of the last crash.
//
// The crash record survives the reset: the active menu item is kept in RTC
// memory, which a crash or watchdog reset doesn't clear, and on the next boot
// the reset reason plus that item are saved to NVS for the Settings screen.

#include "common.h"
#include <stdarg.h>

static bool usbLog = false;

static const uint32_t RTC_MAGIC = 0xC0DEBEEF;           // tells real values from power-on garbage
RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static int32_t rtcItem;

void dbg(const char *fmt, ...) {
  if (!usbLog) return;
  char buf[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  Serial.print(buf);
}

bool usbLogOn() { return usbLog; }

void setUsbLog(bool on) {
  if (on == usbLog) return;
  if (on) Serial.begin(115200);
  else { Serial.flush(); Serial.end(); }
  usbLog = on;
  prefs.putBool("usblog", on);
  dbg("USB log on\n");
}

void initDiag(const char *(*itemName)(int)) {
  usbLog = prefs.getBool("usblog", false);
  if (usbLog) Serial.begin(115200);

  esp_reset_reason_t why = esp_reset_reason();
  const char *kind = nullptr;
  switch (why) {
    case ESP_RST_PANIC:    kind = "crash"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      kind = "watchdog"; break;
    case ESP_RST_BROWNOUT: kind = "brownout"; break;
    case ESP_RST_POWERON:
      // RTC memory survives a pulse on the EN (reset) line, e.g. from the USB
      // chip, but not a real power loss: so a valid record means "reset"
      if (rtcMagic == RTC_MAGIC && rtcItem >= 0) kind = "reset";
      break;
    default: break;
  }
  const char *item = rtcMagic == RTC_MAGIC ? itemName(rtcItem) : "?";
  if (kind) {
    char buf[40];
    snprintf(buf, sizeof buf, "%s in %s", kind, item);
    prefs.putString("lastcrash", buf);
  }
  dbg("\n=== boot: reset reason %d%s%s ===\n", (int)why, kind ? ", " : "", kind ? kind : "");
  noteActiveItem(-1);
}

void noteActiveItem(int item) {
  rtcItem = item;
  rtcMagic = RTC_MAGIC;
}

bool lastCrash(char *buf, size_t n) {
  return prefs.getString("lastcrash", buf, n) > 0;
}
