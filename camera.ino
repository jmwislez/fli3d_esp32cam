/*
 * Fli3d Camera - camera functionality
 * 
 * NOTE: flash led and SD card both use HS2_DATA1/GPIO4 line, so we don't try to control the flash led, though it will flash when accessing the SD
 */
 
// Set camera board and module
#define CAMERA_MODEL_AI_THINKER
#define CAMERA_MODULE_OV2640

#define CAM_H_ANGLE 27
#define SMEAR_ANGLE 20
#define SMEAR_PIX 3

#include "esp_camera.h"
#include "camera_pins.h"
#include "math.h"

// smear in deg/pixel for all camera resolutions
const float smear[11] = {2.0*CAM_H_ANGLE/160.0,   // mode 0, 160x120
                         2.0*CAM_H_ANGLE/240.0,   // mode 1, 240x176; dummy mode
                         2.0*CAM_H_ANGLE/240.0,   // mode 2, 240x176; dummy mode
                         2.0*CAM_H_ANGLE/240.0,   // mode 3, 240x176
                         2.0*CAM_H_ANGLE/320.0,   // mode 4, 320x240
                         2.0*CAM_H_ANGLE/400.0,   // mode 5, 400x300
                         2.0*CAM_H_ANGLE/640.0,   // mode 6, 640x480
                         2.0*CAM_H_ANGLE/800.0,   // mode 7, 800x600
                         2.0*CAM_H_ANGLE/1024.0,  // mode 8, 1024x768
                         2.0*CAM_H_ANGLE/1280.0,  // mode 9, 1280x1024
                         2.0*CAM_H_ANGLE/1600.0}; // mode 10, 1600x1200

bool camera_setup() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }  

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    sprintf(buffer, "Camera init failed with error 0x%x", err);
    publish_event (STS_ESP32CAM, SS_OV2640, EVENT_ERROR, buffer);
    return false;
  }

  sensor_t * s = esp_camera_sensor_get();
  ov2640.resolution = s->status.framesize;
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_INIT, "OV2640 camera initialized");
  return true;
}

void set_camera_resolution (float transRotation, float axialRotation) {
  // transRotation: deg/s transversal to camera axis
  // axialRotation: deg/s axial with camera axis
  // exposureTime: ms
  
  static float transSmear, axialSmear; // deg
  sensor_t * s = esp_camera_sensor_get();
  uint16_t  exposureTime = s->status.aec_value;

  transSmear = transRotation * exposureTime/1000;
  axialSmear = SMEAR_ANGLE * sin (axialRotation*M_PI/180.0 * exposureTime/1000);

  // scan for biggest image resolution with acceptable smear 
  ov2640.resolution = RES_1600x1200;
  while ((transSmear / smear[ov2640.resolution] > SMEAR_PIX or axialSmear / smear[ov2640.resolution] > SMEAR_PIX) and ov2640.resolution > RES_160x120) {
    ov2640.resolution--;
  }
  s->set_framesize(s, (framesize_t)ov2640.resolution);
}
