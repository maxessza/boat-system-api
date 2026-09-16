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
const char* ssid = "iPhone (129)";
const char* password = "1122334455";

// Backend
const char* serverIP = "172.20.10.4";
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

uint8_t boatMacAddress[6] = {
  0xE0,
  0x72,
  0xA1,
  0xD2,
  0xCE,
  0x18
};

//==================================================
// ESP-NOW SECURITY
// Must match Boat
//==================================================

static const uint8_t PMK_KEY[16] = {
  'U', 'S', 'V', '_', 'P', 'M', 'K', '_',
  '1', '6', 'b', 'y', 't', 'e', 's', '!'
};

static const uint8_t LMK_KEY[16] = {
  'U', 'S', 'V', '_', 'L', 'M', 'K', '_',
  '1', '6', 'b', 'y', 't', 'e', 's', '!'
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

  GET_MISSION,

  SEND_ESTOP,

  WAIT_MODE_ESTOP,

  SEND_CLEAR_ESTOP,

  WAIT_MODE_STANDBY,

  WAIT_MODE_RTH,

  GET_ROUTE,

  SEND_ROUTE_BATCH,

  WAIT_ROUTE_BATCH_ACK,

  SEND_UPLOAD_DONE,

  WAIT_UPLOAD_DONE_SEND,

  SEND_START_ARM,

  WAIT_SENSOR

};

BaseState baseState = GET_MISSION;

//==================================================
// Timer
//==================================================

unsigned long lastMissionCheck = 0;

const unsigned long missionInterval = 3000;

unsigned long stateEnterTime = 0;

bool routeDownloaded = false;

bool estopCommandSent = false;

bool clearEstopCommandSent = false;

bool rthCommandSent = false;

bool setHomeCommandSent = false;

bool batchAckReceived = false;

uint8_t lastAckBatchIndex = 0;

uint8_t lastAckStatus = ACK_NACK;

int currentBatchIndex = 0;

int totalBatches = 0;

int lastBatchWaypointCount = 0;

bool lastMissionRunning = false;

bool espNowSendFinished = false;

bool espNowSendSuccess = false;

//==================================================
// Boat telemetry (from ESP-NOW)
//==================================================

uint8_t boatCurrentMode = 0;

uint8_t boatStageIntent = 0;

//==================================================
// TELEMETRY SEQUENCE CHECK
//==================================================

uint32_t lastTelemetrySequence = 0;
bool hasTelemetrySequence = false;

unsigned long lastTelemetryUpload = 0;

const unsigned long telemetryUploadInterval = 5000;

//==================================================
// TELEMETRY QUEUE
//==================================================

QueueHandle_t telemetryQueue;

const int TELEMETRY_QUEUE_SIZE = 10;


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

  Serial.print("[ESP-NOW RX] len = ");
  Serial.println(len);

  //========================================
  // Telemetry
  //========================================
  if (len == sizeof(TelemetryPacket)) {

    TelemetryPacket telemetry;

    memcpy(
      &telemetry,
      incomingData,
      sizeof(TelemetryPacket));


    //========================================
    // PUT TELEMETRY INTO QUEUE
    //========================================

    if (
      telemetryQueue != NULL && xQueueSend(telemetryQueue, &telemetry, 0) != pdTRUE) {

      Serial.println(
        "[QUEUE] Telemetry Queue FULL");
    }

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

    lastAckBatchIndex = ack.packet_index;
    lastAckStatus = ack.ack_status;
    batchAckReceived = true;

    Serial.println("==========");

    return;
  }

  //========================================
  // Stage Status
  //========================================

  if (len == sizeof(StageStatusPacket)) {

    StageStatusPacket stage;

    memcpy(
      &stage,
      incomingData,
      sizeof(StageStatusPacket));

    boatStageIntent = stage.intent;

    Serial.println();
    Serial.println("==========");
    Serial.println("Stage Status Received");

    Serial.print("Stage : ");
    Serial.println(stage.current_stage);

    Serial.print("Intent : ");
    Serial.println(stage.intent);

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

    DeserializationError error =
      deserializeJson(doc, json);

    if (error) {

      Serial.print(
        "Mission JSON Error : ");

      Serial.println(
        error.c_str());

      http.end();

      return;
    }

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

    } else if (
      missionCommand == "STOP" || missionCommand == "EMERGENCY_STOP") {

      Serial.println("E-Stop command");

    } else if (missionCommand == "CLEAR_ESTOP") {

      Serial.println("Clear E-Stop requested");
    } else if (missionCommand == "RTH") {

      Serial.println("Return Home requested");
    }
  } else {
    Serial.print("Mission API Error : ");

    Serial.println(httpCode);
  }

  http.end();
}

//==================================================
// Send Command Packet (5 bytes)
//==================================================

void sendCommandPacket(uint8_t cmd) {

  CommandPacket outgoingCommand = {};

  outgoingCommand.sys_command = cmd;
  outgoingCommand.manual_steer = 0;
  outgoingCommand.manual_speed = 0;

  espNowSendFinished = false;
  espNowSendSuccess = false;

  Serial.println();
  Serial.println("===== COMMAND PACKET =====");

  Serial.print("Command Code : ");
  Serial.println(cmd);

  Serial.print("Packet Size : ");
  Serial.println(sizeof(outgoingCommand));

  esp_err_t result = esp_now_send(
    boatMacAddress,
    (uint8_t*)&outgoingCommand,
    sizeof(outgoingCommand));

  Serial.print("Send Result : ");

  if (result == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }

  Serial.println("==========================");
}

//==================================================
// Send Mission To Boat (legacy wrapper)
//==================================================

void sendMissionESPNow() {

  if (missionCommand == "START") {

    sendCommandPacket(CMD_START_ARM);

  } else if (
    missionCommand == "STOP" || missionCommand == "EMERGENCY_STOP") {

    sendCommandPacket(CMD_EMERGENCY_STOP);

  } else if (missionCommand == "CLEAR_ESTOP") {

    sendCommandPacket(CMD_CLEAR_ESTOP);

  } else if (missionCommand == "RTH") {

    sendCommandPacket(CMD_FORCE_RTH);

  } else {

    sendCommandPacket(CMD_NORMAL);
  }
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

    DeserializationError error =
      deserializeJson(doc, json);

    if (error) {

      Serial.print(
        "Route JSON Error : ");

      Serial.println(
        error.c_str());

      http.end();

      return;
    }

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
// Send Route Batch To Boat (one batch at a time)
//==================================================

void sendRouteBatch(int batchIndex) {

  int startIndex = batchIndex * 12;

  int remaining = totalWaypoints - startIndex;

  int count = (remaining > 12) ? 12 : remaining;

  WaypointArrayPacket packet = {};

  packet.packet_index = batchIndex;
  packet.waypoint_count = count;

  for (int i = 0; i < count; i++) {

    packet.waypoints[i].lat =
      waypointLat[startIndex + i];

    packet.waypoints[i].lon =
      waypointLng[startIndex + i];
  }

  lastBatchWaypointCount = count;
  batchAckReceived = false;

  esp_err_t result = esp_now_send(
    boatMacAddress,
    (uint8_t*)&packet,
    sizeof(packet));

  Serial.println();
  Serial.println("===== ROUTE BATCH =====");

  Serial.print("Batch Index : ");
  Serial.println(batchIndex);

  Serial.print("Waypoint : ");
  Serial.print(startIndex);
  Serial.print(" - ");
  Serial.println(startIndex + count - 1);

  Serial.print("Count : ");
  Serial.println(count);

  Serial.print("Send Result : ");

  if (result == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }

  Serial.println("=======================");
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

  if (isnan(telemetry.water_turbidity)) {
    doc["turbidity_ntu"] = 0.0;
  } else {
    doc["turbidity_ntu"] =
      telemetry.water_turbidity;
  }

  doc["latitude"] =
    telemetry.gps_lat;

  doc["longitude"] =
    telemetry.gps_lon;

  doc["heading"] =
    telemetry.heading_angle;

  doc["current_mode"] =
    telemetry.current_mode;

  doc["battery"] =
    telemetry.battery_percent;

  doc["stage_intent"] =
    boatStageIntent;


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

  String response = http.getString();

  Serial.print("BACKEND RESPONSE: ");
  Serial.println(response);


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
// Protocol: 1=E-Stop → wait mode=4 → 4=Clear → wait mode=0 → upload+ACK → 5 if full → 3=Start
//==================================================

void resetRouteUploadState() {

  routeDownloaded = false;
  currentBatchIndex = 0;
  totalBatches = 0;
  lastBatchWaypointCount = 0;
  batchAckReceived = false;
}

void runBaseStation() {

  switch (baseState) {

    //==========================================
    // GET MISSION
    //==========================================
    case GET_MISSION:

      getMissionCommand();

      if (missionCommand != "SET_HOME") {
        setHomeCommandSent = false;
      }

      //==========================================
      // E-STOP
      //==========================================
      if (
        missionCommand == "EMERGENCY_STOP" || missionCommand == "STOP") {

        if (!estopCommandSent) {

          Serial.println();
          Serial.println("!!! SENDING E-STOP (1) !!!");

          sendCommandPacket(
            CMD_EMERGENCY_STOP);

          estopCommandSent = true;
          clearEstopCommandSent = false;
          rthCommandSent = false;

          lastMissionRunning = false;

          resetRouteUploadState();

          stateEnterTime = millis();

          baseState =
            WAIT_MODE_ESTOP;

        } else {

          baseState =
            WAIT_SENSOR;
        }
      }

      //==========================================
      // CLEAR E-STOP
      //==========================================
      else if (
        missionCommand == "CLEAR_ESTOP") {

        if (!clearEstopCommandSent) {

          Serial.println();
          Serial.println(
            ">>> SENDING CLEAR E-STOP (4) <<<");

          sendCommandPacket(
            CMD_CLEAR_ESTOP);

          clearEstopCommandSent = true;

          stateEnterTime = millis();

          baseState =
            WAIT_MODE_STANDBY;

        } else {

          baseState =
            WAIT_SENSOR;
        }
      }

      //==========================================
      // RTH
      //==========================================
      else if (
        missionCommand == "RTH") {

        // Boat อยู่ RTH อยู่แล้ว
        if (boatCurrentMode == 3) {

          Serial.println(
            "Boat already in RTH (mode=3)");

          rthCommandSent = true;

          baseState =
            WAIT_SENSOR;
        }

        // ยังไม่ได้ส่ง RTH
        else if (!rthCommandSent) {

          Serial.println();
          Serial.println(
            ">>> RTH REQUEST RECEIVED <<<");

          sendCommandPacket(
            CMD_FORCE_RTH);

          Serial.println(
            "RTH Command Code : 2");

          rthCommandSent = true;

          stateEnterTime =
            millis();

          baseState =
            WAIT_MODE_RTH;

        }

        // ส่งไปแล้ว รอ mode=3
        else {

          baseState =
            WAIT_MODE_RTH;
        }
      }

      //==========================================
      // SET HOME
      // Command Code : 6
      //==========================================
      else if (
        missionCommand == "SET_HOME") {

        if (!setHomeCommandSent) {

          Serial.println();
          Serial.println(">>> SET HOME REQUEST RECEIVED <<<");

          sendCommandPacket(
            CMD_SET_HOME);

          Serial.println(
            "SET HOME Command Code : 6");

          setHomeCommandSent = true;
        }

        baseState =
          WAIT_SENSOR;
      }

      //==========================================
      // FORCE_SPIRAL
      // Command Code : 7
      //==========================================

      else if (
        missionCommand == "FORCE_SPIRAL") {

        Serial.println();
        Serial.println(">>> FORCE SPIRAL REQUEST RECEIVED <<<");

        sendCommandPacket(
          CMD_FORCE_SPIRAL);

        Serial.println(
          "FORCE SPIRAL Command Code : 7");

        baseState =
          WAIT_SENSOR;
      }

      //==========================================
      // START
      //==========================================
      else if (
        missionCommand == "START") {

        // ห้าม Start ตอน E-Stop
        if (boatCurrentMode == 4) {

          Serial.println(
            "BLOCKED: boat mode=4 (E-Stop).");

          Serial.println(
            "Send CLEAR_ESTOP(4) first.");

          baseState =
            WAIT_SENSOR;

          break;
        }

        // ต้องอยู่ STANDBY
        if (boatCurrentMode != 0) {

          Serial.println(
            "BLOCKED: Boat not STANDBY (mode=0).");

          Serial.println(
            "Waiting...");

          baseState =
            WAIT_SENSOR;

          break;
        }

        if (!lastMissionRunning) {

          lastMissionRunning = true;

          rthCommandSent = false;

          resetRouteUploadState();

          baseState =
            GET_ROUTE;

        } else {

          baseState =
            WAIT_SENSOR;
        }
      }

      //==========================================
      // UNKNOWN COMMAND
      //==========================================
      else {

        baseState =
          WAIT_SENSOR;
      }

      break;


    //==========================================
    // WAIT E-STOP CONFIRM
    // mode = 4
    //==========================================
    case WAIT_MODE_ESTOP:

      if (boatCurrentMode == 4) {

        Serial.println(
          "Boat confirmed E-Stop (mode=4)");

        baseState =
          WAIT_SENSOR;
      }

      break;


    //==========================================
    // WAIT STANDBY AFTER CLEAR
    // mode = 0
    //==========================================
    case WAIT_MODE_STANDBY:

      if (boatCurrentMode == 0) {

        Serial.println(
          "Boat confirmed STANDBY (mode=0)");

        estopCommandSent = false;

        clearEstopCommandSent = false;

        baseState =
          WAIT_SENSOR;
      }

      break;


      //==========================================
      // WAIT RTH
      // mode = 3
      //==========================================
    case WAIT_MODE_RTH:

      if (boatCurrentMode == 3) {

        Serial.println(
          "Boat confirmed RTH (mode=3)");

        baseState = WAIT_SENSOR;
      }

      else if (boatCurrentMode == 0) {

        Serial.println(
          "Boat completed RTH and returned to STANDBY (mode=0)");

        // RTH mission จบแล้ว
        rthCommandSent = false;

        lastMissionRunning = false;

        routeDownloaded = false;

        baseState = WAIT_SENSOR;
      }

      break;


    //==========================================
    // GET ROUTE
    //==========================================
    case GET_ROUTE:

      if (boatCurrentMode != 0) {

        Serial.println(
          "Abort: Boat is not STANDBY (mode != 0)");

        lastMissionRunning = false;

        baseState =
          WAIT_SENSOR;

        break;
      }

      if (!routeDownloaded) {

        downloadRoute();

        if (totalWaypoints > 0) {

          routeDownloaded = true;

          totalBatches =
            (totalWaypoints + 11) / 12;

          currentBatchIndex = 0;

          Serial.print(
            "Route ready: ");

          Serial.print(
            totalWaypoints);

          Serial.print(
            " waypoints, ");

          Serial.print(
            totalBatches);

          Serial.println(
            " batches");

          baseState =
            SEND_ROUTE_BATCH;

        } else {

          Serial.println(
            "No Route From Backend");

          lastMissionRunning = false;

          baseState =
            WAIT_SENSOR;
        }

      } else {

        baseState =
          SEND_ROUTE_BATCH;
      }

      break;


    //==========================================
    // SEND ROUTE BATCH
    //==========================================
    case SEND_ROUTE_BATCH:

      if (boatCurrentMode != 0) {

        Serial.println(
          "Abort route upload: Boat is not STANDBY");

        lastMissionRunning = false;

        baseState =
          WAIT_SENSOR;

        break;
      }

      sendRouteBatch(
        currentBatchIndex);

      baseState =
        WAIT_ROUTE_BATCH_ACK;

      break;


    //==========================================
    // WAIT ROUTE ACK
    //==========================================
    case WAIT_ROUTE_BATCH_ACK:

      if (
        batchAckReceived && lastAckBatchIndex == currentBatchIndex) {

        batchAckReceived = false;

        if (
          lastAckStatus != ACK_OK) {

          Serial.print(
            "Batch ");

          Serial.print(
            currentBatchIndex);

          Serial.println(
            " NACK — resending");

          baseState =
            SEND_ROUTE_BATCH;

          break;
        }

        Serial.print(
          "Batch ");

        Serial.print(
          currentBatchIndex);

        Serial.println(
          " ACK OK");

        currentBatchIndex++;

        if (
          currentBatchIndex < totalBatches) {

          baseState =
            SEND_ROUTE_BATCH;

        } else {

          Serial.println(
            "All batches ACK'd");

          if (
            lastBatchWaypointCount == 12) {

            baseState =
              SEND_UPLOAD_DONE;

          } else {

            baseState =
              SEND_START_ARM;
          }
        }
      }

      break;


    //==========================================
    // SEND UPLOAD DONE
    //==========================================
    case SEND_UPLOAD_DONE:

      Serial.println(
        "Sending MISSION_UPLOAD_DONE (5)");

      sendCommandPacket(
        CMD_MISSION_UPLOAD_DONE);

      stateEnterTime =
        millis();

      baseState =
        WAIT_UPLOAD_DONE_SEND;

      break;


    //==========================================
    // WAIT UPLOAD DONE
    //==========================================
    case WAIT_UPLOAD_DONE_SEND:

      if (espNowSendFinished) {

        if (espNowSendSuccess) {

          Serial.println(
            "UPLOAD_DONE (5) Send Success");

          baseState =
            SEND_START_ARM;

        } else {

          Serial.println(
            "UPLOAD_DONE (5) Send Failed");

          baseState =
            SEND_UPLOAD_DONE;
        }
      }

      break;


    //==========================================
    // SEND START ARM
    //==========================================
    case SEND_START_ARM:

      Serial.println();
      Serial.println(
        ">>> SENDING START_ARM (3) <<<");

      sendCommandPacket(
        CMD_START_ARM);

      lastMissionRunning = true;

      baseState =
        WAIT_SENSOR;

      break;


    //==========================================
    // WAIT SENSOR / MISSION POLLING
    //==========================================
    case WAIT_SENSOR:

      if (
        millis() - lastMissionCheck >= missionInterval) {

        lastMissionCheck =
          millis();

        baseState =
          GET_MISSION;
      }

      break;
  }
}

//==================================================
// TEST SENSOR UPLOAD
//==================================================

// void testSensorUpload() {
//   //========================================
//   // Create Test Telemetry
//   //========================================

//   TelemetryPacket testTelemetry = {};

//   testTelemetry.sequence_number = 117;

//   testTelemetry.gps_lat =
//     13.75370309;

//   testTelemetry.gps_lon =
//     100.48686313;

//   testTelemetry.water_temp =
//     31.2;

//   testTelemetry.water_ph =
//     7.3;

//   testTelemetry.water_turbidity =
//     20.0;

//   testTelemetry.current_mode =
//     0;

//   testTelemetry.battery_percent =
//     100;

//   testTelemetry.heading_angle =
//     90.0;


//   //========================================
//   // Serial Test
//   //========================================

//   Serial.println();
//   Serial.println(
//     "===== TEST TELEMETRY UPLOAD =====");

//   Serial.print("Sequence : ");
//   Serial.println(
//     testTelemetry.sequence_number);


//   Serial.print("Latitude : ");
//   Serial.println(
//     testTelemetry.gps_lat,
//     8);

//   Serial.print("Longitude : ");
//   Serial.println(
//     testTelemetry.gps_lon,
//     8);

//   Serial.print("Temperature : ");
//   Serial.println(
//     testTelemetry.water_temp);

//   Serial.print("pH : ");
//   Serial.println(
//     testTelemetry.water_ph);

//   Serial.print("Turbidity : ");
//   Serial.println(
//     testTelemetry.water_turbidity);

//   Serial.print("Heading : ");
//   Serial.println(
//     testTelemetry.heading_angle);


//   //========================================
//   // Upload
//   //========================================

//   uploadSensorData(
//     testTelemetry);
// }

//==================================================
// Setup
//==================================================

void setup() {

  Serial.begin(115200);

  //==================================================
  // CREATE TELEMETRY QUEUE
  //==================================================

  telemetryQueue =
    xQueueCreate(
      TELEMETRY_QUEUE_SIZE,
      sizeof(TelemetryPacket));

  if (telemetryQueue == NULL) {

    Serial.println(
      "Telemetry Queue Create Failed");

  } else {

    Serial.println(
      "Telemetry Queue Ready");
  }

  WiFi.mode(WIFI_STA);

  WiFi.setSleep(false);

  esp_wifi_set_ps(WIFI_PS_NONE);

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
  Serial.println(
    WiFi.localIP());

  Serial.print("WiFi Channel : ");
  Serial.println(WiFi.channel());

  Serial.println("Testing connection to Backend...");

  WiFiClient testClient;

  if (testClient.connect(serverIP, serverPort)) {

    Serial.println("Backend Connection : OK");

    testClient.stop();

  } else {

    Serial.println("Backend Connection : FAILED");
  }

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
  // ESP-NOW SECURITY
  //==================================================

  if (esp_now_set_pmk(PMK_KEY) != ESP_OK) {
    Serial.println("ESP-NOW PMK Setup Failed");
    return;
  }

  Serial.println("ESP-NOW PMK Ready");

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

  uint8_t currentProtocol = 0;

  esp_err_t protocolResult =
    esp_wifi_get_protocol(
      WIFI_IF_STA,
      &currentProtocol);

  if (protocolResult == ESP_OK) {

    Serial.print(
      "Current WiFi Protocol : ");

    Serial.println(
      currentProtocol);
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


  //testSensorUpload();


  //==================================================
  // Register Callback
  //==================================================

  esp_now_register_recv_cb(onDataReceive);
  esp_now_register_send_cb(onDataSent);


  //==================================================
  // Add Boat Peer
  //==================================================

  memset(
    &peerInfo,
    0,
    sizeof(peerInfo));

  memcpy(
    peerInfo.peer_addr,
    boatMacAddress,
    6);

  // Use the same Wi-Fi channel as Base
  peerInfo.channel = WiFi.channel();

  // Enable encryption
  peerInfo.encrypt = true;

  // Must match Boat
  memcpy(
    peerInfo.lmk,
    LMK_KEY,
    16);

  // Use STA interface
  peerInfo.ifidx = WIFI_IF_STA;


  //==================================================
  // Print Peer Configuration
  //==================================================

  Serial.println();
  Serial.println("===== ESP-NOW PEER =====");

  Serial.print("Boat MAC : ");

  for (int i = 0; i < 6; i++) {

    if (boatMacAddress[i] < 16) {
      Serial.print("0");
    }

    Serial.print(
      boatMacAddress[i],
      HEX);

    if (i < 5) {
      Serial.print(":");
    }
  }

  Serial.println();

  Serial.print("Channel : ");
  Serial.println(
    peerInfo.channel);

  Serial.print("Encryption : ");

  if (peerInfo.encrypt) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }

  Serial.println("========================");


  //==================================================
  // Add Peer
  //==================================================

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {

    Serial.println("Add Peer Failed");
    return;
  }

  Serial.println("Boat Peer Added");
}
void processTelemetryQueue() {

  if (telemetryQueue == NULL) {
    return;
  }

  TelemetryPacket telemetry;

  // รับทีละ 1 packet เท่านั้น
  if (
    xQueueReceive(
      telemetryQueue,
      &telemetry,
      0)
    != pdTRUE) {

    return;
  }


  //========================================
  // CHECK TELEMETRY SEQUENCE
  //========================================

  if (hasTelemetrySequence) {

    if (
      telemetry.sequence_number > lastTelemetrySequence + 1) {

      uint32_t lost =
        telemetry.sequence_number - lastTelemetrySequence - 1;

      Serial.print("[SEQ] LOST = ");
      Serial.print(lost);

      Serial.print(" packet(s), last=");
      Serial.print(lastTelemetrySequence);

      Serial.print(" current=");
      Serial.println(
        telemetry.sequence_number);
    }
  }

  lastTelemetrySequence =
    telemetry.sequence_number;

  hasTelemetrySequence = true;


  //========================================
  // UPDATE BOAT MODE
  //========================================

  boatCurrentMode =
    telemetry.current_mode;


  //========================================
  // SHOW TELEMETRY
  //========================================

  Serial.println();
  Serial.println("==========");
  Serial.println("Telemetry Packet Processed");

  Serial.print("Sequence : ");
  Serial.println(
    telemetry.sequence_number);

  Serial.print("Current Mode : ");
  Serial.println(
    telemetry.current_mode);

  Serial.print("Latitude : ");
  Serial.println(
    telemetry.gps_lat,
    8);

  Serial.print("Longitude : ");
  Serial.println(
    telemetry.gps_lon,
    8);

  Serial.print("Temperature : ");
  Serial.println(
    telemetry.water_temp);

  Serial.print("pH : ");
  Serial.println(
    telemetry.water_ph);

  Serial.print("Turbidity : ");
  Serial.println(
    telemetry.water_turbidity);


  Serial.print("Battery : ");
  Serial.print(
    telemetry.battery_percent);
  Serial.println("%");

  Serial.println("==========");


  //========================================
  // UPLOAD ONLY EVERY 5 SECONDS
  //========================================

  if (
    millis() - lastTelemetryUpload >= telemetryUploadInterval) {

    lastTelemetryUpload = millis();

    uploadSensorData(
      telemetry);
  }
}
//==================================================
// Loop
//==================================================

void loop() {

  // Process received ESP-NOW telemetry
  processTelemetryQueue();

  // Run Base Station
  runBaseStation();

  delay(10);
}