/*
  Enhanced ESP-NOW Server with Dynamic Auto-Pairing
  Based on Random Nerd Tutorials and enhanced with dynamic pairing capabilities
*/
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <ArduinoJson.h>
#include <Arduino_JSON.h>
#include <HTTPClient.h>

// // Wi-Fi credentials enjoy office
const char* ssid = "TrueEnjoy";
const char* password = "enjoy7777777777";
// Wi-Fi credentials the house
// const char* ssid = "ENJMesh";
// const char* password = "enjoy042611749";

// Server URL for data upload
String URL = "http://enjoycctv.trueddns.com:17521/share/public/espdata_00/upload_01.php";

// Global variables for sensor data
float temperatures[5] = {0};
float humidities[5] = {0};
int readIds[5] = {0};
bool readytoupload[5] = {false};
JSONVar boards[5];

// ESP-NOW peer management
esp_now_peer_info_t slave;
int chan;
int counter = 0;
uint8_t clientMacAddress[6];

// Message types
enum MessageType {
    PAIRING,
    DATA,
    RESET
};

// Data structures
typedef struct struct_message {
    uint8_t msgType;
    uint8_t id;
    float temp;
    float hum;
    unsigned int readingId;
} struct_message;

typedef struct struct_pairing {
    uint8_t msgType;
    uint8_t id;
    uint8_t macAddr[6];
    uint8_t channel;
} struct_pairing;

// Message instances
struct_message incomingReadings;
struct_message outgoingSetpoints;
struct_message outgoingResetPeers;
struct_pairing pairingData;

// Web server
AsyncWebServer server(80);
AsyncEventSource events("/events");

// HTML interface
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP-NOW DASHBOARD</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    p {font-size: 1.2rem;}
    body {margin: 0;}
    .topnav {overflow: hidden; background-color: #2f4468; color: white; font-size: 1.7rem;}
    .content {padding: 20px;}
    .card {background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);}
    .cards {max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));}
    .reading {font-size: 2.8rem;}
    .packet {color: #bebebe;}
    .card.temperature {color: #fd7e14;}
    .card.humidity {color: #1b78e2;}
  </style>
</head>
<body>
  <div class="topnav">
    <h3>ESP-NOW DASHBOARD</h3>
  </div>
  <div class="content">
    <div class="cards">
      <div class="card temperature">
        <h4><i class="fas fa-thermometer-half"></i> BOARD #1 - TEMPERATURE</h4><p><span class="reading"><span id="t1"></span> &deg;C</span></p><p class="packet">Reading ID: <span id="rt1"></span></p>
      </div>
      <div class="card humidity">
        <h4><i class="fas fa-tint"></i> BOARD #1 - HUMIDITY</h4><p><span class="reading"><span id="h1"></span> &percnt;</span></p><p class="packet">Reading ID: <span id="rh1"></span></p>
      </div>
      <div class="card temperature">
        <h4><i class="fas fa-thermometer-half"></i> BOARD #2 - TEMPERATURE</h4><p><span class="reading"><span id="t2"></span> &deg;C</span></p><p class="packet">Reading ID: <span id="rt2"></span></p>
      </div>
      <div class="card humidity">
        <h4><i class="fas fa-tint"></i> BOARD #2 - HUMIDITY</h4><p><span class="reading"><span id="h2"></span> &percnt;</span></p><p class="packet">Reading ID: <span id="rh2"></span></p>
      </div>
    </div>
  </div>
<script>
if (!!window.EventSource) {
 var source = new EventSource('/events');
 
 source.addEventListener('open', function(e) {
  console.log("Events Connected");
 }, false);
 source.addEventListener('error', function(e) {
  if (e.target.readyState != EventSource.OPEN) {
    console.log("Events Disconnected");
  }
 }, false);
 
 source.addEventListener('message', function(e) {
  console.log("message", e.data);
 }, false);
 
 source.addEventListener('new_readings', function(e) {
  console.log("new_readings", e.data);
  var obj = JSON.parse(e.data);
  document.getElementById("t"+obj.id).innerHTML = obj.temperature.toFixed(2);
  document.getElementById("h"+obj.id).innerHTML = obj.humidity.toFixed(2);
  document.getElementById("rt"+obj.id).innerHTML = obj.readingId;
  document.getElementById("rh"+obj.id).innerHTML = obj.readingId;
 }, false);
}
</script>
</body>
</html>)rawliteral";

// === MAC Address Functions ===
void readMacAddress() {
    uint8_t baseMac[6];
    esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
    if (ret == ESP_OK) {
        Serial.printf("MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                      baseMac[0], baseMac[1], baseMac[2],
                      baseMac[3], baseMac[4], baseMac[5]);
    } else {
        Serial.println("Failed to read MAC address");
    }
}

void printMAC(const uint8_t * mac_addr) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    Serial.print(macStr);
}

// === Enhanced Peer Management ===
bool addPeer(const uint8_t *peer_addr) {
    memset(&slave, 0, sizeof(slave));
    const esp_now_peer_info_t *peer = &slave;
    memcpy(slave.peer_addr, peer_addr, 6);
    
    slave.channel = chan;
    slave.encrypt = 0; // no encryption
    
    // Check if the peer exists
    bool exists = esp_now_is_peer_exist(slave.peer_addr);
    if (exists) {
        Serial.println("Already Paired");
        return true;
    } else {
        esp_err_t addStatus = esp_now_add_peer(peer);
        if (addStatus == ESP_OK) {
            Serial.println("Pair success");
            return true;
        } else {
            Serial.println("Pair failed");
            return false;
        }
    }
}

// === Data Preparation Functions ===
void readDataToSend() {
    outgoingSetpoints.msgType = DATA;
    outgoingSetpoints.id = 0;
    outgoingSetpoints.temp = 999;
    outgoingSetpoints.hum = 888;
    outgoingSetpoints.readingId = counter++;
}

void readDataToSendResetPeer() {
    outgoingResetPeers.msgType = RESET;
    outgoingResetPeers.id = 0;
    outgoingResetPeers.temp = 111;
    outgoingResetPeers.hum = 222;
    outgoingResetPeers.readingId = counter++;
}

// === ESP-NOW Callbacks ===
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("Last Packet Send Status: ");
    Serial.print(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success to " : "Delivery Fail to ");
    printMAC(mac_addr);
    Serial.println();
}

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
    Serial.printf("%d bytes of new data received.\n", len);
    
    uint8_t type = incomingData[0]; // First message byte is the type of message
    
    switch (type) {
        case DATA: {
            // Handle data message
            memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
            
            // Create JSON document for web interface
            StaticJsonDocument<1000> root;
            String payload;
            
            root["id"] = incomingReadings.id;
            root["temperature"] = incomingReadings.temp;
            root["humidity"] = incomingReadings.hum;
            root["readingId"] = String(incomingReadings.readingId);
            serializeJson(root, payload);
            
            Serial.print("Event send: ");
            serializeJson(root, Serial);
            
            // Update boards data for XAMPP upload
            boards[incomingReadings.id - 1]["read_module_no"] = incomingReadings.id;
            boards[incomingReadings.id - 1]["temperature"] = incomingReadings.temp;
            boards[incomingReadings.id - 1]["humidity"] = incomingReadings.hum;
            boards[incomingReadings.id - 1]["readingId"] = incomingReadings.readingId;
            
            Serial.printf("Board ID %u: %u bytes\n", incomingReadings.id, len);
            Serial.printf("Temperature: %4.2f\n", incomingReadings.temp);
            Serial.printf("Humidity: %4.2f\n", incomingReadings.hum);
            Serial.printf("Reading ID: %d\n", incomingReadings.readingId);
            Serial.println();
            
            // Store data for upload
            temperatures[incomingReadings.id - 1] = incomingReadings.temp;
            humidities[incomingReadings.id - 1] = incomingReadings.hum;
            readIds[incomingReadings.id - 1] = incomingReadings.readingId;
            readytoupload[incomingReadings.id - 1] = true;
            
            // Send event to web interface
            events.send(payload.c_str(), "new_readings", millis());
            Serial.println();
            break;
        }
        
        case PAIRING: {
            // Handle pairing request
            memcpy(&pairingData, incomingData, sizeof(pairingData));
            Serial.println("Pairing request received");
            Serial.printf("Message Type: %d\n", pairingData.msgType);
            Serial.printf("ID: %d\n", pairingData.id);
            Serial.print("Pairing request from MAC Address: ");
            printMAC(pairingData.macAddr);
            Serial.print(" on channel ");
            Serial.println(pairingData.channel);

            // Store client MAC address
            memcpy(clientMacAddress, pairingData.macAddr, 6);

            if (pairingData.id > 0) { // Do not reply to server itself
                if (pairingData.msgType == PAIRING) {
                    pairingData.id = 0; // 0 is server
                    // Server is in AP_STA mode: peers need to send data to server soft AP MAC address
                    WiFi.softAPmacAddress(pairingData.macAddr);
                    Serial.print("Pairing MAC Address: ");
                    printMAC(clientMacAddress);
                    pairingData.channel = chan;
                    Serial.println(" send response");
                    esp_err_t result = esp_now_send(clientMacAddress, (uint8_t *) &pairingData, sizeof(pairingData));
                    addPeer(clientMacAddress);
                }
            }
            break;
        }
        
        case RESET: {
            Serial.println("Reset command received - ignoring on server");
            break;
        }
        
        default:
            Serial.println("Unknown message type received");
            break;
    }
}

// === Initialize ESP-NOW ===
void initESP_NOW() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        ESP.restart();
    }
    
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    Serial.println("ESP-NOW initialized with dynamic auto-pairing support");
}

// === Data Upload Functions ===
void UploadData2Xampp(int id) {
    Serial.println("Uploading data to server...");

    String postData = "read_module_no=" + String(id) + 
                      "&temperature=" + String(temperatures[id - 1]) + 
                      "&humidity=" + String(humidities[id - 1]) + 
                      "&readingId=" + String(readIds[id - 1]); 

    HTTPClient http; 
    http.begin(URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    int httpCode = http.POST(postData); 
    if (httpCode > 0) {
        String payload = http.getString(); 
        Serial.print("HTTP Code: "); Serial.println(httpCode); 
        Serial.print("Response: "); Serial.println(payload); 
    } else {
        Serial.print("HTTP POST Error: "); Serial.println(httpCode); 
    }
    
    http.end();
    readytoupload[id - 1] = false;
    delay(1000);
}

// === WiFi Connection Management ===
void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected. Attempting reconnection...");
        for (int i = 0; i < 10; i++) {
            Serial.printf("Prepare to Restart!! %d\n", i);
            delay(1000);
        }
        readDataToSendResetPeer();
        esp_now_send(NULL, (uint8_t *) &outgoingResetPeers, sizeof(outgoingResetPeers));
        ESP.restart();
    }
}

// === Setup Function ===
void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 with Dynamic Auto-Pairing ESP-NOW and Web Dashboard");
    delay(2000);

    // Initialize WiFi in AP_STA mode for auto-pairing
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(WIFI_PS_NONE);
    
    Serial.print("Server MAC Address: ");
    readMacAddress();
    
    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.println("[Wi-Fi] Connecting...");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n[Wi-Fi] Connected!");
    Serial.print("Server SOFT AP MAC Address: ");
    Serial.println(WiFi.softAPmacAddress());
    
    chan = WiFi.channel();
    Serial.print("Station IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Wi-Fi Channel: ");
    Serial.println(chan);

    // Initialize ESP-NOW
    initESP_NOW();
    
    // Start Web server
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html);
    });
    
    // Events handler
    events.onConnect([](AsyncEventSourceClient *client){
        if(client->lastId()){
            Serial.printf("Client reconnected! Last message ID: %u\n", client->lastId());
        }
        client->send("hello!", NULL, millis(), 10000);
    });
    server.addHandler(&events);
    
    // Start server
    server.begin();
    
    Serial.println("Setup completed - Ready for dynamic auto-pairing and data reception");
}

// === Main Loop ===
void loop() {
    static unsigned long lastEventTime = 0;
    static unsigned long lastUploadCheck = 0;
    static unsigned long lastWiFiCheck = 0;
    
    const unsigned long EVENT_INTERVAL_MS = 5000;    // 5 seconds for ping/broadcast
    const unsigned long UPLOAD_INTERVAL_MS = 2000;   // 2 seconds for upload check
    const unsigned long WIFI_CHECK_INTERVAL_MS = 30000; // 30 seconds for WiFi check
    
    unsigned long currentTime = millis();
    
    // Check WiFi connection periodically
    if (currentTime - lastWiFiCheck >= WIFI_CHECK_INTERVAL_MS) {
        lastWiFiCheck = currentTime;
        checkWiFiConnection();
    }
    
    // Send periodic ping and broadcast to maintain connections
    if (currentTime - lastEventTime >= EVENT_INTERVAL_MS) {
        lastEventTime = currentTime;
        events.send("ping", NULL, millis());
        
        // Send broadcast message to all paired devices
        readDataToSend();
        esp_err_t result = esp_now_send(NULL, (uint8_t *) &outgoingSetpoints, sizeof(outgoingSetpoints));
        if (result == ESP_OK) {
            Serial.println("Broadcast sent successfully");
        } else {
            Serial.printf("Broadcast failed with error: %d\n", result);
        }
    }
    
    // Upload data to XAMPP server
    if (currentTime - lastUploadCheck >= UPLOAD_INTERVAL_MS) {
        lastUploadCheck = currentTime;
        
        for (int i = 0; i < 5; i++) {
            if (readytoupload[i] && temperatures[i] > 0 && humidities[i] > 0) {
                UploadData2Xampp(i + 1);
            }
        }
    }
    
    delay(100); // Small delay to prevent watchdog issues
}