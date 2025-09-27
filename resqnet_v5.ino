#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <DNSServer.h>

// Pin definitions
#define SCK 18
#define MISO 19
#define MOSI 23
#define SS 5
#define RST 14
#define DIO0 26

// New hardware pins
#define SOS_BUTTON_PIN 4
#define LED_STRIP_PIN 16  // Changed from GPIO 2 to GPIO 16 (much better choice)
#define BUZZER_PIN 15
#define NUM_LEDS 3

// Node configuration
#define FREQUENCY 433E6

// Default configuration
String nodeSSID = "";
String nodePassword = "12345678";
String username = "User";
String nodeId = "";
int ledBrightness = 128;
uint32_t ledColor = 0xFFFFFF; // White default (hex format)

WebServer server(80);
DNSServer dnsServer;
String messageHistory = "";
Adafruit_NeoPixel strip(NUM_LEDS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// Configuration file paths
const char* configFile = "/config.json";
const char* historyFile = "/history.json";

// State variables
bool clientConnected = false;
unsigned long lastClientCheck = 0;
bool sosButtonPressed = false;
unsigned long sosStartTime = 0;
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;

void setup() {
  Serial.begin(115200);
  
  // Initialize hardware
  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Make sure buzzer starts OFF
  
  // Initialize LED strip
  strip.begin();
  strip.setBrightness(50);
  strip.show(); // Initialize all pixels to 'off'
  
  // Boot animation
  bootAnimation();
  
  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  
  // Generate unique node ID
  generateNodeId();
  
  // Load configuration
  loadConfig();
  
  // Initialize LoRa
  setupLoRa();
  
  // Load message history
  loadHistory();
  
  // Initialize WiFi AP
  setupWiFi();
  
  // Initialize Web Server
  setupWebServer();
  
  // Set initial LED state
  setLEDState("ap_started");
  
  Serial.println("System ready!");
  Serial.println("Node ID: " + nodeId);
  Serial.println("Connect to WiFi: " + nodeSSID);
  Serial.println("Password: " + nodePassword);
  Serial.println("Open browser - will auto-redirect to chat");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  checkLoRaMessages();
  checkSOSButton();
  checkClientStatus();
  handleBuzzer();
  delay(10);
}

void bootAnimation() {
  Serial.println("Starting boot animation...");
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.clear(); // Clear all pixels
    strip.setPixelColor(i, strip.Color(0, 0, 255)); // Blue using strip.Color()
    strip.show();
    delay(300);
  }
  strip.clear(); // Clear all pixels
  strip.show();
  Serial.println("Boot animation complete");
}

void setLEDState(String state) {
  Serial.println("Setting LED state: " + state);
  
  // Set brightness to 50 for all non-SOS states
  if (state != "sos_flash" && state != "custom_color") {
    strip.setBrightness(50);
  }
  
  // Clear all pixels first
  strip.clear();
  
  if (state == "ap_started") {
    strip.setPixelColor(0, strip.Color(255, 255, 255)); // First LED white
    Serial.println("AP started - LED 0 white");
  }
  else if (state == "client_connected") {
    strip.setPixelColor(0, strip.Color(255, 255, 255)); // First LED white
    strip.setPixelColor(2, strip.Color(255, 255, 255)); // Last LED white
    Serial.println("Client connected - LED 0 and 2 white");
  }
  else if (state == "message_sent") {
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.clear();
      strip.setPixelColor(i, strip.Color(0, 255, 0)); // Green shifting (keep green for message indication)
      strip.show();
      delay(200);
    }
    Serial.println("Message sent animation complete");
    return; // Don't call strip.show() at the end
  }
  else if (state == "message_received") {
    for (int i = NUM_LEDS - 1; i >= 0; i--) {
      strip.clear();
      strip.setPixelColor(i, strip.Color(255, 0, 0)); // Red shifting (keep red for message indication)
      strip.show();
      delay(200);
    }
    Serial.println("Message received animation complete");
    return; // Don't call strip.show() at the end
  }
  else if (state == "custom_color") {
    strip.setBrightness(ledBrightness);
    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, ledColor); // Use stored color directly
    }
    strip.show();
    strip.setBrightness(50); // Reset to default after showing
    Serial.println("Custom color set");
    return;
  }
  else if (state == "sos_flash") {
    strip.setBrightness(255); // Full brightness for SOS
    //
    digitalWrite(BUZZER_PIN, HIGH);
    //
    for (int flash = 0; flash < 10; flash++) {
      for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(255, 0, 0)); // Red at full brightness
      }
      strip.show();
      delay(200);
      strip.clear();
      strip.show();
      delay(200);
    }
    strip.setBrightness(50); // Reset to default after SOS
    //
    digitalWrite(BUZZER_PIN, LOW);
    //
    Serial.println("SOS flash complete");
  }
  
  strip.show(); // Update the strip
}

void generateNodeId() {
  // Check if nodeId already exists in config
  if (SPIFFS.exists(configFile)) {
    File file = SPIFFS.open(configFile, "r");
    if (file) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, file);
      
      if (doc["nodeId"]) {
        nodeId = doc["nodeId"].as<String>();
        file.close();
        if (nodeSSID == "") {
          nodeSSID = "LRQN_" + nodeId;
        }
        return;
      }
      file.close();
    }
  }
  
  // Generate new random 4-digit nodeId if not found
  randomSeed(analogRead(0) + millis());
  int randomNum = random(1000, 9999);
  nodeId = String(randomNum);
  
  if (nodeSSID == "") {
    nodeSSID = "LRQN_" + nodeId;
  }
  
  // Save the generated nodeId immediately
  saveConfig();
}

void loadConfig() {
  if (SPIFFS.exists(configFile)) {
    File file = SPIFFS.open(configFile, "r");
    if (file) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, file);
      
      if (doc["nodeId"]) nodeId = doc["nodeId"].as<String>();
      if (doc["ssid"]) nodeSSID = doc["ssid"].as<String>();
      if (doc["password"]) nodePassword = doc["password"].as<String>();
      if (doc["username"]) username = doc["username"].as<String>();
      if (doc["ledBrightness"]) ledBrightness = doc["ledBrightness"];
      if (doc["ledColor"]) {
        ledColor = doc["ledColor"];
      }
      
      file.close();
      Serial.println("Configuration loaded");
    }
  }
}

void saveConfig() {
  DynamicJsonDocument doc(1024);
  doc["nodeId"] = nodeId;
  doc["ssid"] = nodeSSID;
  doc["password"] = nodePassword;
  doc["username"] = username;
  doc["ledBrightness"] = ledBrightness;
  doc["ledColor"] = ledColor;
  
  File file = SPIFFS.open(configFile, "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.println("Configuration saved");
  }
}

void loadHistory() {
  if (SPIFFS.exists(historyFile)) {
    File file = SPIFFS.open(historyFile, "r");
    if (file) {
      messageHistory = file.readString();
      file.close();
      Serial.println("Message history loaded");
    }
  }
}

void saveHistory() {
  File file = SPIFFS.open(historyFile, "w");
  if (file) {
    file.print(messageHistory);
    file.close();
  }
}

void setupLoRa() {
  Serial.println("Initializing LoRa...");
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(FREQUENCY)) {
    Serial.println("LoRa initialization failed!");
    while (1);
  }
  
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(8);
  
  Serial.println("LoRa initialized successfully!");
  requestHistory();
}

void setupWiFi() {
  Serial.println("Starting WiFi Access Point...");
  WiFi.softAP(nodeSSID.c_str(), nodePassword.c_str());
  
  // Start DNS server for captive portal
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
}

void setupWebServer() {
  // Captive portal - redirect all requests to main page
  server.onNotFound(handleRoot);
  server.on("/", handleRoot);
  server.on("/send", HTTP_POST, handleSendMessage);
  server.on("/messages", handleGetMessages);
  server.on("/settings", handleSettings);
  server.on("/save-settings", HTTP_POST, handleSaveSettings);
  server.on("/get-settings", handleGetSettings);
  server.on("/set-led", HTTP_POST, handleSetLED);
  
  server.begin();
  Serial.println("Web server started with captive portal");
}

void checkClientStatus() {
  if (millis() - lastClientCheck > 3000) { // Check every 3 seconds (faster response)
    int connectedClients = WiFi.softAPgetStationNum();
    bool newClientStatus = (connectedClients > 0);
    
    Serial.println("Checking client status - Connected clients: " + String(connectedClients));
    
    if (newClientStatus != clientConnected) {
      clientConnected = newClientStatus;
      Serial.println("Client status changed: " + String(clientConnected ? "CONNECTED" : "DISCONNECTED"));
      
      // Always update LED state when client status changes
      if (clientConnected) {
        setLEDState("client_connected");
      } else {
        setLEDState("ap_started");
      }
    }
    lastClientCheck = millis();
  }
}

void checkSOSButton() {
  if (digitalRead(SOS_BUTTON_PIN) == LOW && !sosButtonPressed) {
    sosButtonPressed = true;
    sosStartTime = millis();
    
    // Send SOS message
    String timestamp = String(millis() / 1000);
    sendLoRaMessage("‼️ SOS ‼️", timestamp);
    addToMessageHistory(username, "‼️ SOS ‼️", timestamp, true);
    
    // Start LED flash and buzzer
    setLEDState("sos_flash");
    startSOSBuzzer();
    
    Serial.println("SOS ACTIVATED!");
  }
  
  if (digitalRead(SOS_BUTTON_PIN) == HIGH && sosButtonPressed) {
    sosButtonPressed = false;
  }
}

void startSOSBuzzer() {
  buzzerActive = true;
  buzzerStartTime = millis();
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("SOS Buzzer started");
}

void messageReceivedBeep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("Message received beep");
}

void handleBuzzer() {
  // Handle SOS buzzer (5 seconds)
  if (buzzerActive && (millis() - buzzerStartTime >= 5000)) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
    Serial.println("SOS Buzzer stopped");
    
    // Restore LED state after SOS
    if (clientConnected) {
      setLEDState("client_connected");
    } else {
      setLEDState("ap_started");
    }
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<title>resQNet Messenger - " + nodeId + "</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>";
  html += "body { font-family: Arial; margin: 20px; background: #f0f0f0; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; position: relative; }";
  html += ".header { text-align: center; color: #333; margin-bottom: 20px; }";
  html += ".settings-btn { position: absolute; top: 20px; right: 20px; background: #6c757d; color: white; border: none; border-radius: 50%; width: 40px; height: 40px; cursor: pointer; font-size: 16px; }";
  html += ".settings-btn:hover { background: #5a6268; }";
  html += ".messages { border: 1px solid #ddd; height: 400px; overflow-y: scroll; padding: 10px; margin: 10px 0; background: #fafafa; }";
  html += ".input-group { display: flex; gap: 10px; }";
  html += "input[type=\"text\"] { flex: 1; padding: 10px; border: 1px solid #ddd; border-radius: 5px; }";
  html += "button { padding: 10px 20px; background: #007bff; color: white; border: none; border-radius: 5px; cursor: pointer; }";
  html += "button:hover { background: #0056b3; }";
  html += ".message { margin: 5px 0; padding: 8px; background: #e9ecef; border-radius: 5px; }";
  html += ".message-own { background: #007bff; color: white; margin-left: 50px; }";
  html += ".message-other { background: #28a745; color: white; margin-right: 50px; }";
  html += ".message-sos { background: #dc3545; color: white; text-align: center; font-weight: bold; font-size: 1.2em; }";
  html += ".message-time { font-size: 0.8em; opacity: 0.8; }";
  html += ".message-user { font-weight: bold; margin-bottom: 3px; }";
  html += ".modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.5); }";
  html += ".modal-content { background: white; margin: 5% auto; padding: 20px; border-radius: 10px; width: 80%; max-width: 500px; max-height: 80vh; overflow-y: auto; }";
  html += ".close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }";
  html += ".close:hover { color: black; }";
  html += ".form-group { margin: 15px 0; }";
  html += ".form-group label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += ".form-group input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 3px; }";
  html += ".color-buttons { display: flex; gap: 10px; margin: 10px 0; }";
  html += ".color-btn { width: 40px; height: 40px; border: none; border-radius: 50%; cursor: pointer; }";
  html += ".brightness-control { margin: 10px 0; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class=\"container\">";
  html += "<button class=\"settings-btn\" onclick=\"openSettings()\">S</button>";
  html += "<h1 class=\"header\">resQNet Messenger<br><small>Node: " + nodeId + " | User: " + username + "</small></h1>";
  html += "<div id=\"messages\" class=\"messages\"></div>";
  html += "<div class=\"input-group\">";
  html += "<input type=\"text\" id=\"messageInput\" placeholder=\"Type your message...\">";
  html += "<button onclick=\"sendMessage()\">Send</button>";
  html += "</div>";
  html += "</div>";
  
  // Settings Modal
  html += "<div id=\"settingsModal\" class=\"modal\">";
  html += "<div class=\"modal-content\">";
  html += "<span class=\"close\" onclick=\"closeSettings()\">&times;</span>";
  html += "<h2>Settings</h2>";
  html += "<div class=\"form-group\">";
  html += "<label>Username:</label>";
  html += "<input type=\"text\" id=\"usernameInput\" placeholder=\"Enter username\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label>WiFi SSID:</label>";
  html += "<input type=\"text\" id=\"ssidInput\" placeholder=\"Enter SSID\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label>WiFi Password:</label>";
  html += "<input type=\"password\" id=\"passwordInput\" placeholder=\"Enter password\">";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label>LED Color:</label>";
  html += "<input type=\"color\" id=\"colorPicker\" onchange=\"updateLEDPreview()\">";
  html += "<div class=\"color-buttons\">";
  html += "<button class=\"color-btn\" style=\"background: red\" onclick=\"setQuickColor('red')\"></button>";
  html += "<button class=\"color-btn\" style=\"background: green\" onclick=\"setQuickColor('green')\"></button>";
  html += "<button class=\"color-btn\" style=\"background: blue\" onclick=\"setQuickColor('blue')\"></button>";
  html += "<button class=\"color-btn\" style=\"background: white; border: 1px solid #ccc;\" onclick=\"setQuickColor('white')\"></button>";
  html += "</div>";
  html += "</div>";
  html += "<div class=\"form-group\">";
  html += "<label>LED Brightness:</label>";
  html += "<input type=\"range\" id=\"brightnessSlider\" min=\"10\" max=\"255\" value=\"128\" onchange=\"updateLEDPreview()\">";
  html += "<span id=\"brightnessValue\">128</span>";
  html += "</div>";
  html += "<button onclick=\"saveSettings()\" style=\"width: 100%; margin-top: 10px;\">Save & Restart</button>";
  html += "</div>";
  html += "</div>";
  
  html += "<script>";
  html += "function sendMessage() {";
  html += "const input = document.getElementById('messageInput');";
  html += "const message = input.value.trim();";
  html += "if (message === '') return;";
  html += "const timestamp = new Date().toLocaleString();";
  html += "fetch('/send', {";
  html += "method: 'POST',";
  html += "headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  html += "body: 'message=' + encodeURIComponent(message) + '&timestamp=' + encodeURIComponent(timestamp)";
  html += "});";
  html += "input.value = '';";
  html += "}";
  
  html += "function updateMessages() {";
  html += "fetch('/messages')";
  html += ".then(response => response.text())";
  html += ".then(data => {";
  html += "const messagesDiv = document.getElementById('messages');";
  html += "const wasAtBottom = messagesDiv.scrollHeight - messagesDiv.clientHeight <= messagesDiv.scrollTop + 1;";
  html += "messagesDiv.innerHTML = data;";
  html += "if (wasAtBottom) messagesDiv.scrollTop = messagesDiv.scrollHeight;";
  html += "});";
  html += "}";
  
  html += "function openSettings() {";
  html += "fetch('/get-settings')";
  html += ".then(response => response.json())";
  html += ".then(data => {";
  html += "document.getElementById('usernameInput').value = data.username;";
  html += "document.getElementById('ssidInput').value = data.ssid;";
  html += "document.getElementById('passwordInput').value = data.password;";
  html += "document.getElementById('brightnessSlider').value = data.brightness;";
  html += "document.getElementById('brightnessValue').textContent = data.brightness;";
  html += "document.getElementById('colorPicker').value = data.color;";
  html += "document.getElementById('settingsModal').style.display = 'block';";
  html += "});";
  html += "}";
  
  html += "function closeSettings() {";
  html += "document.getElementById('settingsModal').style.display = 'none';";
  html += "}";
  
  html += "function setQuickColor(color) {";
  html += "const colors = {red: '#ff0000', green: '#00ff00', blue: '#0000ff', white: '#ffffff'};";
  html += "document.getElementById('colorPicker').value = colors[color];";
  html += "document.getElementById('brightnessSlider').value = 255;";
  html += "document.getElementById('brightnessValue').textContent = 255;";
  html += "updateLEDPreview();";
  html += "}";
  
  html += "function updateLEDPreview() {";
  html += "const brightness = document.getElementById('brightnessSlider').value;";
  html += "document.getElementById('brightnessValue').textContent = brightness;";
  html += "const color = document.getElementById('colorPicker').value;";
  html += "fetch('/set-led', {";
  html += "method: 'POST',";
  html += "headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  html += "body: 'color=' + encodeURIComponent(color) + '&brightness=' + brightness";
  html += "});";
  html += "}";
  
  html += "function saveSettings() {";
  html += "const username = document.getElementById('usernameInput').value;";
  html += "const ssid = document.getElementById('ssidInput').value;";
  html += "const password = document.getElementById('passwordInput').value;";
  html += "const brightness = document.getElementById('brightnessSlider').value;";
  html += "const color = document.getElementById('colorPicker').value;";
  html += "fetch('/save-settings', {";
  html += "method: 'POST',";
  html += "headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
  html += "body: 'username=' + encodeURIComponent(username) + '&ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password) + '&brightness=' + brightness + '&color=' + encodeURIComponent(color)";
  html += "}).then(() => {";
  html += "alert('Settings saved! Device will restart in 3 seconds...');";
  html += "setTimeout(() => location.reload(), 3000);";
  html += "});";
  html += "}";
  
  html += "setInterval(updateMessages, 2000);";
  html += "updateMessages();";
  html += "document.getElementById('messageInput').addEventListener('keypress', function(e) {";
  html += "if (e.key === 'Enter') sendMessage();";
  html += "});";
  html += "</script>";
  html += "</body>";
  html += "</html>";
  
  server.send(200, "text/html", html);
}

void handleSendMessage() {
  if (server.hasArg("message")) {
    String message = server.arg("message");
    String timestamp = server.hasArg("timestamp") ? server.arg("timestamp") : "Unknown";
    
    sendLoRaMessage(message, timestamp);
    addToMessageHistory(username, message, timestamp, true);
    
    setLEDState("message_sent");
    delay(500);
    if (clientConnected) {
      setLEDState("client_connected");
    } else {
      setLEDState("ap_started");
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleGetMessages() {
  server.send(200, "text/html", messageHistory);
}

void handleSettings() {
  server.send(200, "text/html", "Settings page");
}

void handleGetSettings() {
  DynamicJsonDocument doc(1024);
  doc["username"] = username;
  doc["ssid"] = nodeSSID;
  doc["password"] = nodePassword;
  doc["brightness"] = ledBrightness;
  
  // Extract RGB from uint32_t color
  uint8_t r = (ledColor >> 16) & 0xFF;
  uint8_t g = (ledColor >> 8) & 0xFF;
  uint8_t b = ledColor & 0xFF;
  
  String colorHex = "#";
  if (r < 16) colorHex += "0";
  colorHex += String(r, HEX);
  if (g < 16) colorHex += "0";
  colorHex += String(g, HEX);
  if (b < 16) colorHex += "0";
  colorHex += String(b, HEX);
  
  doc["color"] = colorHex;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSetLED() {
  if (server.hasArg("color") && server.hasArg("brightness")) {
    String colorHex = server.arg("color");
    ledBrightness = server.arg("brightness").toInt();
    
    // Parse hex color to uint32_t using strip.Color()
    if (colorHex.startsWith("#")) {
      colorHex = colorHex.substring(1);
    }
    
    long colorValue = strtol(colorHex.c_str(), NULL, 16);
    uint8_t r = (colorValue >> 16) & 0xFF;
    uint8_t g = (colorValue >> 8) & 0xFF;
    uint8_t b = colorValue & 0xFF;
    
    ledColor = strip.Color(r, g, b);  // Use strip.Color to create proper format
    setLEDState("custom_color");
  }
  server.send(200, "text/plain", "OK");
}

void handleSaveSettings() {
  if (server.hasArg("username")) username = server.arg("username");
  if (server.hasArg("ssid")) nodeSSID = server.arg("ssid");
  if (server.hasArg("password")) nodePassword = server.arg("password");
  if (server.hasArg("brightness")) ledBrightness = server.arg("brightness").toInt();
  
  if (server.hasArg("color")) {
    String colorHex = server.arg("color");
    if (colorHex.startsWith("#")) {
      colorHex = colorHex.substring(1);
    }
    long colorValue = strtol(colorHex.c_str(), NULL, 16);
    uint8_t r = (colorValue >> 16) & 0xFF;
    uint8_t g = (colorValue >> 8) & 0xFF;
    uint8_t b = colorValue & 0xFF;
    ledColor = strip.Color(r, g, b);  // Use strip.Color to create proper format
  }
  
  saveConfig();
  server.send(200, "text/plain", "OK");
  
  delay(3000);
  ESP.restart();
}

void sendLoRaMessage(String message, String timestamp) {
  DynamicJsonDocument doc(1024);
  doc["type"] = "message";
  doc["from"] = nodeId;
  doc["username"] = username;
  doc["message"] = message;
  doc["timestamp"] = timestamp;
  
  String packet;
  serializeJson(doc, packet);
  
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  
  Serial.println("Sent: " + message);
}

void requestHistory() {
  DynamicJsonDocument doc(512);
  doc["type"] = "history_request";
  doc["from"] = nodeId;
  
  String packet;
  serializeJson(doc, packet);
  
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  
  Serial.println("Requesting history from network");
}

void shareHistory() {
  DynamicJsonDocument doc(2048);
  doc["type"] = "history_share";
  doc["from"] = nodeId;
  doc["history"] = messageHistory;
  
  String packet;
  serializeJson(doc, packet);
  
  if (packet.length() > 200) {
    Serial.println("History too large to share via LoRa");
    return;
  }
  
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  
  Serial.println("Shared history to network");
}

void checkLoRaMessages() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, received);
    
    if (!error) {
      String type = doc["type"];
      String fromNode = doc["from"];
      
      if (fromNode != nodeId) {
        if (type == "message") {
          String senderUsername = doc["username"];
          String message = doc["message"];
          String timestamp = doc["timestamp"];
          
          addToMessageHistory(senderUsername, message, timestamp, false);
          messageReceivedBeep();
          setLEDState("message_received");
          delay(500);
          if (clientConnected) {
            setLEDState("client_connected");
          } else {
            setLEDState("ap_started");
          }
          
          Serial.println("Received from " + fromNode + " (" + senderUsername + "): " + message);
        }
        else if (type == "history_request") {
          shareHistory();
        }
        else if (type == "history_share") {
          String sharedHistory = doc["history"];
          if (sharedHistory.length() > messageHistory.length()) {
            messageHistory = sharedHistory;
            saveHistory();
            Serial.println("Updated history from network");
          }
        }
      }
    }
  }
}

void addToMessageHistory(String user, String message, String timestamp, bool isOwn) {
  String messageClass = "message";
  
  if (message.indexOf("SOS") != -1) {
    messageClass += " message-sos";
  } else if (isOwn) {
    messageClass += " message-own";
  } else {
    messageClass += " message-other";
  }
  
  String newMessage = "<div class=\"" + messageClass + "\">";
  newMessage += "<div class=\"message-user\">" + user + "</div>";
  newMessage += "<div>" + message + "</div>";
  newMessage += "<div class=\"message-time\">" + timestamp + "</div>";
  newMessage += "</div>";
  
  messageHistory += newMessage;
  
  // Keep only last 100 messages to prevent memory issues
  int messageCount = 0;
  for (int i = 0; i < messageHistory.length(); i++) {
    if (messageHistory.substring(i, i+5) == "<div ") {
      messageCount++;
    }
  }
  
  if (messageCount > 100) {
    int firstDiv = messageHistory.indexOf("<div class=\"message");
    int endFirstDiv = messageHistory.indexOf("</div>", messageHistory.indexOf("</div>", messageHistory.indexOf("</div>", firstDiv) + 1) + 1) + 6;
    messageHistory = messageHistory.substring(endFirstDiv);
  }
  
  saveHistory();
}