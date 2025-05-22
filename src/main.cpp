/*
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Complete project details at https://RandomNerdTutorials.com/esp-now-auto-pairing-esp32-esp8266/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  Based on JC Servaye example: https://github.com/Servayejc/esp_now_web_server/
*/
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <ArduinoJson.h>
#include <Arduino_JSON.h>

#include <HTTPClient.h>   // for upload data to webserver (xampp)

// Replace with your network credentials (STATION)
const char* ssid = "ENJMesh";
const char* password = "enjoy042611749";
// Xampp server http link
String URL = "http://enjoycctv.trueddns.com:17521/share/public/espdata_00/upload_01.php";
// Public variables
float temperatures[5] = {0};
float humidities[5] = {0};
int readIds[5] = {0};
bool readytoupload[5] = {false};
JSONVar boards[5];

esp_now_peer_info_t slave;
int chan; 

enum MessageType {PAIRING, DATA, RESET};  //++ Add RESET to enum for Send Reset Command
MessageType messageType;

int counter = 0;

uint8_t clientMacAddress[6];

// Structure example to receive data
// Must match the sender structure
typedef struct struct_message {
  uint8_t msgType;
  uint8_t id;
  float temp;
  float hum;
  unsigned int readingId;
} struct_message;

typedef struct struct_pairing {       // new structure for pairing
    uint8_t msgType;
    uint8_t id;
    uint8_t macAddr[6];
    uint8_t channel;
} struct_pairing;

struct_message incomingReadings;
struct_message outgoingSetpoints;
struct_message outgoingResetPeers;    // new struct_message for Reset All Peers
struct_pairing pairingData;

AsyncWebServer server(80);
AsyncEventSource events("/events");

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP-NOW DASHBOARD</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    p {  font-size: 1.2rem;}
    body {  margin: 0;}
    .topnav { overflow: hidden; background-color: #2f4468; color: white; font-size: 1.7rem; }
    .content { padding: 20px; }
    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
    .cards { max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
    .reading { font-size: 2.8rem; }
    .packet { color: #bebebe; }
    .card.temperature { color: #fd7e14; }
    .card.humidity { color: #1b78e2; }
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

void readMacAddress(){
  uint8_t baseMac[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  if (ret == ESP_OK) {
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
  } else {
    Serial.println("Failed to read MAC address");
  }
}

void readDataToSend() {
  outgoingSetpoints.msgType = DATA;
  outgoingSetpoints.id = 0;
  outgoingSetpoints.temp = 999;   //random(0, 40);
  outgoingSetpoints.hum = 888;    //random(0, 100);
  outgoingSetpoints.readingId = counter++;
}

void readDataToSendResetPeer() {
  outgoingResetPeers.msgType = RESET;
  outgoingResetPeers.id = 0;
  outgoingResetPeers.temp = 111;   //random(0, 40);
  outgoingResetPeers.hum = 222;    //random(0, 100);
  outgoingResetPeers.readingId = counter++;
}

// ---------------------------- esp_ now -------------------------
void printMAC(const uint8_t * mac_addr){
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

bool addPeer(const uint8_t *peer_addr) {      // add pairing
  memset(&slave, 0, sizeof(slave));
  const esp_now_peer_info_t *peer = &slave;
  memcpy(slave.peer_addr, peer_addr, 6);
  
  slave.channel = chan; // pick a channel
  slave.encrypt = 0; // no encryption
  // check if the peer exists
  bool exists = esp_now_is_peer_exist(slave.peer_addr);
  if (exists) {
    // Slave already paired.
    Serial.println("Already Paired");
    return true;
  }
  else {
    esp_err_t addStatus = esp_now_add_peer(peer);
    if (addStatus == ESP_OK) {
      // Pair success
      Serial.println("Pair success");
      return true;
    }
    else 
    {
      Serial.println("Pair failed");
      return false;
    }
  }
} 

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success to " : "Delivery Fail to ");
  printMAC(mac_addr);
  Serial.println();
}

void OnDataRecv(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { 
  Serial.print(len);
  Serial.println(" bytes of new data received.");
  StaticJsonDocument<1000> root;
  String payload;
  uint8_t type = incomingData[0];       // first message byte is the type of message 
  switch (type) {
  case DATA :                           // the message is data type
    memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
    // create a JSON document with received data and send it by event to the web page
    root["id"] = incomingReadings.id;
    root["temperature"] = incomingReadings.temp;
    root["humidity"] = incomingReadings.hum;
    root["readingId"] = String(incomingReadings.readingId);
    serializeJson(root, payload);
    Serial.print("event send :");
    serializeJson(root, Serial);

  //++ for made json package to send to Xampp
    boards[incomingReadings.id - 1]["read_module_no"] = incomingReadings.id;
    boards[incomingReadings.id - 1]["temperature"] = incomingReadings.temp;
    boards[incomingReadings.id - 1]["humidity"] = incomingReadings.hum;
    boards[incomingReadings.id - 1]["readingId"] = incomingReadings.readingId;
    
    Serial.printf("Board ID %u: %u bytes\n", incomingReadings.id, len);
    Serial.printf("Temperature: %4.2f \n", incomingReadings.temp);
    Serial.printf("Humidity: %4.2f \n", incomingReadings.hum);
    Serial.printf("Reading ID: %d \n", incomingReadings.readingId);
    Serial.println();

    temperatures[incomingReadings.id - 1] = incomingReadings.temp;
    humidities[incomingReadings.id - 1] = incomingReadings.hum;
    readIds[incomingReadings.id - 1] = incomingReadings.readingId;
    readytoupload[incomingReadings.id - 1] = true;

  //-- for made json package to send to Xampp
    events.send(payload.c_str(), "new_readings", millis());
    Serial.println();
    break;
  
  case PAIRING:                            // the message is a pairing request 
    memcpy(&pairingData, incomingData, sizeof(pairingData));
    Serial.println(pairingData.msgType);
    Serial.println(pairingData.id);
    Serial.print("Pairing request from MAC Address: ");
    printMAC(pairingData.macAddr);
    Serial.print(" on channel ");
    Serial.println(pairingData.channel);

    clientMacAddress[0] = pairingData.macAddr[0];
    clientMacAddress[1] = pairingData.macAddr[1];
    clientMacAddress[2] = pairingData.macAddr[2];
    clientMacAddress[3] = pairingData.macAddr[3];
    clientMacAddress[4] = pairingData.macAddr[4];
    clientMacAddress[5] = pairingData.macAddr[5];

    if (pairingData.id > 0) {     // do not replay to server itself
      if (pairingData.msgType == PAIRING) { 
        pairingData.id = 0;       // 0 is server
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
}

void initESP_NOW(){
    // Init ESP-NOW
    if (esp_now_init() != ESP_OK) {
      Serial.println("Error initializing ESP-NOW");
      return;
    }
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
} 

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
  
  http.end();  // Close connection
  readytoupload[id - 1] = false;  // reset status to prevent double upload
  delay(1000); // Delay between uploads
}

//++ Function for check WiFi Still Connected if not Reconnect it
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Attempting reconnection...");
    for (int i=0 ; i<10 ; i++) {
      Serial.println("Prepare to Restart!!"+i);
    }
    readDataToSendResetPeer();
    esp_now_send(NULL, (uint8_t *) &outgoingResetPeers, sizeof(outgoingResetPeers));
    ESP.restart();
    
  }
}


//-- Function for check WiFi Still Connected if not Reconnect it
void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Serial.println("Void Setup");
  delay(5000);
  WiFi.mode(WIFI_STA);
  WiFi.STA.begin();
  Serial.print("Server MAC Address: ");
  readMacAddress();

  // Set the device as a Station and Soft Access Point simultaneously
  Serial.println("Start connect wifi");
  delay(2000);
  WiFi.mode(WIFI_AP_STA);
  // Set device as a Wi-Fi Station
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Setting as a Wi-Fi Station..");
  }

  Serial.print("Server SOFT AP MAC Address:  ");
  Serial.println(WiFi.softAPmacAddress());

  chan = WiFi.channel();
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());

  initESP_NOW();
  
  // Start Web server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });
  
  // Events 
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);
  // start server
  server.begin();
}

void loop() {
// check wifi connect
checkWiFiConnection();

  static unsigned long lastEventTime = millis();
  static const unsigned long EVENT_INTERVAL_MS = 5000;
  if ((millis() - lastEventTime) > EVENT_INTERVAL_MS) {
    events.send("ping", NULL, millis());
    lastEventTime = millis();
    readDataToSend();
    esp_now_send(NULL, (uint8_t *) &outgoingSetpoints, sizeof(outgoingSetpoints));
  };
// ++ upload data to xampp
  for (int i = 0; i < 5; i++) {
    if (readytoupload[i] && temperatures[i] > 0 && humidities[i] > 0) {
      UploadData2Xampp(i + 1);
    } else {
      Serial.printf("No data to upload for board %d..\n", i + 1);
    }
  }
// -- upload data to xampp

  Serial.println("End of loop");
  delay(5000);

}