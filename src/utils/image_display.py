import serial
import base64
import os
import re
from PIL import Image
import numpy as np

# ================= USER INPUT =================
SERIAL_PORT = input("Enter Serial Port (e.g., COM22): ").strip()
BAUD_RATE   = int(input("Enter Baud Rate (e.g., 115200): ").strip())

WIDTH  = int(input("Enter Frame Width (e.g., 96): ").strip())
HEIGHT = int(input("Enter Frame Height (e.g., 96): ").strip())

OUTPUT_DIR = input("Enter Output Folder (default: frames): ").strip()
if OUTPUT_DIR == "":
    OUTPUT_DIR = "frames"

RGB888_SIZE = WIDTH * HEIGHT * 3

os.makedirs(OUTPUT_DIR, exist_ok=True)
# ==============================================

print(f"\n Listening on {SERIAL_PORT} @ {BAUD_RATE}")
print(f"   Expected frame size: {RGB888_SIZE} bytes ({WIDTH}x{HEIGHT} RGB888)")
print(f"   Saving to: ./{OUTPUT_DIR}/\n")

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)

buffer = ""
frame_count = 0

# Markers exactly as printed by the embedded code
START_MARKER = "--- RGB888_START_ ---"
END_MARKER   = "--- RGB888_END_ ---"

def save_image(rgb888_bytes, frame_id):
    """Convert raw RGB888 bytes → PIL Image → upscale 4x → save PNG."""
    arr   = np.frombuffer(rgb888_bytes, dtype=np.uint8).reshape((HEIGHT, WIDTH, 3))
    image = Image.fromarray(arr, "RGB")
    image = image.resize((WIDTH * 4, HEIGHT * 4), Image.NEAREST)

    filename = f"frame_{frame_id:05d}.png"
    path = os.path.join(OUTPUT_DIR, filename)
    image.save(path)
    print(f" Saved {path}")

try:
    while True:
        # ---------- read whatever is available ----------
        waiting = ser.in_waiting or 1
        raw = ser.read(waiting)
        if not raw:
            continue
        buffer += raw.decode(errors="ignore")

        # ---------- wait until we have BOTH markers ----------
        start_idx = buffer.find(START_MARKER)
        if start_idx == -1:
            buffer = buffer[-(len(START_MARKER)):]
            continue

        end_idx = buffer.find(END_MARKER, start_idx + len(START_MARKER))
        if end_idx == -1:
            buffer = buffer[start_idx:]
            continue

        # ---------- extract everything between the two markers ----------
        payload_start = start_idx + len(START_MARKER)
        payload_end   = end_idx
        payload       = buffer[payload_start:payload_end]

        buffer = buffer[end_idx + len(END_MARKER):]

        # ---------- parse metadata ----------
        meta = {}
        lines = payload.strip().split("\n")
        b64_parts = []

        for line in lines:
            line = line.strip()
            if ":" in line and not any(c in line for c in "+/="):
                key, _, val = line.partition(":")
                meta[key] = val
                print(f"    {key}: {val}")
            else:
                b64_parts.append(line)

        # ---------- clean base64 ----------
        b64_data = "".join(b64_parts).strip()
        b64_data = re.sub(r"\s+", "", b64_data)

        remainder = len(b64_data) % 4
        if remainder:
            b64_data = b64_data[:len(b64_data) - remainder]

        if len(b64_data) == 0:
            print("  Empty base64 payload — skipping frame")
            continue

        # ---------- decode ----------
        try:
            rgb888 = base64.b64decode(b64_data)
        except Exception as e:
            print(f"  base64 decode error: {e} — skipping frame")
            continue

        # ---------- size check ----------
        if len(rgb888) != RGB888_SIZE:
            print(f"  Size mismatch: got {len(rgb888)} bytes, expected {RGB888_SIZE} — skipping")
            continue

        # ---------- save ----------
        frame_count += 1
        print(f" Frame {frame_count}: {len(rgb888)} bytes decoded OK")
        save_image(rgb888, frame_count)

except KeyboardInterrupt:
    print(f"\n Stopped — {frame_count} frames saved to ./{OUTPUT_DIR}/")
finally:
    ser.close()
