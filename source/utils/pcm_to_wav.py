import sys
import os
import wave
import numpy as np

# -------------------------
# CONFIG
# -------------------------
SAMPLE_RATE = 16000   # change to 48000 if required

# -------------------------
# ARGUMENT CHECK
# -------------------------
if len(sys.argv) != 3:
    print("Usage:")
    print("  python pcm_txt_to_wav.py <input_pcm.txt> <output_name>")
    sys.exit(1)

input_file = sys.argv[1]
output_name = sys.argv[2]

# -------------------------
# INPUT VALIDATION
# -------------------------
if not input_file.lower().endswith(".txt"):
    print("ERROR: Input PCM file must be a .txt file")
    sys.exit(1)

if not os.path.isfile(input_file):
    print(f"ERROR: Input file not found: {input_file}")
    sys.exit(1)

if "." in output_name:
    print("ERROR: Output name should NOT contain an extension")
    print("Example: audio_out (not audio_out.wav)")
    sys.exit(1)

output_file = output_name + ".wav"

# -------------------------
# READ PCM DATA
# -------------------------
with open(input_file, "r") as f:
    raw = f.read().strip()

tokens = raw.split(",")

samples = []
for t in tokens:
    t = t.strip()
    if not t:
        continue
    try:
        v = int(t)
        v = max(-32768, min(32767, v))  # clamp int16
        samples.append(v)
    except ValueError:
        pass  # ignore corrupted tokens

if not samples:
    print("ERROR: No valid PCM samples found")
    sys.exit(1)

print(f"Recovered {len(samples)} PCM samples")

# -------------------------
# WRITE WAV
# -------------------------
pcm = np.array(samples, dtype=np.int16)

with wave.open(output_file, "wb") as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(pcm.tobytes())

print(f"WAV file generated successfully: {output_file}")
