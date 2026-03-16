import serial
import wave
import time
import os

COM_PORT    = "COM3"
BAUD_RATE   = 115200
OUT_FILE    = "C:\\Users\\Debar\\Desktop\\recorded.wav"
SAMPLE_RATE = 16000

print(f"Opening {COM_PORT}...")
try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=30)
    print("Opened!")
except Exception as e:
    print(f"Failed: {e}")
    input("Press Enter to exit")
    exit()

time.sleep(2)
print("Waiting... press RESET on Nucleo\n")

pcm_data = bytearray()

while True:
    try:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if line:
            print(f">> {line}")

        if line == "READY":
            print("\nPress BLUE button and speak for 3 seconds!\n")

        elif line.startswith("PCM_START"):
            expected = int(line.split()[1])
            print(f"Receiving {expected} bytes...")
            pcm_data = bytearray()
            received = 0

            while received < expected:
                chunk = ser.read(min(512, expected - received))
                if chunk:
                    pcm_data.extend(chunk)
                    received += len(chunk)
                    pct = int(received / expected * 100)
                    bar = "#" * (pct // 5) + "." * (20 - pct // 5)
                    print(f"  [{bar}] {pct}%", end="\r")

            print(f"\nGot {received} bytes!")

        elif line == "PCM_END":
            if len(pcm_data) == 0:
                print("No data!")
                continue

            print("Saving WAV file...")
            with wave.open(OUT_FILE, 'wb') as wf:
                wf.setnchannels(1)
                wf.setsampwidth(2)
                wf.setframerate(SAMPLE_RATE)
                wf.writeframes(pcm_data)

            size_kb  = os.path.getsize(OUT_FILE) / 1024
            duration = len(pcm_data) / (SAMPLE_RATE * 2)
            print(f"\n✅ Saved: {OUT_FILE}")
            print(f"   Size    : {size_kb:.1f} KB")
            print(f"   Duration: {duration:.1f} seconds")
            print("\nGo play recorded.wav on your Desktop!")

            again = input("\nRecord again? (y/n): ").strip().lower()
            if again != 'y':
                break

    except KeyboardInterrupt:
        print("\nStopped")
        break

ser.close()
print("Done!")
input("Press Enter to exit")
