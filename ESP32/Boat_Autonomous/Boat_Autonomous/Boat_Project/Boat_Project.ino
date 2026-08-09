//==================================================
// Smart Water Surface Boat
// Autonomous Navigation System
//==================================================


//==================================================
// Library
//==================================================

#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <TinyGPS++.h>
#include <HardwareSerial.h>

#include <Wire.h>
#include <QMC5883LCompass.h>

#include <math.h>


//==================================================
// Configuration
//==================================================

// Backend Server
const char *serverIP = "192.168.1.4";
const int serverPort = 8000;

// API
String routeAPI =
  "http://" + String(serverIP) + ":" + String(serverPort) + "/route";

String sensorAPI =
  "http://" + String(serverIP) + ":" + String(serverPort) + "/data";

String missionAPI =
  "http://" + String(serverIP) + ":" + String(serverPort) + "/mission";


//==================================================
// ESP-NOW Configuration
//==================================================

// เปลี่ยนเป็น MAC ของ Base ESP32 ภายหลัง
uint8_t baseMacAddress[] = {
  0x24,
  0x6F,
  0x28,
  0x00,
  0x00,
  0x00
};

//==================================================
// Hardware Objects
//==================================================

TinyGPSPlus gps;

HardwareSerial GPSserial(1);

QMC5883LCompass compass;


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
};

//--------------------------------------------------
// Route Point
//--------------------------------------------------

struct RoutePoint {
  float latitude;
  float longitude;
};

//--------------------------------------------------
// Route Packet
//--------------------------------------------------

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

MissionPacket incomingMission;

RoutePacket incomingRoute;

SensorPacket outgoingSensor;

AckPacket outgoingAck;

esp_now_peer_info_t peerInfo;

//==================================================
// ESP-NOW Communication
//==================================================

// จะใช้เก็บคำสั่งที่ Base ส่งมา
bool newCommandReceived = false;

// Route ที่ Base ส่งมา
bool newRouteReceived = false;

bool routeReceiving = false;

bool routeReady = false;

int receivedRouteCount = 0;

bool routeDownloadFinished = false;

// Sensor ที่ต้องส่งกลับ
bool sendSensorFlag = false;



//==================================================
// Waypoint Data
//==================================================

float waypointLat[100];

float waypointLng[100];

int totalWaypoints = 0;

int currentWaypoint = 0;

//==================================================
// Boat Information
//==================================================

String boatID = "Boat01";

String missionID = "";

bool missionStarted = false;

String missionCommand = "";

String missionModeString = "";

bool lastMissionState = false;

//==================================================
// Sweep Pattern
//==================================================

const int MAX_SWEEP_POINTS = 100;

float sweepLat[MAX_SWEEP_POINTS];

float sweepLng[MAX_SWEEP_POINTS];

int sweepTotal = 0;

int sweepIndex = 0;

bool sweepFinished = false;


//==================================================
// Current Boat Position
//==================================================

float currentLat = 19.912500;

float currentLng = 99.842500;

float currentHeading = 0;


//==================================================
// Sensor Data
//==================================================

float waterTemperature = 0.0;

float waterPH = 0.0;

float waterTurbidity = 0.0;


//==================================================
// Upload Timer
//==================================================

unsigned long lastUploadTime = 0;

const unsigned long uploadInterval = 2000;

//==================================================
// Connection Monitor
//==================================================

// ตรวจสอบ Backend ทุก ๆ 5 วินาที
unsigned long lastBackendCheck = 0;

const unsigned long backendCheckInterval = 5000;

bool backendConnected = false;

bool missionReceived = false;

//==================================================
// Drift Data
//==================================================

float driftStartLat = 0;

float driftStartLng = 0;

float driftEndLat = 0;

float driftEndLng = 0;

bool driftCompleted = false;

bool navigationStarted = false;

float flowVectorLat = 0;

float flowVectorLng = 0;

float flowDirection = 0;

float compensatedBearing = 0;


//==================================================
// PID Parameter
//==================================================

float kp = 2.0;

//==================================================
// Mission Mode
//==================================================

enum MissionMode {
  MANUAL_MODE,

  SWEEP_MODE
};

MissionMode missionMode = MANUAL_MODE;

//==================================================
// Mission State
//==================================================

enum MissionState {
  IDLE,

  DOWNLOAD_ROUTE,

  START_DRIFT,

  NAVIGATE,

  WAYPOINT_REACHED,

  MISSION_COMPLETE
};

MissionState missionState = IDLE;


//==================================================
// ESP-NOW Receive Callback
//==================================================
void onDataReceive(
  const esp_now_recv_info_t *info,
  const uint8_t *incomingData,
  int len) {

  uint8_t packetType = incomingData[0];

  //--------------------------------------------------
  // Mission Packet
  //--------------------------------------------------

  if (packetType == PACKET_MISSION) {
    memcpy(
      &incomingMission,
      incomingData,
      sizeof(MissionPacket));

    missionCommand = String(incomingMission.command);

    missionModeString = String(incomingMission.mode);

    newCommandReceived = true;

    backendConnected = true;

    Serial.println();
    Serial.println("==========");
    Serial.println("Mission Packet");

    Serial.print("Command : ");
    Serial.println(missionCommand);

    Serial.print("Mode : ");
    Serial.println(missionModeString);

    Serial.println("==========");

    sendAckESPNow(false, true);

  }

  //--------------------------------------------------
  // Route Packet
  //--------------------------------------------------

  else if (packetType == PACKET_ROUTE) {
    memcpy(
      &incomingRoute,
      incomingData,
      sizeof(RoutePacket));

    receiveRoutePacket();
  }
}

void onDataSent(
  const wifi_tx_info_t *info,
  esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW Send Success");
  } else {
    Serial.println("ESP-NOW Send Failed");
  }
}


//--------------------------------------------------
// Download Route from Backend API
//--------------------------------------------------

void downloadRoute()

{
  HTTPClient http;

  http.begin(routeAPI);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String json = http.getString();

    Serial.println(json);

    DynamicJsonDocument doc(4096);

    deserializeJson(doc, json);

    totalWaypoints = 0;

    for (JsonObject point : doc.as<JsonArray>()) {
      waypointLat[totalWaypoints] = point["latitude"];

      waypointLng[totalWaypoints] = point["longitude"];

      totalWaypoints++;
    }

    Serial.println("Download Route Success");

    Serial.print("Total Waypoints : ");
    Serial.println(totalWaypoints);

    for (int i = 0; i < totalWaypoints; i++) {
      Serial.print("Waypoint ");

      Serial.print(i + 1);

      Serial.print(" : ");

      Serial.print(waypointLat[i], 6);

      Serial.print(",");

      Serial.println(waypointLng[i], 6);
    }

  } else {
    Serial.println("Download Failed");
  }

  http.end();
}

//==================================================
// receiveRoutePacket
//==================================================

void receiveRoutePacket() {
  routeReceiving = true;

  waypointLat[incomingRoute.index] =
    incomingRoute.point.latitude;

  waypointLng[incomingRoute.index] =
    incomingRoute.point.longitude;

  receivedRouteCount++;

  totalWaypoints = incomingRoute.total;

  Serial.println();
  Serial.println("Route Packet");

  Serial.print("Index : ");
  Serial.println(incomingRoute.index);

  Serial.print("Latitude : ");
  Serial.println(
    incomingRoute.point.latitude,
    6);

  Serial.print("Longitude : ");
  Serial.println(
    incomingRoute.point.longitude,
    6);

  if (incomingRoute.lastPacket) {
    routeReady = true;

    newRouteReceived = true;

    routeReceiving = false;

    Serial.println();

    Serial.println("Route Download Complete");

    Serial.print("Total Waypoints : ");
    Serial.println(totalWaypoints);

    sendAckESPNow(true, false);
  }
}


//==================================================
// GPS Functions
//==================================================

void readGPS()

{

  while (GPSserial.available()) {

    gps.encode(GPSserial.read());
  }

  if (gps.location.isUpdated()) {
    currentLat = gps.location.lat();

    currentLng = gps.location.lng();

    Serial.println("GPS Updated");

    Serial.print("Latitude : ");

    Serial.println(currentLat, 6);

    Serial.print("Longitude : ");

    Serial.println(currentLng, 6);
  }
}

void readCompass() {
  compass.read();

  currentHeading = compass.getAzimuth();

  Serial.println("Compass Updated");

  Serial.print("Heading : ");
  Serial.println(currentHeading);
}

//==================================================
// Navigation Functions
//==================================================

float calculateBearing(
  float lat1,
  float lon1,
  float lat2,
  float lon2) {

  lat1 = radians(lat1);
  lon1 = radians(lon1);

  lat2 = radians(lat2);
  lon2 = radians(lon2);

  float dLon = lon2 - lon1;

  float y = sin(dLon) * cos(lat2);
  float x =
    cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon);

  float bearing =
    degrees(atan2(y, x));

  if (bearing < 0) {
    bearing = bearing + 360;
  }

  return bearing;
}

float calculateHeadingError(
  float bearing,
  float heading) {
  float error =
    bearing - heading;

  if (error > 180) {
    error -= 360;
  }

  if (error < -180) {
    error += 360;
  }
  return error;
}

void decideDirection(float headingError) {
  int motorSpeed =
    calculateMotorSpeed(headingError);

  if (headingError > 5) {
    turnRight();

    Serial.print("Speed : ");
    Serial.println(motorSpeed);
  } else if (headingError < -5) {
    turnLeft();

    Serial.print("Speed : ");
    Serial.println(motorSpeed);
  } else {
    moveForward();

    Serial.print("Speed : ");
    Serial.println(150);
  }
}

//--------------------------------------------------
// Generate Sweep Pattern
//--------------------------------------------------
void generateSweepPattern() {
  float startLat = 19.912500;

  float startLng = 99.842500;

  float width = 0.000300;

  float height = 0.000300;

  float step = 0.000050;

  bool leftToRight = true;

  sweepTotal = 0;

  for (float y = 0; y <= height; y += step) {
    if (leftToRight) {
      for (float x = 0; x <= width; x += step) {

        waypointLat[sweepTotal] = startLat + y;

        waypointLng[sweepTotal] = startLng + x;

        sweepTotal++;
      }
    } else {
      for (float x = width; x >= 0; x -= step) {

        waypointLat[sweepTotal] = startLat + y;

        waypointLng[sweepTotal] = startLng + x;

        sweepTotal++;
      }
    }

    leftToRight = !leftToRight;
  }

  Serial.println();

  Serial.println("Sweep Pattern Generated");

  Serial.print("Total Sweep Points : ");

  Serial.println(sweepTotal);

  totalWaypoints = sweepTotal;

  for (int i = 0; i < sweepTotal; i++) {
    Serial.print("Point ");

    Serial.print(i + 1);

    Serial.print(" : ");

    Serial.print(waypointLat[i], 6);

    Serial.print(",");

    Serial.println(waypointLng[i], 6);
  }
}
//==================================================
// Motor Functions
//==================================================

void moveForward()

{
  Serial.println("Boat Moving Forward");
}


void turnLeft()

{
  Serial.println("Boat Turning Left");
}

void turnRight()

{
  Serial.println("Boat Turning Right");
}

void stopBoat()

{
  Serial.println("Boat Stop");
}

int calculateMotorSpeed(
  float headingError) {
  float speed =
    abs(headingError) * kp;

  if (speed > 255) {
    speed = 255;
  }

  return speed;
}

void navigateToWaypoint()

{
  float targetLat = waypointLat[currentWaypoint];

  float targetLng = waypointLng[currentWaypoint];

  float bearing = calculateBearing(
    currentLat,
    currentLng,
    targetLat,
    targetLng);

  calculateCompensatedBearing(bearing);

  Serial.print("Bearing : ");
  Serial.println(bearing);

  Serial.println("==========");

  Serial.print("Current Target : ");
  Serial.println(currentWaypoint + 1);

  Serial.print("Current Lat : ");
  Serial.println(currentLat, 6);

  Serial.print("Current Lng : ");
  Serial.println(currentLng, 6);

  Serial.print("Target Lat : ");
  Serial.println(targetLat, 6);

  Serial.print("Target Lng : ");
  Serial.println(targetLng, 6);

  float headingError = calculateHeadingError(
    compensatedBearing,
    currentHeading);

  Serial.print("Bearing : ");
  Serial.println(bearing, 2);

  Serial.print("Heading : ");
  Serial.println(currentHeading, 2);

  Serial.print("Drift Complete : ");
  Serial.println(driftCompleted);

  Serial.print("Navigation Started : ");
  Serial.println(navigationStarted);

  Serial.print("Heading Error : ");
  Serial.println(headingError, 2);

  float distanceLat = targetLat - currentLat;
  float distanceLng = targetLng - currentLng;

  Serial.print("Delta Lat : ");
  Serial.println(distanceLat, 6);

  Serial.print("Delta Lng : ");
  Serial.println(distanceLng, 6);


  decideDirection(
    headingError);

  if (abs(distanceLat) < 0.000050 && abs(distanceLng) < 0.000050) {

    Serial.println("Waypoint Reached");

    missionState = WAYPOINT_REACHED;

    navigationStarted = false;
  }
}


//==================================================
// Simulation Functions
//==================================================

void simulateBoatMovement() {
  currentLat += 0.000010;

  currentLng += 0.000010;

  Serial.println("Boat Moving...");

  Serial.print("Current Lat : ");
  Serial.println(currentLat, 6);

  Serial.print("Current Lng : ");
  Serial.println(currentLng, 6);
}

//--------------------------------------------------
// Start Drift Mode
//--------------------------------------------------
void startDriftMode() {
  Serial.println();

  Serial.println("==========");

  Serial.println("Start Drift Mode");

  driftStartLat = currentLat;
  driftStartLng = currentLng;

  Serial.print("Start Latitude : ");
  Serial.println(driftStartLat, 6);

  Serial.print("Start Longitude : ");
  Serial.println(driftStartLng, 6);

  delay(5000);

  driftEndLat = currentLat;
  driftEndLng = currentLng;

  Serial.print("End Latitude : ");
  Serial.println(driftEndLat, 6);

  Serial.print("End Longitude : ");
  Serial.println(driftEndLng, 6);

  driftCompleted = true;

  navigationStarted = true;

  calculateFlowVector();

  missionState = NAVIGATE;

  Serial.println("Drift Complete");
}

//--------------------------------------------------
// Calculate Flow Vector
//--------------------------------------------------
void calculateFlowVector() {
  flowVectorLat =
    driftEndLat - driftStartLat;

  flowVectorLng =
    driftEndLng - driftStartLng;

  Serial.println();

  Serial.println("Flow Vector");

  Serial.print("Flow Lat : ");

  Serial.println(flowVectorLat, 6);

  Serial.print("Flow Lng : ");

  Serial.println(flowVectorLng, 6);

  calculateFlowDirection();
}

//--------------------------------------------------
// Calculate Flow Direction
//--------------------------------------------------
void calculateFlowDirection() {
  flowDirection =
    degrees(
      atan2(
        flowVectorLng,
        flowVectorLat));

  if (flowDirection < 0) {
    flowDirection += 360;
  }

  Serial.println();

  Serial.println("Flow Direction");

  Serial.print("Direction : ");
  Serial.println(flowDirection, 2);

  if (flowDirection >= 337.5 || flowDirection < 22.5) {
    Serial.println("North");
  } else if (flowDirection < 67.5) {
    Serial.println("North-East");
  } else if (flowDirection < 112.5) {
    Serial.println("East");
  } else if (flowDirection < 157.5) {
    Serial.println("South-East");
  } else if (flowDirection < 202.5) {
    Serial.println("South");
  } else if (flowDirection < 247.5) {
    Serial.println("South-West");
  } else if (flowDirection < 292.5) {
    Serial.println("West");
  } else {
    Serial.println("North-West");
  }
}

//--------------------------------------------------
// Flow Compensation
//--------------------------------------------------
void calculateCompensatedBearing(float bearing) {
  compensatedBearing =
    bearing - (flowDirection * 0.05);

  if (compensatedBearing < 0) {
    compensatedBearing += 360;
  }

  if (compensatedBearing > 360) {
    compensatedBearing -= 360;
  }

  Serial.println();

  Serial.println("Flow Compensation");

  Serial.print("Original Bearing : ");
  Serial.println(bearing, 2);

  Serial.print("Compensated Bearing : ");
  Serial.println(compensatedBearing, 2);
}
//--------------------------------------------------
// Print Mission Status
//--------------------------------------------------
void printMissionStatus() {
  Serial.println("==========");

  Serial.println("Mission Status");

  Serial.print("Current Waypoint : ");
  Serial.println(currentWaypoint + 1);

  Serial.print("Total Waypoints : ");
  Serial.println(totalWaypoints);

  Serial.print("Current Latitude : ");
  Serial.println(currentLat, 6);

  Serial.print("Current Longitude : ");
  Serial.println(currentLng, 6);

  Serial.print("Heading : ");
  Serial.println(currentHeading, 2);

  Serial.print("Drift Complete : ");
  Serial.println(driftCompleted);

  Serial.print("Navigation Started : ");
  Serial.println(navigationStarted);

  Serial.print("Flow Vector Lat : ");
  Serial.println(flowVectorLat, 6);

  Serial.print("Flow Vector Lng : ");
  Serial.println(flowVectorLng, 6);

  Serial.print("Flow Direction : ");
  Serial.println(flowDirection, 2);

  Serial.print("Compensated Bearing : ");
  Serial.println(compensatedBearing, 2);

  Serial.print("Sweep Index : ");
  Serial.println(sweepIndex);

  Serial.print("Sweep Total : ");
  Serial.println(sweepTotal);

  Serial.print("Sweep Finished : ");
  Serial.println(sweepFinished);

  Serial.print("Route Ready : ");
  Serial.println(routeReady);

  Serial.print("Receiving Route : ");
  Serial.println(routeReceiving);

  Serial.print("Received Route Count : ");
  Serial.println(receivedRouteCount);

  Serial.print("New Route Received : ");
  Serial.println(newRouteReceived);
}

//==================================================
// Backend Communication
//==================================================

void sendSensorData() {
  HTTPClient http;

  http.begin("http://192.168.1.4:8000/data");

  http.addHeader(
    "Content-Type",
    "application/json");

  DynamicJsonDocument doc(512);

  doc["boat_id"] = boatID;

  doc["temp_c"] = waterTemperature;

  doc["ph_level"] = waterPH;

  doc["turbidity_ntu"] = waterTurbidity;

  doc["latitude"] = currentLat;

  doc["longitude"] = currentLng;

  String json;

  serializeJson(doc, json);

  int code = http.POST(json);

  if (code == 200) {
    Serial.println("Upload Success");
  } else {
    Serial.println("Upload Failed");
  }

  http.end();
}

void getMissionCommand() {
  HTTPClient http;

  http.begin(missionAPI);

  int httpCode = http.GET();

  if (httpCode == 200) {
    backendConnected = true;

    String json = http.getString();

    DynamicJsonDocument doc(512);

    deserializeJson(doc, json);

    missionCommand = doc["command"].as<String>();

    missionModeString = doc["mode"].as<String>();

    Serial.println();
    Serial.println("Mission Command");

    Serial.print("Command : ");
    Serial.println(missionCommand);

    Serial.print("Mode : ");
    Serial.println(missionModeString);
  } else {
    backendConnected = false;

    Serial.print("Mission API Error : ");
    Serial.println(httpCode);
  }

  http.end();
}

//--------------------------------------------------
// Send ACK to Base
//--------------------------------------------------

void sendAckESPNow(bool routeAck, bool missionAck) {
  outgoingAck.type = PACKET_ACK;

  outgoingAck.routeReceived = routeAck;
  outgoingAck.missionReceived = missionAck;

  esp_now_send(
    baseMacAddress,
    (uint8_t *)&outgoingAck,
    sizeof(outgoingAck));

  Serial.println();
  Serial.println("==========");
  Serial.println("ACK Sent");
  Serial.print("Route ACK : ");
  Serial.println(routeAck);
  Serial.print("Mission ACK : ");
  Serial.println(missionAck);
  Serial.println("==========");
}


void sendSensorESPNow() {

  outgoingSensor.type = PACKET_SENSOR;

  strcpy(outgoingSensor.boatID, boatID.c_str());

  outgoingSensor.temperature = waterTemperature;
  outgoingSensor.ph = waterPH;
  outgoingSensor.turbidity = waterTurbidity;

  outgoingSensor.latitude = currentLat;
  outgoingSensor.longitude = currentLng;

  esp_now_send(
    baseMacAddress,
    (uint8_t *)&outgoingSensor,
    sizeof(outgoingSensor));
}


//==================================================
// Mission Functions
//==================================================

//--------------------------------------------------
// Start Mission
//--------------------------------------------------

void startMission() {

  Serial.println();
  Serial.println("==========");
  Serial.println("Start Mission");
  Serial.println("==========");

  currentWaypoint = 0;

  routeReady = false;

  routeReceiving = false;

  receivedRouteCount = 0;

  totalWaypoints = 0;

  driftCompleted = false;

  navigationStarted = false;

  sweepFinished = false;

  sweepIndex = 0;

  if (missionMode == MANUAL_MODE) {
    missionState = DOWNLOAD_ROUTE;
  } else {
    generateSweepPattern();

    missionState = START_DRIFT;
  }

  missionStarted = true;

  Serial.println("Mission Initialized");
}


//--------------------------------------------------
// Stop Mission
//--------------------------------------------------

void stopMission() {
  Serial.println();
  Serial.println("==========");
  Serial.println("Stop Mission");
  Serial.println("==========");

  stopBoat();

  missionStarted = false;

  navigationStarted = false;

  driftCompleted = false;

  currentWaypoint = 0;

  routeReady = false;

  routeReceiving = false;

  receivedRouteCount = 0;

  totalWaypoints = 0;

  newRouteReceived = false;

  sweepFinished = false;

  sweepFinished = false;

  sweepIndex = 0;

  missionState = IDLE;

  Serial.println("Mission Stopped");
}

//--------------------------------------------------
// Run Mission
//--------------------------------------------------

void runMission() {
  Serial.println();
  Serial.println("========================");
  Serial.println("Mission Running");
  Serial.println("========================");

  printMissionStatus();

  switch (missionState) {

      //--------------------------------------------------
      // IDLE
      //--------------------------------------------------

    case IDLE:

      Serial.println("Mission : IDLE");

      break;

      //--------------------------------------------------
      // DOWNLOAD ROUTE
      //--------------------------------------------------

    case DOWNLOAD_ROUTE:

      Serial.println("Waiting Route From Base...");

      if (routeReady) {
        Serial.println("Route Ready");

        currentWaypoint = 0;

        missionState = START_DRIFT;
      }

      break;

      //--------------------------------------------------
      // START DRIFT
      //--------------------------------------------------

    case START_DRIFT:

      Serial.println("Starting Drift");

      startDriftMode();

      break;

      //--------------------------------------------------
      // NAVIGATION
      //--------------------------------------------------

    case NAVIGATE:

      Serial.println("Navigation");

      if (navigationStarted && !sweepFinished) {
        simulateBoatMovement();

        navigateToWaypoint();
      }

      break;

      //--------------------------------------------------
      // WAYPOINT REACHED
      //--------------------------------------------------

    case WAYPOINT_REACHED:

      Serial.println("Waypoint Reached");

      currentWaypoint++;

      if (currentWaypoint >= totalWaypoints) {
        missionState = MISSION_COMPLETE;
      } else {
        driftCompleted = false;

        navigationStarted = false;

        missionState = START_DRIFT;
      }

      break;

      //--------------------------------------------------
      // MISSION COMPLETE
      //--------------------------------------------------

    case MISSION_COMPLETE:

      stopBoat();

      missionStarted = false;

      navigationStarted = false;

      driftCompleted = false;

      currentWaypoint = 0;

      Serial.println("Mission Complete");

      routeReady = false;

      routeReceiving = false;

      receivedRouteCount = 0;

      totalWaypoints = 0;

      newRouteReceived = false;

      missionState = IDLE;

      Serial.println("Mission Complete");

      break;
  }
}



//==================================================
// Setup
//==================================================

void setup()

{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");

    return;
  }

  Serial.println("ESP-NOW Ready");

  esp_now_register_recv_cb(onDataReceive);

  esp_now_register_send_cb(onDataSent);

  memcpy(
    peerInfo.peer_addr,
    baseMacAddress,
    6);

  peerInfo.channel = 0;

  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Add Peer Failed");
    return;
  }

  Serial.println("Peer Added");

  GPSserial.begin(
    9600,
    SERIAL_8N1,
    16,
    17

  );

  Wire.begin();

  compass.init();

  compass.setCalibration(
    -900,
    1400,
    -900,
    1100,
    -900,
    900);


  // downloadRoute();

  Serial.println();
  Serial.println("==========");
  Serial.println("Hardware Ready");
  Serial.println("GPS Ready");
  Serial.println("Compass Ready");
  Serial.println("==========");
}

//==================================================
// Loop
//==================================================

void loop() {

  readGPS();

  readCompass();

  waterTemperature = 28.5;
  waterPH = 7.2;
  waterTurbidity = 12.3;

  //getMissionCommand();

  if (missionCommand == "START") {
    if (!lastMissionState) {
      if (missionModeString == "MANUAL") {
        missionMode = MANUAL_MODE;
      } else {
        missionMode = SWEEP_MODE;
      }

      startMission();

      lastMissionState = true;
    }
  }

  if (missionCommand == "STOP") {
    if (lastMissionState) {
      stopMission();

      lastMissionState = false;
    }
  }

  if (missionStarted) {
    runMission();
  }
  unsigned long now = millis();

  if (now - lastUploadTime >= uploadInterval) {
    lastUploadTime = now;

    sendSensorESPNow();
  }

  delay(100);
}