from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import pymysql
import os

app = FastAPI()

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

    host = os.getenv("MYSQLHOST", "127.0.0.1")
    user = os.getenv("MYSQLUSER", "root")
    password = os.getenv("MYSQLPASSWORD", "123456M@x")
    database = os.getenv("MYSQLDATABASE", "boat_system")
    port = int(os.getenv("MYSQLPORT", 3306))

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

        predicted_lat = data.latitude + flow_v_lat * 100
        predicted_lng = data.longitude + flow_v_lng * 100

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
def get_history():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        SELECT
            log_time,
            temp_c,
            ph_level,
            turbidity_ntu
        FROM sensor_logs
        ORDER BY id DESC
        LIMIT 50
    """)

    data = cursor.fetchall()

    cursor.close()
    conn.close()

    return data
    