from fastapi import FastAPI
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
    return pymysql.connect(
        host="127.0.0.1",
        user="root",
        password="123456M@x",
        database="boat_system",
        port=3306,
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

    conn = get_connection()
    cursor = conn.cursor()

    # บันทึก sensor_logs
    sql1 = """
    INSERT INTO sensor_logs
    (boat_id, log_time, latitude, longitude, temp_c, ph_level, turbidity_ntu)
    VALUES (%s, NOW(), %s, %s, %s, %s, %s)
    """

    cursor.execute(sql1, (
        data.boat_id,
        data.latitude,
        data.longitude,
        data.temp_c,
        data.ph_level,
        data.turbidity_ntu
    ))

    # คำนวณเวกเตอร์น้ำ
    flow_v_lat = data.latitude * 0.0001
    flow_v_lng = data.longitude * 0.0001

    # คาดการณ์ตำแหน่ง
    predicted_lat = data.latitude + flow_v_lat * 100
    predicted_lng = data.longitude + flow_v_lng * 100

    # บันทึก drift_predictions
    sql2 = """
    INSERT INTO drift_predictions
    (log_time, start_lat, start_lng, end_lat, end_lng,
     flow_v_lat, flow_v_lng, predicted_lat, predicted_lng)
    VALUES (NOW(), %s, %s, %s, %s, %s, %s, %s, %s)
    """

    cursor.execute(sql2, (
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

    cursor.close()
    conn.close()

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

# ✅ ดูข้อมูล sensor_logs ล่าสุด 100 รายการ
@app.get("/logs")
def get_logs():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        SELECT *
        FROM sensor_logs
        ORDER BY log_id DESC
        LIMIT 100
    """)

    data = cursor.fetchall()

    cursor.close()
    conn.close()

    return data

# ✅ ดูข้อมูล prediction ล่าสุด 100 รายการ
@app.get("/predictions")
def get_predictions():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        SELECT *
        FROM drift_predictions
        ORDER BY predict_id DESC
        LIMIT 100
    """)

    data = cursor.fetchall()

    cursor.close()
    conn.close()

    return data


# ✅ ดูเส้นทางเรือทั้งหมด
@app.get("/path")
def get_path():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        SELECT latitude, longitude
        FROM sensor_logs
        ORDER BY log_id ASC
    """)

    data = cursor.fetchall()

    cursor.close()
    conn.close()

    return data


# ✅ ดูข้อมูลล่าสุด 1 รายการ (ใช้กับ Dashboard)
@app.get("/latest")
def get_latest():

    conn = get_connection()
    cursor = conn.cursor()

    cursor.execute("""
        SELECT *
        FROM sensor_logs
        ORDER BY log_id DESC
        LIMIT 1
    """)

    data = cursor.fetchone()

    cursor.close()
    conn.close()

    return data