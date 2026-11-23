"""
Trigger an OTA update over MQTT and wait for the device to reboot on the new
version.

The command is published RETAINED so a deep-sleeping device receives it on its
next wake window. Once the device reports the target version, the retained
command is cleared so it does not re-flash on every subsequent wake.

Usage:
  py trigger_ota.py <ota_url> <target_version> [--token TOKEN] [--timeout SEC]

Example:
  py trigger_ota.py http://192.168.110.85:8000/firmware-1.0.7.bin 1.0.7 --token s3cr3t
"""
import argparse
import json
import sys
import time

import paho.mqtt.client as mqtt

BROKER = "broker.emqx.io"
PORT = 1883
SUB_TOPIC = "varaha/Sub"        # device subscribes here (commands)
PUB_TOPIC = "varaha/sensor"     # device publishes telemetry/device-info here
DN = "TEMP"                     # canonical device-node tag (matches firmware)

parser = argparse.ArgumentParser()
parser.add_argument("url")
parser.add_argument("version")
parser.add_argument("--token", default="", help="OTA shared-secret token")
parser.add_argument("--timeout", type=int, default=120,
                    help="seconds to wait for the new version (raise for long sleep cycles)")
args = parser.parse_args()

found_version = None


def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Connected to MQTT (rc={rc})")
    client.subscribe(PUB_TOPIC, qos=0)


def on_message(client, userdata, msg):
    global found_version
    try:
        data = json.loads(msg.payload.decode("utf-8"))
        v = data.get("fw_version")
        if v:
            print(f"  [{msg.topic}] fw_version={v}")
            found_version = v
    except Exception:
        pass


c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_connect = on_connect
c.on_message = on_message
c.connect(BROKER, PORT, 60)
c.loop_start()
time.sleep(2)

cmd = {"DN": DN, "command": "ota", "url": args.url, "version": args.version}
if args.token:
    cmd["token"] = args.token

print(f">>> Publishing RETAINED to {SUB_TOPIC}: ota -> {args.url}")
c.publish(SUB_TOPIC, json.dumps(cmd), qos=1, retain=True)

deadline = time.time() + args.timeout
while time.time() < deadline:
    if found_version == args.version:
        print(f"\n*** SUCCESS *** device booted with fw_version={found_version}")
        # Clear the retained command so the device does not re-flash on next wake.
        c.publish(SUB_TOPIC, "", qos=1, retain=True)
        time.sleep(1)
        c.loop_stop()
        c.disconnect()
        sys.exit(0)
    time.sleep(1)

print(f"\n*** TIMEOUT *** target={args.version} last seen={found_version}")
print("The retained command remains queued; the device will apply it on its")
print("next wake window. Re-run with a larger --timeout to keep waiting.")
c.loop_stop()
c.disconnect()
sys.exit(1)
