# BOVE Water Meter IoT System - Integrated Solution

## 📋 Project Overview

This project integrates real Modbus RTU water meter readings with cloud connectivity, combining two previously separate systems into a unified IoT solution. The ESP32 reads data from a BOVE ultrasonic water meter via Modbus RTU protocol and transmits telemetry to ThingsBoard cloud platform via MQTT over WiFi.

### System Architecture

```
┌──────────────┐    Modbus RTU      ┌─────────────┐    WiFi    ┌──────────────┐
│  BOVE Meter  │ ◄─────────────────► │    ESP32    │ ◄────────► │    Router    │
│  (RS485)     │   2400 baud, 8E1    │   (Zephyr)  │            └──────────────┘
└──────────────┘                     └─────────────┘                   │
                                                                 Internet
                                                                       │
                                                                       ▼
                                                        ┌──────────────────────────┐
                                                        │  ThingsBoard Cloud       │
                                                        │  (MQTT Broker)           │
                                                        └──────────────────────────┘
```

---

## ✨ Key Features

### Hardware Communication
- **Modbus RTU Protocol**: Function Code 0x03 (Read Holding Registers)
- **Serial Configuration**: 2400 baud, 8 data bits, Even parity, 1 stop bit (8E1)
- **CRC16 Validation**: Ensures data integrity
- **Dynamic UART Switching**: Console ↔ Modbus modes

### Network Connectivity
- **WiFi 2.4GHz**: Automatic connection with reconnection handling
- **MQTT Protocol**: QoS 1 (At Least Once) delivery
- **DNS Resolution**: Automatic broker hostname resolution
- **Connection Monitoring**: Real-time status tracking

### Data Processing
- **Real-time Measurements**:
  - Flow rate (L/h)
  - Forward total volume (m³)
  - Reverse total volume (m³)
  - Pressure (MPa)
  - Temperature (°C)
  - Status flags (leak, empty pipe, low battery)
  - Serial number
  - Modbus ID and baud rate

### Cloud Integration
- **Telemetry Transmission**: JSON-formatted sensor data every 30 seconds
- **Device Attributes**: Firmware version, model, serial number
- **Error Handling**: Automatic reconnection on failure

---

## 🛠️ Hardware Requirements

### Primary Components
- **ESP32 Development Board** (ESP32-DevKitC or compatible)
- **BOVE Ultrasonic Water Meter** (Modbus RTU compatible)
- **RS485 to TTL Converter Module** (MAX485 or similar)

### Connections

```
ESP32 DevKit-C          RS485 Module          BOVE Meter
─────────────           ────────────          ──────────
GPIO1 (TX0) ───────────► DI                   
GPIO3 (RX0) ◄─────────── RO                   
                        A  ◄──────────────────► A
                        B  ◄──────────────────► B
GND ────────────────────► GND                  GND
3.3V ───────────────────► VCC                  
```

**Important Notes**:
- Use a properly isolated RS485 module
- Ensure correct A/B wiring (swap if communication fails)
- Meter must be configured for 2400 baud, 8E1, Modbus ID = 1

---

## 💻 Software Requirements

### Development Environment
- **Zephyr SDK** v0.16.1 or later
- **ESP-IDF Toolchain** (Xtensa)
- **West Build Tool**
- **Python 3.8+**
- **CMake 3.20+**

### Zephyr Modules Required
```bash
- Zephyr Kernel (v3.x+)
- WiFi Driver (ESP32)
- MQTT Client Library
- Network Stack (TCP/IP, sockets)
- Logging Subsystem
```

---

## 📦 Installation & Setup

### 1. Install Zephyr Environment

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-pip \
  python3-setuptools python3-wheel xz-utils file make gcc gcc-multilib \
  g++-multilib libsdl2-dev

# Install West
pip3 install --user -U west

# Create workspace
mkdir ~/zephyrproject
cd ~/zephyrproject

# Initialize Zephyr
west init -m https://github.com/zephyrproject-rtos/zephyr
west update
pip3 install -r zephyr/scripts/requirements.txt
```

### 2. Install Zephyr SDK

```bash
# Download SDK (check for latest version)
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.1/zephyr-sdk-0.16.1_linux-x86_64.tar.xz

# Extract
tar -xvf zephyr-sdk-0.16.1_linux-x86_64.tar.xz -C ~/

# Run setup
cd ~/zephyr-sdk-0.16.1
./setup.sh

# Add to environment
echo 'export ZEPHYR_TOOLCHAIN_VARIANT=zephyr' >> ~/.bashrc
echo 'export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-0.16.1' >> ~/.bashrc
source ~/.bashrc
```

### 3. Configure Project

Create project directory:
```bash
cd ~/zephyrproject
mkdir -p water_meter_iot/src
cd water_meter_iot
```

Copy the following files:
- `src/main.c` - Main application code
- `prj.conf` - Project configuration
- `CMakeLists.txt` - Build configuration
- `esp32_devkitc.overlay` - Device tree overlay

### 4. Configure WiFi and ThingsBoard

Edit `src/main.c` and update:

```c
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PSK "YOUR_WIFI_PASSWORD"
#define ACCESS_TOKEN "YOUR_THINGSBOARD_TOKEN"
```

### 5. ThingsBoard Setup

1. **Create Account**: Sign up at [thingsboard.cloud](https://thingsboard.cloud)
2. **Add Device**: 
   - Go to "Devices" → "Add Device"
   - Name: "BOVE Water Meter"
   - Device Profile: Default
3. **Get Access Token**: 
   - Click on device → "Copy Access Token"
   - Paste into `ACCESS_TOKEN` in code
4. **Create Dashboard** (optional):
   - Add widgets for flow rate, volume, pressure, temperature
   - Configure telemetry keys: `flowRate`, `forwardTotal`, `pressure`, etc.

---

## 🔨 Building & Flashing

### Build the Project

```bash
cd ~/zephyrproject/water_meter_iot

# Build for ESP32
west build -b esp32_devkitc_wroom
```

### Flash to ESP32

```bash
# Connect ESP32 via USB
# Press and hold BOOT button while flashing (if needed)

west flash
```

### Monitor Serial Output

```bash
west espressif monitor
```

Or use any serial terminal:
```bash
minicom -D /dev/ttyUSB0 -b 115200
```

---

## 📊 Data Structure

### Telemetry (Published every 30 seconds)

```json
{
  "flowRate": 15.87,           // Current flow rate (L/h)
  "forwardTotal": 12.345,      // Forward total volume (m³)
  "reverseTotal": 0.000,       // Reverse total volume (m³)
  "pressure": 0.291,           // Pressure (MPa)
  "temperature": 27.15,        // Temperature (°C)
  "status": 0,                 // Status word (0 = normal)
  "leak": 0,                   // Leak detected (0/1)
  "empty": 0,                  // Empty pipe detected (0/1)
  "lowBattery": 0              // Low battery warning (0/1)
}
```

### Device Attributes (Published on startup)

```json
{
  "firmwareVersion": "2.0.0",
  "deviceModel": "BOVE-Modbus-Meter",
  "serialNumber": "12345678",
  "modbusId": 1,
  "baudRate": "2400"
}
```

### Status Flags

| Bit | Description |
|-----|-------------|
| 0x0004 | Empty pipe detected |
| 0x0020 | Low battery warning |

---

## 🔄 Operation Flow

### Startup Sequence

1. **System Initialization** (2s delay)
2. **UART Initialization**: Save console configuration
3. **WiFi Connection**:
   - Connect to SSID
   - Wait for CONNACK (max 30s)
   - Obtain IPv4 via DHCP (max 30s)
4. **DNS Resolution**: Resolve ThingsBoard hostname
5. **MQTT Connection**:
   - Connect to broker (5 retry attempts)
   - Wait for CONNACK (5s timeout per attempt)
6. **Ready State**: System operational

### Main Loop (Every 30 seconds)

```
┌─────────────────────────────┐
│  1. Switch to Modbus Mode   │
│     (2400 baud, 8E1)        │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  2. Send Modbus Read CMD    │
│     (Slave ID 1, FC 0x03)   │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  3. Receive Response        │
│     (Wait max 2 seconds)    │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  4. Switch to Console Mode  │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  5. Validate Response       │
│     - Check length          │
│     - Verify CRC16          │
│     - Check slave ID        │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  6. Parse Meter Data        │
│     - Flow rate             │
│     - Volume totals         │
│     - Pressure, temp        │
│     - Status flags          │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  7. Build JSON Payload      │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  8. Publish to ThingsBoard  │
│     (MQTT, QoS 1)           │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  9. MQTT Maintenance        │
│     - Process messages      │
│     - Send keepalive        │
└──────────┬──────────────────┘
           │
           ▼
┌─────────────────────────────┐
│  10. Wait 30 Seconds        │
└──────────┬──────────────────┘
           │
           └──────► Loop
```

---

## 🐛 Troubleshooting

### WiFi Issues

**Problem**: "WiFi connection timeout"
- **Check**: SSID and password correct
- **Check**: 2.4GHz band enabled on router
- **Check**: ESP32 within WiFi range
- **Try**: Reboot router and ESP32

**Problem**: "IPv4 acquisition timeout"
- **Check**: DHCP enabled on router
- **Try**: Assign static IP in code

### MQTT Issues

**Problem**: "MQTT connection refused"
- **Check**: ACCESS_TOKEN is correct
- **Check**: Device exists in ThingsBoard
- **Check**: Firewall allows port 1883
- **Try**: Use IP address instead of hostname

**Problem**: "DNS resolution failed"
- **Check**: Router DNS settings
- **Check**: Internet connectivity
- **Try**: Use broker IP (e.g., `"167.99.141.104"`)

### Modbus Issues

**Problem**: "Incomplete response" or "No response"
- **Check**: RS485 A/B wiring correct
- **Check**: Meter powered and operational
- **Check**: Baud rate matches (2400)
- **Try**: Swap A and B connections
- **Try**: Check with Modbus simulator first

**Problem**: "CRC error"
- **Check**: Cable quality and length (<100m recommended)
- **Check**: Proper termination resistors (120Ω)
- **Check**: Grounding and shielding
- **Try**: Reduce baud rate (slower = more reliable)

**Problem**: "Invalid response header"
- **Check**: Modbus ID matches (default = 1)
- **Try**: Use Modbus scanner to find ID

### General Issues

**Problem**: Build fails
```bash
# Clean build directory
rm -rf build
# Rebuild
west build -b esp32_devkitc_wroom
```

**Problem**: Flash fails
```bash
# Hold BOOT button on ESP32
# Press RESET button
# Release RESET, keep BOOT pressed
# Run: west flash
# Release BOOT when flashing starts
```

---

## 📈 Performance Metrics

| Metric | Value |
|--------|-------|
| WiFi Connection Time | 2-5 seconds |
| MQTT Connection Time | 1-3 seconds |
| Modbus Read Time | 200-500 ms |
| Telemetry Interval | 30 seconds |
| Message Size | ~200 bytes (JSON) |
| Network Bandwidth | ~7 bytes/second average |
| Memory Usage (RAM) | ~8 KB |
| Flash Usage | ~1.2 MB |

---

## 🔐 Security Recommendations

### Current Implementation (Non-Secure)
- Plain TCP connection (port 1883)
- No encryption
- Token-based authentication

### Recommended Improvements

1. **Enable TLS/SSL**:
```c
#define THINGSBOARD_PORT 8883
client.transport.type = MQTT_TRANSPORT_SECURE;
```

2. **Use X.509 Certificates**: Replace token with certificate authentication

3. **Secure WiFi**: Use WPA3 if available

4. **Credential Storage**: Use secure flash storage for tokens

5. **Network Isolation**: Place meter system on separate VLAN

---

## 🚀 Future Enhancements

### Hardware
- [ ] Add external EEPROM for local data storage
- [ ] Implement multiple meter support (multi-drop Modbus)
- [ ] Add OLED display for local readout
- [ ] Battery backup for power failure handling

### Software
- [ ] OTA firmware updates
- [ ] Historical data buffering
- [ ] Configurable telemetry intervals
- [ ] Remote configuration via MQTT
- [ ] Alarm threshold notifications
- [ ] Daily/weekly consumption reports

### Analytics
- [ ] Leak pattern detection algorithms
- [ ] Predictive maintenance
- [ ] Consumption forecasting
- [ ] Anomaly detection

---

## 📝 Modbus Register Map (BOVE Meter)

| Register | Type | Description | Unit | Scale |
|----------|------|-------------|------|-------|
| 1-2 | UINT32 | Instantaneous Flow | L/h | ×100 |
| 3-4 | UINT32 | Positive Accumulated | m³ | ×1000 |
| 5-6 | UINT32 | Positive Accumulated | L | ×1 |
| 7-8 | UINT32 | Forward Total | m³ | ×1000 |
| 9 | UINT16 | Net Total | m³ | ×1000 |
| 10-11 | UINT32 | Reverse Total | m³ | ×1000 |
| 19 | UINT16 | Pressure | MPa | ×1000 |
| 20 | UINT16 | Status | - | - |
| 30 | UINT16 | Temperature | °C | ×100 |
| 33-34 | UINT32 | Serial Number | BCD | - |
| 35 | UINT16 | Modbus ID | - | - |
| 37 | UINT16 | Baud Rate Code | - | 0=9600, 1=2400, 2=4800, 3=1200 |

---

## 📄 License

SPDX-License-Identifier: Apache-2.0

---

## 👥 Support & Contact

### Documentation Resources
- [Zephyr Project](https://docs.zephyrproject.org)
- [ThingsBoard](https://thingsboard.io/docs)
- [ESP32 Technical Reference](https://www.espressif.com/en/support/documents/technical-documents)
- [Modbus Protocol](https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)

### Troubleshooting Tips
1. Always check serial monitor output first
2. Verify hardware connections with multimeter
3. Test Modbus communication separately before WiFi
4. Use ThingsBoard device logs for MQTT debugging

---

## 📊 Serial Monitor Output Example

```
*** Booting Zephyr OS build v3.5.0 ***
========================================
  BOVE WATER METER IoT SYSTEM
  Version: 2.0.0
========================================
Console UART: 115200 baud
Initializing WiFi connection...
WiFi connected
IPv4 address obtained
WiFi connected successfully
Resolving broker: thingsboard.cloud
Broker resolved successfully
Connecting to ThingsBoard...
MQTT connected
ThingsBoard connected successfully
========================================
System operational - Starting data loop
========================================
Reading meter data...
Meter data read successfully
========================================
METER DATA:
  Flow Rate   : 15.87 L/h
  Forward Flow: 12.345 m³
  Reverse Flow: 0.000 m³
  Pressure    : 0.291 MPa
  Temperature : 27.15 °C
  Status      : 0x0000 (Normal)
========================================
Attributes: {"firmwareVersion":"2.0.0","deviceModel":"BOVE-Modbus-Meter",...}
Telemetry: {"flowRate":15.87,"forwardTotal":12.345,"reverseTotal":0.000,...}
Telemetry published successfully
Waiting 30 seconds...
```

---

**Document Version**: 2.0.0  
**Last Updated**: November 29, 2025  
**Author**: AMR ALI
