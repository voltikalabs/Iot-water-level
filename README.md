# IoT Water Level Monitoring & Pump Automation

An IoT-based water tank monitoring and pump automation system using **Arduino Nano** as the water level detector and **ESP32** as the main controller and MQTT gateway.

---

# Features

- Real-time water tank level monitoring
- Automatic water tank pump control
- Automatic booster pump control
- MQTT-based communication
- Local and internet-based monitoring & control
- Secure MQTT connection via WSS/TLS
- Integrated with Mosquitto MQTT Broker on Raspberry Pi

---

# System Overview

## 1. Water Level Detection

Arduino Nano reads the water level sensor data and sends the information to ESP32 via serial communication.

ESP32 processes the received data and controls the pumps automatically.

---

## 2. Water Tank Pump Automation

The tank filling pump operates automatically based on water level percentage.

| Tank Level | Pump Status |
|------------|-------------|
| `<= 15%`  | Pump ON |
| `>= 99%`  | Pump OFF |

This mechanism ensures the tank always maintains sufficient water availability.

---

## 3. Booster Pump Automation

The booster pump is used for water distribution to faucets.

### When Faucet is Opened
- Flow switch detects water flow
- ESP32 waits for `6 seconds`
- Booster pump turns ON automatically

### When Faucet is Closed
- Flow switch detects no water flow
- ESP32 waits for `2 seconds`
- Booster pump turns OFF automatically

The delay helps prevent rapid pump switching.

---

# System Architecture

```text
Internet Client
(PC / Smartphone / ESP32 Cloud)
        │
   WSS : 443 (TLS)
        │
mqtt.voltikalabs.web.id
        │
  Cloudflare Tunnel
        │
 Raspberry Pi
 (Mosquitto Broker)
        │
   TCP : 1883 (LAN)
        │
   ESP32 / Local PC
