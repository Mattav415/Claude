# Solar / Off-Grid Inverter Dashboard for Home Assistant

Turns your existing hardware (Solar Assistant, Fusion Energy monitor, DTE
Energy Bridge) into a dashboard showing current & daily solar production,
whole-home usage, kWh saved by solar, and daily energy cost / savings —
without any solar production sensor existing on your inverter.

## Your setup, as understood

```
Utility grid
   |
   v
[200A main panel] -----------------------------> unbacked home circuits
   |
   | 240V breaker
   v
[Off-grid inverter] <----> Solar array
   |          <----> Battery bank (Solar Assistant reads this via RS485/BT)
   v
[100A sub-panel] -------> backed-up circuits
```

- DTE Energy Bridge = total grid import, measured at the utility meter
  (upstream of everything — sees both the direct home loads AND whatever
  the inverter pulls from grid to charge batteries / pass through). It's
  also upstream of a garage that's wired to tap off the service *before*
  the main panel, so it's the only sensor in this system that captures that
  garage load — that's why it's used for "grid total" instead of the
  SEM-METER's own main-panel CTs, which would miss the garage entirely.
- Solar Assistant = battery voltage/current/power/SOC only. No visibility
  into solar or inverter output.
- SEM-METER (Fusion Energy Smart Home Energy Monitor) = CT-clamp based,
  MQTT-capable, 16 channels. 4 are used here (inverter output + grid-into-
  inverter, both legs); 2 main CTs are also installed but unused by this
  package.
- Inverter = a black box. No data out at all except the LCD.

## Why you can't just subtract your way to solar production

The DTE Bridge tells you total grid draw, but not the split between "power
used directly by unbacked home circuits" and "power sent into the inverter
to charge batteries or pass through." Without separating those two, any
attempt to compute solar production or true whole-home usage will be wrong.

## The fix: 2 more CT clamp locations on the Fusion Energy monitor

You said you have 16 free channels, so use 4 of them:

1. **Inverter → sub-panel feed** (2 CTs, one per hot leg of the 240V run
   between the inverter's output and the 100A backed-up panel). This is
   the actual power reaching your backed-up loads, from whatever source
   (solar, battery, or grid pass-through).
2. **Main panel → inverter feed** (2 CTs, one per leg of the 240V breaker
   feeding the inverter). This is grid power going INTO the inverter, for
   charging the battery or passing through when solar+battery aren't
   enough.

Do not clamp directly on the solar array wiring or battery wiring — Solar
Assistant already covers the battery side, and there's no accessible spot
to clamp solar production directly (it enters the inverter internally
alongside the battery connection on most off-grid inverters).

## SEM-METER entity map

The SEM-METER was reconfigured/reinstalled and no longer uses the earlier
blueprint-generated entities. It's now set up with a hand-written raw MQTT
sensor config (`sem-meter/mqtt_sensors_combined.yaml` — kept here for
reference only; it lives in the user's own `configuration.yaml`, not in
`config/packages/`).

That file also contains the DTE Energy Bridge's two MQTT sensors, combined
into the same `mqtt:` key as the SEM-METER's. **Do not split MQTT sensors
across multiple files/packages** — a real outage happened from exactly
that: the DTE sensors originally lived in `packages/solar_dashboard.yaml`
as their own separate top-level `mqtt:` key, and once `configuration.yaml`
also defined its own `mqtt:` key for the SEM-METER, the DTE entities
silently stopped being created (Home Assistant does not reliably merge a
domain key like `mqtt:` when it's split across a package and the main
config — one side wins, the other's entities just vanish, with no error at
config-check time). If you ever add more MQTT sensors for anything else,
add them to `mqtt_sensors_combined.yaml`, not a new `mqtt:` key elsewhere.

Circuits currently used by the dashboard package:

| Circuit | Entity | Role |
|---|---|---|
| Inverter In L1 | `sensor.inverter_in_l1_active_power` | Grid power into inverter, leg 1 |
| Inverter In L2 | `sensor.inverter_in_l2_active_power` | Grid power into inverter, leg 2 |
| Inverter Out L1 | `sensor.inverter_out_l1_active_power` | Inverter output to backed-up panel, leg 1 |
| Inverter Out L2 | `sensor.inverter_out_l2_active_power` | Inverter output to backed-up panel, leg 2 |
| A/C | `sensor.a_c_active_power` | HVAC compressor/condenser |
| HVAC Blower | `sensor.hvac_blower_active_power` | HVAC air handler blower |
| Barn L1 | `sensor.barn_l1_active_power` | Barn feed, leg 1 |
| Barn L2 | `sensor.barn_l2_active_power` | Barn feed, leg 2 |

Available but not currently wired into the dashboard: Laundry, Well Pump,
Sump Pumps, and the combined "Main circuit" (total across 3 CTs — not used
since the DTE Bridge already covers whole-home grid total, including the
garage load the SEM-METER's main CTs would miss).

These entity IDs are HA's default name-derived ones (no `unique_id` is set
on the raw MQTT sensors) — if any of the SEM-METER config changes again,
verify the resulting entity IDs in Developer Tools > States before
assuming this table still applies.

## The math

With those 2 new measurement points plus your existing battery power
reading from Solar Assistant, every dashboard metric is derived:

| Metric | Formula |
|---|---|
| Solar production (now) | `inverter_output + battery_power(signed) − grid_into_inverter` |
| Whole home usage (now) | `grid_total − grid_into_inverter + inverter_output` |
| Grid offset / "saved" power | `inverter_output − grid_into_inverter` |
| kWh saved today | daily total of grid offset power |
| Solar production today | daily total of solar production power |
| Energy cost today | `(peak kWh × peak rate) + (off-peak kWh × off-peak rate)` on grid import |
| Saved via solar today | `(peak kWh × peak rate) + (off-peak kWh × off-peak rate)` on grid offset |

Note "solar production" and "kWh saved" are intentionally different
numbers: production is everything your panels harvested (including energy
that went into the battery for use later), while "saved" is energy that
avoided a grid draw *today* by being sourced from solar+battery instead.
They'll track closely but won't be identical, and that's expected.

The battery power sign convention matters: Solar Assistant's power sensor
must read **positive while charging, negative while discharging** for the
formula above to be correct. If yours is the opposite, flip the sign in
`packages/solar_dashboard.yaml` (change `+ battery` to `− battery` in the
Solar Production Power template).

## DTE Energy Bridge (v2) setup

DTE's v2 hardware has no native HA integration and no documented API — the
old `dte_energy_bridge` integration was removed from HA core because DTE
broke it years ago. The working approach: the bridge runs its own local
MQTT broker on a non-standard port, and Mosquitto's built-in **bridge**
feature can mirror its topics into your HA broker, no credentials needed
for the metering topics.

1. **Bridge IP.** `mosquitto/dte_bridge.conf` points at `192.168.68.55:2883`,
   confirmed via the router's connected-devices list. If the bridge ever
   gets a new DHCP lease, re-verify and update the `address` line (keep the
   `:2883` port).

2. **Enable Customize on the Mosquitto broker add-on.** Settings > Add-ons
   > Mosquitto broker > Configuration tab > turn on `customize: active`.
   This makes the add-on read any `*.conf` files placed in
   `/share/mosquitto/`.

3. **Copy the bridge config in.** Using the File Editor or Studio Code
   Server add-on, put `mosquitto/dte_bridge.conf` at
   `/share/mosquitto/dte_bridge.conf`.

4. **Restart the Mosquitto broker add-on** to load the bridge connection.

5. **Verify data is flowing.** Settings > Devices & Services > MQTT >
   "Listen to a topic" > subscribe to `event/metering/#`. You should see
   JSON messages arrive (instantaneous demand roughly every few seconds,
   a per-minute summation reading once a minute).

The two MQTT sensors that parse those topics
(`sensor.dte_instantaneous_demand` in Watts, and a bonus
`sensor.dte_energy_bridge` kWh sensor for HA's native Energy dashboard) are
defined in `sem-meter/mqtt_sensors_combined.yaml`, alongside the
SEM-METER's — see the "SEM-METER entity map" section above for why they
live there rather than in `packages/solar_dashboard.yaml`.

## Install steps

1. **Wire the CTs.** Add the 4 SEM-METER CT clamps as described above.
   Confirm in Developer Tools > States that the 4 corresponding power
   channels update in real time as backed-up loads change.

2. **Set up the DTE Energy Bridge connection** per the section above.

3. **Copy the package file.** Put `packages/solar_dashboard.yaml` into your
   Home Assistant `config/packages/` directory (enable packages in
   `configuration.yaml` first if you haven't: `homeassistant: packages: !include_dir_named packages`).

4. **Rates are hardcoded**, not editable helpers — sourced from the user's
   actual DTE TOU plan (Mon-Fri 11am-7pm peak, both peak and off-peak
   rates season-dependent; see the header comment in
   `packages/solar_dashboard.yaml` for the exact cents/kWh figures and
   effective dates). If DTE revises the plan, update the numbers directly
   in that file's `Current Electricity Rate` / `Energy Cost Today` /
   `Solar Savings Today` / `HVAC Cost Today` templates, and the
   `automation` trigger times if the peak window itself changes.

5. **Restart Home Assistant** (packages require a restart, not just a
   reload) to pick up the new `mqtt`, `sensor`, `utility_meter`, and
   `automation` entities.

6. **Add the dashboard.** Settings > Dashboards > Add Dashboard > take
   control of a new one, switch to YAML mode (top-right ⋮ menu), and paste
   in `dashboards/solar_dashboard.yaml`. Adjust the gauge `max:` values to
   match your array size and typical home load.

## Sanity-check before trusting the numbers

Pick a sunny midday moment with the battery neither charging nor
discharging much (SOC steady): `solar_production_power` should roughly
equal `inverter_output_power` (since `battery_power` and
`grid_into_inverter` are both near zero then).

Then check it at night with the battery discharging and no grid charging:
`solar_production_power` should read ~0, and `inverter_output_power`
should be fully explained by `battery_power` (discharging). If
`solar_production_power` shows a large nonzero value at night, your
battery sign convention is flipped — fix it per the note above.

## Daily summary export

An automation fires at 23:59 daily (just before the utility_meter daily
reset) and posts a full text summary — every daily total, cost, and the
peak/off-peak split behind them, plus battery SOC and current rate — as a
Home Assistant persistent notification (Settings > Notifications, or the
bell icon). Copy/paste the text out from there whenever you want it
reviewed; each day gets its own dated notification so they don't overwrite
each other.

## Files

- `packages/solar_dashboard.yaml` — derived power/energy sensors, utility
  meters, hardcoded TOU rates, and the peak/off-peak switching
  automations. Goes in `config/packages/`. Does NOT define any MQTT
  sensors itself (see "SEM-METER entity map" above for why).
- `dashboards/solar_dashboard.yaml` — the Lovelace view.
- `mosquitto/dte_bridge.conf` — Mosquitto bridge config connecting to the
  DTE Energy Bridge's local broker. Goes in `/share/mosquitto/` (Mosquitto
  add-on's customize folder), not `config/packages/`.
- `sem-meter/mqtt_sensors_combined.yaml` — reference copy of every MQTT
  sensor this project uses (SEM-METER + DTE Bridge, one combined `mqtt:`
  key). Lives in the user's own `configuration.yaml`, not in
  `config/packages/`.
