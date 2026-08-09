from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from fastapi.staticfiles import StaticFiles
import pymysql
import os

app = FastAPI()

app.mount(
    "/assets",
    StaticFiles(directory="assets"),
    name="assets"
)

#=========================
# Mission State
#=========================

mission_running = False

mission_mode = "MANUAL"

boat_state = "Idle"

current_waypoint = 0

total_waypoints = 0

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
    latitude: float
    longitude: float
    temp_c: float
    ph_level: float
    turbidity_ntu: float

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


class MissionRequest(BaseModel):
    mode: str

# ✅ เช็ค API
@app.get("/")
def home():
    return {"message": "API is running"}


# ✅ รับข้อมูล + บันทึก DB + คำนวณ
@app.post("/data")
def receive_data(data: SensorData):
    try:
        conn = get_connection()
        cursor = conn.cursor()

        cursor.execute("""
            INSERT INTO sensor_logs
            (boat_id, log_time, latitude, longitude, temp_c, ph_level, turbidity_ntu)
            VALUES (%s, NOW(), %s, %s, %s, %s, %s)
        """, (
            data.boat_id,
            data.latitude,
            data.longitude,
            data.temp_c,
            data.ph_level,
            data.turbidity_ntu
        ))

        flow_v_lat = data.latitude * 0.0001
        flow_v_lng = data.longitude * 0.0001

        predicted_lat = data.latitude + 0.0003
        predicted_lng = data.longitude + 0.0003

        cursor.execute("""
            INSERT INTO drift_predictions
            (log_time,start_lat,start_lng,end_lat,end_lng,
            flow_v_lat,flow_v_lng,predicted_lat,predicted_lng)
            VALUES
            (NOW(),%s,%s,%s,%s,%s,%s,%s,%s)
        """, (
            data.latitude,
            data.longitude,
            predicted_lat,
            predicted_lng,
            flow_v_lat,
            flow_v_lng,
            predicted_lat,
            predicted_lng
        ))

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
        return {"error": str(e)}

    finally:
        try:
            cursor.close()
            conn.close()
        except:
            pass

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
            SELECT *
            FROM sensor_logs
            ORDER BY id DESC
            LIMIT 1
        """)

        data = cursor.fetchone()

        return data

    except Exception as e:
        return {"error": str(e)}

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

    mission_running = True
    boat_state = "Navigating"
    mission_mode = request.mode.upper()

    return {
        "success": True,
        "status": "Running",
        "mode": mission_mode
    }
#====================================
# Stop Mission
#====================================

@app.post("/mission/stop")
def stopMission():

    global mission_running
    global mission_mode
    global boat_state

    mission_running = False
    boat_state = "Idle"

    return {
        "success": True,
        "status": "Stopped",
        "mode": mission_mode
    }
#====================================
# Mission Status
#====================================

@app.get("/mission/status")
def missionStatus():

   return{

    "running":mission_running,
    "mode":mission_mode,
    "boat_state":boat_state

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

    if mission_running:

        return {
            "command": "START",
            "mode": mission_mode
        }

    return {
        "command": "STOP",
        "mode": mission_mode
    }


@app.post("/generate_sweep")
def generateSweep(area: SweepArea):

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("DELETE FROM routes")

    route = []

    lat = area.start_lat

    direction = 1

    order = 1

    count = 0

    print("========== Sweep ==========")
    print("Start Lat :", area.start_lat)
    print("End Lat   :", area.start_lat + area.height)
    print("Height    :", area.height)
    print("Spacing   :", area.spacing)
    print("===========================")

    while lat <= area.start_lat + area.height:

        count += 1

        print(f"Loop {count}")
        print(f"Current Lat : {lat}")

        if direction == 1:

            route.append({
                "order": order,
                "latitude": lat,
                "longitude": area.start_lng
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
            """,
            (
                "Sweep Mission",
                order,
                lat,
                area.start_lng
            ))

            order += 1

            route.append({
                "order": order,
                "latitude": lat,
                "longitude": area.start_lng + area.width
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
            """,
            (
                "Sweep Mission",
                order,
                lat,
                area.start_lng + area.width
            ))

            order += 1

        else:

            route.append({
                "order": order,
                "latitude": lat,
                "longitude": area.start_lng + area.width
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
            """,
            (
                "Sweep Mission",
                order,
                lat,
                area.start_lng + area.width
            ))

            order += 1

            route.append({
                "order": order,
                "latitude": lat,
                "longitude": area.start_lng
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
            """,
            (
                "Sweep Mission",
                order,
                lat,
                area.start_lng
            ))

            order += 1

        direction *= -1
        
        lat += area.spacing

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