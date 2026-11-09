The hardware is based on the ESP32S3CAM development board; the code is adapted from bread-compact-wifi-lcd.
The camera used is the OV2640.
Note: because the camera uses many IO pins, the ESP32S3 USB pins 19 and 20 are also used.
For wiring, refer to the pin definitions in config.h.

 
# Build Configuration Commands

**Set the build target to ESP32S3:**

```bash
idf.py set-target esp32s3
```

**Open menuconfig:**

```bash
idf.py menuconfig
```

**Select the board:**

```bash
Xiaozhi Assistant -> Board Type -> Bread Compact Wi-Fi + LCD + Camera
```

**Build and flash:**

```bash
idf.py build flash
```
