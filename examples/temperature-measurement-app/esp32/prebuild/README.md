# Temperature Measurement Application for ESP32-C3

This repository contains the temperature measurement application for the ESP32-C3 chip. The following instructions guide you through setting up, flashing, and using the application, as well as retrieving and parsing diagnostic data.

---

## Getting Started

### Prerequisites
- **ESP-IDF**: Ensure ESP-IDF environment is configured for ESP32-C3.
- **esptool.py**: Included with ESP-IDF, used for flashing.
- **chip-tool**: Compiled from the Matter project.
- **Wi-Fi credentials**: Required for device commissioning.

---

## Steps to Flash and Monitor the Application

### 1. Clone the Repository
```
git clone https://github.com/project-chip/connectedhomeip
cd /path/to/connectedhomeip/examples/temperature-measurement-app/esp32
```

### 2. Navigate to the Prebuild Directory
```
cd prebuild/
```

### 3. Flash the Binary File to the ESP32-C3
Connect your ESP32-C3 to your computer via USB, then run:

```
esptool.py --port /dev/ttyUSB0 write_flash 0x0 temperature_measurement_app_esp32c3.bin
```

### 4. Start Monitoring

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
./chip-tool pairing ble-wifi {node_id} {wifi_ssid} {wifi_password} 20202021 3840
```

Replace {node_id}, {wifi_ssid}, and {wifi_password} with your actual values.

3. Start Interactive Chip-Tool

Launch interactive mode:

```
./chip-tool interactive start
```

4. Retrieve Diagnostic Data

Use this command to read diagnostic logs:

```
diagnosticlogs retrieve-logs-request 0 1 {node_id} 0 --TransferFileDesignator {filename}
```

Replace {node_id} and {filename}. The file will be saved in /tmp.

### Parsing Diagnostic Data

Navigate to the tools/ directory in temperature-measurement-app to parse diagnostics:

```
cd temperature-measurement-app/tools/
```

For details on running the diagnostic parser, refer to the README.md in the tools/ directory.