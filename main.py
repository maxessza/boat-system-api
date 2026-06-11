from fastapi import FastAPI
from pydantic import BaseModel
import pymysql
import os

app = FastAPI()

# ✅ ฟังก์ชันเชื่อม MySQL Railway
def get_connection():
    return pymysql.connect(
        host=os.getenv("MYSQLHOST"),
        user=os.getenv("MYSQLUSER"),
        password=os.getenv("MYSQLPASSWORD"),
        database=os.getenv("MYSQLDATABASE"),
        port=int(os.getenv("MYSQLPORT"))
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