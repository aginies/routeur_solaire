# Solar Power Diverter (C++ Version)

This project is a high-performance C++ implementation of a Photovoltaic (PV) Router, optimized for the ESP32 platform using the Arduino framework and PlatformIO.

## Project Origin

This C++ version is a **migration from the original MicroPython implementation for domotique**. The migration was performed by AI agents to achieve better performance, lower latency, and more robust execution on resource-constrained hardware.

## Key Features

- **High-Speed Diversion**: Implements a highly responsive power diversion algorithm (burst-fire/trame mode) to match excess solar production with a resistive load.
- **Multi-Source Monitoring**: Supports data acquisition from Shelly EM (via HTTP or MQTT) and JSY-MK-194 (via UART). Equipment power measurement via Shelly Plus 1PM (HTTP or MQTT).
- **Asynchronous Web Server**: Provides a rich web interface with a **sidebar-based configuration menu** for real-time monitoring, logging, and easy navigation between settings.
- **Phase Angle Calibration Wizard**: Includes a dedicated automated sweep tool (`/web_phase_cal`) to map the power curve of resistive loads when using phase-cutting mode, ensuring precise linear power control.
- **Improved Force Mode UI**: Clearly distinguishes between manual "Boost" and scheduled "Mode Forcé (Plage Horaire)" to avoid confusion.
- **Advanced Statistics**: Tracks daily/hourly energy usage with a **compact horizontal distribution bar** and historical data retention.
- **Safety & Protection**: Includes watchdog timers, temperature monitoring (Internal & SSR heatsink), and automatic fan control.
- **Weather-Aware Control**: Uses Open-Meteo cloud cover, solar radiation, sunrise, and sunset data to improve equipment decisions. When solar panel power and azimuth are configured, estimates real-time expected production to decide whether Equipment 2 can run. **Requires internet access** for Open-Meteo API calls.
- **Home Assistant Integration**: MQTT support with auto-discovery for easy integration into smart home systems.
- **Custom PCB Layout**: Includes a complete hardware design project in the `board/` directory with a KiCad-ready netlist for manufacturing a custom mainboard.

## Architecture

```
                                  Core 1 (Application), Priority 3
                                 ┌──────────────┐
                                 │  monitorTask │
                                 │ (8KB stack)  │
                                 └──────┬───────┘
                                        │ reads shared state
        ┌───────────────────────────────┼───────────────────────────────┐
        │                               │                               │
        ▼                               ▼                               ▼
  ┌────────────┐                 ┌──────────────┐                ┌──────────────┐
  │ GridSensor │                 │ SolarMonitor │                │ Incremental  │
  │ Service    │                 │ Safety       │                │ Controller   │
  │ HTTP/MQTT/ │                 │ PID Control  │                │ (delta/delta │
  │ JSY UART   │                 │ Eq2 logic    │                │  neg)        │
  └────────────┘                 └─────┬────────┘                └──────────────┘
                                       │
                           ┌───────────┼───────────┐
                           ▼           ▼           ▼
                    ┌───────────┐ ┌────────┐ ┌──────────┐
                    │ Eq2Mgr    │ │ Stats  │ │ History  │
                    │ (PAC/Pool │ │ (NVS + │ │ (PSRAM/  │
                    │  schedule)│ │  LFS)  │ │  LFS)    │
                    └───────────┘ └────────┘ └──────────┘
        
        ┌───────────────────────────────────────────────────────────────┐
        │  Core 1 (Application Core), Priority 5                        │
        │  ┌────────────────────────────────────────────────────────┐   │
        │  │  ControlStrategy (task depends on mode):               │   │
        │  │  burst  → burstControlTask                             │   │
        │  │  trame  → cycleStealingTask (ISR Bresenham)            │   │
        │  │  phase  → phaseControlTask (+ ISR notify/timer)        │   │
        │  │  cycle_stealing → cycleStealingTask                    │   │
        │  └────────────────────────────────────────────────────────┘   │
        └───────────────────────────────────────────────────────────────┘
        
  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐
  │ tempTask    │  │ ledTask     │  │ statsTask   │  │weatherTask │
  │ Core 0/prim1│  │ Core 0/prim1│  │ Core 0/prim2│  │Core 0/prim1│
  └─────────────┘  └─────────────┘  └─────────────┘  └────────────┘
  
  ┌────────────────────────────────────────────────────────────────┐
  │  ESPAsyncWebServer (port 80) · MQTT (espMqttClient) · WiFi     │
  │  (Runs on Core 0 via ESP-IDF / Arduino System Tasks)           │
  └────────────────────────────────────────────────────────────────┘
```

## Board Support: S3 vs. WROOM

This project supports two primary ESP32 architectures, each optimized for different use cases:

### ESP32-S3 (Modern & Powerful)
- **Environment**: `esp32s3`
- **Performance**: High-speed processing and native support for modern features.
- **Memory**: Supports various Flash/PSRAM combinations (e.g., N8R2, N16R8) via dynamic build-time selection.
- **Data Retention**: Configured to store up to **365 days** of historical statistics (`MAX_STATS_DAYS=365`).
- **Hardware**: Uses specialized pins (e.g., RGB Internal LED on Pin 48).

### ESP32-WROOM / ESP32-DevKit (Standard & Reliable)
- **Environment**: `esp32dev`
- **Performance**: Standard dual-core performance, highly reliable for general tasks.
- **Memory**: Uses internal SRAM (no PSRAM required). Statistics, history, and data logging are **disabled** entirely (`DISABLE_STATS`, `DISABLE_HISTORY`, `DISABLE_DATA_LOG`) to keep the footprint small.
- **Optimizations**: Specific low-RAM tuning including reduced TCP stack size (8KB) and connection limits. Uses **ArduinoJson Filtering** to parse weather data without OOM risks.
- **Data Retention**: No persistent statistics storage.

## Web Configuration Guide

The system can be fully configured via the web interface. Below is a detailed breakdown of the available settings:

### General & Connectivity
- **System Name & Timezone**: Basic identification and local time synchronization (crucial for logs and night mode).
- **WiFi Setup**: Support for Client mode (connecting to your router) or Access Point mode. Supports Static IP configuration. If the configured Wi-Fi is unreachable for more than 2 minutes, the device automatically starts a fallback Access Point (`W_Solaire`) while continuing to attempt background reconnection (`WIFI_AP_STA` mode).
- **Grid Measure Source (`grid_measure_source`)**:
    - `shelly`: Shelly EM (WiFi HTTP/MQTT)
    - `jsy1`: JSY-MK-194 sur UART 1 (filaire)
    - `jsy2`: JSY-MK-194 sur UART 2 (filaire)
- **Equipment 1 Measure Source (`equip1_measure_source`)**:
    - `shelly`: Mesuré par un Shelly Plus 1PM
    - `jsy1`: Mesuré par le canal JSY sur UART 1
    - `jsy2`: Mesuré par le canal JSY sur UART 2
- **Equipment 1 Enable (`e_equip1`)**: On/off for Eq1 logic only (not a measurement-source selector).
- **Equipment 2 (+Gestion Marche/Arrêt)**: Optional Shelly Plus 1PM for on/off relay control of a secondary equipment.
- **MQTT Broker**: Connection details for integration with Home Assistant or other automation tools.

### Regulation Logic

⚠️ **Expert Settings**: The parameters below define the core behavior of the diversion algorithm. It is highly recommended to keep the default values. Only the current default parameters have been extensively tested for stability.

- **Diverter Parameters**:
    - **Equipment Power**: The rating of your resistive load (e.g., 2000W).
    - **Export Setpoint**: Target grid balance (e.g., 0W for zero export).
    - **Delta/Deltaneg**: Tolerance windows to prevent rapid power oscillations.
    - **Compensation**: Gain factor for the regulation algorithm.
- **Smart Modes**:
    - **Force Window**: Daily scheduled blocks (e.g., for off-peak water heating).
    - **Night Mode**: Reduces polling frequency during non-productive hours. When Open-Meteo is enabled, sunrise and sunset are fetched automatically and used instead of the manually configured night window.
    - **Weather Anticipation**: Optional Open-Meteo integration using latitude/longitude to estimate solar availability and pause Equipment 2 when conditions are not favorable. When `solar_panel_power` is configured, the system estimates real-time production using a time-of-day curve (sine from sunrise to sunset) adjusted by panel azimuth and solar confidence. Requires internet connectivity.

### Hardware & Safety
- **Regulation Modes**:
    - **Trame (Bresenham)**: Optimized distribution of power cycles (Recommended). Decision per full AC cycle.
    - **Cycle Stealing**: Decision per half-cycle for maximum resolution (can cause DC bias).
    - **Burst**: Fixed-period cycles (Slow PWM, not synchronized with ZX).
    - **Phase Control**: Smooth dimming (requires a Random SSR).
- **Cooling & Safety**:
    - **Fan Control**: Automated SSR heatsink cooling.
    - **DS18B20 Sonde**: Heatsink temperature monitoring with a safety cutoff.
- **System**: Adjustable CPU frequency and GPIO pin remapping.

## Flash Storage Layout

The ESP32 partitions its internal flash into three regions:

| Partition | Mount | Content | Size |
| :--- | :--- | :--- | :--- |
| **OTA A** | Flash | Firmware slot A | ~1.5 MB each |
| **OTA B** | Flash | Firmware slot B (canary) | ~1.5 MB each |
| **LittleFS** | `/` | `config.json`, `log.txt`, `solar_data.txt`, `stats.json`, `history.bin`, web UI assets (`web_*.html.gz`, `uPlot.*.gz`, `help.json.gz`, `icons/`) | Remaining |
| **NVS** | NVS namespace | WiFi credentials, MQTT credentials, power-up state, `solar_config` (backup config), `solar_stats` (daily counters) | ~128 KB |

- **config.json** is the primary source of truth. It is mirrored to NVS (`solar_config` key) as a backup recovered on first boot if LittleFS is empty.
- **stats.json** is written every 5 minutes and on every reboot.
- **history.bin** is written on every reboot (power snapshots every 5 seconds).
- **log.txt** and **solar_data.txt** rotate automatically at 20 KB each, producing `.1` and `.2` backups.

## Control Modes

The firmware supports four SSR control strategies (`zero_crossing` is an alias of `cycle_stealing`). Choose based on your SSR hardware type:

| Mode | How It Works | Best For | Notes |
| :--- | :--- | :--- | :--- |
| **trame** | Bresenham line algorithm directly in ZX ISR. Decision is made once per full AC cycle, then applied on both half-cycles. | **Most SSRs** (recommended default). | ✅ Eliminates DC bias/hum. Uses the shared `cycleStealingTask` watchdog task. |
| **cycle_stealing** | Toggles SSR at every zero-crossing event based on running duty accumulator. | SSRs that can switch instantaneously at zero-crossing. | ✅ Highest resolution (half-cycle). Can cause DC offset hum. |
| **phase** | Phase-angle control — ZX ISR computes target delay and notifies `phaseControlTask`, which arms `esp_timer` from task context. | SSRs rated for **random-phase** (triac) triggering. | No `esp_timer` API calls inside ISR. Requires fast gate; produces harmonic distortion. **Requires running the `/web_phase_cal` wizard** to map the load power curve. |
| **burst** | Fixed-period slow PWM (e.g., 500 ms ON/OFF). Not synchronized with mains zero-crossing. | Basic SSRs where flicker/EMI is not a concern. | ⚠️ Does not use ZX pin. Simplest mode, purely task-driven. |

**Important:** `burst` mode ignores the ZX pin entirely. Using burst mode with a zero-crossing SSR will still work but offers no advantage over `trame`.

`trame`, `cycle_stealing`, and `zero_crossing` share the same ISR-driven control path (`handleZxInterrupt`) and watchdog task (`cycleStealingTask`), with different switching semantics.

## Power Redirection Logic

The core of the system is an **Incremental Controller** that continuously adjusts the power sent to the equipment (load) based on the grid power balance.

### Incremental Algorithm
The system targets a balance point between `Delta` (max import tolerated) and `Deltaneg` (max export tolerated).
1. **Importing Power**: If grid consumption exceeds `Delta`, the controller reduces the load's duty cycle proportional to the excess power and the `Compensation` factor.
2. **Exporting Surplus**: If the system detects power being injected into the grid (below `Deltaneg`), it increases the duty cycle to consume that surplus.
3. **Stability (Slow Start)**: To prevent rapid oscillations and "flicker," the increase in power is capped at **20% per cycle**, allowing the system to stabilize smoothly as production changes.

The resulting **Duty Cycle (0-100%)** is then translated into physical pulses by the selected regulation mode (Trame, Burst, or Phase).

### Equipment 2 Priority Modes

Equipment 2 (e.g., heat pump, pool heater) is managed with priority-aware logic:

| Priority | Behavior |
| :--- | :--- |
| **1 — Water Heater First** | Eq2 turns on only when Eq1 is at ~95-100% duty cycle **and** there is surplus above Eq2's power rating plus delta buffer. |
| **2 — PAC First** | Eq2 turns on whenever surplus exceeds Eq2's power rating plus delta (regardless of Eq1's duty cycle). Eq1 gets priority for smaller surplus values. |

**Known Limitation:** When `max_duty_percent < 95%`, priority 1 mode is unreachable — Eq1 can never reach the 95% threshold needed to trigger Eq2 activation. Use priority 2, or raise `max_duty_percent` above 95.

**Schedule Bitmask:** `equip2_schedule` is a 48-bit unsigned integer where each bit represents a 30-minute slot in a 24-hour day (bit 0 = 00:00-00:30, bit 47 = 23:30-24:00). To schedule Eq2 from 08:00 to 20:00, set bits 16 through 39 (binary: `0xFFFFFFFF00000000` = `1099511627775` decimal).

### Practical Schedule Bitmask Examples
| Time Range | Bits Set | Hex Value | Decimal Value |
| :--- | :--- | :--- | :--- |
| 08:00–20:00 (off-peak) | 16 – 39 | `0xFFFFFFFF00000000` | 1,099,511,627,775 |
| 22:00–06:00 (night heating) | 44 – 47 + 0 – 11 | `0x000F0000FFFF` | 43,980,465,126,912 |
| 06:00–08:00 (morning) | 12 – 15 | `0xF00000000000` | 17,390,461,408 |
| 12:00–14:00 (lunch break) | 24 – 27 | `0xF00000000` | 4,026,531,840 |
| All day (always on) | 0 – 47 | `0xFFFFFFFFFFFF` | 281,474,976,710,655 |

**How to set in config:** Use the Eq2 page at `/web_equip2` — click individual slots to toggle. Alternatively, edit `config.json` directly and set `equip2_schedule` to the decimal value above, then save via `/save_config`.

## Safety State Machine

The system uses a priority-ordered state machine. States at higher priority always override lower ones:

| Priority | State | Trigger | Action |
| :--- | :--- | :--- | :--- |
| **0 (Highest)** | `EMERGENCY_FAULT` | ESP32 temp ≥ `max_esp32_temp` + 5°C hysteresis, SSR temp ≥ `ssr_max_temp` + 5°C hysteresis, SSR sensor disconnected (-999°C), or persistent timeout (Shelly offline > safety_timeout) | SSR OFF, relay energized (opens SSR circuit), emergency LED mode |
| **1** | `SAFE_TIMEOUT` | Shelly/JSY sensor data stale > `safety_timeout` seconds (default 10s) | SSR OFF, relay energized |
| **2** | `BOOST` | Manual boost activated via Web UI or manual force window active | SSR 100% ON, relay closed |
| **3** | `NIGHT` | Clock in night window (or weather-based sunrise/sunset) | SSR OFF, relay energized, polling interval extended to `night_poll_interval` |
| **4 (Lowest)** | `NORMAL` | Everything normal. PID controller drives the system. | SSR controlled by PID duty cycle, relay closed |

**Recovery:** States 0 and 1 require a 5°C hysteresis drop below the threshold before returning to NORMAL, preventing rapid state oscillation when temperature/temp is borderline.

## Weather And Solar Confidence

When weather support is enabled (`e_weather = true`), the firmware calls the [Open-Meteo Forecast API](https://open-meteo.com/) every 9 minutes. **This requires internet access** — the ESP32 must be able to reach `api.open-meteo.com` over HTTPS.

The data is used for several decisions:

- **Solar confidence index**: The web interface displays a confidence percentage estimating the available solar potential compared with clear-sky conditions (90% radiation-based, 10% cloud-layer-based).
- **Radiation-based cloud impact**: `shortwave_radiation_instant` is compared with a realistic clear-sky ground reference derived from `terrestrial_radiation_instant` to estimate usable sunlight right now.
- **Cloud layer fallback**: Low, mid, and high cloud cover are still fetched and combined as a fallback/stabilizer when radiation data is missing or not usable.
- **Equipment 2 start condition (percentage mode)**: When `solar_panel_power = 0`, Equipment 2 can start only when the solar confidence reaches the configured `weather_cloud_threshold` percentage, unless it is inside a forced schedule.
- **Equipment 2 start condition (power estimation mode)**: When `solar_panel_power > 0`, the system estimates the expected solar production using an advanced incidence model:
    - **Solar Position**: Estimates sun elevation and azimuth based on time of day.
    - **Incidence Factor**: Calculates the cosine of the angle between the sun and the panel's normal, using `solar_panel_azimuth` and `solar_panel_tilt`.
    - **Diffuse vs Direct**: Blends direct beam radiation and diffuse sky radiation based on current cloud cover and the panel's view of the sky (`skyViewFactor`).
    - **Losses**: Applies system losses configured via `solar_loss_factor`.
    - **Cloud Penalty**: Applies a non-linear penalty for heavy cloud cover to account for spectrum shifts.
    - **Result**: Equipment 2 starts only when `expected_power >= equip2_max_power`.
- **Automatic night mode**: `daily=sunrise,sunset` with `timezone=auto` provides local sunrise and sunset times. When available, these values define night mode instead of the manual `night_start` / `night_end` settings.

Manual night start/end values remain as a fallback when weather support is disabled or sunrise/sunset data has not been received yet.

## Home Assistant (MQTT) Auto-Discovery

The firmware publishes Home Assistant discovery messages on first MQTT connect. The following entities are registered:

| Entity | State Topic | Description |
| :--- | :--- | :--- |
| `grid_power` | `mqtt_name/power` | Grid import/export power (W). Positive = import, Negative = export |
| `equipment_power` | `mqtt_name/equipment_power` | Power redirected to Eq1 (W) |
| `equipment_percent` | `mqtt_name/equipment_percent` | Current duty cycle percentage |
| `esp32_temp` | `mqtt_name/esp32_temp` | Internal ESP32 sensor temperature (°C) |
| `ssr_temp` | `mqtt_name/ssr_temp` | SSR heatsink temperature (°C), only if `e_ssr_temp` is true |
| `fan_active` | `mqtt_name/fan_active` | Fan on/off (`ON` / `OFF`) |
| `fan_percent` | `mqtt_name/fan_percent` | Fan speed (0-100) |
| `status_json` | `mqtt_name/status_json` | Full system state JSON |

**status_json** format:
```json
{
  "grid_power": 50.0,
  "equipment_power": 45.0,
  "equipment_active": true,
  "force_mode": false,
  "equipment_percent": 75.0,
  "ssr_temp": 42.5,
  "esp32_temp": 38.2,
  "fan_active": false,
  "fan_percent": 0
}
```

## HTTP API Reference

All endpoints except `/update` and `/RESET_device` respect the configured `web_user`/`web_password` authentication. Unauthenticated endpoints are intentionally read-only or require no auth for basic functionality.

| Method | Endpoint | Auth | Description |
| :--- | :--- | :--- | :--- |
| GET | `/test` | No | Health check. Returns free heap (for debugging). |
| GET | `/` | Yes | Main web dashboard (`web_command.html.gz`). |
| GET | `/status` | No | Real-time status JSON (grid_power, equipment_power, temps, RAM, RSSI, weather, etc.). |
| GET | `/history` | No | Power history as JSON array (720 points on PSRAM, or 120 on SRAM). |
| GET | `/get_log_action` | No | Stream the last 4 KB of `log.txt`. |
| GET | `/get_solar_data` | No | Stream the last 4 KB of `solar_data.txt`. |
| GET | `/download_logs` | Yes | Download `log.txt` as attachment. |
| GET | `/download_data` | Yes | Download `solar_data.txt` as attachment. |
| GET | `/get_config` | Yes | Export full configuration as JSON. |
| GET | `/web_config` | Yes | Config web page (`web_config.html.gz`). |
| GET | `/web_phase_cal` | Yes | Phase Angle Calibration wizard (`web_phase_cal.html.gz`). |
| GET | `/web_equip2` | Yes | Equipment 2 config page (`web_equip2.html.gz`). |
| GET | `/web_dev` | Yes | Dev Telemetry page (`web_dev.html.gz`). |
| GET | `/stats` | Yes | Stats web page (`web_stats.html.gz`). |
| GET | `/get_stats` | Yes | Statistics JSON (`stats.json` content). |
| POST | `/save_config` | Yes | Save config from form parameters. Reboots after save. |
| POST | `/save_config_eq2` | Yes | Save Eq2-only config. Reboots after save. |
| POST | `/save_eq2_schedule` | Yes | Update Eq2 schedule bitmask. Reboots after save. |
| POST | `/web_command` | Yes | Execute command (boost, cancel_boost, test_fan, etc.). |
| POST | `/boost` | Yes | Start manual boost (optional `min` parameter). |
| POST | `/cancel_boost` | Yes | Cancel manual boost. |
| POST | `/test_fan` | Yes | Test fan at given speed (`speed` param). |
| POST | `/test_shelly` | Yes | Test Shelly connection (`target=em|eq1|eq2`). Returns power and relay state. |
| POST | `/import_stats` | Yes | Upload `stats.json` (200 KB max). Reboots after import. |
| POST | `/update` | **No** | OTA firmware update. ⚠️ **No authentication — trusted network only.** |
| GET | `/RESET_device` | Yes | Immediate reboot. Saves logs/stats first. |
| GET | `/RESET_config` | Yes | Erase config and reboot to default. |

Static assets served directly from LittleFS:
- `/uPlot.iife.min.js` — charting library
- `/uPlot.min.css` — charting styles
- `/help.json` — UI help data
- `/icons/` — weather SVG icons (animated weather-sprite)

## Configuration Field Reference

Default values are shown. All fields are editable via the Web UI.

### System
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `name` | String | `"Solaire"` | Device display name |
| `timezone` | String | `"CET-1CEST,M3.5.0,M10.5.0/3"` | TZ database string for NTP |
| `cpu_freq` | int | `240` | CPU frequency in MHz |
| `internal_led_pin` | int | `48` | WS2812/NeoPixel onboard LED pin |
| `max_esp32_temp` | float | `65.0` | ESP32 die temp emergency cutoff (°C) |

### WiFi / AP
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `e_wifi` | bool | `true` | Enable WiFi client mode |
| `wifi_ssid` | String | `""` | WiFi SSID |
| `wifi_password` | String | `""` | WiFi password |
| `wifi_static_ip` | String | `""` | Static IP (empty = DHCP) |
| `wifi_subnet` | String | `""` | Subnet mask (e.g. `255.255.255.0`) |
| `wifi_gateway` | String | `""` | Gateway (e.g. `192.168.1.1`) |
| `wifi_dns` | String | `""` | DNS (empty = same as gateway) |
| `ap_ssid` | String | `"W_Solaire"` | AP mode SSID |
| `ap_password` | String | `"12345678"` | AP mode password |
| `ap_hidden_ssid` | bool | `false` | Hide AP SSID |
| `ap_channel` | int | `6` | AP channel |
| `ap_ip` | String | `"192.168.66.1"` | AP IP address |

### Hardware Pins
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `ssr_pin` | int | `12` | SSR control output pin (`digitalWrite`) |
| `relay_pin` | int | `13` | Relay output pin (LOW = closes SSR circuit) |
| `ds18b20_pin` | int | `14` | DS18B20 temperature sensor pin (1-Wire) |
| `fan_pin` | int | `5` | Fan PWM output pin (LEDC channel 4) |
| `zx_pin` | int | `15` | Zero-crossing detection input (interrupt) |

### Power Monitoring
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `shelly_em_ip` | String | `"192.168.1.60"` | Shelly EM grid meter IP |
| `shelly_em_index` | int | `0` | Shelly EM meter index (0 or 1) |
| `e_shelly_mqtt` | bool | `true` | Use MQTT for grid sensor data |
| `shelly_mqtt_topic` | String | `"shellies/homeassistant/emeter/0/power"` | MQTT topic for grid power |
| `grid_measure_source` | String | `"shelly"` | Source mesure reseau: `shelly` or `jsy` |
| `fake_shelly` | bool | `false` | Simulate Shelly with random data (for testing) |
| `poll_interval` | int | `1` | Polling interval in seconds |
| `shelly_timeout` | int | `2` | Shelly HTTP request timeout (seconds) |
| `safety_timeout` | int | `10` | Sensor data stale threshold (seconds) |

### Equipment 1 (Primary Load)
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `equip1_name` | String | `"Ballon"` | Equipment 1 display name |
| `equip1_max_power` | float | `2300.0` | Max rated power in watts |
| `export_setpoint` | float | `0.0` | Target grid balance (W) |
| `e_equip1` | bool | `false` | Activer Eq1 (enable/disable only) |
| `equip1_shelly_ip` | String | `""` | Eq1 power measurement Shelly IP |
| `equip1_shelly_index` | int | `0` | Eq1 Shelly meter index |
| `e_equip1_mqtt` | bool | `false` | Use MQTT for Eq1 power data |
| `equip1_mqtt_topic` | String | `""` | Eq1 MQTT power topic |
| `equip1_measure_source` | String | `"shelly"` | Source mesure Eq1: `shelly` or `jsy` |

### Equipment 2 (Secondary)
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `e_equip2` | bool | `false` | Enable equipment 2 |
| `equip2_name` | String | `"Piscine"` | Equipment 2 name |
| `equip2_shelly_ip` | String | `""` | Eq2 Shelly relay IP |
| `equip2_shelly_index` | int | `0` | Eq2 Shelly relay index |
| `e_equip2_mqtt` | bool | `false` | Use MQTT for Eq2 power |
| `equip2_mqtt_topic` | String | `""` | Eq2 MQTT power topic |
| `equip2_max_power` | float | `1900.0` | Eq2 rated power (W) |
| `equip2_priority` | int | `1` | `1` = water heater first, `2` = PAC first |
| `equip2_min_on_time` | int | `15` | Minimum ON duration (minutes) |
| `equip2_schedule` | uint64 | `0` | 48-bit bitmask (30-min slots) |

### Control Algorithm
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `delta` | float | `50.0` | Upper import threshold (W) |
| `deltaneg` | float | `0.0` | Lower export threshold (W) |
| `compensation` | float | `100.0` | Proportional gain factor |
| `dynamic_threshold_w` | float | `200.0` | Error threshold for lag protection |
| `max_duty_percent` | float | `100.0` | Max SSR duty cycle (0-100) |
| `burst_period` | float | `0.5` | PWM period in seconds (burst mode) |
| `min_power_threshold` | float | `10.0` | Min power to consider valid (W) |
| `min_off_time` | int | `1` | Minimum SSR off time (seconds) |
| `boost_minutes` | int | `60` | Default manual boost duration |
| `vacation_until` | uint32 | `0` | Epoch timestamp until when vacation mode is active |

### Force / Night / Weather
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `force_equipment` | bool | `false` | Always force equipment ON |
| `e_force_window` | bool | `false` | Enable scheduled force window |
| `force_start` | String | `"22:05"` | Force window start |
| `force_end` | String | `"05:55"` | Force window end |
| `night_start` | String | `"22:00"` | Night mode start |
| `night_end` | String | `"05:50"` | Night mode end |
| `night_poll_interval` | int | `15` | Night polling interval (seconds) |
| `e_weather` | bool | `false` | Enable Open-Meteo weather |
| `weather_lat` | String | `""` | Latitude |
| `weather_lon` | String | `""` | Longitude |
| `weather_cloud_threshold` | int | `40` | Min solar confidence % for Eq2 (used when `solar_panel_power = 0`) |
| `solar_panel_power` | int | `0` | Solar panel peak power in watts (e.g. 3000 for 3kWc). When > 0, enables power-based Eq2 decision instead of percentage threshold |
| `solar_panel_azimuth` | int | `180` | Panel orientation in degrees: 0=North, 90=East, 180=South, 270=West |
| `solar_panel_tilt` | int | `35` | Panel tilt in degrees (0=horizontal, 90=vertical) |
| `solar_loss_factor` | int | `14` | Percentage of system losses (cables, inverter, dirt) |

### Temperature / Fan
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `e_ssr_temp` | bool | `true` | Enable SSR temperature monitoring |
| `ssr_max_temp` | float | `65.0` | SSR overheating threshold (°C) |
| `e_fan` | bool | `true` | Enable automatic fan control |
| `fan_temp_offset` | int | `10` | Fan start temp below ssr_max (°C) |

### JSY-MK-194 (Wired Measurement)
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `jsy_grid_channel` | int | `1` | Canal JSY réseau (utilisé si `grid_measure_source = jsy1/2`) |
| `jsy_equip1_channel` | int | `2` | Canal JSY Eq1 (utilisé si `equip1_measure_source = jsy1/2`) |
| `jsy1_tx` | int | `5` | UART1 TX pin |
| `jsy1_rx` | int | `4` | UART1 RX pin |
| `jsy2_tx` | int | `17` | UART2 TX pin |
| `jsy2_rx` | int | `16` | UART2 RX pin |

### SSR Control Mode
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `control_mode` | String | `"trame"` | `burst`, `trame`, `phase`, `cycle_stealing` |
| `zx_busypoll_us` | int | `1000` | Legacy field from previous busy-wait phase implementation |
| `zx_timeout_ms` | int | `500` | Legacy config field; watchdog timeout is currently fixed in control task |
| `phase_calibrate` | bool | `false` | Run automated phase-angle calibration sweep on boot |
| `phase_cal_min_us` | int | `50` | Calibration sweep minimum delay from ZX (microseconds) |
| `phase_cal_max_us` | int | `9950` | Calibration sweep maximum delay (microseconds) |
| `phase_cal_step_us` | int | `100` | Calibration sweep step size (microseconds) |
| `phase_cal_hold_ms` | int | `5000` | Calibration dwell time per step (milliseconds) |

## Web UI Pages Overview

The system exposes **six distinct web pages** for configuration and monitoring:

| Page | URL | Purpose | Key Features |
| :--- | :--- | :--- | :--- |
| **Dashboard** | `/` (or `/web_command`) | Real-time status and control | Live power charts, SSR duty cycle, Eq2 status, boost controls, fan test |
| **Configuration** | `/web_config` | All settings in one form | WiFi/MQTT, grid/equipment sources, regulation params, safety thresholds |
| **Equipment 2** | `/web_equip2` | Schedule and control Eq2 (pool/heater) | 48-slot schedule grid (30 min each), bypass status, priority toggle |
| **Phase Calibration** | `/web_phase_cal` | Automated phase-angle sweep | Start/Pause/Resume calibration, saves lookup table to config |
| **Statistics** | `/web_stats` | Historical energy tracking | Daily/hourly charts, total import/export/redirect, 30-day history (configurable) |
| **Dev Telemetry** | `/web_dev` | Debugging and diagnostics | RSSI, heap free, sensor values, state machine status, manual command buttons |

> **Note:** Pages under `/web_*` serve a full HTML page with embedded JavaScript. Direct API endpoints (e.g., `/status`, `/stats`) return raw JSON — useful for MQTT integration or external monitoring tools.

### MQTT
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `e_mqtt` | bool | `true` | Enable MQTT broker connection |
| `mqtt_ip` | String | `"192.168.1.100"` | Broker IP address |
| `mqtt_port` | int | `1883` | Broker port |
| `mqtt_user` | String | `""` | Username (empty = no auth) |
| `mqtt_password` | String | `""` | Password |
| `mqtt_name` | String | `"GuiboSolar"` | MQTT client ID / topic prefix |
| `mqtt_retain` | bool | `false` | Retain all MQTT messages |
| `mqtt_keepalive` | int | `60` | MQTT keepalive interval (seconds) |
| `mqtt_discovery_prefix` | String | `"homeassistant"` | HA discovery topic prefix |
| `mqtt_report_interval` | int | `10` | Status report frequency (seconds) |

### Web Security
| Field | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `web_user` | String | `""` | Web UI username (empty = no auth) |
| `web_password` | String | `""` | Web UI password |

## Monitoring Latency

| Source | Connection | Expected Latency |
| :--- | :--- | :--- |
| **JSY-MK-194** | Wired (UART) | ~100ms |
| **Shelly EM / Plus 1PM MQTT** | Wi-Fi | ~200ms |
| **Shelly EM / Plus 1PM HTTP** | Wi-Fi | ~1000ms+ |

## Recommended Hardware

| Component | Model | Role |
| :--- | :--- | :--- |
| **Grid Sensor** | JSY-MK-194 (Modbus RTU) or Shelly EM | Measures grid power (import/export) |
| **Equipment 1 Meter** | JSY-MK-194 (second channel) or Shelly Plus 1PM | Real power measurement of redirected load |
| **Equipment 2 Control** | Shelly Plus 1PM (or MQTT topic) | On/off relay for secondary equipment |
| **Microcontroller** | ESP32-S3-DevKitC-1 | Main controller with PSRAM |
| **SSR** | ZS3S16 (zero-crossing) or B3RA (random-phase) | Switches the resistive load |
| **DS18B20** | Any one-wire temperature sensor | SSR heatsink monitoring |
| **Fan** | 5V PWM computer fan | SSR heatsink cooling |

## Wiring Guide

| Signal | ESP32-S3 Pin | ESP32-WROOM Pin | Connect To |
| :--- | :--- | :--- | :--- |
| **SSR control** | `17` | `22` | SSR control input |
| **Relay coil** | `6` | `17` | Relay coil (active-LOW, closes SSR circuit) |
| **1-Wire data** | `16` | `23` | DS18B20 DQ pin (+ 4.7K pull-up to 3.3V) |
| **PWM fan** | `7` | `5` | Fan PWM input (LEDC ch4, 10 kHz) |
| **Zero-crossing** | `15` | `19` | ZX sensor output (open-collector, pull-up to 3.3V) |
| **Common ground** | `GND` | `GND` | All sensor grounds |
| **Power** | `3.3V` / `5V` | `3.3V` / `5V` | SSR control, relay, ZX (as rated) |
| **UART1 RX/TX (JSY1)** | `4/5` | `18/15` | JSY-MK-194 RX/TX (4800 baud, 8N1) |
| **UART2 RX/TX (JSY2)** | `16/17` | `32/33` | Second JSY-MK-194 RX/TX |
| **NeoPixel** | `48` | `2` | Onboard WS2812 LED |

> **Note: Config defaults vs. Wiring.** The Wiring Guide columns show the recommended physical pin assignments for each board target (used on this custom PCB). These differ from the `Config` struct defaults (e.g., `ssr_pin=12`), which are generic starting values for any build. When migrating from one board target to another, update both your wiring **and** the `Config` fields accordingly.

**Note:** The control logic uses standard active-HIGH for the SSR and active-LOW for the safety relay:
- `digitalWrite(ssr_pin, HIGH)` = SSR **ON** (power to load)
- `digitalWrite(ssr_pin, LOW)` = SSR **OFF** (no power to load)
- The relay (pin 6 on S3, pin 17 on WROOM) is active-LOW: `LOW` closes the SSR circuit (normal), `HIGH` opens it (safety cut-off).

**Pin validation:** The firmware now validates GPIO by **role + board target** (ESP32-S3 vs ESP32-WROOM), not only by numeric range. Some pins are rejected for SSR/relay/fan/ZX roles because of boot strapping, USB, flash/PSRAM, input-only restrictions, or board-specific caveats.

## Getting Started

1.  **Installation**: Install [PlatformIO](https://platformio.org/).
2.  **Configuration**: Edit `data/config.json` (or use the Web UI) to set your credentials and hardware pins. **Warning: on first boot with no config, the device will create an Access Point with password `12345678` — change it immediately via the Web UI.**
3.  **Build & Flash**:
    - For S3: `./flash.sh` or `pio run -e esp32s3 -t upload`
    - For WROOM: `./flash.sh -e wroom` or `pio run -e esp32dev -t upload`
4.  **Filesystem**: Don't forget to upload the LittleFS image: `./flash.sh --skip-fs` or `pio run -t uploadfs`.

## Flashing Script

The project provides `flash.sh` for simplified flashing:

| Option | Description |
| :--- | :--- |
| `-e s3` | Target ESP32-S3 (default) |
| `-e wroom` | Target ESP32-WROOM (swaps config, disables stats/history) |
| `-v <var>` | **ESP32-S3 Variant**: e.g., `N8R2`, `N16R8` (default), `N8`. Sets Flash/PSRAM configs. |
| `-t` / `--test` | Run unit tests on host (native environment) |
| `--erase` | Full chip erase (clears NVS/Stats) before flashing |
| `--skip-fs` | Skip building and uploading the filesystem (LittleFS) |
| `-m` / `--monitor` | Launch serial monitor after flashing |
| `-d <days>` | Override `MAX_STATS_DAYS` at build time |

**Note**: The default build targets **S3** hardware with **N16R8** (16MB Flash, 8MB Octal PSRAM).

Example: `./flash.sh -v N8R2 --erase -m`

## Native Unit Tests

Four test suites compile for the native (desktop) environment via `platformio.ini`:

| Test | Purpose | Run Command |
| :--- | :--- | :--- |
| `test_temp` | DS18B20 temperature read logic, **latching fault detection**, hysteresis | `./flash.sh -t` or `pio test -e native` |
| `test_safety` | Safety state machine, hysteresis, **integer overflow protection for large timeouts** | `./flash.sh -t` or `pio test -e native` |
| `test_controller` | Incremental PID controller math, **symmetric power capping (20%/cycle)**, overflow protection | `./flash.sh -t` or `pio test -e native` |
| `test_stats` | Stats accumulation, **Night Mode logic**, direct measurement validation | `./flash.sh -t` or `pio test -e native` |

Tests use a `NATIVE_TEST` preprocessor flag to stub Arduino/ESP32 dependencies (String, millis, Preferences, etc.). The `simulate_solar.py` script provides an MQTT-based simulation environment for integration testing.

## Security Notes

- **OTA Update (`/update`)**: Authenticated. Requires `web_user`/`web_password` if configured.
- **Default AP Password**: `12345678` on first boot — change via the web UI immediately.
- **Web Credentials**: Stored in plaintext in `config.json`. Protect your config backup files.
- **Unauthenticated Endpoints**: `/test` bypasses auth for simple health checks. All data-sensitive endpoints (logs, power, stats) require authentication.

## Troubleshooting

### LittleFS Mount Failure
If the serial log shows `LittleFS Mount Failed`, the flash partition is corrupted or too small. Erase and reflash: `./flash.sh --erase`.

### Shelly Timeout / Safe Timeout State
Serial log shows `STATE CHANGE: NORMAL -> SAFE_TIMEOUT (Shelly Timeout!)`. Check:
- Shelly device is powered and on the same network.
- `shelly_em_ip` / `equip1_shelly_ip` / `equip2_shelly_ip` are correct.
- Switch from HTTP to MQTT mode for more reliable connectivity (set `e_shelly_mqtt = true`).
- Check `shelly_timeout` — increase if the Shelly is consistently slow.

### MQTT Disconnect / No HA Entities
Home Assistant entities not appearing after enabling MQTT:
- Verify `mqtt_ip` and `mqtt_port` are correct.
- Check that the MQTT broker allows anonymous connections if `mqtt_user`/`mqtt_password` are empty.

### SSR Not Switching
1. Check safety state: is the device in `SAFE_TIMEOUT` (sensor offline) or `EMERGENCY_FAULT` (overheating)?
2. Verify SSR pin wiring — the ESP32 outputs active-LOW, meaning LOW = SSR energized.
3. Verify `control_mode` matches your SSR type (zero-crossing SSR + `phase` mode will not work; use `trame`).
4. Check ZX pin if using `trame`/`phase`/`cycle_stealing` mode — no ZX signal means SSR stays OFF.

### Random Reboots
Check serial log for `task_wdt: Task watchdog got triggered`. This usually means a task exceeded its 60-second execution window. Common causes:
- Shelly HTTP timeout too long (use `--erase` to reset to defaults).
- `MAX_STATS_DAYS` very large (365) — `stats.json` save can take several seconds.
- Network instability causing `WiFi.reconnect()` to flood (default is every 10 seconds).

### Eq2 Never Turns On
If Equipment 2 (priority=1) never activates:
- Eq1 must reach ≥95% duty cycle AND there must be surplus above Eq2's power + delta.
- If `max_duty_percent` is <95, Eq1 can never hit 95% duty → Eq2 never triggers.
- If `solar_panel_power > 0`: check the estimated production in the weather popup — Eq2 only starts when estimated power >= `equip2_max_power`. Try lowering `solar_panel_power` to be more aggressive, or check `solar_panel_azimuth` matches your real panel orientation.
- If `solar_panel_power = 0`: check `weather_cloud_threshold` — if solar confidence is below threshold, Eq2 is bypassed unless scheduled.
- Verify `e_weather = true` and the ESP32 has internet access (Open-Meteo API requires HTTPS to `api.open-meteo.com`).
- Verify `e_equip2 = true` and the Shelly is reachable.

### Debugging Tips: Interpreting Sensor Data
- **Grid Power (`grid_power`):** Positive values = importing from grid, negative = exporting to grid. Values near 0 indicate successful diversion. A sustained positive value above your export setpoint means Eq1 isn't absorbing enough surplus; a sustained negative value means the system is pushing too much power back.
- **Equipment Percent (`equipment_percent`) during Phase Control:** In phase mode this represents the dimming angle (0° = fully off, 90° = fully on). During calibration at `/web_phase_cal`, you'll see a sweep from `phase_cal_min_us` to `phase_cal_max_us`. If power doesn't change as expected, check that your SSR supports random-phase triggering.
- **RSSI for Shelly/MQTT Connectivity:** Use the Dev Telemetry page (`/web_dev`) to monitor RSSI (Wi-Fi signal strength). Below -70 dBm is considered weak — expect increased timeouts and higher latency for HTTP-based Shelly polling (<1s → 2–3s). MQTT mode is more resilient at lower RSSI.
- **ZX Signal Health:** If using `trame`, `phase`, or `cycle_stealing` modes, the ZX pin must receive a consistent signal. No ZX = SSR stays OFF. Check your Zero-Crossing sensor wiring and that it outputs a TTL-compatible 3.3V signal.

## Known Limitations

- **Controller Deadzone**: When `max_duty_percent < 95%`, Equipment 2 priority-1 mode (water heater first) is unreachable since the equation heater can never reach 95% duty cycle.
- **Burst Mode SSR Compatibility**: Burst mode ignores zero-crossing — use only with true zero-crossing SSRs to avoid EMI and contact wear.
- **WROOM Feature Parity**: The WROOM environment (`esp32dev`) completely disables stats, history, and data logging. Use ESP32-S3 for full feature set. Requires at least 8 MB PSRAM for 365-day statistics retention.
- **Config Serialization Drift**: Config save/load manually maps ~100 fields between JSON and the `Config` struct. Adding a field to `Config` may require updating both `ConfigManager::load()` and `ConfigManager::save()`.
