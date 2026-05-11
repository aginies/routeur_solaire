# Hardware Implementation Guide

## Where to Buy (Shopping List)

You can find all the necessary components for this PCB on AliExpress or other major electronics retailers. Below are curated search and direct links:

| Component | Recommended Part | Purchase Links |
| :--- | :--- | :--- |
| **U1: ESP32-S3** | ESP32-S3-WROOM-1-N16R8 | [AliExpress Search](https://www.aliexpress.com/w/wholesale-ESP32-S3-WROOM-1-N16R8.html) |
| **U2: Energy Meter** | JSY-MK-194G (Single Phase) | [AliExpress Link](https://www.aliexpress.com/item/1005006136142011.html) |
| **U3: LDO Regulator** | AMS1117-3.3 (SOT-223) | [AliExpress Link](https://www.aliexpress.com/item/32832675358.html) |
| **D1: ESD Diode** | PRTR5V0U2X or USBLC6 | [AliExpress Search](https://www.aliexpress.com/w/wholesale-PRTR5V0U2X.html) |
| **J1: USB-C Port** | USB-C 2.0 (GCT USB4135 style) | [AliExpress Search](https://www.aliexpress.com/w/wholesale-USB-C-Vertical-Receptacle.html) |
| **J4/J5: Terminals** | 2-pin 5.08mm Screw Terminals | [AliExpress Link](https://www.aliexpress.com/item/32845661413.html) |
| **J6: Fan** | 4010 5V PWM Fan (e.g. Noctua) | [AliExpress Link (Generic)](https://www.aliexpress.com/w/wholesale-4010-5v-pwm-fan.html) / [Noctua Link](https://www.aliexpress.com/item/1005001936813874.html) |
| **Relay Module** | 5V 1-Channel (High Level Trigger) | [AliExpress Link](https://www.aliexpress.com/item/32858302901.html) |
| **J2/J7: Headers** | 2.54mm Pin Headers / JST-XH | [AliExpress Search](https://www.aliexpress.com/w/wholesale-2.54mm-header-JST-XH.html) |
| **SW1/SW2: Buttons** | 6x6x5mm Tactile SMD 4-pin | [AliExpress Link](https://www.aliexpress.com/item/32858302188.html) |
| **Passives** | 10µF Caps, 100nF Caps, 10k/4.7k Res | [SMD Passive Assortment](https://www.aliexpress.com/w/wholesale-0402-capacitor-resistor-sample-book.html) |

---

## Bill of Materials (BOM)

| Ref | Component | Value / Part | Qty | Notes |
| :--- | :--- | :--- | :--- | :--- |
| U1 | ESP32-S3 module | ESP32-S3-WROOM-1-N16R8 | 1 | 41-pin castellated, PCB antenna |
| U2 | Energy meter | JSY-MK-194G | 1 | 2-channel, TTL Modbus-RTU |
| U3 | LDO regulator | AMS1117-3.3 | 1 | SOT-223, 800mA (Requires 10µF caps) |
| J1 | USB connector | USB-C 2.0 | 1 | Power + optional Serial (see notes) |
| J2 | JSY connector | 5-pin JST-XH 2.54mm | 1 | VCC, GND, JSY1_TX, JSY1_RX, Zx |
| J4 | SSR Output | 2-pin screw terminal 5mm | 1 | SSR Control (IO17) |
| J5 | Relay Output | 3-pin screw terminal 5mm | 1 | 5V, GND, Signal (IO6) |
| J6 | Fan Header | 4-pin 2.54mm header | 1 | 5V, GND, TACH (NC), PWM (IO5) |
| J7 | Temp Sensor | 3-pin 2.54mm header | 1 | 3V3, Data (IO16), GND |
| C1–C2 | Bulk cap | 10µF / 10V electrolytic | 2 | Near LDO and module VCC |
| C3–C8 | Decoupling cap | 100nF MLCC 0402 | 6 | 100nF per VCC pin |
| R1 | Pull-up | 10kΩ 0402 | 1 | EN -> 3V3 |
| R2 | Pull-up | 10kΩ 0402 | 1 | GPIO0 -> 3V3 |
| R3 | Pull-down | 10kΩ 0402 | 1 | GPIO46 -> GND |
| R4 | Pull-up | 4.7kΩ 0402 | 1 | DS18B20 Data (IO16) -> 3V3 |
| R5, R6 | Pull-down | 5.1kΩ 0402 | 2 | USB-C CC1/CC2 -> GND (Critical for 5V sync) |
| SW1 | Boot button | Tactile SMD 4-pin | 1 | GPIO0 to GND |
| SW2 | Reset button | Tactile SMD 4-pin | 1 | EN to GND |
| D1 | TVS diode | PRTR5V0U2X or USBLC6 | 1 | USB port protection |

## ESP32-S3 Pin Map

| ESP32-S3 Pin | Function | Connects to | Notes |
| :--- | :--- | :--- | :--- |
| 3V3 | Power | LDO output, JSY VCC | All VCC pins tied together |
| GND | Ground | Common GND plane | Solid pour, connect all GND pads |
| IO17 | PWM | J4 Pin 1 | Active-HIGH SSR Control |
| IO5 | UART1 TX | J2 JSY RX | GPIO out (ESP) to JSY RX |
| IO4 | UART1 RX | J2 JSY TX | GPIO in (ESP) from JSY TX |
| IO6 | Output | J5 Pin 3 | Active-HIGH trigger for Safety Relay |
| IO16 | Data | J7 Pin 2 | DS18B20 1-Wire Temp Sensor |
| IO7 | PWM | J6 Pin 4 | 5V PWM Fan speed control |
| IO15 | Input | J2 JSY Zx | Zero-crossing sync from JSY |
| IO48 | Status LED | Internal | WS2812 Onboard LED |
| IO0 | Boot mode | 10kΩ -> 3V3, SW1 to GND | Must be HIGH at normal boot |
| EN | Reset | 10kΩ -> 3V3, SW2 to GND | Active-low reset |
| IO46 | Strapping | 10kΩ -> GND | Must be LOW at boot |
| IO19/20 | Native USB | J1 USB-C | Fully available for Serial/DFU |


The schematic is designed for a single mainboard where all peripherals (JSY, SSR, Relay, Fan, Sensor) can be plugged in directly.

Key design decisions:
Power: The AMS1117-3.3 takes 5V from USB/J1. The 5V Fan (J6) is powered directly from the 5V rail to save regulator capacity.

JSY Connection: IO4/IO5 are used for UART1 communication with the JSY-194G. This preserves IO19/IO20 for native USB serial/data.

---
## ESP32-S3 Pin-Out Map (Production Config)

This map groups the central ESP32-S3 module's pins by their physical/logical function:

      [ PERIPHERALS ]           [ ESP32-S3 PINS ]          [ SYSTEM / POWER ]
                                ┌─────────────────┐
    ( Energy Meter )            │       GND   [1] │ <────┐ [ Common Ground ]
    JSY TX ───────────(Data In)─│ IO4   3.3V  [2] │ <────┘ [ AMS1117-3.3 OUT ]
    JSY RX ◀───────(Data Out)─→ │ IO5             │             + [ C1-C8 Caps ]
    JSY Zx ──────────(Sync In)->│IO15        IO19 │ ◀───->  USB D- (Serial/DFU)
                                │            IO20 │ ◀───->  USB D+ (Serial/DFU)
    ( Load Control )            │ [ACTUATORS]     │
    SSR IN ◀──────────(PWM Out)─│ IO17         EN │ ◀────  Reset [ SW2 + R1 ]
    RELAY  ◀──────────(DRV Out)─│ IO6         IO0 │ ◀────  Boot  [ SW1 + R2 ]
    FAN    <——───────(PWM Out)——│ IO7        IO46 │ ────->  [ R3 Pull-Down ]
                                │                 │
    ( Sensing / UI )            │ [UI & SENSE]    │
    TEMP DAT ◀───────(1-Wire)─> │ IO16            │ <────  [ R4 Pull-Up ]
    (WS2812) ◀───────(Status)── │ IO48            │
                                └─────────────────┘

---
## Mandatory Strapping Logic (IO46)

Pin **IO46** is a mandatory strapping pin on the ESP32-S3. It **must** be LOW (Ground) at the exact moment of power-on, or the chip will fail to start the software. 

A "Pull-down" resistor has two ends and is wired as follows:
1.  **End A**: Connects to the ESP32 **IO46** pin.
2.  **End B**: Connects to the **GND** (Ground) plane.

```text
  [ ESP32-S3 ]
       |
    (IO46) Pin
       |
       +-------[ R3 10kΩ ]------- ( GND Plane )
```

**Note on Placement**: Place **R3** physically close to the IO46 pad on your PCB. This prevents the trace from acting as an antenna and picking up noise that could accidentally "pull" the pin HIGH during boot.

---
## Pull-up Resistors (EN, IO0, IO16)

A "Pull-up" resistor does the opposite of a pull-down: it ensures a pin stays at **3.3V (HIGH)** when not being actively pressed or driven. 

Like all resistors, it has two ends:
1.  **End A**: Connects to the ESP32 Pin (or Sensor Data Pin).
2.  **End B**: Connects to the **+3.3V Power Rail**.

```text
       ( +3.3V Power Rail )
               |
               +-------[ R1, R2, or R4 ]------- ( ESP Pin / IO16 )
```

### Specific Roles in this Design:
- **R1 (10kΩ on EN)**: Keeps the ESP32 in "Run" mode. If this pin goes LOW (via SW2 button), the chip resets.
- **R2 (10kΩ on IO0)**: Ensures the chip starts your software. If this pin is LOW during power-on (via SW1 button), the chip enters "Download/Flash" mode.
- **R4 (4.7kΩ on IO16)**: Essential for the **DS18B20** Temperature Sensor. Without this pull-up, the sensor cannot communicate digital data back to the ESP32.

---
## User Interaction (Buttons SW1 & SW2)

These are momentary tactile switches that allow you to control the state of the ESP32-S3. They work by **momentarily shorting** a pin to **Ground** when pressed.

Like all buttons, they have two main sets of pins:
1.  **Side A**: Connects to the ESP32 Pin (**EN** or **IO0**).
2.  **Side B**: Connects to the **GND** (Ground) plane.

```text
  [ ESP32-S3 ]
       |
    (EN or IO0) Pin
       |
       +-------[ PUSH BUTTON ]------- ( GND Plane )
```

### Roles in this Design:
- **SW2 (Reset / EN)**: When you press this, the ESP32 reboots immediately. It pulls the **EN** pin to Ground.
- **SW1 (Boot / IO0)**: Used for flashing firmware. If you hold this button down while powering on (or while tapping Reset), the chip enters a special "Download Mode" so you can upload your code.

**Interaction with Pull-ups**: These buttons work in tandem with resistors **R1 and R2**. The resistors keep the pins at 3.3V (HIGH) normally, and the buttons force them to 0V (LOW) only when you physically press them.

---
## Safety Relay Implementation

The **Safety Relay** is a secondary protection layer. While the SSR handles high-speed power switching, the mechanical relay provides a physical disconnect of the circuit during:
1.  **Emergency Faults** (Overheating, Sensor disconnected).
2.  **Night Mode** (Prevents power leakage during non-solar hours).

### Wiring Connection (J5 Terminals):
The J5 screw terminal provides power and the control signal for an **external 5V Relay Module**.

```text
  [ J5 TERMINAL ]          [ 5V RELAY MODULE ]
        |                         |
      ( Pin 1 ) ────────-> [ VCC / JD-VCC ] (5V)
      ( Pin 2 ) ────────-> [ GND Pin ]
      ( Pin 3 ) ────────-> [ IN / Signal Pin ] (IO6)
```

### Fail-Safe Configuration:
To ensure the water heater still works if the ESP32 is powered off, wire your AC load through the **COM** and **NC (Normally Closed)** pins of the relay module.
- **ESP32 Active (Normal)**: IO6 is `LOW`, Relay is OFF -> Circuit **CLOSED** (Power to SSR).
- **ESP32 Fault / Night**: IO6 is `HIGH`, Relay is ON -> Circuit **OPEN** (No power to SSR).

---
## Overcurrent Protection (Polyfuse F1)

A **Polyfuse** is a resettable thermal fuse. It protects your PCB and your USB port (computer/charger) from damage if there is a short circuit. 

It is connected **in series** with the 5V power line coming from the USB connector:
1.  **End A**: Connects to the **VBUS (5V)** pin of the USB-C connector.
2.  **End B**: Connects to the **Input** of the AMS1117 regulator and the **5V Fan**.

```text
 ( USB-C VBUS 5V )
         |
         +-------[ F1 500mA ]------- ( Internal 5V Rail )
                                             |
                                     +-------+-------+
                                     |               |
                               [ LDO 3.3V IN ]   [ J6 FAN VCC ]
```

**How it works**: If you accidentally short a 5V wire to Ground, the Polyfuse will get hot and "trip," cutting off the current. Once you remove the short and the fuse cools down, it automatically resets and allows power again.

---
## USB ESD Protection (D1 - PRTR5V0U2X)

Static electricity (ESD) can destroy the delicate data pins of the ESP32-S3 when you plug or unplug the USB cable. The **PRTR5V0U2X** is a high-speed protection diode that diverts these dangerous high-voltage spikes away from the chip.

It sits "in parallel" with your USB lines and must be connected as follows:

```text
  ( USB-C PINS )          ( D1 PROTECTION )          ( ESP32 PINS )
        |                        |                         |
      [VBUS] ───────────────-> [ Pin 4 ]                   [VCC]
        |                        |                         |
      [D - ] ──────────┬────-> [ Pin 3 ] ──────────┬─────-> [IO19]
        |              |         |                 |       |
      [D + ] ──────────┼────-> [ Pin 6 ] ──────────┼─────-> [IO20]
        |              |         |                 |       |
      [GND ] ──────────┴────-> [ Pin 1 ] ──────────┴─────-> [GND]
```

**How it works**: Under normal conditions, the diode does nothing. But if a 1000V static spark enters through the USB port, the diode instantly "clamps" the voltage and dumps the excess energy to Ground (GND), saving your ESP32-S3 from being fried.

---
## Power Stability (Capacitors C1-C8)

Capacitors act as tiny temporary batteries that keep the voltage steady. They have two ends and must be connected across the power and ground:
1.  **End A**: Connects to the **Power Rail** (5V or 3.3V).
2.  **End B**: Connects to the **GND** (Ground) plane.

```text
       ( Power Rail: 5V or 3.3V )
               |
               +-------[ Capacitor ]------- ( GND Plane )
```

### Types Used in this Design:

**1. Bulk Capacitors (C1, C2 — 10µF)**:
These are the "reservoir" batteries. They are larger and store more energy to smooth out big dips in voltage and prevent the regulator from oscillating.

They are connected **in parallel** with the power pins of the LDO:
- **C1**: Connected between the **Input (5V)** pin and **GND**.
- **C2**: Connected between the **Output (3.3V)** pin and **GND**.

```text
 ( 5V Internal )                    ( 3.3V Rail )
        |                                 |
        +------[ LDO REGULATOR ]----------+
        |        (AMS1117)                |
        |                                 |
  [ C1 10µF ]                       [ C2 10µF ]
        |                                 |
      (GND)                             (GND)
```

**2. Decoupling Capacitors (C3-C8 — 100nF)**:
These are "filter" batteries. They are smaller and faster, designed to catch high-frequency noise that can crash the module.
- **Requirement**: Place one as close as physically possible to **every 3.3V power pin** on the ESP32-S3 and the JSY-194G.

