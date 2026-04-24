#!/usr/bin/env python3
import asyncio
import json
import random
import time
import paho.mqtt.client as mqtt
from aiohttp import web

# --- Configuration ---
# Set these to match your ESP32's config.json
MQTT_BROKER = "10.0.1.101"
MQTT_PORT = 1883
MQTT_NAME = "GuiboSolar"  # Must match mqtt_name in config.json

# Simulation Constants
MAX_SOLAR       = 2300.0   # W (Peak production)
BASE_LOAD_MIN   = 350.0    # W
BASE_LOAD_MAX   = 2450.0    # W
NOISE_AMPLITUDE = 10.0     # W
EQUIPMENT_MAX   = 2300.0   # W (Matches equipment_max_power)

# --- Shared State ---
class SimulationState:
    def __init__(self):
        self.solar_production = 1000.0
        self.solar_target = 500.0
        self.base_load = 400.0
        self.extra_load = 0.0
        self.equipment_power = 0.0  # Feedback from ESP32
        self.grid_power = 0.0
        self.last_update = time.time()
        
        self.scenario_timer = 0
        self.microwave_timer = 0
        self.microwave_active = False

state = SimulationState()

# --- MQTT Feedback ---
def on_connect(client, userdata, flags, rc):
    print(f"Connected to MQTT broker with result code {rc}")
    # Subscribe to the feedback topic from ESP32
    topic = f"{MQTT_NAME}/equipment_power"
    client.subscribe(topic)
    print(f"Subscribed to {topic}")

def on_message(client, userdata, msg):
    try:
        val = float(msg.payload.decode())
        if val != state.equipment_power:
            print(f"\n[Feedback] ESP32 is now injecting: {val}W")
        state.equipment_power = val
    except Exception as e:
        print(f"MQTT Error: {e}")

# --- Scenarios ---
SCENARIOS = [
    ("STABLE_SUN",  1200.0,  30,  60),
    ("FULL_SUN",    1600.0,  40,  80),
    ("PASSING_CLOUD", 400.0, 10,  20),
    ("VERY_CLOUDY",  150.0,  30,  60),
    ("STORM",         50.0,  20,  40),
    ("DUSK",         100.0,  30,  50),
    ("HIGH_HOUSE_LOAD", 1200.0, 20, 40),
]

async def simulation_loop():
    print("Starting simulation loop...")
    while True:
        try:
            # 1. Handle Scenarios
            if state.scenario_timer <= 0:
                name, target, mn, mx = random.choice(SCENARIOS)
                state.solar_target = target
                state.scenario_timer = random.randint(mn, mx)
                print(f"\n>>> SCENARIO: {name} | Target Solar: {target}W | Duration: {state.scenario_timer}s")
                
                # Microwave chance
                if not state.microwave_active and random.random() < 0.3:
                    state.microwave_active = True
                    state.microwave_timer = random.randint(5, 15)
                    state.extra_load = 1200.0
                    print("!!! MICROWAVE ON (+1200W)")

            # 2. Solar Slew (Smooth transitions)
            diff = state.solar_target - state.solar_production
            slew = 50.0 # W per second
            if abs(diff) < slew:
                state.solar_production = state.solar_target
            else:
                state.solar_production += slew if diff > 0 else -slew

            # 3. Base Load drift
            state.base_load += random.uniform(-5, 5)
            state.base_load = max(BASE_LOAD_MIN, min(BASE_LOAD_MAX, state.base_load))

            # 4. Microwave timer
            if state.microwave_active:
                state.microwave_timer -= 1
                if state.microwave_timer <= 0:
                    state.microwave_active = False
                    state.extra_load = 0.0
                    print("!!! MICROWAVE OFF")

            # 5. Calculate Grid Power (The magic formula)
            # Grid = (House consumption + Heater) - Solar
            noise = random.uniform(-NOISE_AMPLITUDE, NOISE_AMPLITUDE)
            state.grid_power = (state.base_load + state.extra_load + state.equipment_power) - state.solar_production + noise

            state.scenario_timer -= 1
            
            # Debug summary
            print(f"Grid: {state.grid_power:7.1f}W | Solar: {state.solar_production:6.1f}W | House: {state.base_load+state.extra_load:6.1f}W | Heater: {state.equipment_power:6.1f}W", end='\r')

        except Exception as e:
            print(f"Loop Error: {e}")

        await asyncio.sleep(1)

# --- HTTP Server (Fake Shelly) ---
async def handle_status(request):
    data = {
        "emeters": [{
            "power": state.grid_power,
            "reactive": 0.0,
            "voltage": 232.1,
            "is_valid": True,
            "total": 1234.56
        }],
        "fs_free": 123456
    }
    return web.json_response(data)

async def start_http_server():
    app = web.Application()
    app.router.add_get('/status', handle_status)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '0.0.0.0', 80)
    print("HTTP Server (Fake Shelly) listening on port 80")
    await site.start()

# --- Main ---
async def main():
    # Setup MQTT
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_start()
    except Exception as e:
        print(f"Could not connect to MQTT: {e}. Feedback loop will be broken.")

    # Run everything
    await asyncio.gather(
        simulation_loop(),
        start_http_server()
    )

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nSimulation stopped.")
