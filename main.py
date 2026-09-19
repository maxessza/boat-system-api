from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from fastapi.staticfiles import StaticFiles
import pymysql
import os
import math

app = FastAPI()

app.mount(
    "/assets",
    StaticFiles(directory="assets"),
    name="assets"
)

#====================================
# Mission State
#====================================

mission_running = False

mission_mode = "MANUAL"

boat_state = "Idle"

mission_command = "IDLE"

current_waypoint = 0

total_waypoints = 0

# ====================================
# Boat State From Telemetry
# ====================================

current_boat_mode = 0

current_stage_intent = 0

current_battery = None

# ✅ CORS สำหรับ Dashboard
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ✅ ฟังก์ชันเชื่อม MySQL Railway
def get_connection():

    host = "acela.proxy.rlwy.net"
    user = "root"
    password = "NEZvYeMWvkMNSKbxWXgSQzaxVWjYtfEz"
    database = "railway"
    port = 57935

    return pymysql.connect(
        host=host,
        user=user,
        password=password,
        database=database,
        port=port,
        cursorclass=pymysql.cursors.DictCursor
    )

# ✅ Model รับข้อมูล
class SensorData(BaseModel):
    boat_id: str
    latitude: float | None = None
    longitude: float | None = None
    temp_c: float | None = None
    ph_level: float | None = None
    turbidity_ntu: float | None = None
    battery: float | None = None
    heading: float | None = None
    flow_v_lat: float | None = None
    flow_v_lng: float | None = None
    current_mode: int | None = None
    stage_intent: int | None = None

#====================================
# Manual Route Models
#====================================

class RoutePoint(BaseModel):
    waypoint_order: int
    latitude: float
    longitude: float


class RouteData(BaseModel):
    route_name: str
    points: list[RoutePoint]

class SweepArea(BaseModel):

    start_lat: float
    start_lng: float
    width: float
    height: float
    spacing: float
    orientation: str = "vertical"


class MissionRequest(BaseModel):
    mode: str

# ✅ เช็ค API
@app.get("/")
def home():
    return {"message": "API is running"}


# ====================================
# Convert NaN / Infinity -> None
# ====================================

def clean_float(value):
    if value is None:
        return None

    try:
        value = float(value)

        if not math.isfinite(value):
            return None

        return value

    except (TypeError, ValueError):
        return None


# ✅ รับข้อมูล + บันทึก DB + คำนวณ
@app.post("/data")
def receive_data(data: SensorData):

    conn = None
    cursor = None

    global current_boat_mode
    global current_stage_intent
    global current_battery
    global mission_command
    global mission_running
    global boat_state

    # ====================================
    # CLEAN SENSOR VALUES
    # ====================================

    latitude = clean_float(data.latitude)
    longitude = clean_float(data.longitude)
    temp_c = clean_float(data.temp_c)
    ph_level = clean_float(data.ph_level)
    turbidity_ntu = clean_float(data.turbidity_ntu)
    heading = clean_float(data.heading)
    flow_v_lat = clean_float(data.flow_v_lat)
    flow_v_lng = clean_float(data.flow_v_lng)
    battery = clean_float(data.battery)

    if data.current_mode is not None:
        current_boat_mode = data.current_mode

    if data.stage_intent is not None:
        current_stage_intent = data.stage_intent

    if battery is not None:
        current_battery = battery

    # ====================================
    # RTH COMPLETED
    # ====================================

    if (
        data.current_mode == 0
        and (
            mission_command == "RTH"
            or mission_command == "CLEAR_ESTOP"
        )
    ):

        if mission_command == "RTH":
            print("RTH completed -> Mission IDLE")
        else:
            print("E-Stop cleared -> Mission IDLE")

        mission_command = "IDLE"
        mission_running = False
        boat_state = "Idle"

    try:
        conn = get_connection()
        cursor = conn.cursor()

        # ==============================
        # INSERT SENSOR DATA
        # ==============================

        cursor.execute("""
            INSERT INTO sensor_logs
            (
                boat_id,
                log_time,
                latitude,
                longitude,
                heading,
                temp_c,
                ph_level,
                turbidity_ntu,
                flow_v_lat,
                flow_v_lng
            )
            VALUES
            (
                %s,
                NOW(),
                %s,
                %s,
                %s,
                %s,
                %s,
                %s,
                %s,
                %s
            )
        """, (
            data.boat_id,
            latitude,
            longitude,
            heading,
            temp_c,
            ph_level,
            turbidity_ntu,
            flow_v_lat,
            flow_v_lng
        ))

        # ==============================
        # DRIFT PREDICTION
        # ==============================

        if latitude is not None:
           predicted_lat = latitude + 0.0003
        else:
           predicted_lat = None

        if longitude is not None:
           predicted_lng = longitude + 0.0003
        else:
           predicted_lng = None

        cursor.execute("""
            INSERT INTO drift_predictions
            (
                log_time,
                start_lat,
                start_lng,
                end_lat,
                end_lng,
                flow_v_lat,
                flow_v_lng,
                predicted_lat,
                predicted_lng
            )
            VALUES
            (
                NOW(),
                %s,
                %s,
                %s,
                %s,
                %s,
                %s,
                %s,
                %s
            )
        """, (
            latitude,
            longitude,
            predicted_lat,
            predicted_lng,
            flow_v_lat,
            flow_v_lng,
            predicted_lat,
            predicted_lng
        ))

        # ==============================
        # COMMIT
        # ==============================

        conn.commit()

        return {
            "status": "success",
            "flow_vector": {
                "lat": flow_v_lat,
                "lng": flow_v_lng
            },
            "prediction": {
                "predicted_lat": predicted_lat,
                "predicted_lng": predicted_lng
            }
        }

    except Exception as e:

        if conn:
            conn.rollback()

        print("DATA ERROR:", e)

        return {
            "status": "error",
            "error": str(e)
        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()
            
# ✅ ดูข้อมูล sensor_logs ล่าสุด 100 รายการ
@app.get("/logs")
def get_logs():
    try:
        conn = get_connection()
        cursor = conn.cursor()

        cursor.execute("SELECT * FROM sensor_logs LIMIT 100")

        data = cursor.fetchall()

        return data

    except Exception as e:
        return {"error": str(e)}

    finally:
        try:
            cursor.close()
            conn.close()
        except:
            pass

# ✅ ดูข้อมูล prediction ล่าสุด 100 รายการ
@app.get("/predictions")
def get_predictions():
    try:
        conn = get_connection()
        cursor = conn.cursor()

        cursor.execute("SELECT * FROM drift_predictions LIMIT 100")

        data = cursor.fetchall()

        return data

    except Exception as e:
        return {"error": str(e)}

    finally:
        try:
            cursor.close()
            conn.close()
        except:
            pass


# ✅ ดูเส้นทางเรือทั้งหมด
@app.get("/path")
def get_path():
    try:
        conn = get_connection()
        cursor = conn.cursor()

        cursor.execute("""
            SELECT latitude, longitude
            FROM sensor_logs
        """)

        data = cursor.fetchall()

        return data

    except Exception as e:
        return {"error": str(e)}

    finally:
        try:
            cursor.close()
            conn.close()
        except:
            pass


# ✅ ดูข้อมูลล่าสุด 1 รายการ (ใช้กับ Dashboard)
@app.get("/latest")
def get_latest():

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor()

        cursor.execute("""
            SELECT
                s.*,
                d.flow_v_lat,
                d.flow_v_lng,
                d.predicted_lat,
                d.predicted_lng
            FROM sensor_logs s
            LEFT JOIN drift_predictions d
                ON d.predict_id = (
                    SELECT MAX(predict_id)
                    FROM drift_predictions
                )
            ORDER BY s.id DESC
            LIMIT 1
        """)

        data = cursor.fetchone()

        if data is None:
            return {
                "battery": current_battery
            }

        data["battery"] = current_battery

        return data

    except Exception as e:

        return {"error": str(e)}

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()


#====================================
# Clear Sensor & Prediction Data
#====================================

@app.post("/clear_data")
def clear_data():

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor()

        # ลบ Prediction ก่อน
        cursor.execute("""
            DELETE FROM drift_predictions
        """)

        deleted_predictions = cursor.rowcount

        # ลบ Sensor Logs
        cursor.execute("""
            DELETE FROM sensor_logs
        """)

        deleted_logs = cursor.rowcount

        conn.commit()

        return {
            "success": True,
            "message": "Database data cleared successfully",
            "deleted": {
                "drift_predictions": deleted_predictions,
                "sensor_logs": deleted_logs
            }
        }

    except Exception as e:

        if conn:
            conn.rollback()

        return {
            "success": False,
            "message": str(e)
        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()            

@app.get("/dbtest")
def dbtest():
    try:
        conn = get_connection()
        conn.close()
        return {"status": "database connected"}
    except Exception as e:
        return {"error": str(e)}

@app.get("/dashboard")
def dashboard():
    return FileResponse("dashboard.html")
    
@app.get("/history")
def history():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        SELECT
            boat_id,
            log_time,
            latitude,
            longitude,
            temp_c,
            ph_level,
            turbidity_ntu
        FROM sensor_logs
        ORDER BY log_time DESC
    """)

    rows = cursor.fetchall()

    cursor.close()
    conn.close()

    return rows


@app.get("/route")
def get_route():

    conn = get_connection()

    cursor = conn.cursor(pymysql.cursors.DictCursor)

    cursor.execute("""
        SELECT
            waypoint_order,
            latitude,
            longitude
        FROM routes
        ORDER BY waypoint_order
    """)

    data = cursor.fetchall()

    cursor.close()
    conn.close()

    return data
    

#====================================
# Start Mission
#====================================

@app.post("/mission/start")
def startMission(request: MissionRequest):

    global mission_running
    global mission_mode
    global boat_state
    global mission_command

    mission_running = True
    boat_state = "Navigating"
    mission_mode = request.mode.upper()
    mission_command = "START"

    return {
        "success": True,
        "status": "Running",
        "mode": mission_mode
    }

#====================================
# Retrun home
#====================================

@app.post("/mission/rth")
def returnHome():

    global mission_running
    global mission_mode
    global boat_state
    global mission_command

    mission_running = False
    mission_mode = "RTH"
    boat_state = "Returning Home"
    mission_command = "RTH"

    return {
        "success": True,
        "status": "Returning Home",
        "mode": "RTH",
        "command": "RTH"
    }

#====================================
# set home
#====================================

@app.post("/mission/set-home")
def setHome():

    global mission_command
    global boat_state

    mission_command = "SET_HOME"
    boat_state = "Setting Home"

    return {
        "success": True,
        "status": "Setting Home",
        "command": "SET_HOME"
    }

    

#====================================
# force-spiral  
#====================================   

@app.post("/mission/force-spiral")
def forceSpiral():

    global mission_command
    global boat_state

    mission_command = "FORCE_SPIRAL"
    boat_state = "Spiral"

    return {
        "success": True,
        "status": "Spiral",
        "command": "FORCE_SPIRAL"
    }


#====================================
# Clear Emergency Stop
#====================================

@app.post("/mission/clear-estop")
def clearEstop():

    global mission_running
    global boat_state
    global mission_command

    mission_running = False
    boat_state = "Clearing E-Stop"
    mission_command = "CLEAR_ESTOP"

    return {
        "success": True,
        "status": "Clearing E-Stop",
        "command": "CLEAR_ESTOP"
    }

#====================================
# Emergency Stop
#====================================

@app.post("/mission/emergency-stop")
def emergencyStop():

    global mission_running
    global mission_mode
    global boat_state
    global mission_command

    mission_running = False
    boat_state = "Emergency Stop"
    mission_command = "EMERGENCY_STOP"

    return {
        "success": True,
        "status": "Emergency Stop",
        "mode": mission_mode,
        "command": "EMERGENCY_STOP"
    }

#====================================
# Mission Status
#====================================

@app.get("/mission/status")
def missionStatus():

    return {

        "running": mission_running,

        "mode": mission_mode,

        "boat_state": boat_state,

        "current_mode": current_boat_mode,

        "stage_intent": current_stage_intent

    }



#====================================
# Mission Progress
#====================================

@app.get("/mission/progress")
def missionProgress():

    return {

        "current_waypoint": current_waypoint,

        "total_waypoints": total_waypoints

    }    

    
#====================================
# ESP32 Mission API
#====================================
@app.get("/mission")
def getMission():
    global mission_command

    if mission_command == "EMERGENCY_STOP":
        return {
            "command": "EMERGENCY_STOP",
            "mode": mission_mode
        }

    if mission_command == "CLEAR_ESTOP":
        return {
            "command": "CLEAR_ESTOP",
            "mode": mission_mode
        }

    if mission_command == "RTH":
        return {
            "command": "RTH",
            "mode": mission_mode
        }

    if mission_command == "SET_HOME":

        response = {
            "command": "SET_HOME",
            "mode": mission_mode
        }

        mission_command = "IDLE"

        return response

    if mission_command == "FORCE_SPIRAL":
        return {
            "command": "FORCE_SPIRAL",
            "mode": mission_mode
        }

    if mission_running:
        return {
            "command": "START",
            "mode": mission_mode
        }

    return {
        "command": "IDLE",
        "mode": mission_mode
    }
@app.post("/generate_sweep")
def generateSweep(area: SweepArea):

    conn = get_connection()
    cursor = conn.cursor()

    # ==============================
    # Clear Old Route
    # ==============================

    cursor.execute("DELETE FROM routes")

    route = []

    direction = 1
    order = 1
    count = 0

    # ==============================
    # Function: Add Waypoint
    # ==============================

    def add_waypoint(lat, lng):

        nonlocal order

        route.append({
            "order": order,
            "latitude": lat,
            "longitude": lng
        })

        cursor.execute("""
            INSERT INTO routes
            (
                route_name,
                waypoint_order,
                latitude,
                longitude
            )
            VALUES
            (
                %s,
                %s,
                %s,
                %s
            )
        """, (
            "Sweep Mission",
            order,
            lat,
            lng
        ))

        print(
            f"WP {order}: "
            f"Lat={lat}, "
            f"Lng={lng}"
        )

        order += 1

    # ==============================
    # Area
    # ==============================

    start_lng = area.start_lng
    end_lng = area.start_lng + area.width

    start_lat = area.start_lat
    end_lat = area.start_lat + area.height

    # ==============================
    # VERTICAL SWEEP
    # ==============================

    if area.orientation == "vertical":

        lng = start_lng

        while lng <= end_lng:

            count += 1

            print(f"Loop {count}")
            print(f"Current Lng : {lng}")

            # --------------------------
            # Bottom -> Top
            # --------------------------

            if direction == 1:

                # 0%
                add_waypoint(
                    start_lat,
                    lng
                )

                # 33%
                add_waypoint(
                    start_lat + (area.height * 1 / 3),
                    lng
                )

                # 66%
                add_waypoint(
                    start_lat + (area.height * 2 / 3),
                    lng
                )

                # 100%
                add_waypoint(
                    end_lat,
                    lng
                )

            # --------------------------
            # Top -> Bottom
            # --------------------------

            else:

                # 100%
                add_waypoint(
                    end_lat,
                    lng
                )

                # 66%
                add_waypoint(
                    start_lat + (area.height * 2 / 3),
                    lng
                )

                # 33%
                add_waypoint(
                    start_lat + (area.height * 1 / 3),
                    lng
                )

                # 0%
                add_waypoint(
                    start_lat,
                    lng
                )

            # Reverse direction

            direction *= -1

            # Move to next vertical line

            lng += area.spacing

    # ==============================
    # HORIZONTAL SWEEP
    # ==============================

    else:

        lat = start_lat

        while lat <= end_lat:

            count += 1

            print(f"Loop {count}")
            print(f"Current Lat : {lat}")

            # --------------------------
            # Left -> Right
            # --------------------------

            if direction == 1:

                add_waypoint(
                    lat,
                    start_lng
                )

                add_waypoint(
                    lat,
                    start_lng + (area.width * 1 / 3)
                )

                add_waypoint(
                    lat,
                    start_lng + (area.width * 2 / 3)
                )

                add_waypoint(
                    lat,
                    end_lng
                )

            # --------------------------
            # Right -> Left
            # --------------------------

            else:

                add_waypoint(
                    lat,
                    end_lng
                )

                add_waypoint(
                    lat,
                    start_lng + (area.width * 2 / 3)
                )

                add_waypoint(
                    lat,
                    start_lng + (area.width * 1 / 3)
                )

                add_waypoint(
                    lat,
                    start_lng
                )

            direction *= -1

            lat += area.spacing

    # ==============================
    # Finish
    # ==============================

    print("===========================")
    print("Total Loop :", count)
    print("Total Waypoints :", len(route))
    print("===========================")

    conn.commit()

    cursor.close()
    conn.close()

    return route
    
#====================================
# Save Manual Route
#====================================

@app.post("/save_route")
def save_route(route: RouteData):

    global total_waypoints

    total_waypoints = len(route.points)

    conn = get_connection()
    cursor = conn.cursor()

    # ลบ Route เดิม
    cursor.execute("DELETE FROM routes")

    # เพิ่ม Route ใหม่
    for point in route.points:

        cursor.execute("""
        INSERT INTO routes
        (
            route_name,
            waypoint_order,
            latitude,
            longitude
        )
        VALUES
        (
            %s,
            %s,
            %s,
            %s
        )
        """,
        (
            route.route_name,
            point.waypoint_order,
            point.latitude,
            point.longitude
        ))

    conn.commit()

    cursor.close()
    conn.close()

    return {
        "status": "Route Saved",
        "total_points": len(route.points)
    }

#====================================
# Clear Route
#====================================

@app.post("/clear_route")
def clear_route():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("DELETE FROM routes")

    conn.commit()

    cursor.close()
    conn.close()

    return {
        "status":"Route Cleared"
    }