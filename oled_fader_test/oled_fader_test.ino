#include <QNEthernet.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "OSC.h"
#include <TCA9548A.h>  // I2C Multiplexer library
using namespace qindesign::network;

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// I2C Configuration
#define MULTIPLEXER_ADDR 0x70  // TCA9548A I2C multiplexer address

// Motorized Fader Configuration
#define MOTOR_MIN_SPEED 50
#define MOTOR_MAX_SPEED 255
#define MOTOR_DEADZONE 10
#define POSITION_TOLERANCE 5

// Network Configuration
unsigned short receivePort = 8000;
unsigned short sendPort = 9000;
IPAddress sendIP(192, 168, 1, 131);
bool useStaticIP = false;
IPAddress staticIP(192, 168, 1, 50);
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 1, 1);

// Network objects
EthernetUDP udpReceiver;
EthernetUDP udpSender;
char udpBuf[Ethernet.mtu() - 20 - 8];

// I2C Multiplexer
TCA9548A multiplexer(MULTIPLEXER_ADDR);

// OLED Displays
Adafruit_SSD1306 display0(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_SSD1306 display1(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Motorized Fader Class
class MotorizedFader {
  private:
    int motorPinA;
    int motorPinB;
    int wiperPin;
    int touchPin;
    float targetPosition;  // 0.0 to 1.0
    int currentRawPosition;
    unsigned long lastMotorUpdate;
    bool isMoving;
    int adcMin;  // Minimum ADC value (fully down)
    int adcMax;  // Maximum ADC value (fully up)
    bool motorDirectionReversed;
    
  public:
    MotorizedFader(int motorA, int motorB, int wiper, int touch) {
      motorPinA = motorA;
      motorPinB = motorB;
      wiperPin = wiper;
      touchPin = touch;
      targetPosition = 0.5;  // Start at middle
      currentRawPosition = 0;
      lastMotorUpdate = 0;
      isMoving = false;
      adcMin = 50;    // Default minimum (adjust based on your fader)
      adcMax = 973;   // Default maximum (adjust based on your fader)
      motorDirectionReversed = false;  // Set to true if motor moves opposite direction
    }
    
    void setup() {
      pinMode(motorPinA, OUTPUT);
      pinMode(motorPinB, OUTPUT);
      pinMode(wiperPin, INPUT);
      pinMode(touchPin, INPUT_PULLUP);
      
      // Get initial position
      currentRawPosition = analogRead(wiperPin);
      Serial.print("Fader initial ADC: ");
      Serial.println(currentRawPosition);
    }
    
    void setTargetPosition(float position) {
      targetPosition = constrain(position, 0.0, 1.0);
      isMoving = true;
    }
    
    void setMotorDirectionReversed(bool reversed) {
      motorDirectionReversed = reversed;
    }
    
    void setADCRange(int minVal, int maxVal) {
      adcMin = minVal;
      adcMax = maxVal;
    }
    
    void updateMotor() {
      unsigned long now = millis();
      if (now - lastMotorUpdate < 20) return;  // Update every 20ms
      lastMotorUpdate = now;
      
      // Read current position
      currentRawPosition = analogRead(wiperPin);
      
      // Convert target position to raw ADC value
      int targetRaw = map(targetPosition * 1000, 0, 1000, adcMin, adcMax);
      
      // Calculate difference
      int diff = targetRaw - currentRawPosition;
      
      // Check if we're close enough to target
      if (abs(diff) < POSITION_TOLERANCE) {
        // Stop motor
        analogWrite(motorPinA, 0);
        analogWrite(motorPinB, 0);
        isMoving = false;
        return;
      }
      
      // Calculate motor speed based on distance
      int speed = map(abs(diff), 0, (adcMax - adcMin) / 2, MOTOR_MIN_SPEED, MOTOR_MAX_SPEED);
      speed = constrain(speed, MOTOR_MIN_SPEED, MOTOR_MAX_SPEED);
      
      // Move motor in correct direction
      bool shouldMoveUp = diff > MOTOR_DEADZONE;
      if (motorDirectionReversed) {
        shouldMoveUp = !shouldMoveUp;
      }
      
      if (shouldMoveUp) {
        // Move up (towards higher ADC values)
        analogWrite(motorPinA, speed);
        analogWrite(motorPinB, 0);
      } else if (diff < -MOTOR_DEADZONE) {
        // Move down (towards lower ADC values)
        analogWrite(motorPinA, 0);
        analogWrite(motorPinB, speed);
      } else {
        // Stop if in deadzone
        analogWrite(motorPinA, 0);
        analogWrite(motorPinB, 0);
      }
    }
    
    float getCurrentPosition() {
      int clampedADC = constrain(currentRawPosition, adcMin, adcMax);
      return (float)(clampedADC - adcMin) / (adcMax - adcMin);
    }
    
    float getTargetPosition() {
      return targetPosition;
    }
    
    bool isBeingTouched() {
      return digitalRead(touchPin) == LOW;
    }
    
    void stop() {
      analogWrite(motorPinA, 0);
      analogWrite(motorPinB, 0);
      isMoving = false;
    }
    
    int getCurrentADC() {
      return currentRawPosition;
    }
};

// Create motorized fader objects
MotorizedFader motorFader1(0, 1, 9, 10);  // Pins: motor A,B, wiper, touch
MotorizedFader motorFader2(2, 3, 11, 12); // Pins: motor A,B, wiper, touch

// FaderOLED Class
class FaderOLED {
  private:
    String name;
    float value;  // 0.0 to 1.0
    String oscAddress;
    Adafruit_SSD1306* display;
    uint8_t multiplexerChannel;
    MotorizedFader* motorFader;
    
  public:
    FaderOLED(String faderName, String address, Adafruit_SSD1306* oledDisplay, uint8_t muxChannel, MotorizedFader* motor) {
      name = faderName;
      value = 0.0;
      oscAddress = address;
      display = oledDisplay;
      multiplexerChannel = muxChannel;
      motorFader = motor;
    }
    
    void setValue(float newValue) {
      value = constrain(newValue, 0.0, 1.0);
      updateDisplay();
      
      // Also set motor position
      if (motorFader) {
        motorFader->setTargetPosition(value);
      }
    }
    
    float getValue() {
      return value;
    }
    
    String getName() {
      return name;
    }
    
    String getOSCAddress() {
      return oscAddress;
    }
    
    bool matchesOSCAddress(String address) {
      return oscAddress.equals(address);
    }
    
    void updateDisplay() {
      // Select multiplexer channel
      multiplexer.openChannel(multiplexerChannel);
      
      // Clear display
      display->clearDisplay();
      display->setTextColor(SSD1306_WHITE);
      
      // Display fader name (top line, size 2)
      display->setTextSize(2);
      display->setCursor((SCREEN_WIDTH - name.length() * 12) / 2, 0); // Center horizontally
      display->print(name);
      
      // Display value with 2 decimal places (bottom line, size 2)
      String valueStr = String(value, 2);
      display->setCursor((SCREEN_WIDTH - valueStr.length() * 12) / 2, 16); // Center horizontally
      display->print(valueStr);
      
      // Update display
      display->display();
      
      // Close the channel
      multiplexer.closeChannel(multiplexerChannel);
    }
    
    void init() {
      multiplexer.openChannel(multiplexerChannel);
      if(!display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.print("SSD1306 allocation failed for channel ");
        Serial.println(multiplexerChannel);
        while(1);
      }
      display->clearDisplay();
      display->display();
      multiplexer.closeChannel(multiplexerChannel);
      updateDisplay();
    }
    
    void updateMotor() {
      if (motorFader) {
        motorFader->updateMotor();
      }
    }
};

// Create fader objects
FaderOLED fader1("Fader1", "/fader/1", &display0, 0, &motorFader1);
FaderOLED fader2("Fader2", "/fader/2", &display1, 1, &motorFader2);

// Debug Configuration
#define DEBUG_OSC 0  // Set to 1 to enable OSC debug output, 0 to disable

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize I2C
  Wire.begin();
  Wire.setClock(400000);  // 400kHz I2C speed
  
  // Initialize multiplexer
  multiplexer.begin(Wire);
  multiplexer.closeAll();  // Start with all channels closed
  
  // Initialize motorized faders
  motorFader1.setup();
  motorFader2.setup();
  
  // Configure fader ADC ranges and motor directions
  motorFader1.setADCRange(60, 963);
  motorFader2.setADCRange(60, 963);
  motorFader1.setMotorDirectionReversed(false);
  motorFader2.setMotorDirectionReversed(false);
  
  // Initialize OLED displays
  fader1.init();
  fader2.init();
  
  // Initialize Ethernet
  bool ethernetStarted = false;
  
  if (useStaticIP) {
    ethernetStarted = Ethernet.begin(staticIP, subnet, gateway);
  } else {
    ethernetStarted = Ethernet.begin();
    
    if (ethernetStarted) {
      if (!Ethernet.waitForLocalIP(5000)) {
        ethernetStarted = false;
      }
    }
  }
  
  if (ethernetStarted) {
    udpReceiver.begin(receivePort);
    udpSender.begin(0);
  }
}

void loop() {
  // Print wiper readings
  Serial.print("Fader1 ADC: ");
  Serial.print(motorFader1.getCurrentADC());
  Serial.print(" Fader2 ADC: ");
  Serial.println(motorFader2.getCurrentADC());
  
  // Process OSC packets (without debug output)
  while (true) {
    int packetSize = udpReceiver.parsePacket();
    if (packetSize <= 0) {
      break;
    }
    
    memset(udpBuf, 0, sizeof(udpBuf));
    int len = udpReceiver.read(udpBuf, sizeof(udpBuf));
    if (len > 0) {
      OSCMessage oscMsg(udpBuf, len);
      
      if (oscMsg.getParameterCount() > 0 && oscMsg.getParameterType(0) == 'f') {
        float newValue = oscMsg.getFloat(0);
        
        if (fader1.matchesOSCAddress(oscMsg.getAddress())) {
          fader1.setValue(newValue);
        } else if (fader2.matchesOSCAddress(oscMsg.getAddress())) {
          fader2.setValue(newValue);
        }
      }
    }
  }

  // Update motorized faders
  fader1.updateMotor();
  fader2.updateMotor();

  delay(100); // Print readings every 100ms
}
