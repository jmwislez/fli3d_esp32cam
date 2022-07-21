/* 
 *  Fli3d Camera - camera and storage functionality
 *  
 *  For ESP32CAM board with OV2640 camera (compile for AI Thinker ESP32-CAM)
 
 *  Functionality:
 *  - Read initial fli3d system settings from SD card
 *  - Acquire imagery from camera 
 *  - Store imagery on SD card 
 *  - Transmit imagery in real-time through wifi 
 *  - Allow commanding of fli3d system via web interface
 *  - Transmission of status information over serial port to ESP32-minikit
 *  - Acquisition of status information over serial port from ESP32-minikit
 *  - Storage of all status information on SD card
 *  - Transmission of all status information over wifi
 */

/*  This sketch is based on https://hackaday.io/project/168563-esp32-cam-example-expanded, 
 *  an extension/expansion/rework of the 'official' ESP32 Camera example sketch from Expressif:
 *  https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer
*/

// TODO: images corrupted when auto-resolution is on (even with unchanged resolution)
// TODO: tune image
// TODO: post-flight emergency mode: Access SD data over FTP/wifi before battery runs out (FTP access to SD instead of FS?)
// TODO: based on tm_motion_t, void motion_imageSmear () { // TODO: when to send, to be most effective?  on demand?
// provides to camera the current expected image smear, in deg/s
//  sprintf (buffer, "{\"cmd\":\"set_smear\",\"trans\":%.2f,\"axial\":%.2f}", sqrt (mpu6050.gyro_y*mpu6050.gyro_y+mpu6050.gyro_z*mpu6050.gyro_z), mpu6050.gyro_x);
//  publish_event (TC_ESP32CAM, SS_OV2640, EVENT_CMD, buffer);//
//}


// Set versioning
#define SW_VERSION "Fli3d ESP32cam v2.0.2 (20220721)"
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
  Serial.setDebugOutput (true);
  ov2640.camera_mode = CAM_INIT;

  // Load default (hardcoded) WiFi and other settings, as fallback
  load_default_config (); 
  ccsds_init ();

  // If FS enabled and initialization successful, load WiFi and other settings from configuration files on FS (accessible over FTP)
  if (config_this->fs_enable) {
    if (tm_this->fs_enabled = fs_setup ()) {
      fs_load_settings (); 
      fs_load_config (config_this->config_file);
      fs_load_routing (config_this->routing_file);
    }
  }
  #ifdef DEBUG_OVER_SERIAL  
  config_this->debug_over_serial = true;
  tm_this->serial_connected = true;
  #endif // DEBUG_OVER_SERIAL
  sprintf (buffer, "%s started on %s", SW_VERSION, subsystemName[SS_THIS]); 
  publish_event (STS_THIS, SS_THIS, EVENT_INIT, buffer);
  publish_packet ((ccsds_t*)tm_this);  // #1

  // If WiFi enabled (AP/client), initialise
  if (config_this->wifi_enable) {
    if (tm_this->wifi_enabled = wifi_setup ()) {
      esp32cam.wifi_image_enabled = config_esp32cam.wifi_image_enable;
      esp32cam.wifi_udp_enabled = config_esp32cam.wifi_udp_enable;
      esp32cam.wifi_yamcs_enabled = config_esp32cam.wifi_yamcs_enable;  
      publish_packet ((ccsds_t*)tm_this);  // #2
    }
  }

  // If SD enabled, initialise
  if (config_esp32cam.sd_enable) {
    if (esp32cam.sd_enabled = sd_setup ()) {
      esp32cam.sd_image_enabled = config_esp32cam.sd_image_enable;
      esp32cam.sd_json_enabled = config_esp32cam.sd_json_enable;
      esp32cam.sd_ccsds_enabled = config_esp32cam.sd_ccsds_enable;
      publish_packet ((ccsds_t*)tm_this);  // #3
    }
  }  

  // If Camera enabled, initialise
  if (esp32cam.camera_enabled = camera_setup () and esp32cam.wifi_enabled) {
    camera_server_setup ();
    publish_packet ((ccsds_t*)tm_this);  // #4
  }

  // Initialise FTP server // TODO: what about FTP server on SD?
  if (esp32cam.fs_enabled) {
    ftp_setup ();
    publish_packet ((ccsds_t*)tm_this);  // #5
  }

  // Initialise Timer and close initialisation
  timer_setup ();
  if (esp32cam.sd_enabled) { // do this now only, because highest chance that NTP time was set
    sd_create_imagedir ();
  }
  ov2640.camera_mode = CAM_IDLE;
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
  if ((esp32.opsmode == MODE_INIT or esp32.opsmode == MODE_CHECKOUT or esp32.opsmode == MODE_DONE) and tm_this->fs_ftp_enabled) {
    // FTP server is active when Fli3d is being prepared or done (or no data from ESP32)
    start_millis = millis ();    
    ftp_check ();
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
