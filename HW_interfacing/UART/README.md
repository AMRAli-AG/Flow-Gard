# ESP32 to ESP32 UART Communication

A simple project demonstrating serial communication between two ESP32 Wroom DevKit boards using UART2.

## 📋 Description

This project enables two ESP32 boards to communicate with each other using hardware serial (UART). The sender ESP32 transmits messages every second, while a receiver ESP32 (not included in this code) can read and process these messages.

## 🔧 Hardware Requirements

- 2x ESP32 Wroom DevKit boards
- Jumper wires (minimum 3: TX, RX, GND)
- USB cables for programming and monitoring

## 📌 Pin Configuration

### ESP32 Sender (This Code)
- **TX2** → GPIO17
- **RX2** → GPIO16
- **Baud Rate** → 9600
- **Configuration** → 8N1 (8 data bits, No parity, 1 stop bit)

## 🔌 Wiring Diagram

Connect the two ESP32 boards as follows:

```
ESP32 Sender          ESP32 Receiver
--------------        ----------------
GPIO17 (TX2)  ----→   GPIO16 (RX2)
GPIO16 (RX2)  ←----   GPIO17 (TX2)
GND           ----→   GND
```

**Important:** 
- Cross-connect TX to RX and RX to TX
- Always connect GND between both boards
- No level shifters needed (both are 3.3V devices)

## 🚀 Installation & Setup

### 1. Arduino IDE Setup
```bash
1. Install Arduino IDE (version 1.8.x or 2.x)
2. Add ESP32 board support:
   - Go to File → Preferences
   - Add to "Additional Board Manager URLs":
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   - Go to Tools → Board → Board Manager
   - Search for "ESP32" and install
```

### 2. Upload the Code
```bash
1. Open the sender code in Arduino IDE
2. Select board: Tools → Board → ESP32 Arduino → ESP32 Dev Module
3. Select correct COM port: Tools → Port
4. Click Upload button
```

### 3. Monitor Output
```bash
1. Open Serial Monitor: Tools → Serial Monitor
2. Set baud rate to 115200
3. You should see confirmation messages
```

## 💻 Code Explanation

### Key Components

**UART Initialization:**
```cpp
HardwareSerial MyUART(2); // Using UART2
MyUART.begin(9600, SERIAL_8N1, 16, 17); // RX=GPIO16, TX=GPIO17
```

**Message Transmission:**
```cpp
String message = "Hello from ESP32 Wroom!amrr";
MyUART.println(message); // Sends via UART2
```

**Debug Output:**
```cpp
Serial.begin(115200); // USB Serial for debugging
Serial.println("Sent: " + message); // Monitor on PC
```

## 📊 Expected Output

### Serial Monitor (115200 baud):
```
==================================
ESP32 Wroom - Sender Ready
==================================
Sent: Hello from ESP32 Wroom!amrr
Sent: Hello from ESP32 Wroom!amrr
Sent: Hello from ESP32 Wroom!amrr
...
```

## 🛠️ Receiver Code (Example)

For the receiving ESP32, use this companion code:

```cpp
HardwareSerial MyUART(2);

void setup() {
  Serial.begin(115200);
  MyUART.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("ESP32 Receiver Ready");
}

void loop() {
  if (MyUART.available()) {
    String received = MyUART.readStringUntil('\n');
    Serial.print("Received: ");
    Serial.println(received);
  }
}
```

## 🐛 Troubleshooting

### No communication between boards
- ✅ Check wiring (TX→RX, RX→TX crossover)
- ✅ Verify GND connection
- ✅ Confirm both boards use same baud rate (9600)
- ✅ Check GPIO pins match code configuration

### Upload errors
- ✅ Press and hold BOOT button during upload
- ✅ Check correct COM port selected
- ✅ Try different USB cable
- ✅ Install CH340/CP2102 drivers if needed

### Garbled messages
- ✅ Ensure same baud rate on both ends
- ✅ Check for loose connections
- ✅ Verify GND is connected

## 📝 Customization

### Change baud rate:
```cpp
MyUART.begin(115200, SERIAL_8N1, 16, 17); // Both sender & receiver
```

### Use different pins:
```cpp
MyUART.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
// Example: MyUART.begin(9600, SERIAL_8N1, 25, 26);
```

### Change message content:
```cpp
String message = "Your custom message here";
```



## ✍️ Author

Amr Ali

---

**Note:** Make sure both ESP32 boards share a common ground for reliable communication!
