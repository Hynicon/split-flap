// Writes the hall-sensor calibration offset (in steps) into the unit's EEPROM.
// Ported from upstream EEPROM_Write_Offset.ino (David Königsmann, GPL-3.0).
// Usage: flash, open serial monitor at 115200, type the offset (e.g. 100) + Enter,
// then re-flash the unit firmware and check the blank flap shows after homing.
#include <Arduino.h>
#include <EEPROM.h>

int eeAddress = 0;   //Location we want the data to be put.
int calOffsetGet;  //Variable already in EEPROM.

//Serial input stuff
const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
boolean newData = false;

void getData();
void writeToEEPROM(String offsetValue);
void recvWithEndMarker();
void showNewData();

void setup() {
  Serial.begin(115200);
  Serial.println("init");
  getData();
}

void loop() {
  recvWithEndMarker();
  showNewData();
}

void getData() {
  EEPROM.get(eeAddress, calOffsetGet);
  Serial.print("Offset already stored in EEPROM: ");
  Serial.print(calOffsetGet);
  Serial.println();
}

void writeToEEPROM(String offsetValue) {
  //Convert String to int
  int intOffsetValue = offsetValue.toInt();
  //One simple call, with the address first and the object second.
  EEPROM.put(eeAddress, intOffsetValue);

  Serial.print("Value written to EEPROM: ");
  Serial.print(intOffsetValue);
  Serial.println();
}

void recvWithEndMarker() {
  static byte ndx = 0;
  char endMarker = '\n';
  char rc;

  while (Serial.available() > 0 && newData == false) {
    rc = Serial.read();

    if (rc == '\r') continue; // serial monitors may send CRLF

    if (rc != endMarker) {
      receivedChars[ndx] = rc;
      ndx++;
      if (ndx >= numChars) {
        ndx = numChars - 1;
      }
    }
    else {
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
    }
  }
}

void showNewData() {
  if (newData == true) {
    writeToEEPROM(receivedChars);
    newData = false;
  }
}
