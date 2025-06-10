import os
import time
import serial
import glob
import sys
import subprocess

# -------------------- CONFIG --------------------
hex_file = ".pio/build/teensy41/firmware.hex"
teensy_cli = os.path.expanduser("~/.platformio/packages/tool-teensy/teensy_loader_cli")

# ------------------ CHECK HEX ------------------
if not os.path.exists(hex_file):
    print(f"[RESET] Error: firmware not found at {hex_file}")
    sys.exit(1)

# ------------------ CHECK CLI ------------------
if not os.path.exists(teensy_cli):
    print(f"[RESET] Error: teensy_loader_cli not found at {teensy_cli}")
    print("[RESET] Make sure Teensy platform is installed in PlatformIO")
    sys.exit(1)

# # ------------------ FIND PORT ------------------
# # Try both possible port patterns
# ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*")
# if not ports:
#     print("[RESET] No Teensy serial port found!")
#     print("[RESET] Available ports:")
#     all_ports = glob.glob("/dev/cu.*") + glob.glob("/dev/tty.*")
#     for port in all_ports:
#         print(f"[RESET]   {port}")
#     print("[RESET] Make sure Teensy is connected and running your code")
#     sys.exit(1)

# ------------------ FIND PORT INCLUDING WINDOWS COM PORTS ------------------
# Try both possible port patterns
ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*") + glob.glob("COM*")
if not ports:
    print("[RESET] No Teensy serial port found!")
    print("[RESET] Available ports:")
    all_ports = glob.glob("/dev/cu.*") + glob.glob("/dev/tty.*") + glob.glob("COM*")
    for port in all_ports:
        print(f"[RESET]   {port}")
    print("[RESET] Make sure Teensy is connected and running your code")
    sys.exit(1)

serial_port = ports[0]
print(f"[RESET] Using serial port: {serial_port}")

# ----------------- SEND REBOOT ------------------
try:
    print("[RESET] Connecting to Teensy...")
    with serial.Serial(port=serial_port, baudrate=115200, timeout=2) as ser:
        # Allow connection to stabilize
        time.sleep(0.5)
        
        # Clear any pending data
        ser.reset_input_buffer()
        
        print("[RESET] Sending REBOOT_BOOTLOADER command...")
        ser.write(b"REBOOT_BOOTLOADER\n")
        ser.flush()
        
        # Wait for response from Teensy
        response_received = False
        start_time = time.time()
        
        while time.time() - start_time < 3:
            if ser.in_waiting > 0:
                try:
                    response = ser.readline().decode('utf-8', errors='ignore').strip()
                    if response:
                        print(f"[RESET] Teensy: {response}")
                        if "Entering bootloader" in response or "REBOOT" in response:
                            response_received = True
                            break
                except Exception:
                    pass
            time.sleep(0.1)
        
        if response_received:
            print("[RESET] ✓ Reboot command confirmed")
        else:
            print("[RESET] ⚠ No response from Teensy (proceeding anyway)")
            
except Exception as e:
    print(f"[RESET] Failed to send reboot command: {e}")
    print("[RESET] ⚠ Proceeding with upload anyway...")

# Wait for bootloader to be ready
print("[RESET] Waiting for bootloader to fully initialize May need to unplug and replug usb", end="", flush=True)
for i in range(20):  # 8 seconds worth of dots (0.1s each)
    time.sleep(0.1)
    print(".", end="", flush=True)

print(" ✓")
print("[RESET] Bootloader should now be ready for programming...")

# ------------------ UPLOAD HEX (WITH RETRY) ------------------
print(f"[UPLOAD] Uploading {hex_file} via teensy_loader_cli...")

for attempt in range(2):  # Try up to 2 times
    try:
        # Use subprocess for better error handling  
        cmd = [teensy_cli, "-mmcu=TEENSY41", "-w", hex_file]
        print(f"[UPLOAD] Attempt {attempt + 1}: {' '.join(cmd)}")
        
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        
        if result.returncode == 0:
            print("[UPLOAD] ✓ Upload successful!")
            if result.stdout.strip():
                print(f"[UPLOAD] Output: {result.stdout.strip()}")
            break  # Success - exit retry loop
        else:
            print(f"[UPLOAD] ⚠ Attempt {attempt + 1} failed")
            if attempt == 0:  # First attempt failed
                print("[UPLOAD] This is normal - trying again in 2 seconds...")
                time.sleep(2)  # Wait before retry
            else:  # Second attempt failed
                print("[UPLOAD] ✗ Both attempts failed!")
                if result.stderr.strip():
                    print(f"[UPLOAD] Error: {result.stderr.strip()}")
                if result.stdout.strip():
                    print(f"[UPLOAD] Output: {result.stdout.strip()}")
                sys.exit(1)
            
    except subprocess.TimeoutExpired:
        print(f"[UPLOAD] ✗ Attempt {attempt + 1} timed out")
        if attempt == 1:  # Last attempt
            sys.exit(1)
    except Exception as e:
        print(f"[UPLOAD] ✗ Attempt {attempt + 1} failed with exception: {e}")
        if attempt == 1:  # Last attempt
            sys.exit(1)

print("[UPLOAD] Success! Firmware uploaded successfully!")