#!/usr/bin/env python3
"""
Time-Evolving Weather Simulator for ESP32 integration testing.

Replaces the real Open-Meteo API with a realistic, continuous weather simulation:
- Diurnal cycle (sunrise/sunset, radiation curve)
- Gradual weather transitions (clouds passing, storms developing)
- Seasonal variation (base temperature, day length)
- Random events (thunderstorm formation, fog, wind gusts)
- All values interpolated and time-evolving, matching Open-Meteo v1/forecast format

Weather modes can still be manually overridden via HTTP:
    GET /weather?mode=storm      — switch to a specific mode
    GET /weather?mode=clear      — switch back to clear
    POST /weather/sync?solar=1200 — sync radiation to solar production

Unified launcher available via run_simulation.py (starts HTTP server + weather engine).

Run:
    python3 simulate_weather.py --port 9090 --mode clear
    python3 simulate_weather.py --speed-up 10  --speed 10x simulation
"""

import argparse
import asyncio
import math
import random
import json
import time
from dataclasses import dataclass, field, asdict
from typing import Optional
from aiohttp import web

# ───────────────────────────────────────────
# Weather regimes — base values for each regime
# (based on typical Open-Meteo responses for mid-latitude Europe, ~48°N)
# ───────────────────────────────────────────

WEATHER_REGIMES = {
    "clear": {
        "temperature_2m": 22.0,
        "weather_code": 0,
        "cloud_cover": 5,
        "cloud_cover_low": 0,
        "cloud_cover_mid": 5,
        "cloud_cover_high": 0,
        "shortwave_radiation_instant": 920.0,
        "terrestrial_radiation_instant": 380.0,
        "wind_speed_10m": 8.0,
        "wind_direction_10m": 220.0,
        "rain": 0.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 40.0,
        "is_day": True,
    },
    "partly_cloudy": {
        "temperature_2m": 20.0,
        "weather_code": 2,
        "cloud_cover": 40,
        "cloud_cover_low": 10,
        "cloud_cover_mid": 25,
        "cloud_cover_high": 15,
        "shortwave_radiation_instant": 520.0,
        "terrestrial_radiation_instant": 370.0,
        "wind_speed_10m": 12.0,
        "wind_direction_10m": 200.0,
        "rain": 0.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 55.0,
        "is_day": True,
    },
    "overcast": {
        "temperature_2m": 17.0,
        "weather_code": 3,
        "cloud_cover": 95,
        "cloud_cover_low": 60,
        "cloud_cover_mid": 35,
        "cloud_cover_high": 20,
        "shortwave_radiation_instant": 120.0,
        "terrestrial_radiation_instant": 350.0,
        "wind_speed_10m": 15.0,
        "wind_direction_10m": 250.0,
        "rain": 0.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 70.0,
        "is_day": True,
    },
    "light_rain": {
        "temperature_2m": 15.0,
        "weather_code": 61,
        "cloud_cover": 100,
        "cloud_cover_low": 80,
        "cloud_cover_mid": 50,
        "cloud_cover_high": 60,
        "shortwave_radiation_instant": 60.0,
        "terrestrial_radiation_instant": 340.0,
        "wind_speed_10m": 18.0,
        "wind_direction_10m": 270.0,
        "rain": 2.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 85.0,
        "is_day": True,
    },
    "storm": {
        "temperature_2m": 14.0,
        "weather_code": 96,
        "cloud_cover": 100,
        "cloud_cover_low": 90,
        "cloud_cover_mid": 40,
        "cloud_cover_high": 80,
        "shortwave_radiation_instant": 30.0,
        "terrestrial_radiation_instant": 310.0,
        "wind_speed_10m": 45.0,
        "wind_direction_10m": 290.0,
        "rain": 8.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 92.0,
        "is_day": False,
    },
    "dusk": {
        "temperature_2m": 15.0,
        "weather_code": 1,
        "cloud_cover": 25,
        "cloud_cover_low": 5,
        "cloud_cover_mid": 20,
        "cloud_cover_high": 10,
        "shortwave_radiation_instant": 50.0,
        "terrestrial_radiation_instant": 300.0,
        "wind_speed_10m": 6.0,
        "wind_direction_10m": 180.0,
        "rain": 0.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 65.0,
        "is_day": True,
    },
    "night": {
        "temperature_2m": 12.0,
        "weather_code": 0,
        "cloud_cover": 10,
        "cloud_cover_low": 0,
        "cloud_cover_mid": 5,
        "cloud_cover_high": 5,
        "shortwave_radiation_instant": 0.0,
        "terrestrial_radiation_instant": 280.0,
        "wind_speed_10m": 4.0,
        "wind_direction_10m": 160.0,
        "rain": 0.0,
        "snowfall": 0.0,
        "relative_humidity_2m": 75.0,
        "is_day": False,
    },
    "snow": {
        "temperature_2m": -2.0,
        "weather_code": 75,
        "cloud_cover": 100,
        "cloud_cover_low": 80,
        "cloud_cover_mid": 50,
        "cloud_cover_high": 60,
        "shortwave_radiation_instant": 40.0,
        "terrestrial_radiation_instant": 250.0,
        "wind_speed_10m": 20.0,
        "wind_direction_10m": 350.0,
        "rain": 0.0,
        "snowfall": 2.5,
        "relative_humidity_2m": 88.0,
        "is_day": True,
    },
}

# Transition graph — which regime can transition to which, and with what probability
TRANSITION_CHART = {
    "clear":        {"partly_cloudy": 0.15, "overcast": 0.02, "dusk": 0.0, "night": 0.0},
    "partly_cloudy": {"clear": 0.1, "overcast": 0.08, "light_rain": 0.03, "storm": 0.01, "dusk": 0.0, "night": 0.0},
    "overcast":     {"light_rain": 0.05, "storm": 0.01, "clear": 0.02, "partly_cloudy": 0.05},
    "light_rain":   {"storm": 0.03, "overcast": 0.06, "partly_cloudy": 0.04, "clear": 0.005, "dusk": 0.0, "night": 0.0},
    "storm":        {"overcast": 0.04, "light_rain": 0.03, "partly_cloudy": 0.01, "night": 0.0},
    "dusk":         {"night": 0.1, "partly_cloudy": 0.0},
    "night":        {"dusk": 0.05, "clear": 0.0},
    "snow":         {"overcast": 0.04, "storm": 0.01, "partly_cloudy": 0.01},
}


# ───────────────────────────────────────────
# Sunrise/sunset & solar position computation
# ───────────────────────────────────────────

def compute_sun_times(year: int = 2026, month: int = 5, day: int = 6, longitude: float = 48.0, latitude: float = 48.0):
    """Compute approximate sunrise/sunset for a given date (mid-latitude Europe-ish)."""
    doy = sum([31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][:month - 1]) + day
    if month > 2 and year % 4 == 0:
        doy += 1

    gamma = 2 * math.pi / 365 * (doy - 81)
    eqtime = 9.87 * math.sin(2 * gamma) - 7.53 * math.cos(gamma) - 1.5 * math.sin(gamma)
    decl = 0.409 * math.sin(2 * math.pi / 365 * (doy - 81))

    lat_rad = math.radians(latitude)
    cos_ha = -math.tan(lat_rad) * math.tan(decl)
    cos_ha = max(-1.0, min(1.0, cos_ha))
    ha = math.acos(cos_ha)

    sunrise_solar = 12 - ha / math.pi * 12 + eqtime / 60
    sunset_solar = 12 + ha / math.pi * 12 + eqtime / 60

    utc_offset = longitude / 15.0
    sunrise = int(max(0, sunrise_solar - utc_offset))
    sunset = int(min(23, sunset_solar - utc_offset))

    return f"{year:04d}-{month:02d}-{day:02d}T{sunrise:02d}:00:00", \
           f"{year:04d}-{month:02d}-{day:02d}T{sunset:02d}:00:00"


def solar_elevation_angle(hour_fraction: float) -> float:
    """Sun elevation angle [rad] as a function of hour (0=midnight, 0.5=noon).
    Peaks near solar noon, is zero at night."""
    # Sun is above horizon roughly 6:00-18:00 (0.25 - 0.75 fraction)
    # Simple cosine curve centered on noon
    noon = 0.5
    span = 0.35  # daylight half-width
    if abs(hour_fraction - noon) > span:
        return -0.1  # night (negative = below horizon)
    return math.cos(math.pi / (2 * span) * (hour_fraction - noon))


def solar_azimuth_angle(hour_fraction: float) -> float:
    """Azimuth in radians: 0=E, pi/2=S, pi=W. Approximate."""
    noon = 0.5
    span = 0.35
    if abs(hour_fraction - noon) > span:
        return math.pi / 2  # south at night (undefined but consistent)
    progress = (hour_fraction - (noon - span)) / (2 * span)  # 0->1 across daylight
    return (math.pi / 2 * (1 - progress)) + (math.pi * progress)  # East -> West through South


# ───────────────────────────────────────────
# Time-evolving weather state
# ───────────────────────────────────────────

@dataclass
class WeatherSimState:
    """Continuously evolving weather state."""

    # ── Simulated clock ──
    year: int = 2026
    month: int = 5
    day: int = 6
    hour: float = 12.0       # 0-24, floats for sub-hour precision
    speed_up: float = 1.0    # real-time scale factor
    started_at: float = field(default_factory=lambda: time.time())

    # ── Latitude / longitude ──
    latitude: float = 48.0
    longitude: float = 2.3   # Paris coordinates

    # ── Season (0=Jan, 12=Jul, 23=Dec) ──
    season: float = 4.5      # May-ish

    # ── Current regime (for manual override) ──
    mode: str = "clear"

    # ── Transition timing ──
    transition_cooldown: float = 300.0  # seconds before deciding new regime
    time_in_mode: float = 0.0

    # ── Interpolation ──
    current_values: dict = field(default_factory=dict)
    target_values: dict = field(default_factory=dict)
    transition_start_values: dict = field(default_factory=dict)
    transition_progress: float = 0.0  # 0->1 over transition period
    in_transition: bool = False
    transition_duration: float = 180.0  # seconds for a full regime change

    # ── Diurnal base (for radiation scaling) ──
    day_length_hours: float = 15.0  # approx for May at 48°N
    solar_noon_fraction: float = 0.5

    # ── Solar sync (from simulate_solar.py) ──
    solar_production_sync: Optional[float] = None  # if set, override radiation
    force_recompute: bool = False

    # ── Jitter amplitude ──
    jitter_amplitude: float = 0.03  # 3% jitter per tick

    def get_season_month(self):
        """Convert season float (0-23) back to month/day."""
        month = max(1, min(12, int(self.season / 24 * 12) + 1))
        day = 1
        return month, day

    def update(self, dt: float):
        """Advance the weather state by dt seconds."""
        # Advance simulated clock
        self.hour += dt * self.speed_up / 3600.0  # dt is in real seconds, hour is fraction of day

        if self.hour >= 24.0:
            self.hour -= 24.0
            self.day += 1
            # Advance day
            days_in_month = [31, 28 + (1 if (self.year % 4 == 0 and (self.year % 100 != 0 or self.year % 400 == 0)) else 0),
                             31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
            if self.day > days_in_month[self.month - 1]:
                self.day = 1
                self.month += 1
                if self.month > 12:
                    self.month = 1
                    self.year += 1
                    self.season = (self.season + 1) % 24
            # Recalculate day length from season
            self.day_length_hours = 12 + 12 * math.sin(2 * math.pi * self.season / 24)
        elif self.hour < 0.0:
            self.hour += 24.0
            self.day -= 1
            if self.day < 1:
                self.month -= 1
                if self.month < 1:
                    self.month = 12
                    self.year -= 1
                self.day = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][self.month - 1]
                if self.month == 2 and self.year % 4 == 0:
                    self.day = 29

        # Track time in current mode
        if not self.in_transition:
            self.time_in_mode += dt
            if self.time_in_mode >= self.transition_cooldown:
                self._decide_regime_change(dt)
        else:
            self.transition_progress += dt / self.transition_duration
            if self.transition_progress >= 1.0:
                self.transition_progress = 1.0
                self.current_values = self.target_values.copy()
                self.in_transition = False
                self.time_in_mode = 0.0

    def _decide_regime_change(self, dt: float):
        """Decide whether to change regime based on transition probabilities."""
        transitions = TRANSITION_CHART.get(self.mode, {})
        if not transitions:
            return

        roll = random.random()
        cumulative = 0.0
        chosen = None
        for regime, prob in transitions.items():
            cumulative += prob
            if cumulative > roll:
                chosen = regime
                break

        if chosen and chosen != self.mode:
            self._start_transition(chosen, dt)

    def _start_transition(self, target_mode: str, dt: float):
        """Start interpolating to a new regime."""
        self.transition_start_values = self.current_values.copy()
        self.target_values = WEATHER_REGIMES[target_mode].copy()
        # Scale transition duration by difference: bigger changes = longer transitions
        self.transition_duration = max(60.0, abs(self.transition_duration) * 1.2)
        self.transition_progress = 0.0
        self.in_transition = True
        self.time_in_mode = 0.0
        print(f"[weather] transitioning to mode: {target_mode} (duration: {self.transition_duration:.0f}s)")

    @property
    def current_mode(self) -> str:
        """Current effective mode name."""
        if self.in_transition:
            # Find which regime target matches
            for target, values in WEATHER_REGIMES.items():
                if all(abs(self.target_values.get(k, 0) - values.get(k, 0)) < 0.1 for k in values):
                    return target
        return self.mode

    def _interpolate(self, key: str, start: float, end: float, t: float) -> float:
        """Smooth interpolation with easing."""
        # Ease-in-out
        t = t * t * (3 - 2 * t)
        val = start + (end - start) * t
        # Add jitter
        jitter = val * self.jitter_amplitude * random.uniform(-1, 1)
        return val + jitter

    @property
    def resolved_values(self) -> dict:
        """Resolve the current weather values with interpolation and diurnal scaling."""
        if not self.current_values or self._all_zeros(self.current_values):
            self.current_values = WEATHER_REGIMES.get(self.mode, WEATHER_REGIMES["clear"]).copy()

        if self.in_transition:
            base = {}
            for key in self.target_values:
                start_val = self.transition_start_values.get(key, 0.0)
                end_val = self.target_values[key]
                base[key] = self._interpolate(key, start_val, end_val, self.transition_progress)
        else:
            base = self.current_values.copy()

        # ── Diurnal radiation scaling ──
        # Daylight hours: sunrise to sunset centered on noon
        sunrise_hour = (12.0 - self.day_length_hours / 2.0)  # e.g., 4.5 for 15h day
        sunset_hour = sunrise_hour + self.day_length_hours    # e.g., 19.5

        is_daylight = sunrise_hour <= self.hour < sunset_hour

        if is_daylight:
            # Solar position: fraction from sunrise to sunset (0→1)
            day_progress = (self.hour - sunrise_hour) / self.day_length_hours
            # Radiation sine curve: 0 at edges, peak at midday
            sun_factor = math.sin(day_progress * math.pi)
            # Base clear-sky global horizontal irradiance
            clear_sky_rad = 980.0
            # Scale by cloud cover
            cloud_factor = max(0.1, 1.0 - (base.get("cloud_cover", 50) / 100.0) * 0.85)
            base["shortwave_radiation_instant"] = clear_sky_rad * sun_factor * cloud_factor
        else:
            base["shortwave_radiation_instant"] = 0.0

        # Preserve is_day from regime default, overridden by actual solar position
        base["is_day"] = is_daylight

        # ── Terrestrial radiation ──
        cc_frac = max(0.0, min(1.0, base.get("cloud_cover", 50) / 100.0))
        base["terrestrial_radiation_instant"] = (
            310.0  # night base
            + cc_frac * 50.0  # clouds trap IR → more IR at surface
            - (10.0 if is_daylight else 0.0)  # slight decrease during day
        )

        # ── Temperature diurnal cycle ──
        day_temp = base.get("temperature_2m", 15.0)
        night_base = day_temp - 4.0
        if is_daylight:
            # Warmest around 14:00-15:00
            afternoon_progress = min(1.0, (self.hour - 10.0) / 5.0)
            base["temperature_2m"] = night_base + (day_temp - night_base) * afternoon_progress + random.uniform(-0.8, 0.8)
        else:
            # Cooling in the evening, steady at night
            if 21.0 <= self.hour < 24.0:
                cool_progress = (self.hour - 21.0) / 3.0
                base["temperature_2m"] = night_base - 1.0 * cool_progress + random.uniform(-0.5, 0.5)
            else:
                base["temperature_2m"] = night_base - 1.0 + random.uniform(-0.5, 0.5)

        # ── Weather code from conditions ──
        cc = max(0.0, min(100.0, base.get("cloud_cover", 50)))
        rain = max(0.0, base.get("rain", 0.0))
        if base.get("snowfall", 0) > 0:
            base["weather_code"] = 75
        elif rain >= 5:
            base["weather_code"] = 96
        elif rain >= 2:
            base["weather_code"] = 63
        elif rain > 0:
            base["weather_code"] = 61
        elif cc > 95:
            base["weather_code"] = 3
        elif cc > 70:
            base["weather_code"] = 3
        elif cc > 30:
            base["weather_code"] = 2
        elif cc > 10:
            base["weather_code"] = 1
        else:
            base["weather_code"] = 0

        # ── Relative humidity ──
        base["relative_humidity_2m"] = max(30.0, min(100.0,
            60.0 + (cc - 50.0) * 0.5 - base["shortwave_radiation_instant"] / 30.0 +
            (30.0 if self.hour < 6.0 or self.hour > 22.0 else -10.0)
        ))

        # ── Wind ──
        wind_base = base.get("wind_speed_10m", 10.0)
        wind_variation = wind_base * random.uniform(-0.15, 0.15)
        if self.mode == "storm":
            wind_variation *= 1.5
        base["wind_speed_10m"] = max(0.0, wind_base + wind_variation)
        base["wind_direction_10m"] = (base.get("wind_direction_10m", 200.0) + random.uniform(-5.0, 5.0)) % 360

        # Solar sync override
        if self.solar_production_sync is not None:
            load_factor = self.solar_production_sync / 2300.0 if self.solar_production_sync > 0 else 0
            matched_radiation = load_factor * 980.0
            base["shortwave_radiation_instant"] = matched_radiation
            matched_clouds = max(0, min(100, 100 - int(load_factor * 100)))
            base["cloud_cover"] = matched_clouds
            base["cloud_cover_low"] = int(matched_clouds * 0.5)
            base["cloud_cover_mid"] = int(matched_clouds * 0.3)
            base["cloud_cover_high"] = int(matched_clouds * 0.2)
            if matched_clouds > 90:
                base["weather_code"] = 3
            elif matched_clouds > 50:
                base["weather_code"] = 3
            elif matched_clouds > 10:
                base["weather_code"] = 2
            else:
                base["weather_code"] = 0
            base["is_day"] = matched_radiation > 10

        return base

    @staticmethod
    def _all_zeros(d: dict) -> bool:
        return all(v == 0 for v in d.values())


# ────────
weather_state = WeatherSimState()

# ───────────────────────────────────────────
# Build Open-Meteo-compatible response
# ───────────────────────────────────────────

WMO_CODES = {
    0: "Clear sky", 1: "Mainly clear", 2: "Partly cloudy", 3: "Overcast",
    45: "Fog", 48: "Depositing rime fog",
    51: "Light drizzle", 53: "Moderate drizzle", 55: "Dense drizzle",
    56: "Light freezing drizzle", 57: "Dense freezing drizzle",
    61: "Slight rain", 63: "Moderate rain", 65: "Heavy rain",
    66: "Light freezing rain", 67: "Heavy freezing rain",
    71: "Slight snow fall", 73: "Moderate snow fall", 75: "Heavy snow fall",
    77: "Snow grains", 80: "Slight rain showers", 81: "Moderate rain showers",
    82: "Violent rain showers", 85: "Slight snow showers", 86: "Heavy snow showers",
    95: "Thunderstorm", 96: "Thunderstorm with slight hail", 99: "Thunderstorm with heavy hail",
}


def build_response(state: WeatherSimState) -> dict:
    """Build a realistic Open-Meteo v1/forecast-compatible JSON response."""
    vals = state.resolved_values
    sr, ss = compute_sun_times(state.year, state.month, state.day, state.longitude, state.latitude)

    return {
        "latitude": round(state.latitude, 5),
        "longitude": round(state.longitude, 5),
        "generationtime_ms": round(random.uniform(2, 30), 3),
        "utc_offset_seconds": 0,
        "timezone": "GMT",
        "timezone_abbreviation": "GMT",
        "elevation": 200.0,
        "current": {
            "time": f"{state.year:04d}-{state.month:02d}-{state.day:02d}T{int(state.hour):02d}:{int((state.hour % 1) * 60):02d}:00",
            "interval": 900,  # 15 min intervals (standard Open-Meteo)
            "temperature_2m": round(vals["temperature_2m"] + random.uniform(-0.3, 0.3), 1),
            "weather_code": vals["weather_code"],
            "cloud_cover": round(vals["cloud_cover"]),
            "cloud_cover_low": round(vals.get("cloud_cover_low", 0)),
            "cloud_cover_mid": round(vals.get("cloud_cover_mid", 0)),
            "cloud_cover_high": round(vals.get("cloud_cover_high", 0)),
            "shortwave_radiation_instant": round(vals["shortwave_radiation_instant"] + random.uniform(-5, 5), 1),
            "terrestrial_radiation_instant": round(vals.get("terrestrial_radiation_instant", 350.0) + random.uniform(-3, 3), 1),
            "wind_speed_10m": round(vals.get("wind_speed_10m", 10.0) + random.uniform(-2, 2), 1),
            "wind_direction_10m": round(vals.get("wind_direction_10m", 180.0) + random.uniform(-3, 3)),
            "rain": round(vals.get("rain", 0.0) + random.uniform(-0.1, 0.1), 1),
            "snowfall": round(vals.get("snowfall", 0.0) + random.uniform(-0.05, 0.05), 1),
            "relative_humidity_2m": round(vals.get("relative_humidity_2m", 60.0), 0),
            "is_day": vals["is_day"],
            "precipitation": round(vals.get("rain", 0.0), 1),
        },
        " past": {
            "temperature_2m": [round(vals["temperature_2m"] - 2.0, 1)],
            "weather_code": [vals["weather_code"]],
        },
        "daily": {
            "sunrise": [sr],
            "sunset": [ss],
        },
    }


# ───────────────────────────────────────────
# HTTP handlers
# ───────────────────────────────────────────

async def handle_weather(request):
    """GET /weather — Open-Meteo compatible response with dynamically evolving weather."""
    mode = request.query.get("mode")
    force = request.query.get("force", "false").lower() == "true"
    solar_override = request.query.get("solar")

    if solar_override:
        try:
            weather_state.solar_production_sync = float(solar_override)
            weather_state.force_recompute = True
        except ValueError:
            pass

    # Mode override (manual intervention)
    if mode:
        if mode in WEATHER_REGIMES:
            weather_state.mode = mode
            if not weather_state.in_transition:
                weather_state._start_transition(mode, 0.01)
            print(f"[weather] mode overridden to: {mode}")
        else:
            return web.json_response({"error": f"Unknown mode: {mode}. Available: {list(WEATHER_REGIMES.keys())}"}, status=400)

    if force or mode:
        # Quick recompute
        weather_state.force_recompute = True

    # Reset solar sync after one use
    if weather_state.force_recompute and weather_state.solar_production_sync is not None:
        weather_state.solar_production_sync = None

    response = build_response(weather_state)
    return web.json_response(response)


async def handle_weather_status(request):
    """GET /weather/status — current weather state info."""
    vals = weather_state.resolved_values
    return web.json_response({
        "mode": weather_state.mode,
        "current_mode": weather_state.current_mode,
        "hour": round(weather_state.hour, 3),
        "speed_up": weather_state.speed_up,
        "in_transition": weather_state.in_transition,
        "transition_progress": round(weather_state.transition_progress, 3),
        "season": round(weather_state.season, 2),
        "day_length_hours": round(weather_state.day_length_hours, 1),
        "current_values": {k: round(v, 2) if isinstance(v, float) else v for k, v in vals.items()},
    })


async def handle_weather_scenarios(request):
    """GET /weather/scenarios — list available weather regimes."""
    return web.json_response({
        "regimes": list(WEATHER_REGIMES.keys()),
        "defaults": {
            "temperature": "12-22°C (seasonal)",
            "cloud_cover": "0-100%",
            "radiation": "0-980 W/m²",
            "wind_speed": "4-45 km/h",
        }
    })


async def handle_weather_set_mode(request):
    """POST /weather/mode — manually set weather mode.
    Body: {"mode": "clear" | "storm" | ...}"""
    try:
        body = await request.json()
        mode = body.get("mode")
        if not mode:
            return web.json_response({"error": "Missing 'mode' in body"}, status=400)
        if mode not in WEATHER_REGIMES:
            return web.json_response({"error": f"Unknown mode: {mode}"}, status=400)
        weather_state.mode = mode
        if not weather_state.in_transition:
            weather_state._start_transition(mode, 0.01)
        print(f"[weather] mode set via POST: {mode}")
        return web.json_response({"ok": True, "mode": mode})
    except Exception as e:
        return web.json_response({"error": str(e)}, status=400)


async def handle_weather_set_time(request):
    """POST /weather/time — set simulated clock.
    Body: {"hour": 14.5, "day": 15, "month": 5, "year": 2026, "speed_up": 10}"""
    try:
        body = await request.json()
        if "speed_up" in body:
            weather_state.speed_up = body["speed_up"]
        for key in ["hour"]:
            if key in body:
                setattr(weather_state, key, float(body[key]))
        for key in ["day", "month", "year"]:
            if key in body:
                setattr(weather_state, key, int(body[key]))
        print(f"[weather] time set: hour={weather_state.hour:.2f}, day={weather_state.day}, mode={weather_state.mode}, speed={weather_state.speed_up}x")
        return web.json_response({"ok": True})
    except Exception as e:
        return web.json_response({"error": str(e)}, status=400)


async def handle_sunshine(request):
    """GET /sunshine — sun position data for current simulated time."""
    hour_frac = weather_state.hour / 24.0
    elev = solar_elevation_angle(hour_frac)
    azimuth = solar_azimuth_angle(hour_frac)
    elev_deg = math.degrees(elev)
    azimuth_deg = math.degrees(azimuth)
    return web.json_response({
        "simulated_hour": round(weather_state.hour, 3),
        "is_day": weather_state.resolved_values.get("is_day", False),
        "elevation_deg": round(elev_deg, 2),
        "azimuth_deg": round(azimuth_deg, 2),
        "day_length_hours": round(weather_state.day_length_hours, 1),
    })


# ───────────────────────────────────────────
# Main loop — update weather state continuously
# ───────────────────────────────────────────

async def weather_update_loop():
    """Main loop that updates the weather state."""
    last_time = time.time()
    print("[weather] Engine started — weather evolving automatically")
    while True:
        now = time.time()
        dt = now - last_time
        last_time = now
        weather_state.update(dt)
        await asyncio.sleep(0.0)  # yield control each tick


# ───────────────────────────────────────────
# HTTP server
# ───────────────────────────────────────────

def create_app(port: int = 9090):
    """Create and configure the HTTP application."""
    app = web.Application()
    app.router.add_get('/weather', handle_weather)
    app.router.add_get('/weather/status', handle_weather_status)
    app.router.add_get('/weather/scenarios', handle_weather_scenarios)
    app.router.add_post('/weather/mode', handle_weather_set_mode)
    app.router.add_post('/weather/time', handle_weather_set_time)
    app.router.add_get('/sunshine', handle_sunshine)
    return app, port


async def main(port: int = 9090):
    app, port = create_app(port)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, '0.0.0.0', port)
    await site.start()
    print(f"Weather simulator listening on http://0.0.0.0:{port}/weather")
    print(f"Available regimes: {', '.join(WEATHER_REGIMES.keys())}")
    print(f"Example: GET  http://localhost:{port}/weather?mode=storm")
    print(f"Example: POST http://localhost:{port}/weather/mode (body: {{'mode': 'clear'}})")
    print(f"Example: GET  http://localhost:{port}/weather (auto-evolving weather)")
    print(f"Example: POST http://localhost:{port}/weather/time (body: {{'hour': 14.5}})")

    # Run the weather update loop in the background
    await asyncio.gather(
        weather_update_loop(),
        asyncio.get_event_loop().create_future(),  # keep alive
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Time-Evolving Weather Simulator for ESP32")
    parser.add_argument("--port", type=int, default=9090, help="HTTP server port")
    parser.add_argument("--speed-up", type=float, default=1.0, help="Simulation speed multiplier (0.1-100)")
    parser.add_argument("--mode", type=str, default="clear", help="Starting weather mode")
    parser.add_argument("--season", type=float, default=4.5, help="Season (0=Jan, 6=Jul, 12=Jan next)")
    parser.add_argument("--temperature-base", type=float, default=18.0, help="Base temperature")
    parser.add_argument("--latitude", type=float, default=48.0, help="Latitude")
    parser.add_argument("--longitude", type=float, default=2.3, help="Longitude")
    args = parser.parse_args()

    weather_state.mode = args.mode
    weather_state.speed_up = args.speed_up
    weather_state.season = args.season
    weather_state.latitude = args.latitude
    weather_state.longitude = args.longitude

    try:
        print(f"Weather simulator: mode={args.mode}, speed={args.speed_up}x, season={args.season}")
        asyncio.run(main(port=args.port))
    except KeyboardInterrupt:
        print("\nWeather simulator stopped.")
