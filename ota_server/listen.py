import paho.mqtt.client as mqtt
import time

BROKER = "broker.emqx.io"
PORT = 1883

def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Connected rc={rc}")
    client.subscribe("varaha/#", qos=0)
    print("Subscribed to varaha/#")

def on_message(client, userdata, msg):
    print(f"  [{msg.topic}] {msg.payload.decode('utf-8', errors='replace')}")

c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
c.on_connect = on_connect
c.on_message = on_message
c.connect(BROKER, PORT, 60)
c.loop_start()
time.sleep(30)
c.loop_stop()
c.disconnect()
print("done")
