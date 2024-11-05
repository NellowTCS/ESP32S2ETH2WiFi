#include <WiFi.h>
#include <SPI.h>
#include <Ethernet.h>
#include <WiFiUdp.h>

// Configuration
const char* ssid = "wifi";          // Replace with your Wi-Fi SSID
const char* password = "password";  // Replace with your Wi-Fi password
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Pin definitions
#define LED_PIN 15        // LED on LOLIN S2 Mini
#define ETHERNET_CS 5     // GPIO5 for W5500 CS pin

// Network configuration
#define BUFFER_SIZE 1500  // Maximum Ethernet frame size
#define SERVER_PORT 80    // Default port for forwarding
#define UDP_PORT 80       // Default UDP port

// Global objects
EthernetClient ethClient;
WiFiUDP udp;
unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 1000; // Heartbeat every 1 second

// Status indicator class
class StatusLED {
  private:
    uint8_t pin;
    bool state;
    unsigned long lastBlink;
    unsigned long blinkInterval;
    
  public:
    StatusLED(uint8_t ledPin) : pin(ledPin), state(false), lastBlink(0), blinkInterval(200) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
    
    void blink() {
      unsigned long currentMillis = millis();
      if (currentMillis - lastBlink >= blinkInterval) {
        state = !state;
        digitalWrite(pin, state);
        lastBlink = currentMillis;
      }
    }
    
    void setOn() {
      digitalWrite(pin, HIGH);
      state = true;
    }
    
    void setOff() {
      digitalWrite(pin, LOW);
      state = false;
    }
};

StatusLED statusLed(LED_PIN);

// handle web connectivity.
void handleSerialInput() {
  if (Serial.available()) {
    String ssid = Serial.readStringUntil('\n');
    String password = Serial.readStringUntil('\n');
    
    // Remove any whitespace
    ssid.trim();
    password.trim();
    
    if (ssid.length() > 0 && password.length() > 0) {
      Serial.println("Received WiFi credentials");
      Serial.print("Connecting to: ");
      Serial.println(ssid);
      
      // Attempt to connect
      WiFi.begin(ssid.c_str(), password.c_str());
      
      // Wait for connection
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected successfully!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("\nConnection failed!");
      }
    }
  }
}
// Function to forward TCP traffic from Ethernet to Wi-Fi
void forwardTCP() {
  if (ethClient) {
    Serial.println("Ethernet client connected!");
    WiFiClient wifiClient;
    
    if (wifiClient.connect(WiFi.gatewayIP(), SERVER_PORT)) {
      Serial.println("Connected to Wi-Fi server!");
      
      while (ethClient.connected() && wifiClient.connected()) {
        // Forward Ethernet -> WiFi
        if (ethClient.available()) {
          uint8_t buffer[BUFFER_SIZE];
          int len = ethClient.read(buffer, sizeof(buffer));
          if (len > 0) {
            wifiClient.write(buffer, len);
            statusLed.blink();
          }
        }
        
        // Forward WiFi -> Ethernet
        if (wifiClient.available()) {
          uint8_t buffer[BUFFER_SIZE];
          int len = wifiClient.read(buffer, sizeof(buffer));
          if (len > 0) {
            ethClient.write(buffer, len);
            statusLed.blink();
          }
        }
        
        // Heartbeat monitoring
        unsigned long currentMillis = millis();
        if (currentMillis - lastHeartbeat >= HEARTBEAT_INTERVAL) {
          Serial.println("Bridge active - forwarding TCP traffic");
          lastHeartbeat = currentMillis;
        }
      }
      
      wifiClient.stop();
      Serial.println("Disconnected from Wi-Fi server");
    }
    
    ethClient.stop();
    Serial.println("Ethernet client disconnected");
  }
}

// Function to forward UDP traffic from Ethernet to Wi-Fi
void forwardUDP() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    uint8_t buffer[BUFFER_SIZE];
    int len = udp.read(buffer, sizeof(buffer));
    
    if (len > 0) {
      // Forward the UDP packet
      udp.beginPacket(WiFi.gatewayIP(), UDP_PORT);
      udp.write(buffer, len);
      udp.endPacket();
      statusLed.blink();
      
      Serial.printf("Forwarded UDP packet: %d bytes\n", len);
    }
  }
}

// Function to check network connectivity
bool checkConnectivity() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost! Attempting to reconnect...");
    WiFi.reconnect();
    return false;
  }
  
  if (!Ethernet.linkStatus()) {
    Serial.println("Ethernet link down!");
    return false;
  }
  
  return true;
}

// Main bridge function
void startBridge() {
  udp.begin(UDP_PORT);
  
  while (true) {
    if (checkConnectivity()) {
      forwardTCP();
      forwardUDP();
      statusLed.blink();
    } else {
      statusLed.setOff();
      delay(1000); // Wait before retrying
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  
  Serial.println("\nESP32-S2 Network Bridge Starting...");
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str());
  
  // Initialize Ethernet
  Serial.println("Initializing Ethernet...");
  Ethernet.init(ETHERNET_CS);
  
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found!");
    } else if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected!");
    }
    while (true) {
      statusLed.blink();
      delay(100);
    }
  }
  
  Serial.printf("Ethernet IP: %s\n", Ethernet.localIP().toString().c_str());
  Serial.println("Bridge initialization complete!");
  
  startBridge();
}

void loop() {
  handleSerialInput();
  // Nothing else needed here; bridging is handled in startBridge()
}
