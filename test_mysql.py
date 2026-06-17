import pymysql

try:
    conn = pymysql.connect(
        host="localhost",
        user="root",
        password="123456M@x",
        database="boat_system"
    )

    print("Connected Successfully!")

except Exception as e:
    print("Error:", e)