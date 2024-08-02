/*
 * Fli3d Camera - RTC functionality using DS1302
 * 
 *   VCC: Module power supply – 3.3V - 5V
 *   GND: Ground
 *   CLK: Clock pin - GPIO 1 (U0TXD)
 *   DAT: Data pin  - GPIO 3 (U0RXD)
 *   RST: Reset     - GPIO 16 (U2RXD) (Must be HIGH for active mode / Active High)
 */

#ifdef RTC

#define RTC_CLK_PIN    1
#define RTC_DAT_PIN    3
#define RTC_RST_PIN    16

#include <Ds1302.h>
#include <UnixTime.h>

Ds1302::DateTime dt;
Ds1302 rtc(RTC_RST_PIN, RTC_CLK_PIN, RTC_DAT_PIN);

bool rtc_init () {
  // to run once in setup()
  return rtc.init();
}

bool rtc_set_time() {
  // to run once at successful NTP acquisition
  timeClient.update();
  datetime.getDateTime(timeClient.getEpochTime());
  dt = {
            .year = datetime.year-2000,
            .month = datetime.month,
            .day = datetime.day,
            .hour = datetime.hour,
            .minute = datetime.minute,
            .second = datetime.second,
  }; 
  rtc.setDateTime(&dt);
  sprintf (buffer, "RTC time set to %04u-%02u-%02u %02u:%02u:%02u based on NTP time", dt.year+2000, dt.month, dt.day, dt.hour, dt.minute, dt.second);
  publish_event (STS_THIS, SS_THIS, EVENT_INIT, buffer);  
  return true;
}

bool rtc_get_time() {
  // to run once at end of setup(), if NTP not successful
  if(!rtc.isHalted()) {
    rtc.getDateTime(&dt);
    datetime.setDateTime(dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    config_this->boot_epoch = datetime.getUnix()+494805632-millis()/1000;
    tm_this->time_set = true;
    sprintf (buffer, "Set time to %04u-%02u-%02u %02u:%02u:%02u based on RTC", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    publish_event (STS_THIS, SS_THIS, EVENT_INIT, buffer);
    if (tm_this->sd_enabled) {
      create_today_dir (FS_SD_MMC); 
    }
    if (tm_this->fs_enabled) {
      create_today_dir (FS_LITTLEFS);
    }
    return true;
  }
  else {
    publish_event (STS_THIS, SS_THIS, EVENT_ERROR, "RTC clock not running, time not set");  
    return false;
  }
}

#endif
