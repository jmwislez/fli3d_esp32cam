/*
 * Fli3d Camera - TM acquisition timer functionality 
 */

extern bool sync_fs_ccsds ();
 
void timer_setup () {
    var_timer.next_second = 1000*(millis()/1000) + 1000;
}

void timer_loop () {
  // data acquisition and publication timer
  timer_esp32cam.millis = millis ();
  if (timer_esp32cam.millis >= var_timer.next_second) {
    publish_packet ((ccsds_t*)&esp32cam);
    sync_fs_ccsds ();
    var_timer.next_second += 1000;
  }
  if (timer_esp32cam.millis >= var_timer.next_wifi_time) {
    var_timer.do_wifi = true;
    var_timer.next_wifi_time = timer_esp32cam.millis + WIFI_CHECK*1000;
  } 
  if (timer_esp32cam.millis >= var_timer.next_ntp_time) {
    var_timer.do_ntp = true;
    var_timer.next_ntp_time = timer_esp32cam.millis + NTP_CHECK*1000;
  }
}
