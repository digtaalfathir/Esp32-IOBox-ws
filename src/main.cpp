// ===== main.cpp =====
// C++ Standard Libraries
#include <vector>

// Core Arduino Libraries
#include <WiFi.h>
#include <Ethernet.h>
#include <Wire.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include <SPI.h>
#include <ArduinoOTA.h>
#include <time.h>

// Third-party Libraries
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <PCF8574.h>

// =================================================================
// --- Program Constants and Version ---
// =================================================================
const String program_version = "v2.8.0-FullFeatures-HybridWS";

// =================================================================
// --- Hardware & Pin Definitions ---
// =================================================================
const int eth_cs_pin = 5;
const int eth_sclk_pin = 18;
const int eth_mosi_pin = 23;
const int eth_miso_pin = 19;
const int eth_int_pin = -1;
const int eth_rst_pin = -1;
const int eth_power_pin = -1;

const int NUM_PCF_INPUTS = 8;
const int NUM_GPIO_INPUTS = 8;
const uint8_t PCF_PIN_MAP[NUM_PCF_INPUTS] = {0, 1, 2, 3, 4, 5, 6, 7};
const int INPUT_GPIO_PINS[NUM_GPIO_INPUTS] = {32, 33, 25, 26, 27, 14, 12, 13};
const int OUTPUT_PINS[] = {17, 16, 4, 15};

const int NUM_INPUTS = NUM_PCF_INPUTS + NUM_GPIO_INPUTS;
const int NUM_OUTPUTS = sizeof(OUTPUT_PINS) / sizeof(OUTPUT_PINS[0]);

// =================================================================
// --- Network & Server Configuration ---
// =================================================================
String ap_ssid = "IoT-Node ";
const char *ap_password = "12345678";
const char *default_username = "contoh";
const char *default_password = "password";
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

// =================================================================
// --- EEPROM Configuration ---
// =================================================================
#define EEPROM_SIZE 512
#define HARDWARE_ID_ADDR 0
#define SERVER_IP_ADDR 32
#define WIFI_SSID_ADDR 64
#define WIFI_PASS_ADDR 128
#define SERVER_PORT_ADDR 192
#define PATH_ADDR 200
#define COMM_MODE_ADDR 232
#define NETWORK_MODE_ADDR 248
#define ACCESS_TOKEN_ADDR 280
#define IO_STATE_START_ADDR 350
#define OUTPUT_STATE_START_ADDR 366
#define TIMESTAMP_ADDR 370
#define IO_STATE_SIGNATURE_ADDR 374
#define IO_STATE_SIGNATURE 0xA5

// =================================================================
// --- Timing & Control ---
// =================================================================
const unsigned long wifiTryTime = 10 * 1000;
const unsigned long ethTryTime = 10 * 1000;
const unsigned long apTimeout = 150 * 1000;
const unsigned long reconnectInterval = 10 * 1000;
const unsigned long singleAttemptTimeout = 8000;
unsigned long mqttReconnectInterval = 1000; // agresif saat reconnect
const long monitoringInterval = 200;
const unsigned long forceSendInterval = 20 * 60 * 1000;
const size_t MAX_BUFFER_SIZE = 100;

int reconnectAttemptCount = 0;
const int maxReconnectAttempts = 2;

// =================================================================
// --- Configuration Structure ---
// =================================================================
struct Config
{
  String hardwareId, networkMode, serverIP, path, commMode, wifiSSID, wifiPass, accessTokenMQTT;
  int monitoringPort;
};
Config config;

// =================================================================
// --- Globals ---
// =================================================================
WebServer server(80);
PCF8574 pcf8574(0x20);
WiFiClient wifiClient;
EthernetClient ethClient;
WebSocketClient *ws = nullptr; // dari ArduinoHttpClient
PubSubClient mqttClient;

bool isAPMode = false;
bool isAuthenticated = false;
bool isReconnecting = false;
bool readyToSend = false;
static bool eth_connected = false;
static bool wifi_connected = false;
static bool network_ready = false;

unsigned long apStartTime = 0;
unsigned long lastCountdownPrint = 0;
unsigned long lastMonitoringTime = 0;
unsigned long lastForceSendTime = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long reconnectAttemptStartTime = 0;
unsigned long lastMqttReconnectAttempt = 0;

uint8_t lastInputStates[NUM_INPUTS] = {0};
std::vector<String> monitoringBuffer;

// =================================================================
// --- Forward Declarations ---
// =================================================================
void publishStatus();
void publishTelemetry();
bool saveConfig();
void sendMonitoringData();
void setupCommunication();
void setupOTA();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void reconnectMQTT();

// Web Server Handlers
void handleRoot();
void handleAuth();
void handleDashboard();
void handleConfigPage();
void handleControlPage();
void handleStatusPage();
void handleSaveConfig();
void handleToggle();
void handleGetOutputs();
void handleDownloadConfig();
void handleLogout();
void serveErrorPage(String errorMessage);

// Other functions
String readFromEEPROM(int address);
void writeToEEPROM(int address, const String &value);
void startAPMode();
void tryReconnect();
bool initEthernet();
void maintainEthernet();
void checkEthernetStatus();
void startWifiFallback();
void switchToEthernetPrefered();
void wsDisconnect();
bool wsConnect();
bool wsSendText(const String &s);
void wsPump();
bool loadConfig();

// Helper: pilih client aktif (WiFi/Ethernet)
Client &activeClient()
{
  return eth_connected ? (Client &)ethClient : (Client &)wifiClient;
}

// =================================================================
// --- EEPROM Helpers ---
// =================================================================
String readFromEEPROM(int address)
{
  String value = "";
  char ch;
  int currentAddr = address;
  while (currentAddr < EEPROM_SIZE)
  {
    ch = EEPROM.read(currentAddr);
    if (ch == '\0')
      break;
    value += ch;
    currentAddr++;
    if (value.length() > 64)
      break;
  }
  return value;
}
void writeToEEPROM(int address, const String &value)
{
  int i = 0;
  for (; i < value.length() && (address + i) < (EEPROM_SIZE - 1); i++)
  {
    EEPROM.write(address + i, value[i]);
  }
  EEPROM.write(address + i, '\0');
}

// =================================================================
// --- EEPROM I/O STATE ---
// =================================================================
void saveIOStateToEEPROM()
{
  for (int i = 0; i < NUM_INPUTS; i++)
    EEPROM.write(IO_STATE_START_ADDR + i, lastInputStates[i]);
  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    uint8_t outputState = digitalRead(OUTPUT_PINS[i]);
    EEPROM.write(OUTPUT_STATE_START_ADDR + i, outputState);
  }
  unsigned long timestamp = millis();
  EEPROM.write(TIMESTAMP_ADDR + 0, (timestamp >> 24) & 0xFF);
  EEPROM.write(TIMESTAMP_ADDR + 1, (timestamp >> 16) & 0xFF);
  EEPROM.write(TIMESTAMP_ADDR + 2, (timestamp >> 8) & 0xFF);
  EEPROM.write(TIMESTAMP_ADDR + 3, timestamp & 0xFF);
  EEPROM.write(IO_STATE_SIGNATURE_ADDR, IO_STATE_SIGNATURE);
  EEPROM.commit();
}
void loadIOStateFromEEPROM()
{
  if (EEPROM.read(IO_STATE_SIGNATURE_ADDR) != IO_STATE_SIGNATURE)
    return;
  for (int i = 0; i < NUM_INPUTS; i++)
    lastInputStates[i] = EEPROM.read(IO_STATE_START_ADDR + i);
  for (int i = 0; i < NUM_OUTPUTS; i++)
    digitalWrite(OUTPUT_PINS[i], EEPROM.read(OUTPUT_STATE_START_ADDR + i));
}

// =================================================================
// --- WiFi Event Handler ---
// =================================================================
void networkEventHandler(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    if (config.networkMode == "WiFi" || config.networkMode == "Hybrid")
    {
      Serial.println("\n[WiFi] Connected!");
      Serial.println("IP: " + WiFi.localIP().toString());
      wifi_connected = true;
      // Rebind WS & MQTT ke WiFi
      if (config.commMode == "ws")
      {
        wsDisconnect();
        wsConnect();
      }
      if (config.commMode == "mqtt")
      {
        lastMqttReconnectAttempt = 0;
        mqttReconnectInterval = 1000;
      }
    }
    else
    {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    wifi_connected = false;
    if (config.networkMode == "WiFi")
      network_ready = false;
    break;
  default:
    break;
  }
}

// =================================================================
// --- Ethernet (W5500) ---
// =================================================================
bool initEthernet()
{
  Serial.println("\n[Ethernet] Initializing...");
  if (eth_rst_pin >= 0)
  {
    pinMode(eth_rst_pin, OUTPUT);
    digitalWrite(eth_rst_pin, LOW);
    delay(50);
    digitalWrite(eth_rst_pin, HIGH);
    delay(200);
  }
  SPI.begin(eth_sclk_pin, eth_miso_pin, eth_mosi_pin, eth_cs_pin);
  Ethernet.init(eth_cs_pin);

  uint64_t chipid = ESP.getEfuseMac();
  byte mac[6];
  mac[0] = 0xDE;
  mac[1] = 0xAD;
  mac[2] = (chipid >> 32) & 0xFF;
  mac[3] = (chipid >> 24) & 0xFF;
  mac[4] = (chipid >> 16) & 0xFF;
  mac[5] = (chipid >> 8) & 0xFF;

  Serial.print("  MAC: ");
  for (int i = 0; i < 6; i++)
  {
    if (mac[i] < 16)
      Serial.print("0");
    Serial.print(mac[i], HEX);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();

  Serial.print("  Trying DHCP... ");
  if (Ethernet.begin(mac, 10000, 4000) == 0)
  {
    Serial.println("FAILED");
    Serial.print("  Trying Static IP... ");
    IPAddress ip(192, 168, 0, 100), dns(8, 8, 8, 8), gateway(192, 168, 0, 1), subnet(255, 255, 255, 0);
    Ethernet.begin(mac, ip, dns, gateway, subnet);
    delay(1000);
  }
  else
  {
    Serial.println("OK");
  }

  delay(500);
  if (Ethernet.linkStatus() == LinkOFF)
  {
    Serial.println("  No cable detected!");
    return false;
  }

  Serial.println("  Cable connected");
  Serial.print("  IP: ");
  Serial.println(Ethernet.localIP());
  Serial.print("  GW: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("  MASK: ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("  DNS: ");
  Serial.println(Ethernet.dnsServerIP());
  return true;
}

void maintainEthernet()
{
  if (eth_connected)
    Ethernet.maintain();
  if (Ethernet.linkStatus() == LinkOFF && eth_connected)
  {
    Serial.println("[Ethernet] Cable unplugged!");
    eth_connected = false;
    if (config.networkMode == "Ethernet")
      network_ready = false;
  }
}

void checkEthernetStatus()
{
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 500)
    return;
  lastCheck = millis();
  auto link = Ethernet.linkStatus();
  if (link == LinkON)
  {
    if (!eth_connected)
    {
      Serial.println("\n[Ethernet] Link UP!");
      Serial.print("  IP: ");
      Serial.println(Ethernet.localIP());
      eth_connected = true;
      network_ready = true;
      if (config.networkMode == "Hybrid")
        switchToEthernetPrefered();
    }
    Ethernet.maintain();
  }
  else
  {
    if (eth_connected)
    {
      Serial.println("[Ethernet] Link DOWN");
      eth_connected = false;
      if (config.networkMode == "Hybrid")
      {
        startWifiFallback();
        if (config.commMode == "mqtt")
        {
          if (mqttClient.connected())
            mqttClient.disconnect();
          lastMqttReconnectAttempt = 0;
          mqttReconnectInterval = 1000;
        }
        if (config.commMode == "ws")
        {
          wsDisconnect();
          wsConnect();
        }
      }
      else if (config.networkMode == "Ethernet")
      {
        network_ready = false;
      }
    }
  }
}

void startWifiFallback()
{
  if (config.wifiSSID.isEmpty())
    return;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  if (WiFi.getMode() != WIFI_STA)
    WiFi.mode(WIFI_STA);

  int n = WiFi.scanNetworks();
  int idx = -1;
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i) == config.wifiSSID)
    {
      idx = i;
      break;
    }

  if (idx >= 0)
  {
    const uint8_t *bssid = WiFi.BSSID(idx);
    int ch = WiFi.channel(idx);
    int rssi = WiFi.RSSI(idx);
    Serial.printf("[Hybrid] WiFi fallback to %s ch %d RSSI %d\n", config.wifiSSID.c_str(), ch, rssi);
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str(), ch, bssid, true);
  }
  else
  {
    Serial.println("[Hybrid] Target SSID not found, normal begin()");
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
  }
}

void switchToEthernetPrefered()
{
  if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED)
  {
    Serial.println("[Hybrid] Ethernet is back. Disconnecting WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_connected = false;
  }
  if (config.commMode == "mqtt")
  {
    mqttClient.setClient(ethClient);
    if (mqttClient.connected())
      mqttClient.disconnect();
    lastMqttReconnectAttempt = 0;
    mqttReconnectInterval = 1000;
  }
  if (config.commMode == "ws")
  {
    wsDisconnect();
    wsConnect();
  }
}

// =================================================================
// --- File System & Config ---
// =================================================================
bool loadConfig()
{
  bool loadedFromJson = false;
  File file = LittleFS.open("/config.json", "r");
  if (file)
  {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error)
    {
      config.hardwareId = doc["hardwareId"] | "unconfigured_json";
      JsonObject serverObj = doc["server"];
      config.serverIP = serverObj["serverIP"] | "";
      config.path = serverObj["path"] | "/ws";
      config.monitoringPort = doc["monitoringPort"] | 80;
      config.commMode = doc["commMode"] | "ws";
      JsonObject wifiObj = doc["wifi"];
      config.wifiSSID = wifiObj["ssid"] | "";
      config.wifiPass = wifiObj["password"] | "";
      config.accessTokenMQTT = doc["accessTokenMQTT"] | "";
      config.networkMode = doc["networkMode"] | "Hybrid";
      Serial.println("Configuration loaded from LittleFS (JSON).");
      loadedFromJson = true;
    }
    else
    {
      Serial.println("ERR: Parse JSON: " + String(error.c_str()) + ". Trying EEPROM...");
    }
  }
  else
  {
    Serial.println("config.json not found. Trying EEPROM...");
  }

  if (!loadedFromJson)
  {
    Serial.println("Loading from EEPROM...");
    config.hardwareId = readFromEEPROM(HARDWARE_ID_ADDR);
    config.serverIP = readFromEEPROM(SERVER_IP_ADDR);
    config.path = readFromEEPROM(PATH_ADDR);
    config.monitoringPort = readFromEEPROM(SERVER_PORT_ADDR).toInt();
    config.commMode = readFromEEPROM(COMM_MODE_ADDR);
    config.wifiSSID = readFromEEPROM(WIFI_SSID_ADDR);
    config.wifiPass = readFromEEPROM(WIFI_PASS_ADDR);
    config.networkMode = readFromEEPROM(NETWORK_MODE_ADDR);
    config.accessTokenMQTT = readFromEEPROM(ACCESS_TOKEN_ADDR);
    if (config.hardwareId.length() == 0)
      config.hardwareId = "unconfigured_eeprom";
    if (config.path.length() == 0)
      config.path = "/wsiot";
    if (config.monitoringPort <= 0)
      config.monitoringPort = 80;
    if (config.commMode.length() == 0)
      config.commMode = "ws";
    if (config.networkMode.length() == 0)
      config.networkMode = "Hybrid";
  }

  config.hardwareId.trim();
  config.networkMode.trim();
  config.serverIP.trim();
  config.path.trim();
  config.commMode.trim();
  config.wifiSSID.trim();
  config.wifiPass.trim();
  config.accessTokenMQTT.trim();

  if (!config.networkMode.equalsIgnoreCase("WiFi") &&
      !config.networkMode.equalsIgnoreCase("Ethernet") &&
      !config.networkMode.equalsIgnoreCase("Hybrid"))
  {
    Serial.println("[WARN] Invalid networkMode, setting to Hybrid");
    config.networkMode = "Hybrid";
  }

  Serial.println("Syncing initial config to LittleFS...");
  saveConfig();
  return true;
}

bool saveConfig()
{
  bool successJson = false;
  bool successEeprom = false;
  StaticJsonDocument<512> doc;
  doc["hardwareId"] = config.hardwareId;
  JsonObject serverObj = doc.createNestedObject("server");
  serverObj["serverIP"] = config.serverIP;
  serverObj["path"] = config.path;
  doc["monitoringPort"] = config.monitoringPort;
  doc["commMode"] = config.commMode;
  doc["networkMode"] = config.networkMode;
  doc["accessTokenMQTT"] = config.accessTokenMQTT;
  JsonObject wifiObj = doc.createNestedObject("wifi");
  wifiObj["ssid"] = config.wifiSSID;
  wifiObj["password"] = config.wifiPass;
  File file = LittleFS.open("/config.json", "w");
  if (file)
  {
    successJson = (serializeJson(doc, file) > 0);
    file.close();
  }
  writeToEEPROM(HARDWARE_ID_ADDR, config.hardwareId);
  writeToEEPROM(SERVER_IP_ADDR, config.serverIP);
  writeToEEPROM(WIFI_SSID_ADDR, config.wifiSSID);
  writeToEEPROM(WIFI_PASS_ADDR, config.wifiPass);
  writeToEEPROM(SERVER_PORT_ADDR, String(config.monitoringPort));
  writeToEEPROM(PATH_ADDR, config.path);
  writeToEEPROM(COMM_MODE_ADDR, config.commMode);
  writeToEEPROM(NETWORK_MODE_ADDR, config.networkMode);
  writeToEEPROM(ACCESS_TOKEN_ADDR, config.accessTokenMQTT);
  successEeprom = EEPROM.commit();
  return successJson && successEeprom;
}

String loadFile(const char *path)
{
  File file = LittleFS.open(path, "r");
  if (!file)
    return "";
  String content = file.readString();
  file.close();
  return content;
}

// =================================================================
// --- JSON Builders ---
// =================================================================
String buildStatusJSON()
{
  StaticJsonDocument<512> doc;
  doc["type"] = "status";
  doc["hardwareId"] = config.hardwareId;
  doc["firmware_version"] = program_version;
  doc["commMode"] = config.commMode;
  doc["serverIP"] = config.serverIP;
  doc["monitoringPort"] = config.monitoringPort;
  doc["networkMode"] = config.networkMode;
  if (wifi_connected)
  {
    doc["connection_type"] = "WiFi";
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mac_address"] = WiFi.macAddress();
    doc["wifiSSID"] = config.wifiSSID;
  }
  else if (eth_connected)
  {
    doc["connection_type"] = "Ethernet";
    doc["ip_address"] = Ethernet.localIP().toString();
  }
  else
  {
    doc["connection_type"] = "None";
  }
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

String buildMonitoringJSON(bool forThingsBoard = false)
{
  StaticJsonDocument<512> doc;
  if (!forThingsBoard)
  {
    doc["type"] = "telemetry";
    doc["box_id"] = config.hardwareId;
  }
  for (int i = 0; i < NUM_PCF_INPUTS; i++)
  {
    String kondisi_input = String(lastInputStates[i]);
    kondisi_input = (kondisi_input == "1") ? "0" : "1";
    doc["I" + String(i + 1)] = kondisi_input;
  }
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    String kondisi_input = String(lastInputStates[NUM_PCF_INPUTS + i]);
    kondisi_input = (kondisi_input == "1") ? "0" : "1";
    doc["I" + String(NUM_PCF_INPUTS + i + 1)] = kondisi_input;
  }
  for (int i = 0; i < NUM_OUTPUTS; i++)
    doc["Q" + String(i + 1)] = digitalRead(OUTPUT_PINS[i]);
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

// =================================================================
// --- MQTT ---
// =================================================================
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("[MQTT] Message received on topic: ");
  Serial.println(topic);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error)
  {
    Serial.println("[MQTT] JSON parse error");
    return;
  }

  if (!doc.containsKey("box_id") || doc["box_id"].as<String>() != config.hardwareId)
  {
    Serial.println("[MQTT] box_id mismatch, ignoring");
    return;
  }

  if (doc.containsKey("cmd"))
  {
    String cmd = doc["cmd"].as<String>();

    if (cmd == "config")
    {
      StaticJsonDocument<400> resp;
      resp["box_id"] = config.hardwareId;
      resp["serverIP"] = config.serverIP;
      resp["port"] = config.monitoringPort;
      resp["wifiSSID"] = config.wifiSSID;
      resp["networkMode"] = config.networkMode;

      if (wifi_connected)
      {
        resp["ip"] = WiFi.localIP().toString();
        resp["mac"] = WiFi.macAddress();
      }
      else if (eth_connected)
      {
        resp["ip"] = Ethernet.localIP().toString();
      }

      String jsonOut;
      serializeJson(resp, jsonOut);
      String topic = "iot-node/" + config.hardwareId + "/config_response";
      mqttClient.publish(topic.c_str(), jsonOut.c_str());
    }
    else if (cmd == "restart")
    {
      StaticJsonDocument<100> resp;
      resp["box_id"] = config.hardwareId;
      resp["status"] = "restarting";
      String jsonOut;
      serializeJson(resp, jsonOut);
      String topic = "iot-node/" + config.hardwareId + "/status";
      mqttClient.publish(topic.c_str(), jsonOut.c_str());
      delay(500);
      ESP.restart();
    }
  }
  else if (doc.containsKey("serverIP") || doc.containsKey("wifiSSID") ||
           doc.containsKey("monitoringPort") || doc.containsKey("wifiPass") ||
           doc.containsKey("networkMode"))
  {

    bool updated = false;
    if (doc.containsKey("serverIP") && doc["serverIP"].as<String>() != config.serverIP)
    {
      config.serverIP = doc["serverIP"].as<String>();
      updated = true;
    }
    if (doc.containsKey("monitoringPort") && doc["monitoringPort"].as<int>() != config.monitoringPort)
    {
      config.monitoringPort = doc["monitoringPort"].as<int>();
      updated = true;
    }
    if (doc.containsKey("wifiSSID") && doc["wifiSSID"].as<String>() != config.wifiSSID)
    {
      config.wifiSSID = doc["wifiSSID"].as<String>();
      updated = true;
    }
    if (doc.containsKey("wifiPass") && doc["wifiPass"].as<String>().length() > 0)
    {
      config.wifiPass = doc["wifiPass"].as<String>();
      updated = true;
    }
    if (doc.containsKey("networkMode"))
    {
      String newMode = doc["networkMode"].as<String>();
      if ((newMode == "Ethernet" || newMode == "WiFi" || newMode == "Hybrid") &&
          newMode != config.networkMode)
      {
        config.networkMode = newMode;
        updated = true;
      }
    }

    if (updated)
    {
      saveConfig();
      StaticJsonDocument<100> resp;
      resp["box_id"] = config.hardwareId;
      resp["status"] = "config_updated";
      String jsonOut;
      serializeJson(resp, jsonOut);
      String topic = "iot-node/" + config.hardwareId + "/status";
      mqttClient.publish(topic.c_str(), jsonOut.c_str());
      delay(1000);
      ESP.restart();
    }
  }
  else if (doc.containsKey("O1") || doc.containsKey("O2") ||
           doc.containsKey("O3") || doc.containsKey("O4"))
  {
    for (int i = 0; i < NUM_OUTPUTS; i++)
    {
      String key = "O" + String(i + 1);
      if (doc.containsKey(key))
      {
        int state = doc[key].as<int>();
        digitalWrite(OUTPUT_PINS[i], state == 1 ? HIGH : LOW);
      }
    }
    saveIOStateToEEPROM();
  }
}

void reconnectMQTT()
{
  if (millis() - lastMqttReconnectAttempt < mqttReconnectInterval)
    return;
  lastMqttReconnectAttempt = millis();
  mqttClient.setClient(activeClient());
  mqttClient.setServer(config.serverIP.c_str(), config.monitoringPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);

  Serial.println("\n=== MQTT CONNECTION ===");
  Serial.println("Server: " + config.serverIP + ":" + String(config.monitoringPort));
  String clientId = "IoTNode-" + config.hardwareId;
  Serial.println("ClientID: " + clientId);
  Serial.print("Connecting... ");

  bool ok = mqttClient.connect(clientId.c_str());
  if (ok)
  {
    Serial.println("✓ SUCCESS!");
    String cmdTopic = "iot-node/" + config.hardwareId + "/cmd";
    mqttClient.subscribe(cmdTopic.c_str());
    publishStatus();
    readyToSend = true;
    mqttReconnectInterval = 5000;
  }
  else
  {
    Serial.print("✗ FAILED! RC=");
    Serial.println(mqttClient.state());
    readyToSend = false;
    mqttReconnectInterval = 1000;
  }
  Serial.println("=======================\n");
}

void publishTelemetry()
{
  if (!mqttClient.connected())
    return;
  bool isThingsBoard = (config.serverIP.indexOf("thingsboard") >= 0);
  String data = buildMonitoringJSON(isThingsBoard);
  String telemetryTopic = isThingsBoard ? "v1/devices/me/telemetry" : "iot-node/" + config.hardwareId + "/telemetry";
  if (mqttClient.publish(telemetryTopic.c_str(), data.c_str()))
  {
    Serial.println("[MQTT] Telemetry sent to: " + telemetryTopic);
  }
  else
  {
    Serial.println("[MQTT] ✗ Failed to publish Telemetry.");
  }
}

// =================================================================
// --- WebSocket via ArduinoHttpClient ---
// =================================================================
bool wsConnect()
{
  if (ws)
  {
    delete ws;
    ws = nullptr;
  }
  ws = new WebSocketClient(activeClient(), config.serverIP.c_str(), config.monitoringPort);
  Serial.printf("[WS] Connecting to ws://%s:%d%s\n", config.serverIP.c_str(), config.monitoringPort, config.path.c_str());
  ws->begin(config.path.c_str());
  delay(50);
  bool ok = ws->connected();
  Serial.println(ok ? "[WS] Connected" : "[WS] Not connected yet");
  return ok;
}
void wsDisconnect()
{
  if (ws)
  {
    ws->stop();
    delete ws;
    ws = nullptr;
  }
}
bool wsSendText(const String &s)
{
  if (!ws || !ws->connected())
    return false;
  ws->beginMessage(TYPE_TEXT);
  ws->print(s);
  ws->endMessage();
  return true;
}
void wsPump()
{
  if (!ws)
    return;
  int msgType = ws->parseMessage();
  if (msgType == TYPE_TEXT)
  {
    String payload = ws->readString();
    StaticJsonDocument<400> doc;
    if (deserializeJson(doc, payload))
    {
      Serial.println("[WS] JSON parse error");
      return;
    }
    if (!doc.containsKey("box_id") || doc["box_id"].as<String>() != config.hardwareId)
      return;
    if (doc.containsKey("cmd"))
    {
      String cmd = doc["cmd"].as<String>();

      if (cmd == "config")
      {
        StaticJsonDocument<400> resp;
        resp["box_id"] = config.hardwareId;
        resp["serverIP"] = config.serverIP;
        resp["port"] = config.monitoringPort;
        resp["wifiSSID"] = config.wifiSSID;
        resp["networkMode"] = config.networkMode;

        if (wifi_connected)
        {
          resp["ip"] = WiFi.localIP().toString();
          resp["mac"] = WiFi.macAddress();
        }
        else if (eth_connected)
        {
          resp["ip"] = Ethernet.localIP().toString();
        }

        String jsonOut;
        serializeJson(resp, jsonOut);
        String topic = "iot-node/" + config.hardwareId + "/config_response";
        mqttClient.publish(topic.c_str(), jsonOut.c_str());
      }
      else if (cmd == "restart")
      {
        StaticJsonDocument<100> resp;
        resp["box_id"] = config.hardwareId;
        resp["status"] = "restarting";
        String jsonOut;
        serializeJson(resp, jsonOut);
        String topic = "iot-node/" + config.hardwareId + "/status";
        mqttClient.publish(topic.c_str(), jsonOut.c_str());
        delay(500);
        ESP.restart();
      }
    }
    else if (doc.containsKey("serverIP") || doc.containsKey("wifiSSID") ||
             doc.containsKey("monitoringPort") || doc.containsKey("wifiPass") ||
             doc.containsKey("networkMode"))
    {

      bool updated = false;
      if (doc.containsKey("serverIP") && doc["serverIP"].as<String>() != config.serverIP)
      {
        config.serverIP = doc["serverIP"].as<String>();
        updated = true;
      }
      if (doc.containsKey("monitoringPort") && doc["monitoringPort"].as<int>() != config.monitoringPort)
      {
        config.monitoringPort = doc["monitoringPort"].as<int>();
        updated = true;
      }
      if (doc.containsKey("wifiSSID") && doc["wifiSSID"].as<String>() != config.wifiSSID)
      {
        config.wifiSSID = doc["wifiSSID"].as<String>();
        updated = true;
      }
      if (doc.containsKey("wifiPass") && doc["wifiPass"].as<String>().length() > 0)
      {
        config.wifiPass = doc["wifiPass"].as<String>();
        updated = true;
      }
      if (doc.containsKey("networkMode"))
      {
        String newMode = doc["networkMode"].as<String>();
        if ((newMode == "Ethernet" || newMode == "WiFi" || newMode == "Hybrid") &&
            newMode != config.networkMode)
        {
          config.networkMode = newMode;
          updated = true;
        }
      }

      if (updated)
      {
        saveConfig();
        StaticJsonDocument<100> resp;
        resp["box_id"] = config.hardwareId;
        resp["status"] = "config_updated";
        String jsonOut;
        serializeJson(resp, jsonOut);
        String topic = "iot-node/" + config.hardwareId + "/status";
        mqttClient.publish(topic.c_str(), jsonOut.c_str());
        delay(1000);
        ESP.restart();
      }
    }
    else if (doc.containsKey("O1") || doc.containsKey("O2") ||
             doc.containsKey("O3") || doc.containsKey("O4"))
    {
      for (int i = 0; i < NUM_OUTPUTS; i++)
      {
        String key = "O" + String(i + 1);
        if (doc.containsKey(key))
        {
          int state = doc[key].as<int>();
          digitalWrite(OUTPUT_PINS[i], state == 1 ? HIGH : LOW);
        }
      }
      saveIOStateToEEPROM();
    }
  }
  else if (!ws->connected())
  {
    static unsigned long lastTry = 0;
    if (millis() - lastTry > 1000)
    {
      lastTry = millis();
      wsConnect();
    }
  }
}

// =================================================================
// --- HTTP/WS/MQTT Send Helpers ---
// =================================================================
void sendMonitoringData()
{
  if (!network_ready)
    return;
  bool isThingsBoard = (config.serverIP == "mqtt.thingsboard.cloud");
  String data = buildMonitoringJSON(isThingsBoard);
  Serial.println("Sending Telemetry: " + data);

  if (config.commMode == "ws")
  {
    wsSendText(data);
  }
  else if (config.commMode == "mqtt" && mqttClient.connected())
  {
    String topic = isThingsBoard ? "v1/devices/me/telemetry" : "iot-node/" + config.hardwareId + "/telemetry";
    mqttClient.publish(topic.c_str(), data.c_str());
  }
  else if (config.commMode == "httppost")
  {
    HttpClient http(activeClient(), config.serverIP.c_str(), config.monitoringPort);
    StaticJsonDocument<100> doc;
    doc["box_id"] = config.hardwareId;
    doc["status"] = "online";
    String jsonString;
    serializeJson(doc, jsonString);

    http.beginRequest();
    http.post(config.path.c_str());
    http.sendHeader("Content-Type", "application/json");
    http.sendHeader("Content-Length", jsonString.length());
    http.beginBody();
    http.print(jsonString);
    http.endRequest();

    int statusCode = http.responseStatusCode();
    String response = http.responseBody();
    Serial.printf("Initial HTTP POST %s [%d]\n", statusCode > 0 ? "Success" : "Error", statusCode);
  }
}

void publishStatus()
{
  if (!network_ready)
    return;
  String statusData = buildStatusJSON();
  Serial.println("Publishing Status: " + statusData);

  if (config.commMode == "ws")
  {
    wsSendText(statusData);
  }
  else if (config.commMode == "mqtt" && mqttClient.connected())
  {
    String topic = "iot-node/" + config.hardwareId + "/status";
    mqttClient.publish(topic.c_str(), statusData.c_str());
  }
  else if (config.commMode == "httppost")
  {
    HttpClient http(activeClient(), config.serverIP.c_str(), config.monitoringPort);
    StaticJsonDocument<100> doc;
    doc["box_id"] = config.hardwareId;
    doc["status"] = "online";
    String jsonString;
    serializeJson(doc, jsonString);

    http.beginRequest();
    http.post(config.path.c_str());
    http.sendHeader("Content-Type", "application/json");
    http.sendHeader("Content-Length", jsonString.length());
    http.beginBody();
    http.print(jsonString);
    http.endRequest();

    int statusCode = http.responseStatusCode();
    String response = http.responseBody();
    Serial.printf("Initial HTTP POST %s [%d]\n", statusCode > 0 ? "Success" : "Error", statusCode);
  }
}

// =================================================================
// --- I/O & WEB SERVER (ringkas) ---
// =================================================================
bool checkInputChanges()
{
  bool changed = false;
  uint8_t s = pcf8574.read8();
  for (int i = 0; i < NUM_PCF_INPUTS; i++)
  {
    uint8_t current = (s >> PCF_PIN_MAP[i]) & 0x01;
    if (current != lastInputStates[i])
    {
      lastInputStates[i] = current;
      changed = true;
    }
  }
  for (int i = 0; i < NUM_GPIO_INPUTS; i++)
  {
    uint8_t current = digitalRead(INPUT_GPIO_PINS[i]);
    int index = NUM_PCF_INPUTS + i;
    if (current != lastInputStates[index])
    {
      lastInputStates[index] = current;
      changed = true;
    }
  }
  if (changed)
    saveIOStateToEEPROM();
  return changed;
}

String htmlPage(String body)
{
  return String("<html><body>") + body + "</body></html>";
}

void handleDashboard()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/dashboard.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholders
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%NETWORK_MODE%", config.networkMode);
    html.replace("%COMM_MODE%", config.commMode);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));

    server.send(200, "text/html", html);
  }
  else
  {
    serveErrorPage("Dashboard not found");
  }
}

// Handler untuk Config Page
void handleConfigPage()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/config.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholders
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));
    html.replace("%PATH%", config.path);
    html.replace("%WIFI_SSID%", config.wifiSSID);
    html.replace("%WIFI_PASS%", config.wifiPass);
    html.replace("%ACCESS_TOKEN%", config.accessTokenMQTT);

    // Network Mode
    html.replace("%NETWORK_WIFI%", config.networkMode == "WiFi" ? "selected" : "");
    html.replace("%NETWORK_ETH%", config.networkMode == "Ethernet" ? "selected" : "");
    html.replace("%NETWORK_HYBRID%", config.networkMode == "Hybrid" ? "selected" : "");

    // Comm Mode
    html.replace("%COMM_WS%", config.commMode == "ws" ? "selected" : "");
    html.replace("%COMM_HTTP%", config.commMode == "httppost" ? "selected" : "");
    html.replace("%COMM_MQTT%", config.commMode == "mqtt" ? "selected" : "");

    server.send(200, "text/html", html);
  }
  else
  {
    serveErrorPage("Config page not found");
  }
}

// Handler untuk Control Page
void handleControlPage()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/control.html", "r");
  if (file)
  {
    server.streamFile(file, "text/html");
    file.close();
  }
  else
  {
    serveErrorPage("Control page not found");
  }
}

// Handler untuk Status Page
void handleStatusPage()
{
  if (!isAuthenticated)
  {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
    return;
  }

  File file = LittleFS.open("/status.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholders
    html.replace("%HARDWARE_ID%", config.hardwareId);
    html.replace("%NETWORK_MODE%", config.networkMode);
    html.replace("%WIFI_SSID%", config.wifiSSID);
    html.replace("%COMM_MODE%", config.commMode);
    html.replace("%SERVER_IP%", config.serverIP);
    html.replace("%SERVER_PORT%", String(config.monitoringPort));
    html.replace("%PATH%", config.path);

    if (wifi_connected)
    {
      html.replace("%IP_ADDRESS%", WiFi.localIP().toString());
      html.replace("%MAC_ADDRESS%", WiFi.macAddress());
    }
    else if (eth_connected)
    {
      html.replace("%IP_ADDRESS%", Ethernet.localIP().toString());
      html.replace("%MAC_ADDRESS%", "N/A");
    }
    else
    {
      html.replace("%IP_ADDRESS%", "Not Connected");
      html.replace("%MAC_ADDRESS%", "N/A");
    }

    server.send(200, "text/html", html);
  }
  else
  {
    serveErrorPage("Status page not found");
  }
}

// Update handleRoot - redirect to dashboard after login
void handleRoot()
{
  if (!isAuthenticated)
  {
    File file = LittleFS.open("/login.html", "r");
    if (file)
    {
      server.streamFile(file, "text/html");
      file.close();
    }
    else
    {
      server.send(404, "text/plain", "Login page not found");
    }
    return;
  }

  // Redirect to dashboard
  server.sendHeader("Location", "/dashboard");
  server.send(302, "text/plain", "");
}

// Update handleAuth - redirect to dashboard on success
void handleAuth()
{
  if (server.arg("username") == default_username &&
      server.arg("password") == default_password)
  {
    isAuthenticated = true;
    // Serial.println("[Auth] Login successful");
    server.sendHeader("Location", "/dashboard");
    server.send(302, "text/plain", "");
  }
  else
  {
    // Serial.println("[Auth] Login failed - Invalid credentials");
    server.sendHeader("Location", "/?error=1");
    server.send(302, "text/plain", "");
  }
}
void handleSaveConfig()
{
  // Check authentication
  if (!isAuthenticated)
  {
    serveErrorPage("Unauthorized access. Please login first.");
    return;
  }

  // ===== VALIDATION =====

  // Validate Hardware ID
  if (!server.hasArg("hardwareId") || server.arg("hardwareId").length() == 0)
  {
    serveErrorPage("Hardware ID cannot be empty.");
    return;
  }

  // Validate Server IP
  if (!server.hasArg("serverIP") || server.arg("serverIP").length() == 0)
  {
    serveErrorPage("Server IP cannot be empty.");
    return;
  }

  // Validate Server Port
  if (!server.hasArg("serverPort") || server.arg("serverPort").toInt() <= 0 || server.arg("serverPort").toInt() > 65535)
  {
    serveErrorPage("Invalid server port number. Must be between 1-65535.");
    return;
  }

  // ===== CHECK FOR CHANGES =====
  bool changed = false;

  Serial.println("[Config] Checking for changes...");

  // Hardware ID
  if (server.hasArg("hardwareId") && server.arg("hardwareId") != config.hardwareId)
  {
    config.hardwareId = server.arg("hardwareId");
    config.hardwareId.trim();
    changed = true;
  }

  // Server IP
  if (server.hasArg("serverIP") && server.arg("serverIP") != config.serverIP)
  {
    config.serverIP = server.arg("serverIP");
    config.serverIP.trim();
    changed = true;
  }

  // Server Port
  if (server.hasArg("serverPort") && server.arg("serverPort").toInt() != config.monitoringPort)
  {
    config.monitoringPort = server.arg("serverPort").toInt();
    changed = true;
  }

  // Server Path
  if (server.hasArg("path") && server.arg("path") != config.path)
  {
    config.path = server.arg("path");
    config.path.trim();
    if (config.path.length() == 0)
    {
      config.path = "/wsiot";
    }
    changed = true;
  }

  // Communication Mode
  if (server.hasArg("commMode") && server.arg("commMode") != config.commMode)
  {
    config.commMode = server.arg("commMode");
    config.commMode.trim();
    changed = true;
  }

  // MQTT Access Token
  if (server.hasArg("accessToken") && server.arg("accessToken") != config.accessTokenMQTT)
  {
    config.accessTokenMQTT = server.arg("accessToken");
    config.accessTokenMQTT.trim();
    changed = true;
  }

  // WiFi SSID
  if (server.hasArg("ssid") && server.arg("ssid") != config.wifiSSID)
  {
    config.wifiSSID = server.arg("ssid");
    config.wifiSSID.trim();
    changed = true;
  }

  // WiFi Password
  if (server.hasArg("password") && server.arg("password").length() > 0 &&
      server.arg("password") != config.wifiPass)
  {
    config.wifiPass = server.arg("password");
    config.wifiPass.trim();
    changed = true;
  }

  // Network Mode
  if (server.hasArg("networkMode") && server.arg("networkMode") != config.networkMode)
  {
    config.networkMode = server.arg("networkMode");
    config.networkMode.trim();
    changed = true;
  }

  // ===== SAVE OR NO CHANGES =====

  if (!changed)
  {
    Serial.println("[Config] No changes detected");

    server.send(200, "text/html",
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<meta http-equiv='refresh' content='2;url=/config'>"
                "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600&display=swap' rel='stylesheet'>"
                "<style>"
                "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);"
                "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
                ".box{background:white;padding:50px 40px;border-radius:24px;text-align:center;max-width:400px;"
                "box-shadow:0 25px 50px rgba(0,0,0,0.3);animation:slideUp 0.5s ease;}"
                "@keyframes slideUp{from{opacity:0;transform:translateY(30px)}to{opacity:1;transform:translateY(0)}}"
                "h2{color:#1e293b;margin-bottom:15px;font-size:24px;}"
                "p{color:#64748b;font-size:15px;line-height:1.6;}"
                ".icon{font-size:60px;margin-bottom:20px;}"
                "</style>"
                "</head><body>"
                "<div class='box'>"
                "<div class='icon'>ℹ️</div>"
                "<h2>No Changes Detected</h2>"
                "<p>Configuration is already up to date.<br>Redirecting back...</p>"
                "</div>"
                "</body></html>");
    return;
  }

  // ===== SAVE CONFIGURATION =====

  bool saveSuccess = saveConfig();

  if (!saveSuccess)
  {
    serveErrorPage("Failed to save configuration. Please try again.");
    return;
  }

  // ===== SERVE SUCCESS PAGE =====

  File file = LittleFS.open("/success.html", "r");
  if (file)
  {
    Serial.println("[Config] Serving success page");
    String html = file.readString();
    file.close();

    // Send response
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", html);

    // Ensure response is fully sent
    delay(100);

    for (int i = 5; i > 0; i--)
    {
      Serial.printf("[System] %d...\n", i);
      delay(1000);
    }
    delay(100);

    ESP.restart();
  }
  else
  {
    // Fallback dengan tampilan bagus
    Serial.println("[Config] Warning: success.html not found, using enhanced fallback");

    String fallbackHtml =
        "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<link href='https://fonts.googleapis.com/css2?family=Inter:wght@400;600;700&display=swap' rel='stylesheet'>"
        "<style>"
        "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);"
        "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
        ".box{background:white;padding:50px 40px;border-radius:24px;text-align:center;max-width:450px;"
        "box-shadow:0 25px 50px rgba(0,0,0,0.3);animation:slideUp 0.5s ease;}"
        "@keyframes slideUp{from{opacity:0;transform:translateY(30px)}to{opacity:1;transform:translateY(0)}}"
        "h2{color:#10b981;margin-bottom:15px;font-size:28px;font-weight:700;}"
        "p{color:#64748b;line-height:1.6;margin-bottom:25px;font-size:15px;}"
        ".countdown{font-size:18px;font-weight:600;color:#667eea;margin-bottom:15px;}"
        ".number{font-size:32px;font-weight:700;color:#667eea;display:inline-block;min-width:40px;}"
        ".progress{background:#e2e8f0;border-radius:100px;height:8px;overflow:hidden;margin-bottom:20px;}"
        ".bar{height:100%;background:linear-gradient(90deg,#667eea,#764ba2);border-radius:100px;animation:fill 5s linear;}"
        "@keyframes fill{from{width:0%}to{width:100%}}"
        ".check{font-size:70px;margin-bottom:20px;}"
        "</style>"
        "</head><body>"
        "<div class='box'>"
        "<div class='check'>✅</div>"
        "<h2>Configuration Saved!</h2>"
        "<p>Your settings have been saved successfully.<br>Device will restart to apply changes.</p>"
        "<div class='progress'><div class='bar'></div></div>"
        "<div class='countdown'>Restarting in <span class='number' id='timer'>5</span> seconds</div>"
        "</div>"
        "<script>"
        "let c=5;const t=setInterval(()=>{"
        "c--;document.getElementById('timer').textContent=c;"
        "if(c<=0){clearInterval(t);window.location.href='/';}},1000);"
        "</script>"
        "</body></html>";

    server.sendHeader("Connection", "close");
    server.send(200, "text/html", fallbackHtml);

    delay(5100); // 5 seconds + buffer
    ESP.restart();
  }
}

// =================================================================
// --- Toggle Output Handler ---
// =================================================================
void handleToggle()
{
  if (!isAuthenticated)
  {
    server.send(401, "text/plain", "Unauthorized");
    return;
  }

  if (!server.hasArg("pin") || !server.hasArg("state"))
  {
    server.send(400, "text/plain", "Missing parameters");
    return;
  }

  int pin = server.arg("pin").toInt();
  String state = server.arg("state");

  if (pin < 0 || pin >= NUM_OUTPUTS)
  {
    server.send(400, "text/plain", "Invalid pin number");
    return;
  }

  if (state != "on" && state != "off")
  {
    server.send(400, "text/plain", "Invalid state value");
    return;
  }

  digitalWrite(OUTPUT_PINS[pin], state == "on" ? HIGH : LOW);
  saveIOStateToEEPROM();

  Serial.printf("[Control] Output O%d set to %s\n", pin + 1, state.c_str());
  server.send(200, "text/plain", "OK");
}

// =================================================================
// --- Get Outputs Handler ---
// =================================================================
void handleGetOutputs()
{
  if (!isAuthenticated)
  {
    server.send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  StaticJsonDocument<200> doc;
  JsonArray outputs = doc.createNestedArray("outputs");

  for (int i = 0; i < NUM_OUTPUTS; i++)
  {
    outputs.add(digitalRead(OUTPUT_PINS[i]));
  }

  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
}

// =================================================================
// --- Download Config Handler ---
// =================================================================
void handleDownloadConfig()
{
  if (!isAuthenticated)
  {
    serveErrorPage("Unauthorized access. Please login first.");
    return;
  }

  StaticJsonDocument<512> doc;
  doc["hardwareId"] = config.hardwareId;
  doc["serverIP"] = config.serverIP;
  doc["monitoringPort"] = config.monitoringPort;
  doc["path"] = config.path;
  doc["commMode"] = config.commMode;
  doc["networkMode"] = config.networkMode;
  doc["wifiSSID"] = config.wifiSSID;
  doc["wifiPass"] = config.wifiPass;
  doc["accessTokenMQTT"] = config.accessTokenMQTT;

  String jsonString;
  serializeJsonPretty(doc, jsonString);

  if (jsonString.length() == 0)
  {
    serveErrorPage("Failed to generate configuration file.");
    return;
  }

  String filename = "config_" + config.hardwareId + ".json";
  server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
  server.send(200, "application/json", jsonString);

  Serial.println("[Config] Configuration downloaded: " + filename);
}

// =================================================================
// --- Logout Handler ---
// =================================================================
void handleLogout()
{
  isAuthenticated = false;
  Serial.println("[Auth] User logged out");
  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

// =================================================================
// --- Error Page Handler ---
// =================================================================
void serveErrorPage(String errorMessage)
{
  Serial.println("[Error] " + errorMessage);

  File file = LittleFS.open("/error.html", "r");
  if (file)
  {
    String html = file.readString();
    file.close();

    // Replace placeholder dengan pesan error
    html.replace("%ERROR_MESSAGE%", errorMessage);

    server.send(400, "text/html", html);
  }
  else
  {
    // Fallback jika error.html tidak ditemukan
    server.send(400, "text/html",
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<style>"
                "body{font-family:'Inter',sans-serif;background:linear-gradient(135deg,#667eea,#764ba2);"
                "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}"
                ".box{background:white;padding:40px;border-radius:20px;text-align:center;max-width:400px;box-shadow:0 20px 60px rgba(0,0,0,0.3);}"
                "h2{color:#ef4444;margin-bottom:15px;font-size:24px;}"
                "p{color:#64748b;line-height:1.6;}"
                "button{margin-top:20px;padding:12px 24px;background:#667eea;color:white;border:none;border-radius:10px;font-weight:600;cursor:pointer;}"
                "</style>"
                "</head><body>"
                "<div class='box'>"
                "<div style='font-size:60px;margin-bottom:20px;'>⚠️</div>"
                "<h2>Error Occurred</h2>"
                "<p>" +
                    errorMessage + "</p>"
                                   "<button onclick='history.back()'>← Go Back</button>"
                                   "</div>"
                                   "</body></html>");
  }
}

// =================================================================
// --- OTA ---
// =================================================================
void setupOTA()
{
  ArduinoOTA.setPort(3232);
  ArduinoOTA.setHostname(config.hardwareId.c_str());
  ArduinoOTA.setPassword(default_password);
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

// =================================================================
// --- NETWORK SETUP (WiFi/Ethernet/Hybrid + AP Fallback) ---
// =================================================================
void setupNetwork()
{
  WiFi.onEvent(networkEventHandler);
  isAPMode = false;
  network_ready = false;
  wifi_connected = false;
  eth_connected = false;

  Serial.println("Starting network sequence (Mode: " + config.networkMode + ")");
  bool connected = false;

  if (config.networkMode == "Ethernet")
  {
    Serial.println("[Ethernet] Trying (" + String(ethTryTime / 1000) + "s)...");
    WiFi.mode(WIFI_OFF);
    if (initEthernet())
    {
      eth_connected = true;
      network_ready = true;
      connected = true;
      Serial.println("[Ethernet] connected.");
    }
    else
      Serial.println("[Ethernet] init failed.");

    unsigned long startEth = millis();
    while (millis() - startEth < ethTryTime)
    {
      if (eth_connected)
      {
        connected = true;
        break;
      }
      delay(100);
      Serial.print(".");
    }
    Serial.println();
  }
  else if (config.networkMode == "WiFi")
  {
    Serial.println("[WiFi] Trying (" + String(wifiTryTime / 1000) + "s)...");
    if (config.wifiSSID.length() > 0)
    {
      WiFi.mode(WIFI_STA);
      WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
      unsigned long startWiFi = millis();
      while (millis() - startWiFi < wifiTryTime)
      {
        if (WiFi.status() == WL_CONNECTED)
        {
          wifi_connected = true;
          connected = true;
          break;
        }
        delay(250);
        Serial.print(".");
      }
      Serial.println();
    }
    else
    {
      Serial.println("[WiFi] SSID not configured.");
    }
  }
  else
  { // Hybrid
    Serial.println("[Hybrid] Trying Ethernet first...");
    if (initEthernet())
    {
      eth_connected = true;
      connected = true;
      Serial.println("[Ethernet] Connected!");
    }
    else
    {
      Serial.println("[Ethernet] Failed. Trying WiFi...");
      startWifiFallback();
      Serial.println("[Hybrid] WiFi connecting in background...");
      unsigned long startWiFi = millis();
      while (millis() - startWiFi < wifiTryTime)
      {
        if (WiFi.status() == WL_CONNECTED)
        {
          wifi_connected = true;
          connected = true;
          break;
        }
        delay(250);
        Serial.print(".");
      }
      Serial.println();
    }
  }

  if (!connected)
  {
    Serial.println("[Network] All connection attempts failed! Starting AP Mode");
    startAPMode();
  }
  else
  {
    Serial.println("[Network] Successfully connected.");
    network_ready = true;
    setupCommunication();
  }
}

void setupCommunication()
{
  if (isAPMode || readyToSend)
    return;
  Serial.println("Memulai layanan komunikasi (" + config.commMode + ")...");

  if (config.commMode == "ws")
  {
    wsConnect();
  }
  else if (config.commMode == "mqtt")
  {
    mqttClient.setClient(activeClient());
    mqttClient.setServer(config.serverIP.c_str(), config.monitoringPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024);
    mqttClient.setKeepAlive(60);
    mqttClient.setSocketTimeout(15);
  }
  else if (config.commMode == "httppost")
  {
    HttpClient http(activeClient(), config.serverIP.c_str(), config.monitoringPort);
    StaticJsonDocument<100> doc;
    doc["box_id"] = config.hardwareId;
    doc["status"] = "online";
    String jsonString;
    serializeJson(doc, jsonString);

    http.beginRequest();
    http.post(config.path.c_str());
    http.sendHeader("Content-Type", "application/json");
    http.sendHeader("Content-Length", jsonString.length());
    http.beginBody();
    http.print(jsonString);
    http.endRequest();

    int statusCode = http.responseStatusCode();
    String response = http.responseBody();
    Serial.printf("Initial HTTP POST %s [%d]\n", statusCode > 0 ? "Success" : "Error", statusCode);
  }
  readyToSend = true;
}

void startAPMode()
{
  isAPMode = true;
  network_ready = false;
  wifi_connected = false;
  eth_connected = false;
  String finalApSsid = ap_ssid + config.hardwareId;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(finalApSsid.c_str(), ap_password);
  apStartTime = millis();
  Serial.println("[AP MODE] Started: " + finalApSsid + " with IP: " + WiFi.softAPIP().toString());
  if (ws)
    wsDisconnect();
  if (mqttClient.connected())
    mqttClient.disconnect();
  readyToSend = false;
}

void tryReconnect()
{
  if (config.networkMode == "WiFi")
  {
    if (config.wifiSSID.length() > 0)
    {
      WiFi.disconnect(true);
      delay(300);
      WiFi.mode(WIFI_STA);
      WiFi.begin(config.wifiSSID.c_str(), config.wifiPass.c_str());
    }
    else
    {
      startAPMode();
      return;
    }
  }
  else if (config.networkMode == "Ethernet")
  {
    if (wifi_connected)
    {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      wifi_connected = false;
    }
    if (initEthernet())
      eth_connected = true;
  }
  else
  { // Hybrid
    if (Ethernet.linkStatus() == LinkOFF)
    {
      startWifiFallback();
      isReconnecting = false;
      return;
    }
    eth_connected = true;
    isReconnecting = false;
    return;
  }
  isReconnecting = true;
  reconnectAttemptStartTime = millis();
}

// =================================================================
// --- SETUP ---
// =================================================================
void setupIO()
{
  if (pcf8574.begin())
    Serial.println("PCF8574: Connected.");
  else
    Serial.println("PCF8574: Failed!");
  pcf8574.write8(0xFF);
  for (int pin : INPUT_GPIO_PINS)
    pinMode(pin, INPUT_PULLUP);
  for (int pin : OUTPUT_PINS)
  {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  loadIOStateFromEEPROM();
}

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Serial.println("\n\n==> Starting IoT Node " + program_version + " <==");
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("[FATAL] EEPROM init fail");
    delay(1000);
    ESP.restart();
  }
  if (!LittleFS.begin())
  {
    Serial.println("[FATAL] LittleFS mount failed.");
    while (1)
    {
      delay(1000);
    }
  }

  loadConfig();

  Serial.println("box_id: " + config.hardwareId);
  Serial.println("serverIP: " + config.serverIP);
  Serial.println("path: " + config.path);
  Serial.println("monitoringPort: " + String(config.monitoringPort));
  Serial.println("commMode: " + config.commMode);
  Serial.println("wifiSSID: " + config.wifiSSID);

  setupIO();
  setupNetwork();

  if (!isAPMode)
  {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    setupOTA();
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/auth", HTTP_POST, handleAuth);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/control", HTTP_GET, handleControlPage);
  server.on("/status", HTTP_GET, handleStatusPage);
  server.on("/save-config", HTTP_POST, handleSaveConfig);
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/get-outputs", HTTP_GET, handleGetOutputs);
  server.on("/download-config", HTTP_GET, handleDownloadConfig);
  server.on("/logout", HTTP_GET, handleLogout);

  // 404 handler
  server.onNotFound([]()
                    {
  Serial.println("[Web] 404 - Page not found: " + server.uri());
  serveErrorPage("Page not found: " + server.uri()); });

  server.begin();
}

// =================================================================
// --- LOOP ---
// =================================================================
void loop()
{
  server.handleClient();

  if (isAPMode)
  {
    unsigned long now = millis();
    unsigned long elapsed = now - apStartTime;
    if (apStartTime > 0 && now - lastCountdownPrint >= 1000)
    {
      lastCountdownPrint = now;
      unsigned long remaining = (apTimeout > elapsed) ? (apTimeout - elapsed) / 1000 : 0;
      Serial.printf("[AP MODE] Active. Restarting in %lu seconds...\n", remaining);
    }
    if (elapsed >= apTimeout)
    {
      Serial.println("[AP MODE] Timeout. Restarting...");
      delay(200);
      ESP.restart();
    }
    return;
  }

  if (config.networkMode == "Ethernet" || config.networkMode == "Hybrid")
    checkEthernetStatus();
  if (eth_connected)
    maintainEthernet();

  bool isNetworkConnected = wifi_connected || eth_connected;
  if (isNetworkConnected)
  {
    if (isReconnecting)
    {
      isReconnecting = false;
      reconnectAttemptCount = 0;
      reconnectAttemptStartTime = 0;
    }
    if (!readyToSend)
    {
      setupCommunication();
    }

    ArduinoOTA.handle();

    if (config.commMode == "ws")
      wsPump();
    else if (config.commMode == "mqtt")
    {
      if (!mqttClient.connected())
        reconnectMQTT();
      mqttClient.loop();
    }

    unsigned long currentTime = millis();
    if (currentTime - lastMonitoringTime >= monitoringInterval)
    {
      lastMonitoringTime = currentTime;
      bool hasChanged = checkInputChanges();
      bool forceSend = (currentTime - lastForceSendTime >= forceSendInterval);
      if (hasChanged || forceSend)
      {
        sendMonitoringData();
        publishTelemetry();
        lastForceSendTime = currentTime;
      }
    }
  }
  else
  {
    if (network_ready)
    {
      network_ready = false;
      readyToSend = false;
      if (ws)
        wsDisconnect();
      if (mqttClient.connected())
        mqttClient.disconnect();
      isReconnecting = false;
      reconnectAttemptCount = 0;
      reconnectAttemptStartTime = 0;
    }
    if (!isReconnecting)
    {
      if (reconnectAttemptCount >= maxReconnectAttempts)
      {
        Serial.println("\n[Reconnect] Max attempts reached. Starting AP Mode...");
        startAPMode();
        return;
      }
      if (reconnectAttemptCount > 0)
        delay(2000);
      tryReconnect();
      reconnectAttemptCount++;
    }
    else
    {
      unsigned long attemptElapsed = millis() - reconnectAttemptStartTime;
      if (attemptElapsed >= singleAttemptTimeout)
      {
        Serial.println("\n[Reconnect] Attempt timeout");
        isReconnecting = false;
      }
      else
      {
        static unsigned long lastDot = 0;
        if (millis() - lastDot >= 500)
        {
          Serial.print(".");
          lastDot = millis();
        }
        delay(100);
      }
    }
  }
}