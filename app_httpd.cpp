/*
 * Fli3d Camera - web server and camera control functionality
 */

// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp_http_server.h"
#include "esp_camera.h"
#include "Arduino.h"
#include "camera_index_ov2640.h"
#include "fli3d.h"

// Defined in other tabs
extern char buffer[JSON_MAX_SIZE];
float transRot, axialRot;
void sd_save_image (const uint8_t*, size_t);
uint16_t set_camera_resolution (float, float);

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

static esp_err_t capture_handler(httpd_req_t *req) {
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_ACK, "Command 'capture' received over web interface");
  static uint32_t timestamp_send;
  ov2640.camera_mode = CAM_SINGLE;

  // grab single frame
  //if (ov2640.auto_res) { set_camera_resolution(transRot, axialRot); } // TODO: solve bug
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  ov2640.millis = millis ();
  fb = esp_camera_fb_get();
  sensor_t * s = esp_camera_sensor_get();
  esp32cam.camera_rate++;
  if (!fb) {
    publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_FAIL, "Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  size_t out_len, out_width, out_height;
  uint8_t * out_buf;

  size_t fb_len = 0;
  if (fb->format == PIXFORMAT_JPEG) {
    if (esp32cam.wifi_image_enabled) {
      timestamp_send = millis();
      fb_len = fb->len;
      res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
      ov2640.wifi_ms = millis()-timestamp_send;
      ov2640.exposure_ms = s->status.aec_value;
    }
    sd_save_image ((const uint8_t *)fb->buf, fb->len);
  } else {
    publish_event (STS_ESP32CAM, SS_OV2640, EVENT_ERROR, "Grabbed picture is no jpeg");
  }
  
  esp_camera_fb_return(fb);
  publish_packet ((ccsds_t*)&ov2640);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  static uint32_t timestamp_send;
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_ACK, "Command 'start stream' received over web interface");
  ov2640.camera_mode = CAM_STREAM;

  // continuously grab frames
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while(true) {
    ov2640.camera_mode = CAM_STREAM;
    //if (ov2640.auto_res) { set_camera_resolution(transRot, axialRot); }
    ov2640.millis = millis();
    fb = esp_camera_fb_get();
    sensor_t * s = esp_camera_sensor_get();
    esp32cam.camera_rate++;
    if (!fb) {
      publish_event (STS_ESP32CAM, SS_OV2640, EVENT_ERROR, "Camera capture failed");
      res = ESP_FAIL;
    } 
    else {
      if (fb->format == PIXFORMAT_JPEG) {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
      else {
        publish_event (STS_ESP32CAM, SS_OV2640, EVENT_ERROR, "Grabbed picture is no jpeg");
      }
    }
    if (esp32cam.wifi_image_enabled) {
      timestamp_send = millis();
      if (res == ESP_OK) {
        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
        res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
      }
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
      }
      ov2640.wifi_ms = millis()-timestamp_send;
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    ov2640.exposure_ms = s->status.aec_value;
    sd_save_image ((const uint8_t *)fb->buf, fb->len);
    publish_packet ((ccsds_t*)&ov2640);
    
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      ov2640.camera_mode = CAM_IDLE;
      publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_ACK, "Command 'stop stream' received over web interface");
      break;
    }
  }
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  char value[32] = {0,};

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
      } else {
        free(buf);
        httpd_resp_send_404(req);
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }
    free(buf);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  int val = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize")) {
    if (s->pixformat == PIXFORMAT_JPEG) {
      res = s->set_framesize(s, (framesize_t)val);
      ov2640.resolution = val;
    }
  }
  else if (!strcmp(variable, "quality")) res = s->set_quality(s, val);
  else if (!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
  else if (!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
  else if (!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
  else if (!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
  else if (!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
  else if (!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
  else if (!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
  else if (!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
  else if (!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
  else if (!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
  else if (!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
  else if (!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
  else if (!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
  else if (!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
  else if (!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
  else if (!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
  else if (!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
  else if (!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
  else if (!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
  else if (!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
  else {
    res = -1;
    sprintf (buffer, "Command not understood (%s::%d)", variable, val);
    publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_FAIL, buffer);
  }
  if (res) {
    sprintf (buffer, "Command execution failed (%s::%d)", variable, val);
    publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_FAIL, buffer);
    return httpd_resp_send_500(req);
  }

  sprintf (buffer, "Command '%s::%d' executed successfully", variable, val);
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_RESP, buffer);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req) {
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_ACK, "Command 'status' received over web interface");
  static char json_response[1024];
  sensor_t * s = esp_camera_sensor_get();
  char * p = json_response;
  *p++ = '{';
  p+=sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p+=sprintf(p, "\"quality\":%u,", s->status.quality);
  p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p+=sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p+=sprintf(p, "\"awb\":%u,", s->status.awb);
  p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p+=sprintf(p, "\"aec\":%u,", s->status.aec);
  p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p+=sprintf(p, "\"agc\":%u,", s->status.agc);
  p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p+=sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p+=sprintf(p, "\"colorbar\":%u", s->status.colorbar);
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t index_handler(httpd_req_t *req) {
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_CMD_ACK, "Command 'index' received over web interface");
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "identity");
  sensor_t * s = esp_camera_sensor_get();
  return httpd_resp_send(req, (const char *)index_ov2640_html, index_ov2640_html_len);
}

void camera_server_setup () {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri = {
      .uri       = "/",
      .method    = HTTP_GET,
      .handler   = index_handler,
      .user_ctx  = NULL
  };

  httpd_uri_t status_uri = {
      .uri       = "/status",
      .method    = HTTP_GET,
      .handler   = status_handler,
      .user_ctx  = NULL
  };

  httpd_uri_t cmd_uri = {
      .uri       = "/control",
      .method    = HTTP_GET,
      .handler   = cmd_handler,
      .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = {
      .uri       = "/capture",
      .method    = HTTP_GET,
      .handler   = capture_handler,
      .user_ctx  = NULL
  };

  httpd_uri_t stream_uri = {
      .uri       = "/stream",
      .method    = HTTP_GET,
      .handler   = stream_handler,
      .user_ctx  = NULL
  };

  sprintf (buffer, "Starting camera web server on %s:%d", config_esp32cam.server_ip, config.server_port);
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_INIT, buffer);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  sprintf (buffer, "Starting camera stream server on port %s:%d", config_esp32cam.server_ip, config.server_port);
  publish_event (STS_ESP32CAM, SS_OV2640, EVENT_INIT, buffer);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}
