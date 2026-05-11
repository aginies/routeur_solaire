#!/usr/bin/env python3
"""
Unified launcher for the complete Routeur Solaire simulation environment.

Starts both:
  - simulate_solar.py   (MQTT events + fake Shelly HTTP on port 80)
  - simulate_weather.py (Open-Meteo-compatible weather on port 9090)

Cross-syncs solar production ↔ weather radiation so they feel consistent.

Usage:
    python3 run_simulation.py
    python3 run_simulation.py --weather-port 9090 --solar-port 80
    python3 run_simulation.py --weather-mode clear --speed-up 10
    python3 run_simulation.py --help
"""

import argparse
import asyncio
import importlib.util
import os
import random
import time

from aiohttp import web
import paho.mqtt.client as mqtt

# === Configuration ===
DEFAULT_MQTT_BROKER = "10.0.1.101"
DEFAULT_MQTT_PORT = 1883
MQTT_NAME = "GuiboSolar"

DEFAULT_SOLAR_PORT = 80
DEFAULT_WEATHER_PORT = 9090

MAX_SOLAR = 2300.0
BASE_LOAD_MIN = 350.0
BASE_LOAD_MAX = 2450.0
NOISE_AMPLITUDE = 10.0


# === Shared State ===
class SimulationState:
    def __init__(self):
        self.solar_production = 1000.0
        self.solar_target = 500.0
        self.base_load = 400.0
        self.extra_load = 0.0
        self.equipment_power = 0.0
        self.grid_power = 0.0
        self.scenario_timer = 0
        self.microwave_timer = 0
        self.microwave_active = False
        self.weather_mode = "clear"

state = SimulationState()


# === Scenarios (solar) ===
SCENARIOS = [
    ("MORNING_CLOUD",    300.0,  120,  180),
    ("BUILDING_SUN",     900.0,   90,  120),
    ("FULL_SUN",        1800.0,   60,  120),
    ("PEAK_SUN",        2200.0,   30,   60),
    ("PASSING_CLOUD",    400.0,   10,   20),
    ("AFTERNOON_CLEAR", 1600.0,   60,   90),
    ("OVERCAST",         150.0,   30,   60),
    ("STORM",             50.0,   20,   40),
    ("DUSK",             100.0,   30,   50),
    ("HIGH_HOUSE_LOAD", 1200.0,   20,   40),
]


# ============================================================
# MQTT
# ============================================================

def on_mqtt_connect(client, userdata, flags, rc):
    print(f"    [mqtt] Connected (rc={rc})")
    topic = f"{MQTT_NAME}/equipment_power"
    client.subscribe(topic)
    print(f"    [mqtt] Listening on {topic}")

def on_mqtt_message(client, userdata, msg):
    try:
        val = float(msg.payload.decode())
        if abs(val - state.equipment_power) > 0.1:
            print(f"    [mqtt] ESP32 feedback: equipment_power = {val}W")
        state.equipment_power = val
    except Exception as e:
        print(f"    [mqtt] Error: {e}")


# ============================================================
# Solar simulation
# ============================================================

def _mode_for_solar(solar_w: float) -> str:
    """Map solar power target to a consistent weather mode."""
    if solar_w > 1500:
        return "clear"
    if solar_w > 800:
        return "partly_cloudy"
    if solar_w > 300:
        return "overcast"
    return "storm"

async def simulation_loop():
    print("    [solar] Starting simulation loop...")
    while True:
        try:
            # Scenario switch
            if state.scenario_timer <= 0:
                name, target, mn, mx = random.choice(SCENARIOS)
                state.solar_target = target
                state.scenario_timer = random.randint(mn, mx)
                state.weather_mode = _mode_for_solar(target)
                print(f"\n    [solar] >>> {name} -> {target:.0f}W | {state.scenario_timer}s")

                if not state.microwave_active and random.random() < 0.3:
                    state.microwave_active = True
                    state.microwave_timer = random.randint(5, 15)
                    state.extra_load = 1200.0
                    print(f"    [solar]   Microwave ON (+{state.extra_load}W)")

            # Slew solar production toward target
            diff = state.solar_target - state.solar_production
            slew = 50.0
            if abs(diff) < slew:
                state.solar_production = state.solar_target
            else:
                state.solar_production += slew if diff > 0 else -slew

            # Load drift
            state.base_load += random.uniform(-5, 5)
            state.base_load = max(BASE_LOAD_MIN, min(BASE_LOAD_MAX, state.base_load))

            # Microwave countdown
            if state.microwave_active:
                state.microwave_timer -= 1
                if state.microwave_timer <= 0:
                    state.microwave_active = False
                    state.extra_load = 0.0
                    print(f"    [solar]   Microwave OFF")

            # Grid formula
            noise = random.uniform(-NOISE_AMPLITUDE, NOISE_AMPLITUDE)
            state.grid_power = (state.base_load + state.extra_load + state.equipment_power) - state.solar_production + noise
            state.scenario_timer -= 1

            print(f"    [grid] grid={state.grid_power:7.1f}W | solar={state.solar_production:6.1f}W | house={state.base_load+state.extra_load:7.1f}W | heater={state.equipment_power:6.1f}W", end='\r', flush=True)

        except Exception as e:
            print(f"    [solar] Error: {e}")

        await asyncio.sleep(1)


# ============================================================
# Fake Shelly HTTP
# ============================================================

async def handle_shelly_status(request):
    return web.json_response({
        "emeters": [{
            "power": state.grid_power,
            "reactive": 0.0,
            "voltage": 232.1,
            "is_valid": True,
            "total": 1234.56,
        }],
    })

async def start_shelly_server(port: int):
    app = web.Application()
    app.router.add_get('/status', handle_shelly_status)
    app.router.add_get('/', handle_shelly_status)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '0.0.0.0', port)
    await site.start()
    print(f"    [shelly] Fake Shelly HTTP on http://0.0.0.0:{port}/status")


# ============================================================
# Cross-sync loop: solar -> weather radiation
# ============================================================

async def cross_sync_loop(weather_state_obj):
    """Continuously update the weather simulator's solar override."""
    while True:
        if state.solar_production > 0:
            weather_state_obj.solar_production_sync = state.solar_production
        await asyncio.sleep(0.5)


# ============================================================
# Main — launch everything together
# ============================================================

async def main(
    mqtt_broker: str = DEFAULT_MQTT_BROKER,
    mqtt_port: int = DEFAULT_MQTT_PORT,
    solar_port: int = DEFAULT_SOLAR_PORT,
    weather_port: int = DEFAULT_WEATHER_PORT,
):
    # --- MQTT ---
    try:
        client = mqtt.Client()
        client.on_connect = on_mqtt_connect
        client.on_message = on_mqtt_message
        client.connect(mqtt_broker, mqtt_port, 60)
        client.loop_start()
        print(f"    [mqtt] Broker: {mqtt_broker}:{mqtt_port}")
    except Exception as e:
        print(f"    [mqtt] Connect failed: {e} (feedback loop will be broken)")

    # --- Load weather module ---
    script_dir = os.path.dirname(os.path.abspath(__file__))
    weather_py = os.path.join(script_dir, "simulate_weather.py")
    spec = importlib.util.spec_from_file_location("simulate_weather", weather_py)
    wmod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(wmod)

    ws = wmod.weather_state
    ws.mode = state.weather_mode
    ws.speed_up = 1.0
    ws.season = 4.5
    ws.solar_production_sync = state.solar_production

    # --- Start weather update loop (in background) ---
    weather_update_task = asyncio.create_task(wmod.weather_update_loop())

    # --- Start weather HTTP server ---
    weather_app, _ = wmod.create_app(port=weather_port)
    weather_runner = web.AppRunner(weather_app)
    await weather_runner.setup()
    weather_site = web.TCPSite(weather_runner, '0.0.0.0', weather_port)
    await weather_site.start()
    print(f"    [weather] Open-Meteo-compatible server on http://0.0.0.0:{weather_port}/weather")
    print(f"               Regimes: {', '.join(wmod.WEATHER_REGIMES.keys())}")

    # --- Start Shelly HTTP server (in background) ---
    shelly_server_task = asyncio.create_task(start_shelly_server(solar_port))

    # --- Launch cross-sync loop ---
    sync_loop_task = asyncio.create_task(cross_sync_loop(ws))

    # --- Main simulation (blocking) ---
    await simulation_loop()


# ============================================================
# Entry point
# ============================================================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Unified Routeur Solaire Simulator")
    parser.add_argument("--mqtt-broker", type=str, default=DEFAULT_MQTT_BROKER)
    parser.add_argument("--mqtt-port", type=int, default=DEFAULT_MQTT_PORT)
    parser.add_argument("--solar-port", type=int, default=DEFAULT_SOLAR_PORT)
    parser.add_argument("--weather-port", type=int, default=DEFAULT_WEATHER_PORT, help="Port for weather mock server")
    parser.add_argument("--speed-up", type=float, default=1.0, help="Simulation time multiplier (0.1-100)")
    parser.add_argument("--weather-mode", type=str, default="clear", help="Initial weather regime")
    parser.add_argument("--season", type=float, default=4.5, help="Season (0=Jan, 6=May, 12=Jul)")
    parser.add_argument("--latitude", type=float, default=48.0)
    parser.add_argument("--longitude", type=float, default=2.3)
    args = parser.parse_args()

    print()
    print("=" * 72)
    print("  Routeur Solaire -- Unified Simulation")
    print("=" * 72)
    print(f"  Fake Shelly     : http://localhost:{args.solar_port}/status")
    print(f"  Weather server  : http://localhost:{args.weather_port}/weather")
    print(f"  MQTT broker     : {args.mqtt_broker}:{args.mqtt_port}")
    print("=" * 72)
    print()

    asyncio.run(main(
        mqtt_broker=args.mqtt_broker,
        mqtt_port=args.mqtt_port,
        solar_port=args.solar_port,
        weather_port=args.weather_port,
    ))
