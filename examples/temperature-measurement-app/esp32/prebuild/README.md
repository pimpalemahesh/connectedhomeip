# Pre Build Temperature Measurement Application Binary for ESP32-C3

This repository contains the temperature measurement application binary for the ESP32-C3 chip. The following instructions guide you through setting up, flashing, and using the application, as well as retrieving and parsing diagnostic data.

## Steps to Flash and Monitor the Application

### 1. Flash the Binary File to the ESP32-C3
Connect your ESP32-C3 to your computer via USB, then run:

```
esptool.py --port {chip_port} write_flash 0x0 temperature_measurement_app_esp32c3.bin
```
{chip_port} will be actual port where esp32c3 chip is connected.
for ex. /dev/ttyUSB0

### 2. Start Monitoring

Return to the main directory and start the ESP-IDF monitor to view logs:

```
cd ../
idf.py monitor
```

### Commissioning Using the Chip-Tool
1. Navigate to the Chip-Tool Directory

```
cd /path/to/chip-tool
```

2. Commission the Device

Run this command to connect your ESP32-C3 to Wi-Fi and start commissioning:

```
./chip-tool pairing ble-wifi 1 {wifi_ssid} {wifi_password} 20202021 3840
```

Replace {wifi_ssid}, and {wifi_password} with your actual values.

3. Start Interactive Chip-Tool

Launch interactive mode:

```
./chip-tool interactive start
```

4. Retrieve Diagnostic Data

Use this command to read diagnostic logs:

```
diagnosticlogs retrieve-logs-request 0 1 1 0 --TransferFileDesignator user.log
```

### Parsing Diagnostic Data

Navigate to the tools/ directory in temperature-measurement-app to parse diagnostics:

```
cd temperature-measurement-app/tools/
```

For details on running the diagnostic parser, refer to the README.md in the tools/ directory.