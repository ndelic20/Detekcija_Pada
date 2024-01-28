#include <Arduino.h>
#include "SparkFun_MMA8452Q.h"

MMA8452Q accel;

const int numReadings = 10;
float xReadings[numReadings];
float yReadings[numReadings];
float zReadings[numReadings];
int readingIndex = 0;


bool detectFall();

void setup() {
  Serial.begin(115200);
  Serial.println("Fall Detection Test Code!");
  Wire.begin();

  if (accel.begin() == false) {
    Serial.println("Not Connected. Please check connections and read the hookup guide.");
    while (1);
  }
}

void loop() {
  if (accel.available()) {
    float x = accel.getCalculatedX();
    float y = accel.getCalculatedY();
    float z = accel.getCalculatedZ();

    
    xReadings[readingIndex] = x;
    yReadings[readingIndex] = y;
    zReadings[readingIndex] = z;

    
    readingIndex = (readingIndex + 1) % numReadings;

    if (detectFall()) {
      Serial.println("Fall detected!");
      delay(1000);  
    }
  }

  delay(100);
}


bool detectFall() {
  
  float yThreshold = 0.1; 
  float xThreshold = 0.1; 
  float zThreshold = 0.1; 

  
  float deltaY = yReadings[readingIndex] - yReadings[(readingIndex + numReadings - 1) % numReadings];
  float deltaX = xReadings[readingIndex] - xReadings[(readingIndex + numReadings - 1) % numReadings];
  float deltaZ = zReadings[readingIndex] - zReadings[(readingIndex + numReadings - 1) % numReadings];

  return (yReadings[readingIndex] < 0 && yReadings[(readingIndex + numReadings - 1) % numReadings] >= 0) &&
         ((xReadings[readingIndex] < 0 && xReadings[(readingIndex + numReadings - 1) % numReadings] >= 0) ||
          (xReadings[readingIndex] > 0 && xReadings[(readingIndex + numReadings - 1) % numReadings] <= 0)) &&
         ((zReadings[readingIndex] < 0 && zReadings[(readingIndex + numReadings - 1) % numReadings] >= 0) ||
          (zReadings[readingIndex] > 0 && zReadings[(readingIndex + numReadings - 1) % numReadings] <= 0));
}