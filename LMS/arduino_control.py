import serial
import sys
import time

cmd = sys.argv[1]  # 1 or 0

ser = serial.Serial('COM4', 9600, timeout=2)
time.sleep(2)

ser.write((cmd + '\n').encode())
time.sleep(0.5)

resp = ser.readline().decode().strip()
print(resp if resp else "NO_RESPONSE")

ser.close()