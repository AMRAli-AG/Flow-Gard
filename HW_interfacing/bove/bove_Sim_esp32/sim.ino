#include <ModbusRTU.h>
#include <EEPROM.h>

ModbusRTU mb;

uint32_t forwardTotal = 12345678;  
uint32_t reverseTotal = 0;
float flowRate = 158.74;          
uint8_t meterID = 1;

#define EEPROM_SIZE 512
#define ADDR_TOTAL 0
#define ADDR_ID    16

void setup() {
  Serial.begin(115200);
  Serial2.begin(2400, SERIAL_8E1, 16, 17);
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(ADDR_TOTAL, forwardTotal);
  meterID = EEPROM.read(ADDR_ID);
  if (meterID < 1 || meterID > 247) meterID = 1;

  mb.begin(&Serial2);
  mb.slave(meterID);
  mb.addHreg(1, 0, 38);

  Serial.println("===========================================");
  Serial.println("      BOVE Ultrasonic Meter Simulator");
  Serial.println("===========================================");
  Serial.printf("Meter ID      : %d\n", meterID);
  Serial.printf("Total (m³)    : %.3f\n", forwardTotal / 1000.0);
  Serial.println("Baud Rate     : 2400 8E1");
  Serial.println("-------------------------------------------");
}

void loop() {
  mb.task();
  updateFlow();
  delay(10);
}

void updateFlow() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();

  forwardTotal += (uint32_t)(flowRate * 1000 / 3600.0);

  static uint8_t counter = 0;
  if (++counter >= 30) {
    EEPROM.put(ADDR_TOTAL, forwardTotal);
    EEPROM.write(ADDR_ID, meterID);
    EEPROM.commit();
    Serial.println("[EEPROM] Data Saved.");
    counter = 0;
  }

  writeU32(1,  (uint32_t)(flowRate * 100));     // Instantaneous Flow
  writeU32(7,  forwardTotal);                   // Forward Total ×1000
  writeU32(10, reverseTotal);                   // Reverse Total ×1000
  mb.Hreg(19, 291);                             // Pressure = 0.291 MPa
  mb.Hreg(20, 0);                               // Status = Normal
  mb.Hreg(30, 2715);                            // Temp = 27.15°C
  mb.Hreg(33, 0x5678); mb.Hreg(34, 0x1234);     // Serial = 12345678
  mb.Hreg(35, meterID);                         // Current Modbus ID
  mb.Hreg(37, 1);                               // Baud = 2400



  Serial.println("------ 1s Update ------");
  Serial.printf("Flow Rate     : %.2f L/h\n", flowRate);
  Serial.printf("Flow Instant  : %u (×0.01)\n", (uint32_t)(flowRate * 100));
  Serial.printf("Forward Total : %.3f m³  (RAW=%u)\n", forwardTotal / 1000.0, forwardTotal);
  Serial.printf("Reverse Total : %.3f m³\n", reverseTotal / 1000.0);
  Serial.println("Registers:");
  Serial.printf("  Reg 19 (Pressure) = %u\n", mb.Hreg(19));
  Serial.printf("  Reg 20 (Status)   = %u\n", mb.Hreg(20));
  Serial.printf("  Reg 30 (Temp)     = %u\n", mb.Hreg(30));
  Serial.printf("  Serial Number     = %04X%04X\n", mb.Hreg(34), mb.Hreg(33));
  Serial.printf("  Modbus ID         = %u\n", meterID);
  Serial.println("------------------------");
}

void writeU32(uint16_t reg, uint32_t val) {
  mb.Hreg(reg, val & 0xFFFF);
  mb.Hreg(reg + 1, val >> 16);
}
