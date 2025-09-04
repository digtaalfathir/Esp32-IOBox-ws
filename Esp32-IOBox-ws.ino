#include <WiFi.h>              // WiFi support
#include <WebServer.h>         // HTTP Web server
#include <WebSocketsClient.h>  // WebSocket client
#include <WebSocketsServer.h>  // WebSocket server
#include <EEPROM.h>            // EEPROM for storing configuration
#include <ArduinoJson.h>       // JSON serialization/deserialization
#include <ESPmDNS.h>           // mDNS responder
#include <WiFiUdp.h>           // UDP for OTA
#include <ArduinoOTA.h>        // Over-The-Air update
#include "EmonLib.h"           // Energy monitoring library
#include <Wire.h>              // I2C communication
#include <vector>              // For dynamic array (buffer)
#include "time.h"              // Time library for NTP
// #include <PCF8574.h>        // PCF8574 library for I/O expansion

const String program_version = "v1.0.3"; // Program version
bool readytoSend = false;
bool init2Ws = false;

//---------- Time ----------
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600;  // GMT+7 (WIB)
const int   daylightOffset_sec = 0;

//---------- HTML pages for web interface ----------
#include "login-page.h"       // Login page
#include "config-page.h"      // Configuration page
#include "success-page.h"     // Success page after saving configuration

//---------- EEPROM configuration ----------
#define EEPROM_SIZE 512       // EEPROM size in bytes
#define HARDWARE_ID_ADDR 64   // Hardware ID address in EEPROM
#define SERVER_IP_ADDR 96     // Server IP address in EEPROM
#define HARDWARE_IP_ADDR 128  // Hardware IP address in EEPROM
#define WIFI_SSID_ADDR 160    // WiFi SSID address in EEPROM
#define WIFI_PASS_ADDR 192    // Password SSID address in EEPROM
#define SERVER_PORT_ADDR 224  // Server port address in EEPROM

//---------- Access Point configuration ----------
String ap_ssid = "IoT-Node ";                    // SSID Access Point
const char* ap_password = "12345678";            // Password Access Point
bool isAPMode = true;                            // Status mode AP
bool wasOnline = true;                           // Last known online status
unsigned long apStartTime = 0;
const unsigned long apTimeout = 3 * 60 * 1000UL; // 3 minutes timeout for AP mode
unsigned long lastCountdownPrint = 0;

//---------- Configuration PCF8574 ----------
// PCF8574 pcf8574(0x20);  // Address PCF8574

//---------- Monitoring server configuration ----------
int monitoring_port = 80;  // Port server monitoring

//---------- Device configuration variables ----------
String hardwareId = "001";                      // ID hardware
String serverIP = "192.168.1.91";               // IP server monitoring
String hardwareIP = "192.168.1.100";            // IP hardware
std::vector<String> monitoringBuffer;           // Buffer for monitoring data
const size_t MAX_BUFFER_SIZE = 100;             // Maximum buffer size for monitoring data

//---------- Input and output pins ----------
// const int INPUT_PINS[] = { 12, 13, 14, 25, 26, 27, 32, 33 };         // Pin input
// const int OUTPUT_PINS[] = { 4, 15, 16, 17};                          // Pin output

//---------- Input and output pins ----------
const int INPUT_PINS[] = {12, 13, 14, 25, 26, 27, 32, 33};                // Pin input
const int OUTPUT_PINS[] = { 4, 15, 16, 17};                               // Pin output
const int NUM_INPUTS = sizeof(INPUT_PINS) / sizeof(INPUT_PINS[0]);        // Sum of input pins
const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);     // Sum of output pins

// const int NUM_INPUTS = sizeof(INPUT_PINS) / sizeof(INPUT_PINS[0]);     // Sum of input pins
// const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);  // Sum of output pins
uint8_t lastInputStates[12] = { 0 };                                      // Array to store last input states

//---------- Current sensor (SCT-013) configuration ----------
const int SCT_013_PIN = 32;
float nilai_kalibrasi = 3.15;

//---------- Object initialization ----------
WebSocketsClient webSocket;  // WebSocket client
WebServer server(80);        // Web server on port 80
EnergyMonitor emon1;         // Energy monitoring object

//---------- WiFi credentials ----------
String storedSSID = "";      // SSID WiFi
String storedPassword = "";  // Password WiFi
bool wifiReconnecting = false;
unsigned long wifiReconnectStart = 0;

//---------- Timing control ----------
unsigned long lastMonitoringTime = 0;  // Time of last monitoring data send
const long monitoringInterval = 200;   // Interval for sending monitoring data (in milliseconds)
unsigned long lastForceSendTime = 0;   // Time of last forced send
const unsigned long forceSendInterval = 60000;  // Interval for forced send (in milliseconds)

//---------- Default login credentials for web UI ----------
const char* default_username = "contoh";    // Username default
const char* default_password = "password";  // Password default
bool isAuthenticated = false;               // Authentication status

//---------- Helper: Check if EEPROM address has been initialized ----------
bool isEEPROMInitialized(int address) {
  byte value = EEPROM.read(address);
  return value != 255;  // Check if the value is not 255 (uninitialized)
}

//---------- Helper: Check if IP valid ----------
bool isValidIPAddress(const String& ip) {
  int parts = 0;
  int lastIndex = -1;

  for (int i = 0; i < ip.length(); i++) {
    if (ip[i] == '.') {
      String part = ip.substring(lastIndex + 1, i);
      int num = part.toInt();
      if (num < 0 || num > 255) return false;
      parts++;
      lastIndex = i;
    }
  }

  String lastPart = ip.substring(lastIndex + 1);
  int num = lastPart.toInt();
  if (num < 0 || num > 255) return false;

  return (parts == 3); // should be exactly 3 dots
}

//---------- Detect changes in input pins ----------
bool checkInputChanges() {
  bool changed = false;

  // Check input pins
  for (int i = 0; i < NUM_INPUTS; i++) {
    uint8_t current = digitalRead(INPUT_PINS[i]);
    if (current != lastInputStates[i]) {
      lastInputStates[i] = current;
      changed = true;
    }
  }

  // Check PCF8574 inputs (if used)
  // for (int i = 0; i < 8; i++) {
  //   uint8_t current = pcf8574.read(i);
  //   if (current != lastInputStates[i + 4]) {
  //     lastInputStates[i + 4] = current;
  //     changed = true;
  //   }
  // }

  return changed;
}

//---------- Setup OTA update functionality ----------
void setupOTA() {
  ArduinoOTA.setPort(3232);                // Set port OTA
  ArduinoOTA.setHostname("contoh");        // Set hostname
  ArduinoOTA.setPassword("password");      // Set password OTA

  // Callback when OTA starts
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });

  // Callback when OTA ends
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  // Callback for progress OTA
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  // Callback for OTA error
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

bool lastShootState = false;  // false = tidak aktif (0), true = aktif (1)

//---------- Main setup function ----------
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // PUSR
  Wire.begin();
  delay(1000);
  Serial.println("\n==> Starting IoT Node . . . \n");

  EEPROM.begin(EEPROM_SIZE);

  // Initialize PCF8574
  // if (!pcf8574.begin()) {
  //   Serial.println("could not initialize...");
  // }
  // if (!pcf8574.isConnected()) {
  //   Serial.println("=> not connected");
  // } else {
  //   Serial.println("=> connected!!");
  // }

  // Initialize pin IO
  for (int i = 0; i < NUM_INPUTS; i++) {
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
  }
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    pinMode(OUTPUT_PINS[i], OUTPUT);
    digitalWrite(OUTPUT_PINS[i], LOW);
  }

  // Check if EEPROM is initialized
  if (!isEEPROMInitialized(HARDWARE_ID_ADDR)) {
    writeToEEPROM(HARDWARE_ID_ADDR, "001");
    writeToEEPROM(HARDWARE_IP_ADDR, "192.168.1.100");
    writeToEEPROM(SERVER_IP_ADDR, "192.168.1.91");
    writeToEEPROM(WIFI_SSID_ADDR, "");
    writeToEEPROM(WIFI_PASS_ADDR, "");
    EEPROM.commit();
  }

  serverIP = readFromEEPROM(SERVER_IP_ADDR);
  if (serverIP.length() == 0) {
    serverIP = "192.168.1.91";  // default IP Server
  }

  hardwareIP = readFromEEPROM(HARDWARE_IP_ADDR);
  if ((hardwareIP.length() == 0) || !isValidIPAddress) {
    hardwareIP = "192.168.1.100";  // default IP Hardware
    Serial.println("Invalid hardware IP found in EEPROM. Using default: " + hardwareIP);
  }

  hardwareId = readFromEEPROM(HARDWARE_ID_ADDR);
  if (hardwareId.length() == 0) {
    hardwareId = "001";  // default Hardware ID
  }

  String portStr = readFromEEPROM(SERVER_PORT_ADDR);
  if (portStr.length() == 0) {
    monitoring_port = 80;  //
  } else {
    monitoring_port = portStr.toInt();
  }

  storedSSID = readFromEEPROM(WIFI_SSID_ADDR);
  storedPassword = readFromEEPROM(WIFI_PASS_ADDR);

  Serial.println("Try Connect to :");
  Serial.println(storedSSID);
  Serial.println(storedPassword);

  // Try to connect to WiFi if credentials are available
  if (storedSSID.length() > 0 && storedPassword.length() > 0 && connectToWiFi()) {
    isAPMode = false;
    setupOTA();
    setupWebSocket();
    Serial.println("Port: " + portStr);
    Serial.println("MAC Address: " + WiFi.macAddress());
    // Sinkronisasi waktu dari NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time synchronized from NTP.");
  } else {
    setupAP();
  }

  // Setup route web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/save-config", HTTP_POST, handleSaveConfig);
  server.begin();

  // Initialize current sensor
  emon1.current(SCT_013_PIN, nilai_kalibrasi);
}

//---------- Main loop function  ----------
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  handleSerialInput();

  if (!isAPMode) {
    ensureWiFiConnected();
    webSocket.loop();
    if (init2Ws) {
      bool intervalPassed = millis() - lastMonitoringTime >= monitoringInterval;
      bool forceSendDue = millis() - lastForceSendTime >= forceSendInterval;

      if (intervalPassed && (checkInputChanges() || (forceSendDue && readytoSend))) {
        sendMonitoringData();
        lastMonitoringTime = millis();
        if (forceSendDue) {
          lastForceSendTime = millis();
        }
      }
    }
  } else {
    // === Timeout AP Mode dengan countdown ===
    unsigned long elapsed = millis() - apStartTime;
    unsigned long remaining = (apTimeout > elapsed) ? (apTimeout - elapsed) : 0;

    // Print remaining time
    if (millis() - lastCountdownPrint >= 1000) {
      Serial.print("AP Mode active, remaining time: ");
      Serial.print(remaining / 1000);
      Serial.println(" sec");
      lastCountdownPrint = millis();
    }

    if (elapsed > apTimeout) {
      Serial.println("AP Mode timeout, restarting...");
      ESP.restart();   // Restart device to try reconnecting to WiFi
    }
  }
}

//---------- Function to handle current sensor data  ----------
float readCurrent() {
  double current = emon1.calcIrms(1480);
  if (current < 0.01) {
    current = 0;
  }
  char currentStr[6];
  dtostrf(current, 6, 3, currentStr);
  return atof(currentStr);
}

//---------- Function to bulit monitoring data  ----------
String buildMonitoringJSON(bool addTimestamp = false) {
  StaticJsonDocument<512> doc;

  doc["box_id"] = hardwareId;

  // Input status
  for (int i = 0; i < NUM_INPUTS; i++) {
    String key = "I" + String(i + 1);
    doc[key] = digitalRead(INPUT_PINS[i]) ? 0 : 1;
  }

  // Output status
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    String key = "O" + String(i + 1);
    doc[key] = digitalRead(OUTPUT_PINS[i]) ? 1 : 0;
  }

  // char currentStr[6];
  // dtostrf(readCurrent(), 0, 2, currentStr);
  // doc["I"] = String(currentStr);

  // Add timestamp if requested
  if (addTimestamp) {
    doc["timestamp"] = getTimestamp();
  }

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

String buildDefaultJSON() {
  StaticJsonDocument<512> doc;

  doc["box_id"] = hardwareId;

  // inputs = 0
  for (int i = 0; i < NUM_INPUTS; i++) {
    String key = "I" + String(i + 1);
    doc[key] = 0;
  }

  // Outputs = 0
  for (int i = 0; i < NUM_OUTPUTS; i++) {
    String key = "O" + String(i + 1);
    doc[key] = 0;
  }

  // Current = 0
  doc["I"] = "0.00";

  // Timestamp
  doc["timestamp"] = getTimestamp();

  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "2025-01-01 00:00:00";  // default timestamp
  }

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

//---------- Function send monitoring data  ----------
void sendMonitoringData() {
  bool isOnline = (WiFi.status() == WL_CONNECTED && webSocket.isConnected());

  // Check for transition from online to offline
  if (!isOnline && wasOnline) {
    String defaultData = buildDefaultJSON();
    if (monitoringBuffer.size() >= MAX_BUFFER_SIZE) {
      monitoringBuffer.erase(monitoringBuffer.begin());
    }
    monitoringBuffer.push_back(defaultData);
    Serial.println("Buffered (default snapshot on disconnect): " + defaultData);
  }

  String data = buildMonitoringJSON();  // Normal monitoring data

  if (isOnline) {
    // === ONLINE ===
    webSocket.sendTXT(data);
    Serial.println("Sending (live): " + data);
  } else {
    // === OFFLINE ===
    String newData = buildMonitoringJSON(true);
    if (monitoringBuffer.size() >= MAX_BUFFER_SIZE) {
      monitoringBuffer.erase(monitoringBuffer.begin());
    }
    monitoringBuffer.push_back(newData);
    Serial.println("Buffered (offline): " + newData);
  }

  // Send to Serial2 for PUSR
  Serial2.println(data);

  // Update status
  wasOnline = isOnline;
}

//---------- Function Access Point  ----------
void setupAP() {
  WiFi.mode(WIFI_AP);
  String apSSID = ap_ssid + hardwareId;
  const char* apSSID_c = apSSID.c_str();
  WiFi.softAP(apSSID_c, ap_password);
  Serial.println("\nAP Mode: " + WiFi.softAPIP().toString());
  Serial.println("Please connect to the Access Point and configure the device.");

  apStartTime = millis();  // Start timeout timer
}

//---------- Function Connect to WiFi  ----------
bool connectToWiFi() {
  WiFi.mode(WIFI_STA);  // Set mode Wifi

  // Parse IP address strings to IPAddress objects
  // IPAddress staticIP;
  // IPAddress gateway;
  // IPAddress subnet(255, 255, 255, 0);

  // if (!staticIP.fromString(hardwareIP)) {
  //     Serial.println("Error: Invalid hardware IP format");
  //     return false;
  // }

  // Put gateway IP as the last octet of hardware IP
  // String gatewayStr = hardwareIP.substring(0, hardwareIP.lastIndexOf('.')) + ".1";
  // if (!gateway.fromString(gatewayStr)) {
  //     Serial.println("Error: Invalid gateway IP format");
  //     return false;
  // }

  // Configure static IP if needed
  // if (!WiFi.config(staticIP, gateway, subnet)) {
  //     Serial.println("Error: Failed to configure Static IP");
  //     return false;
  // }

  // Start connecting to WiFi
  WiFi.begin(storedSSID.c_str(), storedPassword.c_str());

  // Timeout for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  // Check if connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi!");
    Serial.println("IP: " + WiFi.localIP().toString());
    // Verify if the assigned IP matches the configured static IP
    // if (WiFi.localIP() != staticIP) {
    //     Serial.println("Warning: Assigned IP different from configured static IP");
    //     return false;
    // }
    return true;
  }
  return false;
}

//---------- Function to ensure WiFi is connected ----------
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiReconnecting = false;  // Reset flag jika sudah terhubung
    return;
  }

  if (!wifiReconnecting) {
    Serial.println("WiFi disconnected. Starting reconnect...");
    WiFi.disconnect();
    WiFi.begin(storedSSID.c_str(), storedPassword.c_str());
    wifiReconnectStart = millis();
    wifiReconnecting = true;
  } else {
    if (millis() - wifiReconnectStart > 10000) {
      Serial.println("Reconnect timeout. Failed to connect.");
      wifiReconnecting = false;  // Stop trying for now
      return;
    }

    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 500) {
      Serial.print(".");
      lastPrint = millis();
    }

    // Check if reconnected
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconnected to WiFi!");
      Serial.println("IP: " + WiFi.localIP().toString());
      wifiReconnecting = false;
    }
  }
}

//---------- Function to setup ws ----------
void setupWebSocket() {
  webSocket.begin(serverIP.c_str(), monitoring_port, "/ws"); // Connect to WebSocket server
  webSocket.onEvent(webSocketEvent);                         // Set callback event
  webSocket.setReconnectInterval(500);                       // Reconnect every 0.5s if disconnected
  Serial.println("WS Client started");
  Serial.println("Connecting to ws server: " + serverIP);
}

//---------- Function to handle event WebSocket ----------
void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("Disconnect from websocket!");
      readytoSend = false;
      break;
    case WStype_CONNECTED:
      Serial.println("Connect to websocket!");

      // Send initial JSON with box_id
      {
        StaticJsonDocument<100> doc;
        doc["box_id"] = hardwareId;
        String jsonString;
        serializeJson(doc, jsonString);
        webSocket.sendTXT(jsonString);
        Serial.println("Sent initial JSON: " + jsonString);
        delay(200);
      }

      // Send buffered data if any
      if (monitoringBuffer.size() > 0) {
        Serial.println("Reconnected: Sending buffered data...");
        for (String& bufferedData : monitoringBuffer) {
          webSocket.sendTXT(bufferedData);
          delay(200);  // delay to avoid flooding the server
        }
        monitoringBuffer.clear();  // Clear the buffer after sending
      }
      readytoSend = true;
      init2Ws = true;
      break;
    case WStype_TEXT: {
        String data = String((char*)payload);
        Serial.println("Data received: " + data);

        StaticJsonDocument<400> doc;
        DeserializationError error = deserializeJson(doc, data);

        if (error) {
          Serial.println("Error parsing JSON!");
          return;
        }

        if (doc.containsKey("box_id") && doc.containsKey("cmd")) {
          String receivedId = doc["box_id"].as<String>();
          String cmd = doc["cmd"].as<String>();

          // === CMD: config ===
          if (receivedId == hardwareId && cmd == "config") {
            StaticJsonDocument<400> resp;
            resp["server_ip"]   = serverIP;
            resp["hw_ip"]       = WiFi.localIP().toString();
            resp["box_id"]      = hardwareId;
            resp["port"]        = monitoring_port;
            resp["ssid"]        = storedSSID;
            resp["pass"]        = storedPassword;
            resp["mac_address"] = WiFi.macAddress();

            String jsonOut;
            serializeJson(resp, jsonOut);

            webSocket.sendTXT(jsonOut);
            Serial.println("Send config JSON:");
            Serial.println(jsonOut);
            return;
          }

          // === CMD: restartesp ===
          if (receivedId == hardwareId && cmd == "restartesp") {
            Serial.println("Received restart command. Sending confirmation...");

            StaticJsonDocument<200> resp;
            resp["box_id"] = hardwareId;
            resp["status"] = "restarting";

            String jsonOut;
            serializeJson(resp, jsonOut);
            webSocket.sendTXT(jsonOut);

            Serial.println("Sent confirmation JSON:");
            Serial.println(jsonOut);

            delay(1000);
            ESP.restart();
            return;
          }
        }

        if (doc.containsKey("box_id")) {
          String receivedId = doc["box_id"].as<String>();
          if (receivedId == hardwareId) {
            bool updated = false;

            if (doc.containsKey("server_ip")) {
              serverIP = doc["server_ip"].as<String>();
              writeToEEPROM(SERVER_IP_ADDR, serverIP);
              updated = true;
            }
            if (doc.containsKey("port")) {
              monitoring_port = doc["port"].as<int>();
              writeToEEPROM(SERVER_PORT_ADDR, String(monitoring_port));
              updated = true;
            }
            if (doc.containsKey("ssid")) {
              storedSSID = doc["ssid"].as<String>();
              writeToEEPROM(WIFI_SSID_ADDR, storedSSID);
              updated = true;
            }
            if (doc.containsKey("pass")) {
              storedPassword = doc["pass"].as<String>();
              writeToEEPROM(WIFI_PASS_ADDR, storedPassword);
              updated = true;
            }

            if (updated) {
              // Commit supaya data benar-benar tersimpan
              EEPROM.commit();

              // Konfirmasi ke server
              StaticJsonDocument<200> resp;
              resp["box_id"] = hardwareId;
              resp["status"] = "config_updated";

              String jsonOut;
              serializeJson(resp, jsonOut);
              webSocket.sendTXT(jsonOut);

              Serial.println("Sent config_updated JSON:");
              Serial.println(jsonOut);

              delay(1000);
              ESP.restart(); // restart supaya config baru dipakai
              return;
            }

            for (int i = 0; i < NUM_OUTPUTS; i++) {
              String key = "O" + String(i + 1);
              if (doc.containsKey(key)) {
                int state = doc[key].as<int>();
                digitalWrite(OUTPUT_PINS[i], state == 1 ? HIGH : LOW);
              }
            }
          } else {
            Serial.println("Ignore commands for ID: " + receivedId);
          }
        } else {
          //          Serial.println("No box_id in received data!");
        }
        break;
      }

  }
}

//---------- Function handle main page ----------
void handleRoot() {
  if (!isAuthenticated) {
    server.send(200, "text/html", loginHTML);  // Show login page if not authenticated
  } else {
    // Show configuration page if authenticated
    String html = String(configHTML);
    html.replace("%HARDWARE_ID%", hardwareId);
    html.replace("%HARDWARE_IP%", hardwareIP);
    html.replace("%SERVER_IP%", serverIP);
    html.replace("%WIFI_SSID%", storedSSID);
    html.replace("%WIFI_PASS%", storedPassword);
    server.send(200, "text/html", html.c_str());
  }
}

//---------- Function to check if the user is authenticated ----------
bool checkAuth(String username, String password) {
  return (username == default_username && password == default_password);
}

//---------- Function to handle login form submission ----------
void handleAuth() {
  String username = server.arg("username");  // Take username from form
  String password = server.arg("password");  // Take password from form

  // Check if the username and password are correct
  if (checkAuth(username, password)) {
    isAuthenticated = true;
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");  // Redirect to main page
  } else {
    server.send(401, "text/html", loginHTML);  // Show login page with error
  }
}

//---------- Function to save configuration ----------
void handleSaveConfig() {
  // Check if the user is authenticated
  if (!isAuthenticated) {
    server.send(401, "text/html", loginHTML);
    return;
  }

  // Take configuration data from form
  String newHardwareId = server.arg("hardwareId");
  String newHardwareIP = server.arg("hardwareIP");
  String newServerIP = server.arg("serverIP");
  String newSSID = server.arg("ssid");
  String newPassword = server.arg("password");

  // Validate input data
  if (newHardwareId.length() > 0 && newHardwareIP.length() > 0 && newServerIP.length() > 0 && newSSID.length() > 0 && newPassword.length() > 0) {

    // Save configuration to EEPROM
    writeToEEPROM(HARDWARE_ID_ADDR, newHardwareId);
    writeToEEPROM(HARDWARE_IP_ADDR, newHardwareIP);
    writeToEEPROM(SERVER_IP_ADDR, newServerIP);
    writeToEEPROM(WIFI_SSID_ADDR, newSSID);
    writeToEEPROM(WIFI_PASS_ADDR, newPassword);

    EEPROM.commit();  // Commit changes to EEPROM

    // Update global variables
    hardwareId = newHardwareId;
    hardwareIP = newHardwareIP;
    serverIP = newServerIP;
    storedSSID = newSSID;
    storedPassword = newPassword;

    // Show success page
    server.send(200, "text/html", successHTML);
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "Input tidak valid");
  }
}

//---------- Function to read configuration from EEPROM ----------
String readFromEEPROM(int address) {
  String value = "";
  char c;
  while ((c = EEPROM.read(address++)) != '\0') {
    value += c;
  }
  return value;
}

//---------- Function to write configuration to EEPROM ----------
void writeToEEPROM(int address, String value) {
  for (int i = 0; i < value.length(); i++) {
    EEPROM.write(address + i, value[i]);
  }
  EEPROM.write(address + value.length(), '\0');  // Add null terminator
}

// Function to handle serial input for configuration
void handleSerialInput() {
  static String inputString = "";

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      inputString.trim();

      if (inputString.equalsIgnoreCase("config")) {
        Serial.println("==> Current Configuration:");
        Serial.println("{");
        Serial.println("  \"server_ip\": \"" + serverIP + "\",");
        Serial.println("  \"hw_ip\": \"" + WiFi.localIP().toString() + "\",");
        Serial.println("  \"box_id\": \"" + hardwareId + "\",");
        Serial.println("  \"port\": " + String(monitoring_port) + ",");
        Serial.println("  \"ssid\": \"" + storedSSID + "\",");
        Serial.println("  \"pass\": \"" + storedPassword + "\"");
        Serial.println("  \"mac_address\": \"" + WiFi.macAddress() + "\"");
        Serial.println("}");
        inputString = "";
        return;
      } else if (inputString.equalsIgnoreCase("restartesp")) {
        Serial.println("==> Restart ESP32");
        ESP.restart();
        inputString = "";
        return;
      }  else if (inputString.equalsIgnoreCase("vers")) {
        Serial.println(program_version);
        inputString = "";
        return;
      } else if (inputString.equalsIgnoreCase("help")) {
        Serial.println("==> Available commands:");
        Serial.println("  help           - Show this help message");
        Serial.println("  config         - Show current configuration");
        Serial.println("  restartesp     - Restart the ESP32");
        Serial.println("  <JSON>         - Send new configuration in JSON format");
        Serial.println("");
        Serial.println("==> JSON format example:");
        Serial.println("{");
        Serial.println("  \"server_ip\": \"192.168.1.100\",  // Server IP address");
        Serial.println("  \"port\": 80,                    // Port number for monitoring");
        Serial.println("  \"box_id\": \"001\",               // Unique hardware identifier");
        Serial.println("  \"hw_ip\": \"192.168.1.50\",       // Static IP address for ESP32");
        Serial.println("  \"ssid\": \"YourWiFiSSID\",        // WiFi SSID");
        Serial.println("  \"pass\": \"YourWiFiPassword\"     // WiFi Password");
        Serial.println("}");
        inputString = "";
        return;
      }

      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, inputString);
      if (!error) {
        if (doc.containsKey("server_ip")) {
          serverIP = doc["server_ip"].as<String>();
          writeToEEPROM(SERVER_IP_ADDR, serverIP);
        }
        if (doc.containsKey("box_id")) {
          hardwareId = doc["box_id"].as<String>();
          writeToEEPROM(HARDWARE_ID_ADDR, hardwareId);
        }
        if (doc.containsKey("port")) {
          monitoring_port = doc["port"].as<int>();
          writeToEEPROM(SERVER_PORT_ADDR, String(monitoring_port));
        }
        if (doc.containsKey("hw_ip")) {
          hardwareIP = doc["hw_ip"].as<String>();
          writeToEEPROM(HARDWARE_IP_ADDR, hardwareIP);
        }
        if (doc.containsKey("ssid")) {
          storedSSID = doc["ssid"].as<String>();
          writeToEEPROM(WIFI_SSID_ADDR, storedSSID);
        }
        if (doc.containsKey("pass")) {
          storedPassword = doc["pass"].as<String>();
          writeToEEPROM(WIFI_PASS_ADDR, storedPassword);
        }

        EEPROM.commit();
        Serial.println("==> Configuration saved:");
        serializeJsonPretty(doc, Serial);
        Serial.println("\n Restarting ESP32 . . .");
      } else {
        Serial.println("==> JSON parsing error: " + String(error.c_str()));
      }

      inputString = "";
    } else {
      inputString += c;
    }
  }
}
