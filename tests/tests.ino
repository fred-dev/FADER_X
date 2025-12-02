#include <QNEthernet.h>
#include "OSC.h"
using namespace qindesign::network;

// Configuration
unsigned short receivePort = 8000;      // Port to receive OSC messages on
unsigned short sendPort = 9000;         // Port to send OSC messages to
IPAddress sendIP(192, 168, 1, 131);     // IP address to forward messages to

// Network configuration - choose one:
// Option 1: Use DHCP (set useStaticIP = false)
// Option 2: Use Static IP (set useStaticIP = true and configure below)
bool useStaticIP = false;
IPAddress staticIP(192, 168, 1, 50);      // Static IP address
IPAddress subnet(255, 255, 255, 0);        // Subnet mask
IPAddress gateway(192, 168, 1, 1);         // Gateway

// Network objects
EthernetUDP udpReceiver;
EthernetUDP udpSender;

// Buffer for receiving UDP packets
char udpBuf[Ethernet.mtu() - 20 - 8];

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("OSC Receiver Starting...");
  
  // Initialize Ethernet
  bool ethernetStarted = false;
  
  if (useStaticIP) {
    // Use static IP configuration
    Serial.print("Configuring static IP: ");
    Serial.println(staticIP);
    ethernetStarted = Ethernet.begin(staticIP, subnet, gateway);
  } else {
    // Use DHCP
    Serial.println("Requesting IP address via DHCP...");
    ethernetStarted = Ethernet.begin();
    
    if (ethernetStarted) {
      // Wait for DHCP to assign an IP (optional, but good practice)
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
  if (udpSender.begin(0)) {  // 0 = any available port
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
  int packetSize = udpReceiver.parsePacket();
  if (packetSize > 0) {
    Serial.print("Received OSC packet, size: ");
    Serial.println(packetSize);

    // Read the packet into buffer
    int len = udpReceiver.read(udpBuf, sizeof(udpBuf));
    if (len > 0) {

      // Create OSC message from buffer
      OSCMessage oscMsg(udpBuf, len);

      // Print OSC message details
      Serial.print("Address: ");
      Serial.println(oscMsg.getAddress());

      Serial.print("Parameter count: ");
      Serial.println(oscMsg.getParameterCount());

      // Print parameters
      for(byte i = 0; i < oscMsg.getParameterCount(); i++) {
        Serial.print("Param ");
        Serial.print(i);
        Serial.print(" (");
        Serial.print(oscMsg.getParameterType(i));
        Serial.print("): ");

        if(oscMsg.getParameterType(i) == 'i') {
          Serial.println(oscMsg.getInt(i));
        } else if(oscMsg.getParameterType(i) == 'f') {
          Serial.println(oscMsg.getFloat(i));
        } else if(oscMsg.getParameterType(i) == 's') {
          Serial.println(oscMsg.getString(i));
        }
      }

      // Forward the message
      udpSender.beginPacket(sendIP, sendPort);
      oscMsg.writeUDP(&udpSender);
      udpSender.endPacket();

      Serial.println("Message forwarded");
      Serial.println("---");
    }
  }

  delay(10); // Small delay to prevent overwhelming serial output
}