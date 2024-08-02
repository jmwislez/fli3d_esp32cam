# fli3d_ESP32cam

To compile in the Arduino IDE, ESP32 core v1.0.6/2.0.17/3.0.3, for board "AI-THINKER ESP32-CAM", with partitioning scheme "Minimal SPIFFS". If no partitioning schemes are available, add the following lines to `C:\Users\[YOURNAME]\Documents\ArduinoData\packages\esp32\hardware\esp32\1.0.6\boards.txt`:

`esp32cam.menu.PartitionScheme.min_spiffs=Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`
`esp32cam.menu.PartitionScheme.min_spiffs.build.partitions=min_spiffs`
`esp32cam.menu.PartitionScheme.min_spiffs.upload.maximum_size=1966080`

For the libraries needed to compile, see https://github.com/jmwislez/fli3d_lib

For the physical configuration and connections, see https://github.com/jmwislez/fli3d

## Inheritance 

This sketch is heavily based on https://hackaday.io/project/168563-esp32-cam-example-expanded, 
 *  an extension/expansion/rework of the 'official' ESP32 Camera example sketch from Expressif:
 *  https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/Camera/CameraWebServer

## Flashing

* The AI-THINKER board requires use of an external **3.3v** serial adapter (like FTDI) to program. Be careful not to use a 5v serial adapter since this will damage the ESP32.
* Connect the **RX** line from the serial adapter to the **TX** pin on ESP32
* The adapters **TX** line goes to the ESP32 **RX** pin
* The **GPIO0** pin of the ESP32 must be held LOW (to ground) when the unit is powered up to allow it to enter it's programming mode. This can be done with simple jumper cable connected at poweron, fitting a switch for this is useful if you will be reprogramming a lot.
* You must supply 5v to the ESP32 in order to power it during programming, the FTDI board can supply this.
