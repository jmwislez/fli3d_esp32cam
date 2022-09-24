/*
 * Fli3d Camera - SD functionality
 */

#include "FS.h"
#include "SD_MMC.h"
#include "SPI.h"
#include <NTPClient.h>

extern NTPClient timeClient;
extern char today_dir[16];

void sd_save_image (const uint8_t * fb_buf, size_t fb_len) {
  if (esp32cam.sd_enabled and esp32cam.sd_image_enabled) {
    static uint32_t timestamp_save = millis();
    datetime.getDateTime(config_this->boot_epoch + millis()/1000);
    sprintf (ov2640.filename, "%s/%02d%02d%02d-%04d.jpg", today_dir, datetime.hour, datetime.minute, datetime.second, ov2640.packet_ctr);
    ov2640.filesize = fb_len;
    File imagefile = SD_MMC.open (ov2640.filename, FILE_WRITE);
    if (!imagefile) {
      publish_event (STS_ESP32CAM, SS_SD, EVENT_ERROR, "Failed to open image file in writing mode");
      esp32cam.sd_image_enabled = false;
      esp32cam.err_sd_dataloss = true;
    } 
    else {
      imagefile.write(fb_buf, fb_len); // payload (image), payload length
      //esp32cam.sd_current = true;
    } 
    imagefile.close();
    ov2640.sd_ms = millis()-timestamp_save;
  }
}
