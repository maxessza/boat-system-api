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
#include "Packets.h"


//==================================================
// Configuration
//==================================================

// WiFi
const char* ssid = "IBS_211 2.4G";
const char* password = "Icebrightesso211";


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

void uploadSensorData(const TelemetryPacket& telemetry);


void onDataReceive(
  const esp_now_recv_info_t* info,
  const uint8_t* incomingData,
  int len) {

  if (incomingData == nullptr || len <= 0) {
    return;
  }

  //========================================
  // Telemetry
  //========================================

  if (len == sizeof(TelemetryPacket)) {
    TelemetryPacket telemetry;

    memcpy(
      &telemetry,
      incomingData,
      sizeof(TelemetryPacket));

    Serial.println();
    Serial.println("==========");
    Serial.println("Telemetry Packet Received");

    Serial.print("Sequence : ");
    Serial.println(telemetry.sequence_number);

    Serial.print("Latitude : ");
    Serial.println(telemetry.gps_lat, 8);

    Serial.print("Longitude : ");
    Serial.println(telemetry.gps_lon, 8);

    Serial.print("Temperature : ");
    Serial.println(telemetry.water_temp);

    Serial.print("pH : ");
    Serial.println(telemetry.water_ph);

    Serial.print("Turbidity : ");
    Serial.println(telemetry.water_turbidity);

    Serial.print("Heading : ");
    Serial.println(telemetry.heading_angle);

    Serial.print("Battery : ");
    Serial.println(telemetry.battery_percent);

    Serial.println("==========");

    uploadSensorData(telemetry);

    return;
  }


  //========================================
  // Waypoint ACK
  //========================================

  if (len == sizeof(WaypointACKPacket)) {

    WaypointACKPacket ack;

    memcpy(
      &ack,
      incomingData,
      sizeof(WaypointACKPacket));

    Serial.println();
    Serial.println("==========");
    Serial.println("Waypoint ACK Received");

    Serial.print("Packet Index : ");
    Serial.println(
      ack.packet_index);

    Serial.print("ACK Status : ");
    Serial.println(
      ack.ack_status);

    if (ack.ack_status == ACK_OK) {
      Serial.println("Status : ACK OK");
    } else {
      Serial.println("Status : ACK NACK");
    }

    Serial.println("==========");

    return;
  }


  //========================================
  // Unknown packet
  //========================================

  Serial.print("Unknown Packet Length : ");
  Serial.println(len);
}



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
  CommandPacket outgoingCommand = {};

  //========================================
  // Convert Mission Command
  //========================================

  if (missionCommand == "START") {

    outgoingCommand.sys_command = CMD_START_ARM;

  } else if (missionCommand == "STOP") {

    outgoingCommand.sys_command = CMD_NORMAL;

  } else if (missionCommand == "EMERGENCY_STOP") {

    outgoingCommand.sys_command = CMD_EMERGENCY_STOP;

  } else if (missionCommand == "RTH") {

    outgoingCommand.sys_command = CMD_FORCE_RTH;

  } else if (missionCommand == "CLEAR_ESTOP") {

    outgoingCommand.sys_command = CMD_CLEAR_ESTOP;

  } else {

    outgoingCommand.sys_command = CMD_NORMAL;
  }


  //========================================
  // Manual Control
  //========================================

  outgoingCommand.manual_steer = 0;
  outgoingCommand.manual_speed = 0;


  //========================================
  // Send Command
  //========================================

  esp_err_t result = esp_now_send(
    boatMacAddress,
    (uint8_t*)&outgoingCommand,
    sizeof(outgoingCommand));


  //========================================
  // Serial Monitor
  //========================================

  Serial.println();
  Serial.println("===== COMMAND SENT TO BOAT =====");

  Serial.print("Command : ");
  Serial.println(missionCommand);

  Serial.print("Mode : ");
  Serial.println(missionMode);

  Serial.print("Command Code : ");
  Serial.println(
    outgoingCommand.sys_command);

  Serial.print("Send Result : ");

  if (result == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }

  Serial.println("===============================");
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

  int startIndex = 0;
  int batchIndex = 0;

  // ส่งครั้งละไม่เกิน 12 waypoint
  while (startIndex < totalWaypoints) {

    WaypointArrayPacket packet = {};

    int remaining =
      totalWaypoints - startIndex;

    int count =
      (remaining > 12) ? 12 : remaining;

    packet.packet_index = batchIndex;
    packet.waypoint_count = count;

    // ใส่ waypoint ลงใน packet
    for (int i = 0; i < count; i++) {

      packet.waypoints[i].lat =
        waypointLat[startIndex + i];

      packet.waypoints[i].lon =
        waypointLng[startIndex + i];
    }

    // ส่ง packet
    esp_err_t result = esp_now_send(
      boatMacAddress,
      (uint8_t*)&packet,
      sizeof(packet));

    Serial.println();
    Serial.print("Send Batch : ");
    Serial.println(batchIndex);

    Serial.print("Waypoint : ");
    Serial.print(startIndex);
    Serial.print(" - ");
    Serial.println(
      startIndex + count - 1);

    Serial.print("Waypoint Count : ");
    Serial.println(count);

    Serial.print("Send Result : ");

    if (result == ESP_OK) {
      Serial.println("OK");
    } else {
      Serial.println("FAILED");
    }

    startIndex += count;
    batchIndex++;

    delay(100);
  }

  Serial.println();
  Serial.println("Route Send Complete");

  // บอก Boat ว่า Upload Route เสร็จแล้ว
  sendMissionESPNow();
}

//==================================================
// Upload Sensor To Backend
//==================================================

void uploadSensorData(const TelemetryPacket& telemetry) {
  HTTPClient http;

  http.begin(sensorAPI);

  http.addHeader(
    "Content-Type",
    "application/json");

  DynamicJsonDocument doc(512);

  //========================================
  // Boat ID
  //========================================

  doc["boat_id"] = "Boat01";


  //========================================
  // Sensor Data
  //========================================

  doc["temp_c"] =
    telemetry.water_temp;

  doc["ph_level"] =
    telemetry.water_ph;

  doc["turbidity_ntu"] =
    telemetry.water_turbidity;

  doc["latitude"] =
    telemetry.gps_lat;

  doc["longitude"] =
    telemetry.gps_lon;

  doc["heading"] =
    telemetry.heading_angle;


  //========================================
  // Flow
  //========================================

  doc["flow_v_lat"] = 0.0;
  doc["flow_v_lng"] = 0.0;


  //========================================
  // Convert JSON
  //========================================

  String json;

  serializeJson(
    doc,
    json);


  //========================================
  // Serial Monitor
  //========================================

  Serial.println();
  Serial.println(
    "===== SENDING TO BACKEND =====");

  Serial.print("URL: ");
  Serial.println(sensorAPI);

  Serial.print("JSON: ");
  Serial.println(json);


  //========================================
  // POST
  //========================================

  int httpCode =
    http.POST(json);

  Serial.print("HTTP CODE: ");
  Serial.println(httpCode);


  if (httpCode == 200) {
    Serial.println(
      "Upload Success");
  } else {
    Serial.print(
      "Upload Failed : ");

    Serial.println(
      httpCode);
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

void testSensorUpload()
{
  //========================================
  // Create Test Telemetry
  //========================================

  TelemetryPacket testTelemetry = {};

  testTelemetry.sequence_number = 117;

  testTelemetry.gps_lat =
    13.75370309;

  testTelemetry.gps_lon =
    100.48686313;

  testTelemetry.water_temp =
    31.2;

  testTelemetry.water_ph =
    7.3;

  testTelemetry.water_turbidity =
    20.0;

  testTelemetry.current_mode =
    0;

  testTelemetry.battery_percent =
    100;

  testTelemetry.heading_angle =
    90.0;


  //========================================
  // Serial Test
  //========================================

  Serial.println();
  Serial.println(
    "===== TEST TELEMETRY UPLOAD ====="
  );

  Serial.print("Sequence : ");
  Serial.println(
    testTelemetry.sequence_number
  );

  Serial.print("Latitude : ");
  Serial.println(
    testTelemetry.gps_lat,
    8
  );

  Serial.print("Longitude : ");
  Serial.println(
    testTelemetry.gps_lon,
    8
  );

  Serial.print("Temperature : ");
  Serial.println(
    testTelemetry.water_temp
  );

  Serial.print("pH : ");
  Serial.println(
    testTelemetry.water_ph
  );

  Serial.print("Turbidity : ");
  Serial.println(
    testTelemetry.water_turbidity
  );

  Serial.print("Heading : ");
  Serial.println(
    testTelemetry.heading_angle
  );


  //========================================
  // Upload
  //========================================

  uploadSensorData(
    testTelemetry
  );
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