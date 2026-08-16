//==================================================
// Smart Water Surface Boat
// Base Station
//==================================================


//==================================================
// Library
//==================================================

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


//==================================================
// Configuration
//==================================================

// WiFi
const char* ssid = "max";
const char* password = "0924639159";


// Backend
const char* serverIP = "192.168.1.36";
const int serverPort = 8000;


// API
String missionAPI =
  "http://" + String(serverIP) + ":" + String(serverPort) + "/mission";

String routeAPI =
  "http://" + String(serverIP) + ":" + String(serverPort) + "/route";

String sensorAPI =
  "http://" + String(serverIP) + ":" + String(serverPort) + "/data";


//==================================================
// Boat MAC Address
//==================================================

uint8_t boatMacAddress[] = {
  0x24,
  0x6F,
  0x28,
  0x00,
  0x00,
  0x00
};

//==================================================
// ESP-NOW Packet
//==================================================

enum PacketType {
  PACKET_MISSION = 1,
  PACKET_SENSOR = 2,
  PACKET_ROUTE = 3,
  PACKET_ACK = 4
};

struct MissionPacket {
  uint8_t type;

  char command[16];

  char mode[16];
};

struct RoutePoint {
  float latitude;

  float longitude;
};

struct RoutePacket {
  uint8_t type;

  int index;

  int total;

  bool lastPacket;

  RoutePoint point;
};

struct AckPacket {
  uint8_t type;

  bool routeReceived;

  bool missionReceived;
};

struct SensorPacket {
  int8_t type;
  int index;
  int total;

  float latitude;
  float longitude;

  bool lastPacket;

  char boatID[16];

  float temperature;
  float ph;
  float turbidity;

  float heading;
  float flow_v_lat;
  float flow_v_lng;
};

MissionPacket outgoingMission;

RoutePacket outgoingRoute;

AckPacket incomingAck;

SensorPacket incomingSensor;

esp_now_peer_info_t peerInfo;

//==================================================
// Route
//==================================================

float waypointLat[100];

float waypointLng[100];

int totalWaypoints = 0;


//==================================================
// Mission
//==================================================

String missionCommand = "";

String missionMode = "";

//==================================================
// Base State
//==================================================

enum BaseState {

  BASE_IDLE,

  GET_MISSION,

  GET_ROUTE,

  SEND_ROUTE,

  WAIT_ROUTE_ACK,

  SEND_MISSION,

  WAIT_MISSION_ACK,

  WAIT_SENSOR

};

BaseState baseState = GET_MISSION;

//==================================================
// Timer
//==================================================

unsigned long lastMissionCheck = 0;

const unsigned long missionInterval = 3000;

bool routeDownloaded = false;

bool routeSent = false;

bool missionSent = false;

bool routeAck = false;

bool missionAck = false;

bool lastMissionRunning = false;

bool espNowSendFinished = false;
bool espNowSendSuccess = false;

//==================================================
// ESP-NOW Callback
//==================================================

void uploadSensorData();

void onDataReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* incomingData,
  int len) {

  uint8_t packetType = incomingData[0];

  if (packetType == PACKET_SENSOR) {
    memcpy(
      &incomingSensor,
      incomingData,
      sizeof(SensorPacket));

    Serial.println();

    Serial.println("==========");
    Serial.println("Sensor Packet Received");

    Serial.print("Boat : ");
    Serial.println(incomingSensor.boatID);

    Serial.print("Temperature : ");
    Serial.println(incomingSensor.temperature);

    Serial.print("pH : ");
    Serial.println(incomingSensor.ph);

    Serial.print("Turbidity : ");
    Serial.println(incomingSensor.turbidity);

    Serial.println("==========");

    uploadSensorData();
  } else if (packetType == PACKET_ACK) {
    memcpy(
      &incomingAck,
      incomingData,
      sizeof(AckPacket));

    routeAck = incomingAck.routeReceived;
    missionAck = incomingAck.missionReceived;

    Serial.println("ACK Received");
  }  // ปิด else if

}  // <<< เพิ่มบรรทัดนี้ เพื่อปิด onDataReceive()


void onDataSent(
  const wifi_tx_info_t* info,
  esp_now_send_status_t status) {
  espNowSendFinished = true;

  if (status == ESP_NOW_SEND_SUCCESS) {
    espNowSendSuccess = true;

    Serial.println("ESP-NOW Send Success");
  } else {
    espNowSendSuccess = false;

    Serial.println("ESP-NOW Send Failed");
  }
}



//==================================================
// Get Mission From Backend
//==================================================

void getMissionCommand() {
  HTTPClient http;

  http.begin(missionAPI);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String json = http.getString();

    DynamicJsonDocument doc(512);

    deserializeJson(doc, json);

    missionCommand =
      doc["command"].as<String>();

    missionMode =
      doc["mode"].as<String>();

    Serial.println();
    Serial.println("Mission Download");

    Serial.print("Command : ");
    Serial.println(missionCommand);

    Serial.print("Mode : ");
    Serial.println(missionMode);

    if (missionCommand == "START") {
      Serial.println("Mission Ready");

    } else if (missionCommand == "STOP") {
      Serial.println("Mission Stop");
    }
  } else {
    Serial.print("Mission API Error : ");

    Serial.println(httpCode);
  }

  http.end();
}

//==================================================
// Send Mission To Boat
//==================================================

void sendMissionESPNow() {
  outgoingMission.type = PACKET_MISSION;

  strcpy(
    outgoingMission.command,
    missionCommand.c_str());

  strcpy(
    outgoingMission.mode,
    missionMode.c_str());

  esp_now_send(
    boatMacAddress,
    (uint8_t*)&outgoingMission,
    sizeof(outgoingMission));

  Serial.println();
  Serial.println("Mission Sent To Boat");

  Serial.print("Command : ");
  Serial.println(missionCommand);

  Serial.print("Mode : ");
  Serial.println(missionMode);
}

//==================================================
// Download Route
//==================================================

void downloadRoute() {
  HTTPClient http;

  http.begin(routeAPI);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String json = http.getString();

    DynamicJsonDocument doc(4096);

    deserializeJson(doc, json);

    totalWaypoints = 0;

    for (JsonObject point : doc.as<JsonArray>()) {
      waypointLat[totalWaypoints] =
        point["latitude"];

      waypointLng[totalWaypoints] =
        point["longitude"];

      totalWaypoints++;
    }

    if (totalWaypoints == 0) {
      Serial.println("Route Empty");

      return;
    }

    Serial.println();

    Serial.println("Route Download Success");

    Serial.print("Total : ");

    Serial.println(totalWaypoints);
  } else {
    Serial.println("Route Download Failed");
  }

  http.end();
}

//==================================================
// Send Route To Boat
//==================================================

void sendRouteESPNow() {
  Serial.println();
  Serial.println("Sending Route To Boat...");

  for (int i = 0; i < totalWaypoints; i++) {
    outgoingRoute.type = PACKET_ROUTE;

    outgoingRoute.index = i;

    outgoingRoute.total = totalWaypoints;

    outgoingRoute.point.latitude =
      waypointLat[i];

    outgoingRoute.point.longitude =
      waypointLng[i];

    if (i == totalWaypoints - 1) {
      outgoingRoute.lastPacket = true;
    } else {
      outgoingRoute.lastPacket = false;
    }

    esp_now_send(
      boatMacAddress,
      (uint8_t*)&outgoingRoute,
      sizeof(outgoingRoute));

    Serial.print("Send Waypoint ");

    Serial.println(i + 1);

    delay(100);
  }

  Serial.println();
  Serial.println("Route Send Complete");
}


//==================================================
// Upload Sensor To Backend
//==================================================

void uploadSensorData() {
  HTTPClient http;

  http.begin(sensorAPI);

  http.addHeader(
    "Content-Type",
    "application/json");

  DynamicJsonDocument doc(512);

  doc["boat_id"] = incomingSensor.boatID;

  doc["temp_c"] = incomingSensor.temperature;

  doc["ph_level"] = incomingSensor.ph;

  doc["turbidity_ntu"] = incomingSensor.turbidity;

  doc["latitude"] = incomingSensor.latitude;

  doc["longitude"] = incomingSensor.longitude;

  doc["heading"] = incomingSensor.heading;

  doc["flow_v_lat"] = incomingSensor.flow_v_lat;

  doc["flow_v_lng"] = incomingSensor.flow_v_lng;

  String json;

  serializeJson(doc, json);

  Serial.println("===== SENDING TO BACKEND =====");
  Serial.print("URL: ");
  Serial.println(sensorAPI);

  Serial.print("JSON: ");
  Serial.println(json);

  int httpCode = http.POST(json);

  Serial.print("HTTP CODE: ");
  Serial.println(httpCode);

  if (httpCode == 200) {
    Serial.println("Upload Success");
  } else {
    Serial.print("Upload Failed : ");

    Serial.println(httpCode);
  }

  http.end();
}


//==================================================
// Base State Machine
//==================================================
void runBaseStation() {

  switch (baseState) {

    //------------------------------------------
    // GET MISSION
    //------------------------------------------
    case GET_MISSION:

      getMissionCommand();

      if (missionCommand == "START") {

        if (!lastMissionRunning) {

          lastMissionRunning = true;

          routeDownloaded = false;
          routeSent = false;
          missionSent = false;

          routeAck = false;
          missionAck = false;

          espNowSendFinished = false;
          espNowSendSuccess = false;

          baseState = GET_ROUTE;

        } else {

          baseState = WAIT_SENSOR;
        }

      } else if (missionCommand == "STOP") {

        lastMissionRunning = false;

        routeDownloaded = false;
        routeSent = false;
        missionSent = false;

        routeAck = false;
        missionAck = false;

        espNowSendFinished = false;
        espNowSendSuccess = false;

        baseState = WAIT_SENSOR;

      } else {

        baseState = WAIT_SENSOR;
      }

      break;

    //------------------------------------------
    // GET ROUTE
    //------------------------------------------
    case GET_ROUTE:

      if (!routeDownloaded) {

        downloadRoute();

        if (totalWaypoints > 0) {

          routeDownloaded = true;
          baseState = SEND_ROUTE;

        } else {

          Serial.println("No Route From Backend");
          baseState = WAIT_SENSOR;
        }

      } else {

        baseState = SEND_ROUTE;
      }

      break;

    //------------------------------------------
    // SEND ROUTE
    //------------------------------------------
    case SEND_ROUTE:

      if (!routeSent) {

        espNowSendFinished = false;
        espNowSendSuccess = false;

        routeAck = false;

        sendRouteESPNow();

        routeSent = true;
      }

      baseState = WAIT_ROUTE_ACK;

      break;

    //------------------------------------------
    // WAIT ROUTE ACK
    //------------------------------------------
    case WAIT_ROUTE_ACK:

      if (routeAck) {

        Serial.println();
        Serial.println("==========");
        Serial.println("Route ACK Received");
        Serial.println("==========");

        baseState = SEND_MISSION;
      }

      break;

    //------------------------------------------
    // SEND MISSION
    //------------------------------------------
    case SEND_MISSION:

      if (!missionSent) {

        espNowSendFinished = false;
        espNowSendSuccess = false;

        missionAck = false;

        sendMissionESPNow();

        missionSent = true;
      }

      baseState = WAIT_MISSION_ACK;

      break;

      //------------------------------------------
      // WAIT MISSION ACK
      //------------------------------------------
    case WAIT_MISSION_ACK:

      if (missionAck) {

        Serial.println();
        Serial.println("==========");
        Serial.println("Mission ACK Received");
        Serial.println("==========");

        routeSent = false;
        missionSent = false;

        baseState = WAIT_SENSOR;
      }

      break;

    //------------------------------------------
    // WAIT SENSOR
    //------------------------------------------
    case WAIT_SENSOR:

      if (millis() - lastMissionCheck >= missionInterval) {

        lastMissionCheck = millis();
        baseState = GET_MISSION;
      }

      break;

    //------------------------------------------
    // IDLE
    //------------------------------------------
    case BASE_IDLE:

      break;
  }
}
//==================================================
// TEST SENSOR UPLOAD
//==================================================

void testSensorUpload() {

  strcpy(
    incomingSensor.boatID,
    "Boat01");

  incomingSensor.temperature = 31.2;

  incomingSensor.ph = 7.3;

  incomingSensor.turbidity = 20.0;

  incomingSensor.latitude = 13.75370309;

  incomingSensor.longitude = 100.48686313;

  incomingSensor.heading = 90.0;

  incomingSensor.flow_v_lat = 0.00137537;

  incomingSensor.flow_v_lng = 0.0100487;


  Serial.println();

  Serial.println("===== TEST SENSOR UPLOAD =====");


  Serial.print("Boat ID : ");
  Serial.println(incomingSensor.boatID);


  Serial.print("Temperature : ");
  Serial.println(incomingSensor.temperature);


  Serial.print("pH : ");
  Serial.println(incomingSensor.ph);


  Serial.print("Turbidity : ");
  Serial.println(incomingSensor.turbidity);


  Serial.println("STEP A");


  Serial.print("Latitude : ");
  Serial.println(incomingSensor.latitude);


  Serial.println("STEP B");


  Serial.print("Longitude : ");
  Serial.println(incomingSensor.longitude);


  Serial.println("STEP C");


  uploadSensorData();


  Serial.println("STEP D");
}

//==================================================
// Setup
//==================================================

void setup() {

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);

  //==================================================
  // BASE MAC
  //==================================================

  Serial.print("Base MAC : ");
  Serial.println(WiFi.macAddress());


  //==================================================
  // ESP-NOW
  //==================================================

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  Serial.println("ESP-NOW Ready");

  //==================================================
  // Wi-Fi
  //==================================================

  WiFi.begin(
    ssid,
    password);

  Serial.println();
  Serial.println("Connecting WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");

  Serial.print("IP : ");
  Serial.println(WiFi.localIP());

  Serial.print("WiFi Channel : ");
  Serial.println(WiFi.channel());

  //==================================================
  // ENABLE LONG RANGE ESP-NOW
  //==================================================

  Serial.println();
  Serial.println("==============================");
  Serial.println("LONG RANGE ESP-NOW");
  Serial.println("==============================");

  uint8_t protocol =
    WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR;

  esp_err_t lrResult =
    esp_wifi_set_protocol(
      WIFI_IF_STA,
      protocol);

  if (lrResult == ESP_OK) {

    Serial.println("LONG RANGE ESP-NOW : ENABLED");

  } else {

    Serial.print("LONG RANGE ESP-NOW : FAILED, ERROR = ");
    Serial.println(lrResult);
  }


  // Check current protocol

  uint8_t protocolCheck = 0;

  esp_err_t checkResult =
    esp_wifi_get_protocol(
      WIFI_IF_STA,
      &protocolCheck);

  Serial.print("Protocol Check Result : ");

  if (checkResult == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }

  Serial.print("Protocol Bitmap : 0x");
  Serial.println(protocolCheck, HEX);

  Serial.println("==============================");


  testSensorUpload();


  //==================================================
  // Register Callback
  //==================================================

  esp_now_register_recv_cb(onDataReceive);
  esp_now_register_send_cb(onDataSent);


  //==================================================
  // Add Boat Peer
  //==================================================

  memcpy(
    peerInfo.peer_addr,
    boatMacAddress,
    6);

  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Add Peer Failed");
    return;
  }

  Serial.println("Boat Peer Added");
}

//==================================================
// Loop
//==================================================

void loop() {
  runBaseStation();

  delay(100);
}