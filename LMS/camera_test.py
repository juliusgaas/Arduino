from flask import Flask, render_template, Response, request
import cv2, os, time
import serial
import sys
import time
import threading
serial_lock = threading.Lock()

app = Flask(__name__)

# OPEN SERIAL ONCE
ser = serial.Serial("COM6", 9600, timeout=2)
time.sleep(2)  # wait Arduino reset

camera = cv2.VideoCapture(0)
camera.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
camera.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

face_cascade = cv2.CascadeClassifier(
    "haarcascade_frontalface_default.xml"
)

CAPTURE_DELAY = 3
last_capture_time = 0

def gen_frames():
    global last_capture_time

    while True:
        success, frame = camera.read()
        if not success:
            continue

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        faces = face_cascade.detectMultiScale(gray, 1.3, 5)

        # draw boxes
        for (x, y, w, h) in faces:
            cv2.rectangle(frame, (x, y),
                          (x + w, y + h), (0, 255, 0), 2)

        now = time.time()
        if len(faces) > 0 and now - last_capture_time > CAPTURE_DELAY:
            last_capture_time = now

            # clear old face images
            for f in os.listdir("static/faces"):
                os.remove("static/faces/" + f)

            # save full frame
            cv2.imwrite("static/capture.jpg", frame)

            # save each face separately
            for i, (x, y, w, h) in enumerate(faces, start=1):
                face_img = frame[y:y+h, x:x+w]
                cv2.imwrite(f"static/faces/face_{i}.jpg", face_img)

        cv2.putText(frame, f"Faces detected: {len(faces)}",
                    (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    1, (255, 0, 0), 2)

        ret, buffer = cv2.imencode(".jpg", frame)
        frame = buffer.tobytes()

        yield (b"--frame\r\n"
               b"Content-Type: image/jpeg\r\n\r\n" +
               frame + b"\r\n")

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/video")
def video():
    return Response(gen_frames(),
        mimetype="multipart/x-mixed-replace; boundary=frame")

@app.route("/control")
def control():
    cmd = request.args.get("cmd")

    if not cmd:
        return "NO_CMD", 400

    try:
        with serial_lock:
            ser.write((cmd + "\n").encode())
            time.sleep(0.05)

            if ser.in_waiting:
                return ser.readline().decode().strip()

        return "OK"

    except Exception as e:
        return str(e), 500
    
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
