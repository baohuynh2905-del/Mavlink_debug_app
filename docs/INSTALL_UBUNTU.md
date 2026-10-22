# Install and test on Ubuntu (native)

Step-by-step setup on a native Ubuntu 24.04 desktop, for example a dual-boot laptop, with a Gremsy
VIO connected over USB-C. Native Linux needs no WSL workarounds: RTSP over UDP and live network
capture both work.

Tested environment: Ubuntu 24.04 (Qt 6.4, GStreamer 1.24, g++ 13). Other releases are untested.
Ubuntu 22.04 ships Qt 6.2 and may need changes.

---

## 1. Get access to the repository

The repository is private, so git needs your GitHub credentials. The simplest way is the GitHub CLI:

```bash
sudo apt update
sudo apt install -y git gh
gh auth login            # GitHub.com -> HTTPS -> "Login with a web browser"
gh auth setup-git        # lets plain `git` use the same login
```

(An SSH key added to your GitHub account works too. In that case clone with
`git@github.com:baohuynh2905-del/Mavlink_debug_app.git`.)

## 2. Install the build dependencies

```bash
sudo apt install -y build-essential cmake pkg-config \
    qt6-base-dev libgl-dev libpcap-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav \
    libgstrtspserver-1.0-dev
```

- `libgstrtspserver-1.0-dev` is optional. It enables the video test, which runs a local RTSP
  server.
- `gstreamer1.0-libav` provides the H.264 decoder the VIO stream needs.

## 3. Clone, build, and run the tests

```bash
git clone --recursive https://github.com/baohuynh2905-del/Mavlink_debug_app.git
cd Mavlink_debug_app
scripts/build.sh
```

`scripts/build.sh`:

1. checks the dependencies and prints the `apt install` line for anything missing
2. fetches the Gremsy PayloadSDK submodule if needed
3. builds (CMake applies `patches/payloadsdk-transport-observer.patch` to the SDK automatically)
4. runs the hardware-free tests

Expected result: `100% tests passed, 0 tests failed out of 9`.

If the machine is short of memory, limit the parallel compiles: `JOBS=2 scripts/build.sh`.

Optional install into your home directory:

```bash
cmake --install build --prefix ~/.local    # ~/.local/bin/mavlink_debug_app, mavlink_capture
```

## 4. Connect the VIO over USB-C

The VIO appears as a USB network adapter (RNDIS and/or NCM). Ubuntu's NetworkManager normally gets
an address from it automatically. On Windows the laptop received `192.168.55.100`.

```bash
ip -4 addr | grep -B2 192.168.55       # find the interface (e.g. usb0 or enx...) and its address
ping -c 3 192.168.55.1                 # IP reachability only, not proof that MAVLink works
```

If no `192.168.55.x` address appears:

1. Open *Settings → Network*, choose the new wired/USB connection, and set *IPv4 → Automatic
   (DHCP)*.
2. If that does not give an address, set it manually:
   `sudo nmcli con add type ethernet ifname <iface> ipv4.method manual ipv4.addresses 192.168.55.100/24`

No firewall change is needed. The app starts the UDP exchange, and Ubuntu's default `ufw` (inactive,
or allowing replies) lets the answers back in.

## 5. Run the app

```bash
./build/mavlink_debug_app
```

1. Enter `192.168.55.1`, port `14566`, and click **Connect**. The status should go to **Heartbeat
   received**, with heartbeats from `1/26, 1/100, 1/154`.
2. **Video** tab: **Start video**. On native Linux, try both **TCP** and **UDP** transport (only
   TCP worked under WSL).
3. **Profile → Save…** stores the connection for next time.

## 6. Live network capture (optional)

Capturing other devices' MAVLink (for example payload ↔ autopilot) needs raw-socket permission.
Give it to the binary once. You must repeat this after every rebuild, because rebuilding replaces
the file and drops the capability.

```bash
sudo setcap cap_net_raw,cap_net_admin=eip build/mavlink_debug_app
```

Then, in **MAVLink Inspector → Network capture**, pick the interface (for example `any`, or the one
on the drone network) and click **Start capture**.

You only see traffic that reaches this laptop. For other devices' unicast traffic, use a switch
with port mirroring or a hub, or record with `tcpdump -w file.pcap` on one of the devices and use
**Open .pcap…**.

## 7. Hardware checks still open

These have not been run on a VIO yet. Each one writes files you can send back for analysis
(attach them, or put them in a folder and share the path).

### a. Passive capture (sends nothing that moves the payload)

```bash
build/mavlink_capture --ip 192.168.55.1 --seconds 30 --out ~/vio_native
```

Keep the printed report and `~/vio_native.tlog` / `.jsonl`. This confirms the native network path
and compares it with the WSL results in the README.

### b. Read-only GUI screenshots, including video

```bash
mkdir -p ~/shots
HW_IP=192.168.55.1 SCREENSHOT_DIR=$HOME/shots QT_QPA_PLATFORM=offscreen \
    build/test_main_window hardwareScreenshots
```

### c. Control path (moves the gimbal)

**Before running:** the gimbal must be free to move, nothing within its reach, and you next to it,
able to unplug it. Each motion is small and undone afterwards. No photo or recording is taken.

```bash
build/hw_vio_commands --ip 192.168.55.1 --steps camera,angle,rate,mode-param --record ~/vio_cmds.jsonl
```

### d. Not yet run on any VIO (decide before running)

These need an explicit decision, because they change the payload's state or storage:

- **Gimbal modes OFF / RESET / MAPPING:** use the Gimbal tab. OFF asks for confirmation; the camera
  is not stabilised while it is off.
- **Photo capture and video recording:** use the Camera tab. Files are written to the SD card.
- **Rate control retest:** with `RC_MODE` = Standard. This is a persistent payload setting; note
  the current value first so it can be restored.

## Troubleshooting

| Symptom | Check |
|---|---|
| `Socket open, nothing received` | Wrong IP/port, or no route: `ip route get 192.168.55.1` should use the USB interface |
| `Receiving data, no valid MAVLink` | Wrong port: something other than the payload's MAVLink is answering |
| Video: "Could not connect to server" | Is `rtsp://192.168.55.1:8554/payload` correct? Try `gst-launch-1.0 rtspsrc location=rtsp://192.168.55.1:8554/payload ! fakesink` |
| Video: no decoder / not-negotiated | `sudo apt install gstreamer1.0-libav gstreamer1.0-plugins-bad` |
| Capture: "needs root or CAP_NET_RAW" | Run the `setcap` line again (needed after every rebuild) |
| Build stops with "Killed" or the machine freezes | Out of memory: `JOBS=2 scripts/build.sh` |
| Units and enum names missing (status bar warning) | Run from the build tree or an installed copy, or set `MAVLINK_XML_DIR` |
