/*
 * Fli3d Camera - SD functionality
 */

#include "FS.h"
#include "SD_MMC.h"
#include "SPI.h"
#include <NTPClient.h>

extern NTPClient timeClient;

char sd_dir[20] = "/";

bool sd_setup () {
  if (!SD_MMC.begin()) {
    publish_event (STS_ESP32CAM, SS_SD, EVENT_ERROR, "Card reader initialisation failed");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    publish_event (STS_ESP32CAM, SS_SD, EVENT_ERROR, "No SD card inserted");
    return false;
  }
  sprintf (buffer, "SD card mounted: size: %llu MB; space: %llu MB; used: %llu MB", SD_MMC.cardSize() / (1024 * 1024), SD_MMC.totalBytes() / (1024 * 1024), SD_MMC.usedBytes() / (1024 * 1024));
  publish_event (STS_ESP32CAM, SS_SD, EVENT_INIT, buffer);
  return true;
}

bool sd_load_settings() { // TODO: use JSON command strings instead (same as fs_load_settings?)
/*  char linebuffer[80];
  File file = SD_MMC.open ("/CONFIG.TXT");
  if (!file) {
    publish_event (STS_ESP32CAM, SS_SD, EVENT_ERROR, "Failed to open configuration file CONFIG.TXT");
    return false;
  }
  uint8_t i = 0; 
  while (file.available ()) {
    linebuffer[i] = file.read();
    if (linebuffer[i] != '\r') { // ignore \r
      if (linebuffer[i] == '\n') { // full line read
        linebuffer[i] = 0; // mark end of c string
        if (String(linebuffer).startsWith("ESP32_WIFI")) {
          strcpy (wifi_type, String(linebuffer).substring(5).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_SSID")) {
          strcpy (config_network_esp32.wifi_ssid, String(linebuffer).substring(5).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_PASSWORD")) {
          strcpy (config_network_esp32.wifi_password, String(linebuffer).substring(9).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_AP_SSID")) {
          strcpy (config_network_esp32.ap_ssid, String(linebuffer).substring(8).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_AP_PASSWORD")) {
          strcpy (config_network_esp32.ap_password, String(linebuffer).substring(12).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_YAMCS_SERVER")) {
          strcpy (config_network_esp32.yamcs_server, String(linebuffer).substring(11).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_YAMCS_PORT")) {
          udp_port = String(linebuffer).substring(9).toInt();
        }
        if (String(linebuffer).startsWith("ESP32_UDP_SERVER")) {
          strcpy (config_network_esp32.udp_server, String(linebuffer).substring(11).c_str());
        }
        if (String(linebuffer).startsWith("ESP32_UDP_PORT")) {
          config_network_esp32.udp_port = String(linebuffer).substring(9).toInt();
        }
        if (String(linebuffer).startsWith("ESP32_YAMCS_PORT")) {
          config_network_esp32.yamcs_port = String(linebuffer).substring(9).toInt();
        }
        if (String(linebuffer).startsWith("ESP32_NTP_SERVER")) {
          strcpy (config_network_esp32.ntp_server, String(linebuffer).substring(11).c_str());
        }
        i = 0;
      }
      else {
        i++;
      }
    }
  }
  file.close();
  if (wifi_ssid != "" and wifi_password != "") {
    extbus_println ("sts_esp32cam {\"type\":\"init\",\"msg\":\"Read settings from SD card\"}");
    return true;
  }
  else {
    extbus_println ("sts_esp32cam {\"type\":\"error\",\"msg\":\"Failed to read settings from SD card\"}");
    error_ctr++;    
    return false;
  } */
}

void sd_save_image (const uint8_t * fb_buf, size_t fb_len) {
  if (esp32cam.sd_enabled and esp32cam.sd_image_enabled) {
    static uint32_t timestamp_save = millis();
    timeClient.update();
    sprintf (ov2640.filename, "%s/%02d%02d%02d-%04d.jpg", sd_dir, timeClient.getHours(), timeClient.getMinutes(), timeClient.getSeconds(), ov2640.packet_ctr);
    ov2640.filesize = fb_len;
    File imagefile = SD_MMC.open (ov2640.filename, FILE_WRITE);
    if (!imagefile) {
      publish_event (STS_ESP32CAM, SS_SD, EVENT_ERROR, "Failed to open image file in writing mode");
      esp32cam.sd_image_enabled = false;
    } 
    else {
      imagefile.write(fb_buf, fb_len); // payload (image), payload length
      //esp32cam.sd_current = true;
    } 
    imagefile.close();
    ov2640.sd_ms = millis()-timestamp_save;
  }
}

void sd_create_imagedir () {
  timeClient.update();
  sprintf (sd_dir, "/%s%s%s", timeClient.getFormattedDate().substring(0,4), timeClient.getFormattedDate().substring(5,7), timeClient.getFormattedDate().substring(8,10));
  if (SD_MMC.open (sd_dir)) {
    sprintf (buffer, "Directory %s already exists", sd_dir);
    publish_event (STS_ESP32CAM, SS_SD, EVENT_INIT, buffer);
  }
  else {
    if (SD_MMC.mkdir(sd_dir)) {
      sprintf (buffer, "Created directory %s", sd_dir);  
      publish_event (STS_ESP32CAM, SS_SD, EVENT_INIT, buffer);
    } else {
      sprintf (buffer, "Failed to create directory %s", sd_dir);  
      publish_event (STS_ESP32CAM, SS_SD, EVENT_ERROR, buffer);
    }
  }
}
