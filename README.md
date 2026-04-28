# Solar Power Diverter (C++ Version)

This project is a high-performance C++ implementation of a Photovoltaic (PV) Router, optimized for the ESP32 platform using the Arduino framework and PlatformIO.

## Project Origin

This C++ version is a **migration from the original MicroPython implementation for domotique**. The migration was performed by AI agents to achieve better performance, lower latency, and more robust execution on resource-constrained hardware.

## Key Features

- **High-Speed Diversion**: Implements a highly responsive power diversion algorithm (burst-fire/trame mode) to match excess solar production with a resistive load.
- **Multi-Source Monitoring**: Supports data acquisition from Shelly EM (via HTTP or MQTT) and JSY-MK-194 (via UART).
- **Asynchronous Web Server**: Provides a rich web interface for real-time monitoring, logging, and configuration without blocking the control loop.
- **Advanced Statistics**: Tracks and stores daily/hourly energy usage (Import/Export/Redirected) with historical data retention.
- **Safety & Protection**: Includes watchdog timers, temperature monitoring (Internal & SSR heatsink), and automatic fan control.
- **Weather-Aware Control**: Uses Open-Meteo cloud cover, solar radiation, sunrise, and sunset data to improve equipment decisions.
- **Home Assistant Integration**: MQTT support with auto-discovery for easy integration into smart home systems.

## Board Support: S3 vs. WROOM

This project supports two primary ESP32 architectures, each optimized for different use cases:

### ESP32-S3 (Modern & Powerful)
- **Environment**: `esp32s3`
- **Performance**: High-speed processing and native support for modern features.
- **Memory**: Utilizes **PSRAM** for extended buffer and data handling.
- **Data Retention**: Configured to store up to **365 days** of historical statistics (`MAX_STATS_DAYS=365`).
- **Hardware**: Uses specialized pins (e.g., RGB Internal LED on Pin 48).

### ESP32-WROOM / ESP32-DevKit (Standard & Reliable)
- **Environment**: `esp32dev`
- **Performance**: Standard dual-core performance, highly reliable for general tasks.
- **Memory**: Uses internal SRAM (no PSRAM required).
- **Data Retention**: Optimized for smaller memory footprints, storing up to **14 days** of statistics (`MAX_STATS_DAYS=14`) to prevent memory exhaustion (OOM).

## Web Configuration Guide

The system can be fully configured via the web interface. Below is a detailed breakdown of the available settings:

### General & Connectivity
- **System Name & Timezone**: Basic identification and local time synchronization (crucial for logs and night mode).
- **WiFi Setup**: Support for Client mode (connecting to your router) or Access Point mode. Supports Static IP configuration.
- **Power Meter (Shelly EM)**:
    - **Shelly IP**: The target meter address.
    - **MQTT Mode**: If enabled, uses low-latency pushes instead of HTTP polling.
    - **Poll Interval**: Frequency of data requests when in standard HTTP mode.
- **MQTT Broker**: Connection details for integration with Home Assistant or other automation tools.

### Regulation Logic
- **Diverter Parameters**:
    - **Equipment Power**: The rating of your resistive load (e.g., 2000W).
    - **Export Setpoint**: Target grid balance (e.g., 0W for zero export).
    - **Delta/Deltaneg**: Tolerance windows to prevent rapid power oscillations.
    - **Compensation**: Gain factor for the regulation algorithm.
- **Smart Modes**:
    - **Force Window**: Daily scheduled blocks (e.g., for off-peak water heating).
    - **Night Mode**: Reduces polling frequency during non-productive hours. When Open-Meteo is enabled, sunrise and sunset are fetched automatically and used instead of the manually configured night window.
    - **Weather Anticipation**: Optional Open-Meteo integration using latitude/longitude to estimate solar availability and pause Equipment 2 when conditions are not favorable.

### Hardware & Safety
- **Regulation Modes**:
    - **Trame (Bresenham)**: Optimized distribution of power cycles (Recommended).
    - **Burst**: Fixed-period cycles.
    - **Phase Control**: Smooth dimming (requires a Random SSR).
- **Cooling & Safety**:
    - **Fan Control**: Automated SSR heatsink cooling.
    - **DS18B20 Sonde**: Heatsink temperature monitoring with a safety cutoff.
- **System**: Adjustable CPU frequency and GPIO pin remapping.

## Power Redirection Logic

The core of the system is an **Incremental Controller** that continuously adjusts the power sent to the equipment (load) based on the grid power balance.

### Incremental Algorithm
The system targets a balance point between `Delta` (max import tolerated) and `Deltaneg` (max export tolerated).
1.  **Importing Power**: If grid consumption exceeds `Delta`, the controller reduces the load's duty cycle proportional to the excess power and the `Compensation` factor.
2.  **Exporting Surplus**: If the system detects power being injected into the grid (below `Deltaneg`), it increases the duty cycle to consume that surplus.
3.  **Stability (Slow Start)**: To prevent rapid oscillations and "flicker," the increase in power is capped at **20% per cycle**, allowing the system to stabilize smoothly as production changes.

The resulting **Duty Cycle (0-100%)** is then translated into physical pulses by the selected regulation mode (Trame, Burst, or Phase).

## Weather And Solar Confidence

When weather support is enabled, the firmware calls the Open-Meteo Forecast API every 9 minutes. The data is used for two separate decisions:

- **Solar confidence index**: The web interface displays a simple confidence percentage estimating the available solar potential compared with clear-sky conditions.
- **Radiation-based cloud impact**: `shortwave_radiation_instant` is compared with a realistic clear-sky ground reference derived from `terrestrial_radiation_instant` to estimate usable sunlight right now.
- **Cloud layer fallback**: Low, mid, and high cloud cover are still fetched and combined as a fallback/stabilizer when radiation data is missing or not usable.
- **Equipment 2 start condition**: Equipment 2 can start only when the solar confidence reaches the configured minimum threshold, unless it is inside a forced schedule.
- **Automatic night mode**: `daily=sunrise,sunset` with `timezone=auto` provides local sunrise and sunset times. When available, these values define night mode instead of the manual `night_start` / `night_end` settings.

Manual night start/end values remain as a fallback when weather support is disabled or sunrise/sunset data has not been received yet.

## Getting Started

1.  **Installation**: Install [PlatformIO](https://platformio.org/).
2.  **Configuration**: Edit `data/config.json` (or use the Web UI) to set your credentials and hardware pins.
3.  **Build & Flash**:
    - For S3: `pio run -e esp32s3 -t upload`
    - For WROOM: `pio run -e esp32dev -t upload`
4.  **Filesystem**: Don't forget to upload the LittleFS image: `pio run -t uploadfs`.

## Monitoring Latency

| Source | Connection | Expected Latency |
| :--- | :--- | :--- |
| **JSY-MK-194** | Wired (UART) | ~100ms |
| **Shelly MQTT** | Wi-Fi | ~200ms |
| **Shelly HTTP** | Wi-Fi | ~1000ms+ |
