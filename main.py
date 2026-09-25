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
# Current Mission
# ====================================

current_mission_id = None

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
    global current_mission_id

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
                mission_id,
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
            current_mission_id,
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

# ====================================
# Mission History
# ====================================

@app.get("/missions")
def get_missions():

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor(pymysql.cursors.DictCursor)

        cursor.execute("""
            SELECT
                mission_id,
                boat_id,
                mission_name,
                mode,
                start_time,
                end_time,
                status,
                total_waypoints,
                completed_waypoints,
                distance_m,
                sensor_samples,
                avg_temp,
                avg_ph,
                avg_turbidity,
                max_turbidity,
                min_turbidity,
                created_at
            FROM missions
            ORDER BY mission_id DESC
        """)

        missions = cursor.fetchall()

        return missions

    except Exception as e:

        return {
            "success": False,
            "message": str(e)
        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()

@app.get("/missions/{mission_id}")
def get_mission(mission_id: int):

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor(pymysql.cursors.DictCursor)

        cursor.execute("""
            SELECT
                mission_id,
                boat_id,
                mission_name,
                mode,
                start_time,
                end_time,
                status,
                total_waypoints,
                completed_waypoints,
                distance_m,
                sensor_samples,
                avg_temp,
                avg_ph,
                avg_turbidity,
                max_turbidity,
                min_turbidity,
                created_at
            FROM missions
            WHERE mission_id = %s
        """, (
            mission_id,
        ))

        mission = cursor.fetchone()

        if not mission:

            return {
                "success": False,
                "message": "Mission not found"
            }

        return mission

    except Exception as e:

        return {
            "success": False,
            "message": str(e)
        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()

@app.get("/missions/{mission_id}/route")
def get_mission_route(mission_id: int):

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor(pymysql.cursors.DictCursor)

        cursor.execute("""
            SELECT
                id,
                route_name,
                waypoint_order,
                latitude,
                longitude,
                created_at,
                mission_id
            FROM routes
            WHERE mission_id = %s
            ORDER BY waypoint_order
        """, (
            mission_id,
        ))

        route = cursor.fetchall()

        return route

    except Exception as e:

        return {
            "success": False,
            "message": str(e)
        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()

@app.get("/missions/{mission_id}/logs")
def get_mission_logs(mission_id: int):

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor(pymysql.cursors.DictCursor)

        cursor.execute("""
            SELECT
                id,
                boat_id,
                mission_id,
                log_time,
                latitude,
                longitude,
                heading,
                temp_c,
                ph_level,
                turbidity_ntu,
                flow_v_lat,
                flow_v_lng
            FROM sensor_logs
            WHERE mission_id = %s
            ORDER BY log_time ASC
        """, (
            mission_id,
        ))

        logs = cursor.fetchall()

        return logs

    except Exception as e:

        return {
            "success": False,
            "message": str(e)
        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()                                    


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

    global current_mission_id

    conn = get_connection()

    cursor = conn.cursor(pymysql.cursors.DictCursor)

    # ====================================
    # ถ้ามี Mission กำลังทำงาน
    # ให้ส่ง Route ของ Mission นั้น
    # ====================================

    if current_mission_id is not None:

        print(
            "GET /route -> Active Mission:",
            current_mission_id
        )

        cursor.execute("""
            SELECT
                waypoint_order,
                latitude,
                longitude
            FROM routes
            WHERE mission_id = %s
            ORDER BY waypoint_order
        """, (
            current_mission_id,
        ))

    # ====================================
    # ถ้ายังไม่มี Mission
    # ให้ส่ง Draft Route
    # ====================================

    else:

        print(
            "GET /route -> Draft Route"
        )

        cursor.execute("""
            SELECT
                waypoint_order,
                latitude,
                longitude
            FROM routes
            WHERE mission_id IS NULL
            ORDER BY waypoint_order
        """)

    data = cursor.fetchall()

    print(
        "GET /route -> Total:",
        len(data)
    )

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
    global current_mission_id
    global current_waypoint
    global total_waypoints

    # ====================================
    # Prevent duplicate mission start
    # ====================================

    if mission_running:

        return {
            "success": False,
            "message": "Mission is already running",
            "mission_id": current_mission_id
        }

    conn = None
    cursor = None

    try:

        conn = get_connection()
        cursor = conn.cursor()

        # ====================================
        # Mission Mode
        # ====================================

        mission_mode = request.mode.upper()

        # ====================================
        # Find Boat ID
        # ====================================

        cursor.execute("""
            SELECT boat_id
            FROM sensor_logs
            WHERE boat_id IS NOT NULL
            ORDER BY id DESC
            LIMIT 1
        """)

        boat_row = cursor.fetchone()

        if boat_row:

            boat_id = boat_row["boat_id"]

        else:

            boat_id = "Boat01"

        # ====================================
        # Count Draft Route
        # ====================================

        cursor.execute("""
            SELECT COUNT(*) AS total
            FROM routes
            WHERE mission_id IS NULL
        """)

        route_row = cursor.fetchone()

        total_waypoints = route_row["total"]

        # ====================================
        # Create New Mission
        # ====================================

        cursor.execute("""
            INSERT INTO missions
            (
                boat_id,
                mission_name,
                mode,
                start_time,
                status,
                total_waypoints,
                completed_waypoints
            )
            VALUES
            (
                %s,
                %s,
                %s,
                NOW(),
                'RUNNING',
                %s,
                0
            )
        """, (
            boat_id,
            "New Mission",
            mission_mode,
            total_waypoints
        ))

        current_mission_id = cursor.lastrowid

        # ====================================
        # Attach Draft Route To Mission
        # ====================================

        cursor.execute("""
            UPDATE routes
            SET mission_id = %s
            WHERE mission_id IS NULL
        """, (
            current_mission_id
        ))

        conn.commit()

        # ====================================
        # Update Runtime State
        # ====================================

        mission_running = True

        boat_state = "Navigating"

        mission_command = "START"

        current_waypoint = 0

        print(
            "===================================="
        )

        print(
            "MISSION STARTED"
        )

        print(
            "Mission ID :",
            current_mission_id
        )

        print(
            "Mode       :",
            mission_mode
        )

        print(
            "Waypoints  :",
            total_waypoints
        )

        print(
            "===================================="
        )

        return {

            "success": True,

            "status": "Running",

            "mode": mission_mode,

            "mission_id": current_mission_id,

            "total_waypoints": total_waypoints

        }

    except Exception as e:

        if conn:
            conn.rollback()

        print(
            "START MISSION ERROR:",
            e
        )

        return {

            "success": False,

            "message": str(e)

        }

    finally:

        if cursor:
            cursor.close()

        if conn:
            conn.close()

# ====================================
# Complete Mission
# ====================================

@app.post("/mission/complete")
def completeMission():

    global mission_running
    global mission_mode
    global boat_state
    global mission_command
    global current_mission_id
    global current_waypoint

    conn = None
    cursor = None

    try:

        if current_mission_id is None:
            return {
                "success": False,
                "message": "No active mission"
            }

        conn = get_connection()
        cursor = conn.cursor(pymysql.cursors.DictCursor)

        # ==============================
        # COUNT SENSOR DATA
        # ==============================

        cursor.execute("""
            SELECT
                COUNT(*) AS sensor_samples,
                AVG(temp_c) AS avg_temp,
                AVG(ph_level) AS avg_ph,
                AVG(turbidity_ntu) AS avg_turbidity,
                MAX(turbidity_ntu) AS max_turbidity,
                MIN(turbidity_ntu) AS min_turbidity
            FROM sensor_logs
            WHERE mission_id = %s
        """, (
            current_mission_id,
        ))

        sensor_stats = cursor.fetchone()

        # ==============================
        # COUNT COMPLETED WAYPOINTS
        # ==============================

        cursor.execute("""
            SELECT COUNT(*) AS total
            FROM routes
            WHERE mission_id = %s
        """, (
            current_mission_id,
        ))

        route_stats = cursor.fetchone()

        completed_waypoints = route_stats["total"]

        # ==============================
        # UPDATE MISSION
        # ==============================

        cursor.execute("""
            UPDATE missions
            SET
                end_time = NOW(),
                status = 'COMPLETED',
                completed_waypoints = %s,
                sensor_samples = %s,
                avg_temp = %s,
                avg_ph = %s,
                avg_turbidity = %s,
                max_turbidity = %s,
                min_turbidity = %s
            WHERE mission_id = %s
        """, (
            completed_waypoints,
            sensor_stats["sensor_samples"],
            sensor_stats["avg_temp"],
            sensor_stats["avg_ph"],
            sensor_stats["avg_turbidity"],
            sensor_stats["max_turbidity"],
            sensor_stats["min_turbidity"],
            current_mission_id
        ))

        conn.commit()

        mission_running = False
        mission_mode = "IDLE"
        boat_state = "Mission Completed"
        mission_command = "STOP"

        completed_mission_id = current_mission_id

        current_mission_id = None
        current_waypoint = 0

        return {
            "success": True,
            "message": "Mission completed",
            "mission_id": completed_mission_id,
            "completed_waypoints": completed_waypoints,
            "sensor_samples": sensor_stats["sensor_samples"]
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

    cursor.execute("""
       DELETE FROM routes
       WHERE mission_id IS NULL
    """)

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
    # Waypoint spacing in meters
    # ==============================

    step_m = area.spacing

    mid_lat = (start_lat + end_lat) / 2

    meters_per_lat_degree = 111_320
    meters_per_lng_degree = 111_320 * max(
        abs(math.cos(math.radians(mid_lat))),
        1e-6
    )

    lat_step_deg = step_m / meters_per_lat_degree
    lng_step_deg = step_m / meters_per_lng_degree

    lat_distance_m = (
        abs(end_lat - start_lat) * meters_per_lat_degree
    )
    lng_distance_m = (
        abs(end_lng - start_lng) * meters_per_lng_degree
    )

    # จำนวนช่วงเต็ม 2.5 เมตรในแต่ละทิศ
    lat_intervals = int(math.floor(lat_distance_m / step_m + 1e-9))
    lng_intervals = int(math.floor(lng_distance_m / step_m + 1e-9))

    lat_direction = 1 if end_lat >= start_lat else -1
    lng_direction = 1 if end_lng >= start_lng else -1

    latitudes = [
        start_lat + lat_direction * lat_step_deg * i
        for i in range(lat_intervals + 1)
    ]

    longitudes = [
        start_lng + lng_direction * lng_step_deg * i
        for i in range(lng_intervals + 1)
    ]

    # ==============================
    # Generate serpentine route
    # ==============================

    if area.orientation.lower() == "vertical":

        count = len(longitudes)

        for column_index, lng in enumerate(longitudes):

            if column_index % 2 == 0:
                lat_sequence = latitudes
            else:
                lat_sequence = reversed(latitudes)

            for lat in lat_sequence:
                add_waypoint(lat, lng)

    else:

        count = len(latitudes)

        for row_index, lat in enumerate(latitudes):

            if row_index % 2 == 0:
                lng_sequence = longitudes
            else:
                lng_sequence = reversed(longitudes)

            for lng in lng_sequence:
                add_waypoint(lat, lng)

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

    # ลบเฉพาะ Route ที่ยังไม่ได้ผูกกับ Mission
    cursor.execute("""
       DELETE FROM routes
       WHERE mission_id IS NULL
    """)

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

    cursor.execute("""
       DELETE FROM routes
       WHERE mission_id IS NULL
    """)

    conn.commit()

    cursor.close()
    conn.close()

    return {
        "status":"Route Cleared"
    }