"""
BiocharSense MQTT console: watch everything a device sends, and send it commands.

  pip install paho-mqtt

  # watch all devices (data, keepalive)
  python tools/mqtt_console.py watch

  # send a command to one device, then watch its reply
  python tools/mqtt_console.py cmd BS-1A2B3C start_cycle --batch KT-114-2026-10-21
  python tools/mqtt_console.py cmd BS-1A2B3C end_cycle
  python tools/mqtt_console.py cmd BS-1A2B3C get_status
  python tools/mqtt_console.py cmd BS-1A2B3C resend --from-seq 0
  python tools/mqtt_console.py cmd BS-1A2B3C set_config --set sample_s=30 --set upload_s=120

Defaults match the firmware's factory settings (broker.emqx.io:8883, TLS).
"""
import argparse
import json
import ssl
import sys
import time
import uuid

import paho.mqtt.client as mqtt

ROOT = "biocharsense"


def make_client(args, on_message):
    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"console-{uuid.uuid4().hex[:8]}")
    if args.tls:
        c.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    if args.user:
        c.username_pw_set(args.user, args.password)
    c.on_message = on_message
    c.connect(args.host, args.port, 60)
    return c


def pretty(msg):
    try:
        body = json.dumps(json.loads(msg.payload), indent=2)
    except ValueError:
        body = msg.payload.decode("utf-8", "replace")
    return f"\n[{time.strftime('%H:%M:%S')}] {msg.topic}\n{body}"


def watch(args):
    c = make_client(args, lambda cl, ud, msg: print(pretty(msg), flush=True))
    c.subscribe(f"{ROOT}/#", qos=1)
    print(f"Watching {ROOT}/# on {args.host}:{args.port} (Ctrl+C to stop)")
    c.loop_forever()


def send(args):
    payload = {"id": uuid.uuid4().hex[:8], "cmd": args.command}
    if args.batch:
        payload["batch_id"] = args.batch
    if args.from_seq is not None:
        payload["from_seq"] = args.from_seq
    if args.set:
        payload["config"] = {}
        for kv in args.set:
            key, _, value = kv.partition("=")
            payload["config"][key] = json.loads(value) if _is_json(value) else value   # numbers/booleans as JSON

    done = {"reply": False}

    def on_message(cl, ud, msg):
        print(pretty(msg), flush=True)
        try:
            reply = json.loads(msg.payload)
            if reply.get("type") == "ack" and reply.get("id") == payload["id"]:
                done["reply"] = True
        except ValueError:
            pass

    c = make_client(args, on_message)
    c.subscribe(f"{ROOT}/{args.device}/data", qos=1)       # replies are type "ack" on data
    c.loop_start()
    time.sleep(1)
    print(f"Sending to {ROOT}/{args.device}/cmd: {json.dumps(payload)}")
    c.publish(f"{ROOT}/{args.device}/cmd", json.dumps(payload), qos=1)
    deadline = time.time() + args.timeout
    while time.time() < deadline and not done["reply"]:
        time.sleep(0.5)
    c.loop_stop()
    if not done["reply"]:
        print(f"No reply within {args.timeout} s. The broker keeps the command; "
              "the device will answer when it next connects.")
        sys.exit(1)


def _is_json(v):
    try:
        json.loads(v)
        return True
    except ValueError:
        return False


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="broker.emqx.io")
    p.add_argument("--port", type=int, default=8883)
    p.add_argument("--no-tls", dest="tls", action="store_false")
    p.add_argument("--user", default="")
    p.add_argument("--password", default="")
    sub = p.add_subparsers(dest="mode", required=True)
    sub.add_parser("watch")
    c = sub.add_parser("cmd")
    c.add_argument("device")
    c.add_argument("command", choices=["start_cycle", "end_cycle", "get_status", "get_config",
                                       "set_config", "resend", "reboot"])
    c.add_argument("--batch")
    c.add_argument("--from-seq", type=int)
    c.add_argument("--set", action="append", help="key=value for set_config (repeatable)")
    c.add_argument("--timeout", type=int, default=60)
    args = p.parse_args()
    watch(args) if args.mode == "watch" else send(args)


if __name__ == "__main__":
    main()
