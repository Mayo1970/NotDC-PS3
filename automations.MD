# Automated test runs on the PS3

Status: plan only. Nothing here is built yet.

Target hardware: PS3 Slim, Evilnat CFW (full CFW, Cobra built in), multiMAN installed.
RPCS3 is not used. All runs are on the real console.

## Goal

A script on the PC (or a Linux box) does these steps without manual input:

1. Builds the SELF/PKG in Docker (`./build.sh pkg`).
2. Sends the build to the PS3.
3. Starts the emulator with a named disc.
4. Collects the UDP log (port 18194) and the stats.
5. Stops the run and gets the console back to a known state.
6. Saves the results (CSV per run) for comparison.

Render correctness still needs a human check. Speed, freezes and crashes can be fully automatic.

## Components

### 1. Orchestrator

- Use the Windows PC with Docker, or a small Linux box (for example a Raspberry Pi).
- One script controls the full cycle: build, deploy, launch, log, stop.
- The UDP log listener is already in use (Windows Python, port 18194). The script adds a CSV parser for the `[hb]`, `[rend]` and stats lines.

### 2. Deploy without a PKG install

- A PKG install wipes `/dev_hdd0/data/NullDC/`, so every menu toggle resets. Avoid it for automated runs.
- Option A: multiMAN FTP server (port 21). Upload the new EBOOT.BIN over the installed app.
- Option B: webMAN MOD FTP (port 21). webMAN MOD runs on Evilnat.
- Option C: `ps3load` (TCP port 4299) sends a SELF directly.

### 3. Remote control over the network

- webMAN MOD has HTTP commands. The script calls them with `curl`:
  - launch or mount an app,
  - go back to the XMB,
  - show a popup,
  - restart or shut down the console.
- Current state: only multiMAN is installed, not webMAN MOD. multiMAN has no HTTP control, and its FTP server runs only while multiMAN is open.
- To use this step, install webMAN MOD (PKG, loaded as a Cobra plugin on Evilnat).

### 4. Bench mode in the emulator (most important)

Add a config entry, for example `nullDC.cfg [ps3] Bench=`. When it is set, the emulator does these steps:

1. Skips the browser and boots the named disc.
2. Runs for N seconds.
3. Sends a final summary line over UDP (SPEED%, VPS, RPS, `job_max`, `drop`, freeze flag).
4. Exits to the XMB.

With bench mode, most runs need no controller input.
The script writes the `Bench=` settings into `nullDC.cfg` over FTP before each run.
This also allows A/B runs of a toggle (for example Render Thread ON/OFF, TA Fast Store fsm/split).

### 5. Controller input over USB (optional)

- Plug a Raspberry Pi Pico into a PS3 USB port and flash it as a USB HID gamepad.
- A plain USB-to-USB (A-to-A) cable from the PC does not work for this. Both ends are USB hosts, so the PC cannot act as a gamepad. The cable can also damage a port. The device on the PS3 side must support USB device mode (Pico, Arduino Leonardo, or a Raspberry Pi in OTG gadget mode).
- The orchestrator drives it over a serial link.
- It can press buttons in the XMB and in games (menus, driving, attract mode).
- webMAN MOD also has a virtual pad. It is not known if `ioPad` in homebrew sees it. Test this on hardware first.

### 6. Freeze recovery

This is the weak point of the setup.

- Soft failure (only the SH4 thread dies, the OS is alive): webMAN MOD can still restart the console.
- Hard failure (the RSX hangs the full console): only a power cycle recovers it.
- Decision (Matt): the power cycle is manual. No relay.
- The orchestrator detects a freeze (no UDP log, or no `[hb]` progress in `pc`/`vbl`, for N seconds) and notifies Matt.
- After Matt power-cycles the console, the script continues with the next run.
- Evilnat is a full CFW, so homebrew runs directly after a cold boot. No re-enable step is necessary.

### 7. Screen capture (optional)

- An HDMI capture card on the orchestrator gives screenshots and short clips.
- Use it to compare frames between builds and to detect a black or frozen picture.

## Recommended order

1. Steps 2, 3 and 4. They need no extra hardware.
2. Step 6 freeze detection and notification.
3. Step 5 USB gamepad, for XMB navigation and input-driven tests.
4. Step 7 capture card, only if frame checks become necessary.

## Open questions

- Does the webMAN MOD virtual pad reach `ioPad` in homebrew?
