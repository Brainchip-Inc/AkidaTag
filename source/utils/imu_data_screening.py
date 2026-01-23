import serial
import matplotlib.pyplot as plt
from collections import deque

PORT = "/dev/ttyUSB0"   # change if needed
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)

acc_x = deque(maxlen=200)
acc_y = deque(maxlen=200)
acc_z = deque(maxlen=200)

gyr_x = deque(maxlen=200)
gyr_y = deque(maxlen=200)
gyr_z = deque(maxlen=200)

plt.ion()
fig, (ax1, ax2) = plt.subplots(2, 1)

while True:
    line = ser.readline().decode(errors="ignore").strip()
    if not line:
        continue

    try:
        ax, ay, az, gx, gy, gz = map(float, line.split(","))
    except ValueError:
        continue

    acc_x.append(ax)
    acc_y.append(ay)
    acc_z.append(az)

    gyr_x.append(gx)
    gyr_y.append(gy)
    gyr_z.append(gz)

    ax1.clear()
    ax1.plot(acc_x, label="Ax (g)")
    ax1.plot(acc_y, label="Ay (g)")
    ax1.plot(acc_z, label="Az (g)")
    ax1.set_ylim(-2, 2)
    ax1.legend()
    ax1.set_title("Accelerometer")

    ax2.clear()
    ax2.plot(gyr_x, label="Gx (dps)")
    ax2.plot(gyr_y, label="Gy (dps)")
    ax2.plot(gyr_z, label="Gz (dps)")
    ax2.set_ylim(-50, 50)
    ax2.legend()
    ax2.set_title("Gyroscope")

    plt.pause(0.01)
