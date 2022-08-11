/* 
 *  Fli3d Camera - camera and storage functionality
 *  
 *  For ESP32CAM board with OV2640 camera (compile for AI Thinker ESP32-CAM), to compile with "Default with spiffs" partitioning
 *
 */

/*  This sketch is based on https://hackaday.io/project/168563-esp32-cam-example-expanded, 
 *  an extension/expansion/rework of the 'official' ESP32 Camera example sketch from Expressif:
 *  https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer
*/

// TODO: solve ftp server conflict with web server
// TODO: allow both SD and FS
// TODO: ftp server unreliable
// TODO: images corrupted when auto-resolution is on (even with unchanged resolution)
// TODO: tune image quality
// TODO: post-flight emergency mode: Access SD data over FTP/wifi before battery runs out
// TODO: based on tm_motion_t, calculate the current expected image smear, in deg/s
//  trans: sqrt (mpu6050.gyro_y*mpu6050.gyro_y+mpu6050.gyro_z*mpu6050.gyro_z)
//  axial: mpu6050.gyro_x


// Set versioning
#define SW_VERSION "Fli3d ESP32cam v2.1.0 (20220803)"
#define PLATFORM_ESP32CAM // tell which platform we are on 

//#define SERIAL_TCTM
//#define SERIAL_KEEPALIVE_OVERRIDE

// Libraries
#include "fli3d.h"
#include <ArduinoOTA.h>

// Functions declared in app_httpd.cpp
void camera_server_setup();

// Global variables used in this file
extern char buffer[JSON_MAX_SIZE];

void setup() {
  // Initialize serial connection to ESP32 (or for debug)
  serial_setup ();
  ov2640.camera_mode = CAM_INIT;
  esp32cam.opsmode = MODE_INIT;

  // Load default (hardcoded) WiFi and other settings, as fallback
  load_default_config(); 
  ccsds_init();

  // If SD to be enabled and initialization successful, load WiFi and other settings from configuration files on SD
  if (config_esp32cam.sd_enable) {
    if (esp32cam.sd_enabled = sd_setup()) {
      publish_packet ((ccsds_t*)tm_this);  // #0
      if (file_load_settings (FS_SD_MMC)) {
        file_load_config (FS_SD_MMC, config_this->config_file);
        file_load_routing (FS_SD_MMC, config_this->routing_file);
      }
      esp32cam.sd_image_enabled = config_esp32cam.sd_image_enable;
      esp32cam.sd_json_enabled = config_esp32cam.sd_json_enable;
      esp32cam.sd_ccsds_enabled = config_esp32cam.sd_ccsds_enable;
      publish_packet ((ccsds_t*)tm_this);  // #3
    }
  }
    
  // If FS to be enabled and initialization successful, load WiFi and other settings from configuration files on FS
  if (esp32cam.sd_enabled and config_this->fs_enable) {
    publish_event (STS_THIS, SS_THIS, EVENT_WARNING, "Only one file system can be set up (keeping SD, disabling FS).");
  }
  if (config_this->fs_enable) {
    if (tm_this->fs_enabled = fs_setup()) {
      publish_packet ((ccsds_t*)tm_this);  // #0
      if (file_load_settings (FS_LITTLEFS)) {
        file_load_config (FS_LITTLEFS, config_this->config_file);
        file_load_routing (FS_LITTLEFS, config_this->routing_file);
      }
    }
  }

  // Now that TM buffering should be hopefully set up, announce we're there
  sprintf (buffer, "%s (%s) started on %s", SW_VERSION, LIB_VERSION, subsystemName[SS_THIS]); 
  publish_event (STS_THIS, SS_THIS, EVENT_INIT, buffer);
  publish_packet ((ccsds_t*)tm_this);  // #1

  // If WiFi to be enabled (AP/client), initialise
  if (config_this->wifi_enable) {
    wifi_setup();
    esp32cam.wifi_image_enabled = config_esp32cam.wifi_image_enable;
    esp32cam.wifi_udp_enabled = config_esp32cam.wifi_udp_enable;
    esp32cam.wifi_yamcs_enabled = config_esp32cam.wifi_yamcs_enable;  
    publish_packet ((ccsds_t*)tm_this);  // #2
  }

  // If Camera to be enabled and WiFi enabled, initialise
  if (config_esp32cam.ftp_enable and config_esp32cam.camera_enable) {
    config_esp32cam.ftp_enable = false;
    publish_event (STS_THIS, SS_THIS, EVENT_WARNING, "FTP server and web server incompatible. Disabling FTP server.");    
  }
  if (config_esp32cam.camera_enable and esp32cam.wifi_enabled) {
    if (esp32cam.camera_enabled = camera_setup()) {
      camera_server_setup();
      publish_packet ((ccsds_t*)tm_this);  // #4
    }
  } 

  // If FTP is to be enabled and FS enabled, initialise FTP server
  if (config_esp32cam.ftp_enable and ((config_esp32cam.ftp_fs == FS_LITTLEFS and esp32cam.fs_enabled) or (config_esp32cam.ftp_fs == FS_SD_MMC and esp32cam.sd_enabled))) {
    esp32cam.ftp_enabled = ftp_setup();
    publish_packet ((ccsds_t*)tm_this);  // #5
  }

  // Initialize OTA
  if (config_esp32cam.ota_enable) {
    ota_setup();
  }
  
  // Initialise Timer and close initialisation
  ntp_check();
  timer_setup();
  ov2640.camera_mode = CAM_IDLE;
  esp32cam.opsmode = MODE_CHECKOUT;
  publish_event (STS_THIS, SS_THIS, EVENT_INIT, "Initialisation complete");  
}

void loop() {
  static uint32_t start_millis;

  timer_loop();

  if (esp32cam.opsmode == MODE_NOMINAL and esp32cam.camera_enabled and esp32cam.sd_image_enabled) { // TODO: this is workaround for not working streaming mode in http interface
    if (var_timer.do_camera) {
      grab_picture();
      var_timer.do_camera = false;
    }
  }

  #ifdef SERIAL_TCTM
  // Check for TM from ESP32
  if (serial_check()) {
    start_millis = millis();
    serial_parse();
    timer_esp32cam.serial_duration = min((uint32_t)255, (uint32_t)millis() - start_millis);
  } 

  // Serial keepalive mechanism
  #ifndef SERIAL_KEEPALIVE_OVERRIDE
  start_millis = millis();    
  serial_keepalive();
  timer_esp32cam.serial_duration += millis() - start_millis;
  #else
  tm_this->serial_connected = true;
  tm_this->warn_serial_connloss = false;
  #endif
  #endif // SERIAL_TCTM

  // OTA check
  if (config_esp32cam.ota_enable) {
    start_millis = millis();    
    ArduinoOTA.handle();
    esp32cam.ota_enabled = true;
    timer_esp32cam.ota_duration += millis() - start_millis;
  }
  
 // FTP check
  if ((esp32cam.opsmode == MODE_INIT or esp32cam.opsmode == MODE_CHECKOUT or esp32cam.opsmode == MODE_DONE) and tm_this->ftp_enabled) {
    // FTP server is active when Fli3d is being prepared or done (or no data from ESP32)
    start_millis = millis();    
    ftp_check (config_this->buffer_fs);
    timer_esp32cam.ftp_duration += millis() - start_millis;
  }

  // wifi check
  if (var_timer.do_wifi and tm_this->wifi_enabled) {
    start_millis = millis();
    wifi_check();
    timer_esp32cam.wifi_duration += millis() - start_millis;
  }

  // NTP check
  if (!tm_this->time_set and (esp32cam.opsmode == MODE_INIT or esp32cam.opsmode == MODE_CHECKOUT) and var_timer.do_ntp and esp32cam.wifi_enabled) {
    start_millis = millis();
    ntp_check();
    timer_esp32cam.wifi_duration += millis() - start_millis;
  }
}
