#include <Servo.h>
#include <SoftwareSerial.h>

#define BUTTON_PIN 6          // Button to simulate coin insert
#define PULSE_OUT 7           // Output to ESP coin slot pin (ESP D6)
#define COIN_ENABLE 8         // From ESP (optional)
#define SENSOR_CAN_BOTTLE 5   // Sensor input pin
#define COIN_PIN 0 
Servo myServo;
Servo coinServo;
SoftwareSerial esp(2, 3); // RX, TX

const int servoPin = 9;
const int servoCoinPin = 8;
int pos = 0;
int count = 0;
String data ="";
unsigned long startTime = 0;
unsigned long detectDuration = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PULSE_OUT, OUTPUT);
  pinMode(COIN_ENABLE, INPUT_PULLUP);
  pinMode(SENSOR_CAN_BOTTLE, INPUT);

  digitalWrite(PULSE_OUT, HIGH); // Idle HIGH (no coin)
  
  myServo.attach(servoPin);
  myServo.write(0); // Servo start position (closed)

  coinServo.attach(servoCoinPin); //coin changes
  coinServo.write(20); // Servo start position (closed)
  
  Serial.begin(9600);
  esp.begin(9600);
  Serial.println("System Ready ✅");

}

void loop() {
  while (esp.available()) {
    String  c = esp.readStringUntil('\n');
    Serial.println(c);
    c.trim(); // remove any trailing \r or spaces
    if (c.length() > 0) {   
      int count = c.toInt();
      Serial.print("Received: ");
      Serial.println(count);

      for (int i = 0; i < count; i++) {
        rotateServo();
      }
     
    }
  }
   

  int sensorVal = digitalRead(SENSOR_CAN_BOTTLE);
  // Start timing when object detected (LOW)
  if (sensorVal == LOW && startTime == 0) {
    startTime = millis();
  }

  // Stop timing when detection ends
  if (sensorVal == HIGH && startTime != 0) {
    detectDuration = millis() - startTime;
    startTime = 0;
 
    // Classify
    if (detectDuration > 300) {
      
      // 💰 Then send coin pulse to ESP
      sendCoinPulse();
    } else {
      
      // 💰 Then send coin pulse to ESP
      sendCoinPulse();
    }

   

    

    delay(500); // debounce
  }

  // Manual test button (simulate coin)
  if (digitalRead(BUTTON_PIN) == LOW) {
    
    openStorage();
    sendCoinPulse();
    delay(500);
  }

  delay(10);
}

void rotateServo() {
  Serial.println("push coin");
  coinServo.write(90);   // move
  delay(800);
  Serial.println("back coin");
  coinServo.write(20);    // back
  delay(500);
}

void openStorage() {
  Serial.println("Opening storage...");
  
  // Open servo
  for (pos = 0; pos <= 180; pos += 10) {
    myServo.write(pos);
    delay(10);
  }

  delay(600); // hold open for 1 second

  // Close servo
  for (pos = 180; pos >= 0; pos -= 10) {
    myServo.write(pos);
    delay(10);
  }

  Serial.println("Storage closed.");
}

void sendCoinPulse() {
  Serial.println("💰 Sending coin pulse...");
  digitalWrite(PULSE_OUT, LOW);
  delay(100);
  digitalWrite(PULSE_OUT, HIGH);

   // 🧠 Open storage FIRST
    openStorage();
}
