/* 
 *  Fli3d - camera and storage functionality
 *  
 *  For ESP32CAM board woth OV2640 camera (compile for AI Thinker ESP32-CAM)
 
 *  Functionality:
 *  - Read initial fli3d system settings from SD card
 *  - Acquiring imagery from camera 
 *  - Storing imagery on SD card 
 *  - Transmitting imagery in real-time through wifi 
 *  - Allowing commanding of fli3d system via web interface
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
// TODO: post-flight emergency mode: dump all SD data over wifi before battery runs out
// TODO: based on tm_motion_t, void motion_imageSmear () { // TODO: when to send, to be most effective?  on demand?
  // provides to camera the current expected image smear, in deg/s
//  sprintf (buffer, "{\"cmd\":\"set_smear\",\"trans\":%.2f,\"axial\":%.2f}", sqrt (mpu6050.gyro_y*mpu6050.gyro_y+mpu6050.gyro_z*mpu6050.gyro_z), mpu6050.gyro_x);
//  publish_event (TC_ESP32CAM, SS_OV2640, EVENT_CMD, buffer);//
//}


// Set versioning
#define SW_VERSION "Fli3d ESP32cam v2.0.1 (20200409)"
#define PLATFORM_ESP32CAM // tell which platform we are one 

// Compilation options
//#define DEBUG_OVER_SERIAL // disable when ESP32 and ESP32CAM are to be connected

#include "fli3d.h"

// Functions declared in app_httpd.cpp
void camera_server_setup ();

// Global variables used in this file
extern char buffer[TM_MAX_MSG_SIZE], bus_buffer[TM_MAX_MSG_SIZE + 25];

void setup() {
  ov2640.camera_mode = CAM_INIT;
  Serial.begin (SerialBaud);
  Serial.println ();
  Serial.setDebugOutput (true);
  eeprom_setup (); 
  eeprom_load (EEPROM_NETWORK, 0);
  eeprom_load (EEPROM_THIS, sizeof(eeprom_network));
  sprintf (buffer, "%s started on %s", SW_VERSION, subsystemName[SS_THIS]); 
  bus_publish_event (STS_THIS, SS_THIS, EVENT_INIT, buffer);  
  bus_publish_pkt (TM_THIS);  
  if (eeprom_esp32cam.sd_enable) {
    if (esp32cam.sd_enabled = sd_setup ()) {
      sd_load_settings ();
      esp32cam.sd_image_enabled = eeprom_esp32cam.sd_image_enable;
      esp32cam.sd_json_enabled = eeprom_esp32cam.sd_json_enable;
      esp32cam.sd_ccsds_enabled = eeprom_esp32cam.sd_ccsds_enable;
      bus_publish_pkt (TM_THIS);  
    }
  }  
  if (eeprom_esp32cam.wifi_enable) {
    if (esp32cam.wifi_enabled = wifi_setup ()) {
      esp32cam.wifi_image_enabled = eeprom_esp32cam.wifi_image_enable;
      esp32cam.wifi_udp_enabled = eeprom_esp32cam.wifi_udp_enable;
      esp32cam.wifi_yamcs_enabled = eeprom_esp32cam.wifi_yamcs_enable;  
      bus_publish_pkt (TM_THIS);  
    }
  }
  if (esp32cam.camera_enabled = camera_setup () and esp32cam.wifi_enabled) {
    camera_server_setup ();
    bus_publish_pkt (TM_THIS);  
  }
  timer_setup ();
  if (esp32cam.sd_enabled) { // do this now only, because highest chance that NTP time was set
    sd_create_imagedir ();
  }
  ov2640.camera_mode = CAM_IDLE;
  bus_publish_event (STS_THIS, SS_THIS, EVENT_INIT, "Initialisation complete");  
}

void loop() {
  static uint32_t start_millis;
  static uint8_t data_len;
  // timer
  timer_loop ();
  if (data_len = serial_check ()) {
    if (esp32cam.timer_debug) { start_millis = millis (); }
    serial_parse (data_len);
    if (esp32cam.timer_debug) { timer.esp32cam_duration = min((uint32_t)255, (uint32_t)millis() - start_millis); }
  } 
}
