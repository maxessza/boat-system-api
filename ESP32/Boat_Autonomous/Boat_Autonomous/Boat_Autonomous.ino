//==================================================
// Smart Water Surface Boat
// Autonomous Navigation System
//==================================================


//==================================================
// Library
//==================================================

#include <WiFi.h>
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

// WiFi
const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

// API
const char* apiUrl = "http://192.168.1.4:8000/route";


//==================================================
// Hardware Objects
//==================================================

TinyGPSPlus gps;

HardwareSerial GPSserial(1);

QMC5883LCompass compass;


//==================================================
// Waypoint Data
//==================================================

float waypointLat[100];

float waypointLng[100];

int totalWaypoints = 0;

int currentWaypoint = 0;

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
// Mission State
//==================================================

enum MissionState 
{
  NAVIGATE,

  MISSION_COMPLETE
};

MissionState missionState = NAVIGATE;

//--------------------------------------------------
// Connect ESP32 to WiFi
//--------------------------------------------------

void connectWiFi() 

{
  Serial.println("Connecting WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected");
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());
}

//--------------------------------------------------
// Download Route from Backend API
//--------------------------------------------------

void downloadRoute() 

{
  HTTPClient http;

  http.begin(apiUrl);

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

void decideDirection(float headingError)
{
    int motorSpeed =
        calculateMotorSpeed(headingError);

    if (headingError > 5)
    {
        turnRight();

        Serial.print("Speed : ");
        Serial.println(motorSpeed);
    }
    else if (headingError < -5)
    {
        turnLeft();

        Serial.print("Speed : ");
        Serial.println(motorSpeed);
    }
    else
    {
        moveForward();

        Serial.print("Speed : ");
        Serial.println(150);
    }
}

//--------------------------------------------------
// Generate Sweep Pattern
//--------------------------------------------------
void generateSweepPattern()
{
    float startLat = 19.912500;

    float startLng = 99.842500;

    float width = 0.000300;

    float height = 0.000300;

    float step = 0.000050;

    bool leftToRight = true;

    sweepTotal = 0;

    for (float y = 0; y <= height; y += step)
   {
     if (leftToRight)
     {
       for (float x = 0; x <= width; x += step)
       {

        waypointLat[sweepTotal] = startLat + y;

        waypointLng[sweepTotal] = startLng + x;

        sweepTotal++;
       }    
      }
    else
     {
         for (float x = width; x >= 0; x -= step)
       {

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

    for (int i = 0; i < sweepTotal; i++)
  {
    Serial.print("Point ");

    Serial.print(i + 1);

    Serial.print(" : ");

    Serial.print(waypointLat[i],6);

    Serial.print(",");

    Serial.println(waypointLng[i],6);
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

  float bearing = calculateBearing
    (
    currentLat,
    currentLng,
    targetLat,
    targetLng
    );

  calculateCompensatedBearing
  (bearing);

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

  float headingError = calculateHeadingError
  (
  compensatedBearing,
  currentHeading
  );

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

    currentWaypoint++;

   if (currentWaypoint >= totalWaypoints)

   {
     navigationStarted = false;

     stopBoat();

     missionState = MISSION_COMPLETE;

     currentWaypoint = 0;
   }
  }
}

//==================================================
// Simulation Functions
//==================================================

void simulateBoatMovement()
{
    currentLat += 0.000010;

    currentLng += 0.000010;

    Serial.println("Boat Moving...");

    Serial.print("Current Lat : ");
    Serial.println(currentLat,6);

    Serial.print("Current Lng : ");
    Serial.println(currentLng,6);
}

//--------------------------------------------------
// Start Drift Mode
//--------------------------------------------------
void startDriftMode()
{
    Serial.println();

    Serial.println("==========");

    Serial.println("Start Drift Mode");

    driftStartLat = currentLat;
    driftStartLng = currentLng;

    Serial.print("Start Latitude : ");
    Serial.println(driftStartLat,6);

    Serial.print("Start Longitude : ");
    Serial.println(driftStartLng,6);

    delay(5000);

    driftEndLat = currentLat;
    driftEndLng = currentLng;

    Serial.print("End Latitude : ");
    Serial.println(driftEndLat,6);

    Serial.print("End Longitude : ");
    Serial.println(driftEndLng,6);

    driftCompleted = true;

    navigationStarted = true;

    calculateFlowVector();

    Serial.println("Drift Complete");
}

//--------------------------------------------------
// Calculate Flow Vector
//--------------------------------------------------
void calculateFlowVector()
{
    flowVectorLat =
        driftEndLat - driftStartLat;

    flowVectorLng =
        driftEndLng - driftStartLng;

    Serial.println();

    Serial.println("Flow Vector");

    Serial.print("Flow Lat : ");

    Serial.println(flowVectorLat,6);

    Serial.print("Flow Lng : ");

    Serial.println(flowVectorLng,6);

    calculateFlowDirection();
}

//--------------------------------------------------
// Calculate Flow Direction
//--------------------------------------------------
void calculateFlowDirection()
{
    flowDirection =
    degrees(
    atan2(
    flowVectorLng,
    flowVectorLat));

    if (flowDirection < 0)
    {
        flowDirection += 360;
    }

    Serial.println();

    Serial.println("Flow Direction");

    Serial.print("Direction : ");
    Serial.println(flowDirection, 2);

    if (flowDirection >= 337.5 || flowDirection < 22.5)
    {
        Serial.println("North");
    }
    else if (flowDirection < 67.5)
    {
        Serial.println("North-East");
    }
    else if (flowDirection < 112.5)
    {
        Serial.println("East");
    }
    else if (flowDirection < 157.5)
    {
        Serial.println("South-East");
    }
    else if (flowDirection < 202.5)
    {
        Serial.println("South");
    }
    else if (flowDirection < 247.5)
    {
        Serial.println("South-West");
    }
    else if (flowDirection < 292.5)
    {
        Serial.println("West");
    }
    else
    {
        Serial.println("North-West");
    }
}

//--------------------------------------------------
// Flow Compensation
//--------------------------------------------------
void calculateCompensatedBearing(float bearing)
{
    compensatedBearing =
        bearing - (flowDirection * 0.05);

    if (compensatedBearing < 0)
    {
        compensatedBearing += 360;
    }

    if (compensatedBearing > 360)
    {
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
void printMissionStatus()
{
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
}


//==================================================
// Mission Functions
//==================================================

void runMission()
{
    Serial.println("==========");
    Serial.println("Mission Running");

    printMissionStatus();

    if (missionState == NAVIGATE)
    {
        // ถ้ายังไม่ได้ทำ Drift
        if (!driftCompleted)
        {
            startDriftMode();
        }

        // หลังจาก Drift เสร็จแล้วจึงเริ่ม Navigation
       if (navigationStarted && !sweepFinished)
     {
       simulateBoatMovement();

       navigateToWaypoint();
     } 

    }

    else if (missionState == MISSION_COMPLETE)
     {
       stopBoat();

       Serial.println("Mission Complete");

       Serial.println("All Waypoints Finished");

       Serial.println("Boat Stop");
     }
}

//==================================================
// Setup
//==================================================

void setup() 

{
  Serial.begin(115200);

  connectWiFi();

  downloadRoute();

  // downloadRoute();
  
  generateSweepPattern();

}

//==================================================
// Loop
//==================================================

void loop() 

{
  runMission();

  delay(1000);
}