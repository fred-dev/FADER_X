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

// FaderOLED Class
class FaderOLED {
  private:
    String name;
    float value;  // 0.0 to 1.0
    String oscAddress;
    Adafruit_SSD1306* display;
    uint8_t multiplexerChannel;
    
  public:
    FaderOLED(String faderName, String address, Adafruit_SSD1306* oledDisplay, uint8_t muxChannel) {
      name = faderName;
      value = 0.0;
      oscAddress = address;
      display = oledDisplay;
      multiplexerChannel = muxChannel;
    }
    
    void setValue(float newValue) {
      value = constrain(newValue, 0.0, 1.0);
      updateDisplay();
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
};

// Create fader objects
FaderOLED fader1("Fader1", "/fader/1", &display0, 0);
FaderOLED fader2("Fader2", "/fader/2", &display1, 1);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("OLED Fader Test Starting...");
  
  // Initialize I2C
  Wire.begin();
  Wire.setClock(400000);  // 400kHz I2C speed
  
  // Initialize multiplexer
  multiplexer.begin(Wire);
  multiplexer.closeAll();  // Start with all channels closed
  
  Serial.println("Initializing OLED displays...");
  
  // Initialize displays
  fader1.init();
  fader2.init();
  
  Serial.println("OLED displays initialized");
  
  // Initialize Ethernet
  bool ethernetStarted = false;
  
  if (useStaticIP) {
    Serial.print("Configuring static IP: ");
    Serial.println(staticIP);
    ethernetStarted = Ethernet.begin(staticIP, subnet, gateway);
  } else {
    Serial.println("Requesting IP address via DHCP...");
    ethernetStarted = Ethernet.begin();
    
    if (ethernetStarted) {
      if (!Ethernet.waitForLocalIP(5000)) {
        Serial.println("DHCP timeout - no IP assigned");
        ethernetStarted = false;
      }
    }
  }
  
  if (ethernetStarted) {
    Serial.print("Ethernet initialized. IP: ");
    Serial.println(Ethernet.localIP());
  } else {
    Serial.println("Failed to initialize Ethernet");
    return;
  }
  
  // Start UDP receiver
  if (udpReceiver.begin(receivePort)) {
    Serial.print("OSC Receiver listening on port ");
    Serial.println(receivePort);
  } else {
    Serial.println("Failed to start UDP receiver");
  }
  
  // Start UDP sender
  if (udpSender.begin(0)) {
    Serial.println("UDP Sender ready");
  } else {
    Serial.println("Failed to start UDP sender");
  }
  
  Serial.print("Forwarding OSC messages to ");
  Serial.print(sendIP);
  Serial.print(":");
  Serial.println(sendPort);
  Serial.println("Ready!");
}

void loop() {
  // Process all available OSC packets
  while (true) {
    int packetSize = udpReceiver.parsePacket();
    if (packetSize <= 0) {
      break; // No more packets available
    }
    
    Serial.print("Received OSC packet, size: ");
    Serial.println(packetSize);

    // Clear the buffer before reading
    memset(udpBuf, 0, sizeof(udpBuf));
    
    // Read the packet into buffer
    int len = udpReceiver.read(udpBuf, sizeof(udpBuf));
    if (len > 0) {
      Serial.print("Read ");
      Serial.print(len);
      Serial.println(" bytes into buffer");
      
      // Debug: print first few bytes of buffer
      Serial.print("Buffer start: ");
      for(int i = 0; i < min(20, len); i++) {
        if(udpBuf[i] >= 32 && udpBuf[i] <= 126) { // printable ASCII
          Serial.print((char)udpBuf[i]);
        } else {
          Serial.print("[");
          Serial.print((int)udpBuf[i]);
          Serial.print("]");
        }
      }
      Serial.println();
      
      // Create OSC message from buffer
      OSCMessage oscMsg(udpBuf, len);

      // Print OSC message details
      Serial.print("Parsed Address: '");
      Serial.print(oscMsg.getAddress());
      Serial.println("'");

      Serial.print("Parameter count: ");
      Serial.println(oscMsg.getParameterCount());

      // Check if this is a fader value message
      if (oscMsg.getParameterCount() > 0 && oscMsg.getParameterType(0) == 'f') {
        float newValue = oscMsg.getFloat(0);
        Serial.print("Float value: ");
        Serial.println(newValue);
        
        // Check which fader this message is for
        if (fader1.matchesOSCAddress(oscMsg.getAddress())) {
          Serial.print("Updating Fader 1 (");
          Serial.print(fader1.getName());
          Serial.print(") to: ");
          Serial.println(newValue);
          fader1.setValue(newValue);
        } else if (fader2.matchesOSCAddress(oscMsg.getAddress())) {
          Serial.print("Updating Fader 2 (");
          Serial.print(fader2.getName());
          Serial.print(") to: ");
          Serial.println(newValue);
          fader2.setValue(newValue);
        } else {
          Serial.println("No fader matched this OSC address");
        }
      }
    } else {
      Serial.println("Failed to read UDP packet");
    }
  }

  delay(10); // Small delay to prevent overwhelming serial output
}
