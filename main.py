from fastapi import FastAPI
from pydantic import BaseModel
from datetime import datetime
import pymysql

app = FastAPI()

# ✅ เชื่อม MySQL
conn = pymysql.connect(
    host="localhost",
    user="root",
    password="123456M@x",
    database="boat_system"
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

    cursor = conn.cursor()

    # 🔥 บันทึก sensor_logs
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

    # 🔥 คำนวณ vector น้ำ
    flow_v_lat = data.latitude * 0.0001
    flow_v_lng = data.longitude * 0.0001

    # 🔥 คาดการณ์ตำแหน่ง
    predicted_lat = data.latitude + flow_v_lat * 100
    predicted_lng = data.longitude + flow_v_lng * 100

    # 🔥 บันทึก drift_predictions
    sql2 = """
    INSERT INTO drift_predictions
    (log_time, start_lat, start_lng, end_lat, end_lng, flow_v_lat, flow_v_lng, predicted_lat, predicted_lng)
    VALUES (NOW(), %s, %s, %s, %s, %s, %s, %s, %s)
    """

    cursor.execute(sql2, (
    data.latitude,     # start_lat
    data.longitude,    # start_lng
    predicted_lat,     # end_lat
    predicted_lng,     # end_lng
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