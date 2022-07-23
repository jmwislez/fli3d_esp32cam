/* 
 *  Fli3d Camera - camera and storage functionality
 *  
 *  For ESP32CAM board with OV2640 camera (compile for AI Thinker ESP32-CAM)
 *
 */

/*  This sketch is based on https://hackaday.io/project/168563-esp32-cam-example-expanded, 
 *  an extension/expansion/rework of the 'official' ESP32 Camera example sketch from Expressif:
 *  https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer
*/

// TODO: ftp server conflicts with web server
// TODO: images corrupted when auto-resolution is on (even with unchanged resolution)
// TODO: tune image
// TODO: post-flight emergency mode: Access SD data over FTP/wifi before battery runs out
// TODO: based on tm_motion_t, void motion_imageSmear () { // TODO: when to send, to be most effective?  on demand?
// provides to camera the current expected image smear, in deg/s
//  sprintf (buffer, "{\"cmd\":\"set_smear\",\"trans\":%.2f,\"axial\":%.2f}", sqrt (mpu6050.gyro_y*mpu6050.gyro_y+mpu6050.gyro_z*mpu6050.gyro_z), mpu6050.gyro_x);
//  publish_event (TC_ESP32CAM, SS_OV2640, EVENT_CMD, buffer);//
//}


// Set versioning
#define SW_VERSION "Fli3d ESP32cam v2.0.4 (20220723)"
#define PLATFORM_ESP32CAM // tell which platform we are on 

// Compilation options
#define DEBUG_OVER_SERIAL // disable when ESP32 and ESP32CAM are to be connected

// Libraries
#include "fli3d.h"

// Functions declared in app_httpd.cpp
void camera_server_setup ();

// Global variables used in this file
extern char buffer[JSON_MAX_SIZE];

void setup() {
  // Initialize serial connection to ESP32 (or for debug)
  Serial.begin (SerialBaud);
  Serial.println ();
  Serial.setDebugOutput (true); // TODO: needs to be false in case of CCSDS packet transfers?
  ov2640.camera_mode = CAM_INIT;
  esp32cam.opsmode = MODE_INIT;

  // Load default (hardcoded) WiFi and other settings, as fallback
  load_default_config (); 
  ccsds_init ();

  // If SD to be enabled and initialization successful, load WiFi and other settings from configuration files on SD
  if (config_esp32cam.sd_enable) {
    if (esp32cam.sd_enabled = sd_setup ()) {
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
  if (config_this->fs_enable) {
    if (tm_this->fs_enabled = fs_setup ()) {
      publish_packet ((ccsds_t*)tm_this);  // #0
      if (file_load_settings (FS_LITTLEFS)) {
        file_load_config (FS_LITTLEFS, config_this->config_file);
        file_load_routing (FS_LITTLEFS, config_this->routing_file);
      }
    }
  }

  // Now that TM buffering should be hopefully set up, announce we're there
  sprintf (buffer, "%s started on %s", SW_VERSION, subsystemName[SS_THIS]); 
  publish_event (STS_THIS, SS_THIS, EVENT_INIT, buffer);

  #ifdef DEBUG_OVER_SERIAL  
  config_this->debug_over_serial = true;
  tm_this->serial_connected = true;
  #endif // DEBUG_OVER_SERIAL
  publish_packet ((ccsds_t*)tm_this);  // #1

  // If WiFi to be enabled (AP/client), initialise
  if (config_this->wifi_enable) {
    wifi_setup ();
    esp32cam.wifi_image_enabled = config_esp32cam.wifi_image_enable;
    esp32cam.wifi_udp_enabled = config_esp32cam.wifi_udp_enable;
    esp32cam.wifi_yamcs_enabled = config_esp32cam.wifi_yamcs_enable;  
    publish_packet ((ccsds_t*)tm_this);  // #2
  }

  // If Camera to be enabled and WiFi enabled, initialise
  if (config_esp32cam.camera_enable and esp32cam.wifi_enabled) {
    if (esp32cam.camera_enabled = camera_setup ()) {
      camera_server_setup ();
      publish_packet ((ccsds_t*)tm_this);  // #4
    }
  } 

  // If FTP is to be enabled and FS enabled, initialise FTP server
  if (config_esp32cam.ftp_enable and ((config_esp32cam.ftp_fs == FS_LITTLEFS and esp32cam.fs_enabled) or (config_esp32cam.ftp_fs == FS_SD_MMC and esp32cam.sd_enabled))) {
    esp32cam.ftp_enabled = ftp_setup ();
    publish_packet ((ccsds_t*)tm_this);  // #5
  }

  // Initialise Timer and close initialisation
  timer_setup ();
  ov2640.camera_mode = CAM_IDLE;
  esp32cam.opsmode = MODE_NOMINAL;
  publish_event (STS_THIS, SS_THIS, EVENT_INIT, "Initialisation complete");  
}

void loop() {
  static uint32_t start_millis;

  timer_loop ();
  
  if (serial_check ()) {
    start_millis = millis ();
    serial_parse ();
    timer_esp32cam.serial_duration = min((uint32_t)255, (uint32_t)millis() - start_millis);
  } 

  // Serial keepalive mechanism
  start_millis = millis ();    
  if (!config_this->debug_over_serial and timer_esp32cam.millis - var_timer.last_serial_out_millis > KEEPALIVE_INTERVAL) {
    if (tm_this->serial_connected) {
      Serial.println ("O");
    }
    else {
      Serial.println ("o");
    }
    var_timer.last_serial_out_millis = millis();
  }
  if (!config_this->debug_over_serial and tm_this->serial_connected and millis()-var_timer.last_serial_in_millis > 2*KEEPALIVE_INTERVAL) {
    tm_this->serial_connected = false;
    tm_this->warn_serial_connloss = true;
  }
  timer_esp32cam.serial_duration += millis() - start_millis;

 // FTP check
  if ((esp32.opsmode == MODE_INIT or esp32.opsmode == MODE_CHECKOUT or esp32.opsmode == MODE_DONE) and tm_this->ftp_enabled) {
    // FTP server is active when Fli3d is being prepared or done (or no data from ESP32)
    start_millis = millis ();    
    ftp_check (config_this->buffer_fs);
    timer_esp32cam.ftp_duration += millis() - start_millis;
  }

  // wifi check
  if (var_timer.do_wifi and tm_this->wifi_enabled) {
    start_millis = millis ();
    wifi_check ();
    timer_esp32cam.wifi_duration += millis() - start_millis;
  }

  // NTP check
  if ((esp32.opsmode == MODE_INIT or esp32.opsmode == MODE_CHECKOUT) and var_timer.do_ntp and esp32cam.wifi_enabled) {
    start_millis = millis ();
    time_check ();
    timer_esp32cam.wifi_duration += millis() - start_millis;
  }
}
