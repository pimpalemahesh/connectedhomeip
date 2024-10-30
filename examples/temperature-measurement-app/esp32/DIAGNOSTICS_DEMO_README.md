# Follow these steps to flash and monitor binary as well as parse diagnostics data obtained through chip-tool

## Steps to flash and monitor app bin

1. Navigate to prebuild directory and flash binary
```
cd prebuild/
esptool.py --port /dev/ttyUSB0 write_flash 0x0 temperature_measurement_app_esp32c3.bin
```

2. Monitor using idf
```
cd ..
idf.py monitor
```

## Steps to read data using chip-tool

1. Open new terminal and navigate to chip-tool directory
```
cd path/to/chip-tool
```

2. Commission device using chip-tool
```
./chip-tool pairing ble-wifi 1 {wifi_ssid} {wifi_password} 20202021 3840
```

3. Start chip-tool in interactive mode
```
./chip-tool interactive start
```

4. Run below command in interactive session
```
diagnosticlogs retrieve-logs-request 0 1 1 0 --TransferFileDesignator user.log
```
### ⚠️ Important Note
**Make sure you provide different file name for each retrieve logs request or delete existing user.log file from /tmp directory.**

## Steps to parse diagnostic data

1. Open new terminal and navigate to tools directory
```
cd tools/
```

2. Export path of the connectedhomeip repo
```
export CHIP_HOME=path/to/connectedhomeip/
```

3. Give the script executable permission:
```
chmod +x tlv_diagnostic_parser.py
```

4. Run the parser application:
```
./tlv_diagnostic_parser.py
```
## Usage

1. Enter binary file path in input text box and click on Parse button. for ex. "/tmp/user.log"

2. The parsed data will be displayed in a structured table format under different sections for Traces, Metrics and Counters.
