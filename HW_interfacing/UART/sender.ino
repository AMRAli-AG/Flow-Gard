// ESP32 Wroom DevKit - Sender
// TX2 = GPIO17, RX2 = GPIO16

HardwareSerial MyUART(2); // Using UART2

void setup() {
  // Serial for USB (Serial Monitor)
  Serial.begin(115200);
  
  // UART2 for communication with other ESP32
  MyUART.begin(9600, SERIAL_8N1, 16, 17); // RX=GPIO16, TX=GPIO17
  
  Serial.println("==================================");
  Serial.println("ESP32 Wroom - Sender Ready");
  Serial.println("==================================");
  delay(2000);
}

void loop() {
  // Send a message every second
  String message = "Hello from ESP32 Wroom!amrr";
  
  MyUART.println(message);
  
  Serial.print("Sent: ");
  Serial.println(message);
  
  delay(1000); // Wait 1 second
}
