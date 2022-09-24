/*
 * Fli3d Camera - TM acquisition timer functionality 
 */

extern bool sync_fs_ccsds();
extern File file_ccsds;
extern File file_json;
 
void timer_setup() {
    var_timer.next_second = 1000*(millis()/1000) + 1000;
    var_timer.camera_interval = (1000 / config_esp32cam.camera_rate);
}

void timer_loop() {
  // data acquisition and publication timer
  timer_esp32cam.millis = millis();
  if (timer_esp32cam.millis >= var_timer.next_second) {
    publish_packet ((ccsds_t*)&esp32cam);
    publish_packet ((ccsds_t*)&timer_esp32cam);
    if (file_ccsds) { sync_file_ccsds(); }
    if (file_json) { sync_file_json(); }
    var_timer.next_second += 1000;
  }
  if (tm_this->opsmode != MODE_MAINTENANCE) {
    if (timer_esp32cam.millis >= var_timer.next_camera_time) {
        var_timer.do_camera = true;
        var_timer.next_camera_time = timer_esp32cam.millis + var_timer.camera_interval;
      }
    if (timer_esp32cam.millis >= var_timer.next_wifi_time) {
      var_timer.do_wifi = true;
      var_timer.next_wifi_time = timer_esp32cam.millis + WIFI_CHECK*1000;
    } 
    if (!tm_this->time_set and timer_esp32cam.millis >= var_timer.next_ntp_time) {
      var_timer.do_ntp = true;
      var_timer.next_ntp_time = timer_esp32cam.millis + NTP_CHECK*1000;
    }
  }
}
