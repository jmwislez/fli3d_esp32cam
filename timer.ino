/*
 * Fli3d - TM acquisition timer functionality 
 */
 
uint32_t now, next_second;
uint32_t last_serial_in_millis, last_serial_out_millis, last_wifi_check;

void timer_setup () {
    next_second = 1000*(millis()/1000) + 1000;
}

void timer_loop () {
  // timer
  now = millis ();
  esp32cam.timer_rate++;
  if (now >= next_second) {
    bus_publish_pkt (TM_ESP32CAM);
    next_second += 1000;
  }
  // Serial keepalive mechanism
  if (!eeprom_this->debug_over_serial and now - last_serial_out_millis > KEEPALIVE_INTERVAL) {
    if (tm_this->serial_connected) {
      Serial.println ("O");
    }
    else {
      Serial.println ("o");
    }
    last_serial_out_millis = millis();
  }
  if (!eeprom_this->debug_over_serial and tm_this->serial_connected and millis()-last_serial_in_millis > 2*KEEPALIVE_INTERVAL) {
    sprintf (buffer, "Lost serial connection to %s", subsystemName[SS_OTHER]);
    bus_publish_event (STS_THIS, SS_OTHER, EVENT_WARNING, buffer);
    tm_this->serial_connected = false;  
  }
  // Wifi monitoring mechanism
  if (now - last_wifi_check) {
    if (WiFi.status() != WL_CONNECTED) {
      if (tm_this->wifi_connected) {
        sprintf (buffer, "Wifi connection on %s lost", subsystemName[SS_THIS]);
        bus_publish_event (STS_THIS, SS_THIS_WIFI, EVENT_WARNING, buffer);
        tm_this->wifi_connected = false;
      }
    }
    else {
      if (!tm_this->wifi_connected) {
        sprintf (buffer, "Wifi connection on %s recovered", subsystemName[SS_THIS]);
        bus_publish_event (STS_THIS, SS_THIS_WIFI, EVENT_INIT, buffer);
        tm_this->wifi_connected = true;
      }
    }
    last_wifi_check = millis();
  }
}
  
