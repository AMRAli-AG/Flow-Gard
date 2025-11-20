// ESP32 Dev Board - Receiver
// Wiring:
// GPIO16 (U2_RXD) ← Connect to TX of other ESP32
// GPIO17 (TX) → Connect to RX of other ESP32
// GND ↔ Connect to GND of other ESP32

HardwareSerial MyUART(2); // Using UART2

void setup() {
  // Serial for USB debugging (Serial Monitor)
  Serial.begin(115200);
  
  // UART2 for receiving data from other ESP32
  // RX = GPIO16, TX = GPIO17
  MyUART.begin(9600, SERIAL_8N1, 16, 17);
  
  Serial.println("==================================");
  Serial.println("ESP32 Dev Board - Receiver Ready");
  Serial.println("Waiting for data...");
  Serial.println("==================================");
  Serial.println();
}

void loop() {
  // Check if data is available from UART
  if (MyUART.available()) {
    // Read the received message until newline
    String receivedMessage = MyUART.readStringUntil('\n');
    
    // Remove any trailing whitespace
    receivedMessage.trim();
    
    // Display the received message on Serial Monitor
    Serial.println("----------------------------------");
    Serial.print("📩 Received: ");
    Serial.println(receivedMessage);
    Serial.print("⏰ Time: ");
    Serial.print(millis());
    Serial.println(" ms");
    Serial.println("----------------------------------");
  }
}
