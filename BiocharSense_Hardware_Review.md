# BiocharSense — Hardware & Integration Overview (For Review)

**Purpose:** Quick review of the hardware list and integration approach for BiocharSense before we commit to the production build. Looking for your confirmation that this is the right direction, or any adjustments before we place orders.

---

## What the device does

A small battery-powered IoT box that sits next to a biochar Kontiki kiln, measures the temperature continuously using a probe inserted into the biochar, and sends the data to our cloud over WiFi. After every burn cycle the battery can be recharged using a Type-C charger or a power bank.

---

## Hardware list

| # | Component | Purpose |
|---|---|---|
| 1 | **ESP32 microcontroller** | The "brain" — reads temperature, connects to WiFi, sends data, handles remote firmware updates |
| 2 | **MAX31855 thermocouple amplifier** | Reads the millivolt signal from the temperature probe and converts it to a digital number the ESP32 can use |
| 3 | **Industrial K-Type thermocouple probe (Inconel sheath)** | The actual probe inserted into the kiln. Rated up to 1100 °C, built to last years in the harsh biochar environment |
| 4 | **Thermowell (Inconel sleeve around the probe)** | Protects the probe from corrosive pyrolysis gases. Allows probe replacement without opening the kiln |
| 5 | **K-Type compensation extension cable (10 m)** | Connects the probe (near the kiln) to the electronics box (placed safely 5–10 m away) |
| 6 | **18650 rechargeable battery + 5V Type-C charging module** | Power source. Recharges from any USB-C charger or power bank |
| 7 | **Bulk capacitor + small protection components** | Keeps power stable, prevents resets during WiFi transmission bursts |
| 8 | **Weatherproof outdoor enclosure (polycarbonate, IP65)** | Protects all electronics from dust, rain, and outdoor heat |
| 9 | **Custom PCB / perfboard** | Holds everything together cleanly inside the enclosure |

---

## How it will work — step by step

1. Operator switches ON the device (one button on the enclosure).
2. Device boots, connects to the on-site WiFi automatically using credentials saved from earlier.
   - If WiFi is new or password has changed, operator can use their phone to update WiFi from a captive portal page (no laptop needed).
3. ESP32 reads temperature from the probe (every few seconds).
4. Readings are buffered locally on the device.
5. At fixed intervals (planned: every 30 minutes per your guidance), the device wakes up the WiFi, sends the batch of readings to the cloud (MQTT), then turns WiFi off.
   - This keeps battery life long (5+ hours per charge, potentially full day depending on burn duration).
6. Data is visible in real-time on our cloud dashboard / MQTT broker.
7. Firmware updates can be pushed remotely over the air — no need to physically retrieve the device.

---

## Connection flow / integration

```
   KILN (hot zone, up to 800 °C)            SAFE ZONE (5–10 m away)
   +--------------------------+
   |    Kontiki biochar       |
   |                          |              +-------------------------+
   |      [thermowell]        |              |  Weatherproof enclosure |
   |      [probe tip]         |              | +---------------------+ |
   |            |             |   10 m       | | MAX31855            | |
   |            +-- probe ----+--- extension-+-+ thermocouple chip   | |
   |                          |   cable      | +----------+----------+ |
   |                          |              |            |            |
   +--------------------------+              |            v            |
                                             | +---------------------+ |
                                             | | ESP32 microcontroller| |
                                             | |  (WiFi + MQTT)       | |
                                             | +----------+----------+ |
                                             |            |            |
                                             |            v            |
                                             | +---------------------+ |
                                             | | 18650 Battery +      | |
                                             | | Charging module      | |
                                             | | (USB-C input)        | |
                                             | +---------------------+ |
                                             |                         |
                                             |  Type-C port for        |
                                             |  recharging from        |
                                             |  power bank / charger   |
                                             +------------+------------+
                                                          |
                                                       WiFi -> MQTT cloud
```

**Key points of the integration:**

1. Only the probe + thermowell sits near the kiln. Everything else is in a safe enclosure 5–10 m away.
2. The 10 m extension cable handles the distance so the electronics never see kiln heat.
3. The enclosure is weatherproof so it can be mounted in the open at the site.
4. The battery + Type-C module means no external power needed during a burn — recharge between burns.
5. WiFi is used in short bursts (per your batched-upload guidance) so battery lasts the full burn cycle without interruption.

---

## What I'd like you to confirm

1. **Hardware list looks right?** Any addition / substitution you'd suggest?
2. **Approach for the probe + thermowell + extension cable** is correct for a Kontiki environment?
3. **Battery + Type-C charging** is acceptable, or do you want me to consider mains power (with a UPS backup) for sites that have an AC outlet?
4. **The batched-upload + WiFi-off cycle** that you mentioned earlier — confirming I should implement that before the production build (vs. the current always-on prototype firmware)?

Will wait for your green light before placing orders.

Best regards,
[Your name]
