#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

//========================
// WiFi
//========================
const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";

//========================
// API
//========================
const char* apiUrl = "http://192.168.1.4:8000/route";

//========================
// Waypoints
//========================
float waypointLat[100];
float waypointLng[100];

int totalWaypoints = 0;

int currentWaypoint = 0;

float currentLat = 19.912500;

float currentLng = 99.842500;

void connectWiFi() {
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

void downloadRoute() {
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

void navigateToWaypoint() {
  float targetLat = waypointLat[currentWaypoint];

  float targetLng = waypointLng[currentWaypoint];

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

  float distanceLat = targetLat - currentLat;
  float distanceLng = targetLng - currentLng;

  Serial.print("Delta Lat : ");
  Serial.println(distanceLat, 6);

  Serial.print("Delta Lng : ");
  Serial.println(distanceLng, 6);
}

if (abs(distanceLat) < 0.000050 && abs(distanceLng) < 0.000050) {
  Serial.println("Waypoint Reached");

  currentWaypoint++;

  if (currentWaypoint >= totalWaypoints) {
    currentWaypoint = 0;
  }
}

void setup() {
  Serial.begin(115200);

  connectWiFi();

  downloadRoute();
}

void loop() {
  navigateToWaypoint();

  delay(3000);
}