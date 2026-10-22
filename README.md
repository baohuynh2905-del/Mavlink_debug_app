# Gremsy MAVLink Debug App

Desktop tool (C++17, Qt 6 Widgets, CMake) for controlling Gremsy payloads through the official
PayloadSDK and inspecting the real MAVLink traffic of that connection.

**Status: Stage 5.** Connection management with saved profiles, the MAVLink Inspector, VIO
gimbal and camera controls, telemetry, command/acknowledgement tracking, and live RTSP video are
done. Payload models other than the VIO are not built yet. Checked against a real VIO (see *Hardware checks*): connection, capture,
telemetry, read-only requests, camera zoom/focus/settings, gimbal angle and rate moves, and
gimbal mode changes.

## Layout

| Path | Contents |
|---|---|
| `third_party/PayloadSdk` | Gremsy PayloadSDK, git submodule pinned to `payloadsdk_v3` @ `fba3c70` (GPL-3.0) |
| `patches/payloadsdk-transport-observer.patch` | The only SDK modification; applied automatically by CMake |
| `src/mavlink_frame.*` | Qt-free MAVLink framing, CRC validation, and field decoding |
| `src/payload_link.*` | The single owner of the SDK connection; moves frames from the SDK's I/O threads to the GUI thread |
| `src/link_health.*` | Pure logic: endpoint validation and link-state evaluation |
| `src/frame_model.*`, `src/captured_frame.h` | Bounded table of captured frames, and traffic counters |
| `src/mavlink_dict.*` | Units, enum names, bitmasks, and descriptions from the SDK's MAVLink XML |
| `src/frame_text.*` | Summary, validation text, hex dump, and CSV/JSON encoding (one source for the table, the details pane, and the files) |
| `src/frame_filter.*`, `src/flow_stats.*`, `src/frame_writer.*` | Inspector filters, per-flow statistics, and the background file writer |
| `src/command_tracker.*` | Matches each UI action to its outgoing frame and to the payload's ACK |
| `src/telemetry.*` | Latest validated RX frames, the payload's DEBUG parameter stream, and gimbal flag rules |
| `src/payload_profile*` | Per-model parameter IDs and values, taken from the SDK's `payload-define/<model>_sdk.h` |
| `src/video_player.*`, `src/video_page.*` | RTSP video (GStreamer `appsink`) and the Video tab |
| `src/profiles.*` | Saved connection profiles |
| `src/pcap_capture.*` | Network capture (libpcap): live interface or `.pcap` file, shown as NET |
| `src/main_window.*`, `src/inspector_widget.*`, `src/stats_widget.*`, `src/payload_pages.*` | GUI |
| `tests/` | Hardware-free tests (CTest). `fake_payload.h` is a loopback UDP peer used in place of a payload. |
| `tools/check_tlog.py` | Reads a `.tlog` file with pymavlink, an independent check of recorded files |
| `tools/mavlink_capture.cpp` | Headless capture and report (`build/mavlink_capture`), for hardware checks |
| `tests/hw_vio_commands.cpp` | Hardware check of the control path on a real VIO; moves the gimbal; never run by ctest |

## Dependencies (Ubuntu 24.04, native or WSL2)

```bash
sudo apt install build-essential cmake git pkg-config \
    qt6-base-dev libgl-dev libpcap-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
# optional, enables the video test (local RTSP server):
sudo apt install libgstrtspserver-1.0-dev
```

At runtime, video needs `rtspsrc` (plugins-good), `h264parse` (plugins-bad), and `avdec_h264`
(libav).

## Build and test

```bash
git submodule update --init
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/mavlink_debug_app      # under WSLg the window opens on the Windows desktop
```

Enter the payload IP and UDP port (default `192.168.55.1:14566`), then click **Connect**. Connecting
never sends a movement or camera command.

- **Profiles:** **Save…** stores the current IP, port, and timeout under a name. Choosing a profile
  fills those fields.
- **Persistence:** profiles, the last values, inspector filters, and video settings are restored on
  start-up. Each stored value is validated again before use, and invalid entries are dropped.

### Connection status

The status only reports what has actually been observed on the link:

| Status | Meaning |
|---|---|
| Socket open, nothing received | The SDK is sending heartbeats; no reply yet. This does not prove the payload is reachable. |
| Receiving data, no valid MAVLink | Datagrams are arriving, but none parsed as a valid frame. |
| MAVLink traffic, no heartbeat | Valid frames are arriving, but no HEARTBEAT within the timeout. |
| Heartbeat received | A HEARTBEAT arrived within the timeout. "Heartbeats from" lists every sysid/compid with its age. |
| Stale | Traffic was seen earlier, but nothing within the timeout. The SDK keeps sending, so the link resumes when the payload returns. |

## MAVLink Inspector

The inspector shows every frame this app sends and receives on the SDK's link, from every system
and component ID, in observation order.

- **Table columns:** time, direction, peer endpoint, MAVLink version, sysid, compid, seq, message ID
  and name, length, target system/component, status, and the decoded summary (hover a cell for the
  full text).
- **Statuses:**
  - `OK` – CRC verified against the message's CRC_EXTRA.
  - `BAD_CRC` – known message ID, but the checksum does not match.
  - `UNKNOWN_ID` – message ID not in the ardupilotmega dialect, so the CRC cannot be verified.
  - `TRUNCATED` – the datagram ended before the frame did.
  - `GARBAGE` – bytes outside any frame.
  - Extra markers: `signed (unverified)` and `SEND FAILED: <reason>`.
- **Details pane:** validation result; decoded fields with type, units and enum or bitmask names
  (values equal to a field's "invalid" marker, such as NaN, are flagged); the header, including
  incompat/compat flags and the component name; and an annotated hex dump showing the header,
  payload, CRC and signature byte ranges.
- **Filters** (saved between runs):
  - direction
  - system and component ID
  - message (a number matches the ID exactly; text matches part of the name)
  - free text in the decoded values
  - status (valid only / problems / unknown ID)
- **Pause display** freezes the table only. Capture, statistics and recording continue; held frames
  are appended on resume.
- **Statistics tab:**
  - count, rate, bytes and last-seen time for each direction, sysid, compid and message
  - RX sequence gaps per sender, reported as *possible* loss (see below)

### Network capture (other devices' traffic)

The capture bar in the inspector adds MAVLink that is **not** this app's own link, for example
payload ↔ autopilot.

- **Live** on an interface, with a BPF filter. **Open .pcap…** loads a file, for example from
  `tcpdump -w drone.pcap` on a companion computer.
- **Display:** frames are tagged **NET**, with `source → destination` endpoints and the packet
  timestamps. They use the same table, details, filters, statistics, and export as the app's own
  link.
- **Isolation:** NET frames never feed the link status, telemetry, or command tracking. RX sequence
  statistics ignore them.
- **Supported packets:** IPv4/UDP over Ethernet (including VLAN), Linux "any" (SLL/SLL2), and raw
  IP. Fragments and non-UDP packets are counted as skipped.
- **Default filter:** `udp or (vlan and udp)`. Plain `udp` silently misses VLAN-tagged packets.
- **Permissions:** live capture needs root or the capability on the binary:
  `sudo setcap cap_net_raw,cap_net_admin=eip build/mavlink_debug_app`. Reading a file needs neither.
- **Visibility:** you only see traffic that reaches this interface. Other devices' unicast needs a
  hub, switch port mirroring, or capturing on one of the endpoints. Under WSL2 NAT the WSL
  interface sees only WSL's own traffic, so use native Linux for live capture.
- **Duplicates:** capturing on the interface used by the app's own link shows that traffic twice,
  once as RX/TX and once as NET. Filter by direction to separate them.

### Export and recording

- **Export visible…** writes the rows that pass the current filter. **Record…** writes every
  captured frame, unfiltered, until stopped.
- Both run on a background thread. If the disk cannot keep up, frames are dropped and the count is
  shown; write errors stop the recording with a message.

| Format | Contents |
|---|---|
| `.tlog` | Standard MAVLink telemetry log: an 8-byte big-endian UTC µs timestamp, then the raw frame. Opens in pymavlink, MAVExplorer, and Mission Planner. Contains only frames with a complete header and records no direction. Verified with `tools/check_tlog.py`. |
| `.csv` | One row per frame: index, datagram, UTC time, monotonic ns, direction, peer, header fields, target, status, signed flag, send errno, decoded summary, raw hex. |
| `.jsonl` | One JSON object per frame, with the same metadata plus typed `fields` (NaN and Inf are written as the strings `"nan"`, `"inf"`, `"-inf"`). |

## Payload control (VIO)

Every command goes through the PayloadSDK API. Displayed "measured" values come only from received
frames that passed CRC validation, never from what was sent.

| Page | Controls | Feedback |
|---|---|---|
| Gimbal | Modes OFF (with confirmation), LOCK, FOLLOW, MAPPING, RESET via `PARAM_EXT_SET GB_MODE`; move to angle (pitch ±90°, roll/yaw ±180°, the limits `setGimbalSpeed()` enforces); hold-to-move rate buttons | Measured attitude, rates, flags, failure flags and the `GB_MODE` parameter, next to the last commanded angle and the difference |
| Camera | Photo/video mode, capture, start/stop recording; view source, record source and IR palette (VIO values from `vio_sdk.h`); hold-to-zoom and hold-to-focus, zoom to a percentage, auto focus | `CAMERA_CAPTURE_STATUS`, `STORAGE_INFORMATION`, `CAMERA_SETTINGS`, and the values the payload reports in its DEBUG stream |
| Telemetry | Request payload info and parameter streams | Component and camera identity, app version, rangefinder, and every DEBUG parameter with its age |
| Commands | — | Every action: outgoing frame, target, state, ACK result, queue and ACK latency. Double-click a frame number to open it in the inspector. |

Notes:

- **Gimbal modes:** set with `PARAM_EXT_SET GB_MODE` (Mavlink_Command_List "Set gimbal mode";
  values from `vio_sdk.h`) and acknowledged by `PARAM_EXT_ACK`.
  - The SDK's newer route, flags in `GIMBAL_DEVICE_SET_ATTITUDE` as in the current
    `examples/gimbal_set_mode.cpp`, had no effect on VIO software 3.1.4.18. It is kept in
    `PayloadLink::gimbalMode()` and the hardware check, not in the GUI.
  - Bit `0x4000` of the reported flags is Gremsy's "mapping" bit; it is not defined in the MAVLink
    XML.
- **Hold-to-move rates and continuous zoom/focus:** rates are re-sent every 100 ms while a button is
  held, like Gremsy's `ui_demo`, and a zero rate is sent on release. Zoom and focus send a
  start command on press and a stop on release. Rate repeats stop if the link closes.
- **Focus direction:** the buttons use MAVLink semantics (−1 = near, +1 = far). The SDK's
  `Camera_Focus_Value` enum names these the other way round.
- **Settings drop-downs:** a value is sent only when you pick it; refreshing the page never sends
  anything. The values the payload reports are shown separately on the status side.
- **Controls when disconnected:** all controls are disabled while the link is not open. Nothing is
  sent on application start-up.
- **Automatic info request:** once per connection, when the payload core's heartbeat (compid 26)
  first arrives, the app requests the info messages and starts the DEBUG parameter streams at
  Gremsy's `ui_demo` rates. These are read requests only, listed on the Commands tab with an
  `Auto:` prefix.

### Command states

| State | Meaning |
|---|---|
| Queued in SDK | The SDK API was called; the frame has not been seen on the wire yet. |
| Not seen on wire | Nothing matching left within 3 s. |
| Send failed | The frame was seen, but `sendto()` failed. |
| Sent, awaiting ACK | Waiting for `COMMAND_ACK` (matched by MAV_CMD and answering component) or `PARAM_EXT_ACK` (matched by `param_id`). |
| Accepted / In progress / Rejected | The ACK result (`MAV_RESULT` / `PARAM_ACK`). |
| Sent (no ACK defined) | `GIMBAL_DEVICE_SET_ATTITUDE` has no acknowledgement; check the gimbal telemetry instead. |
| No ACK (timeout) | Nothing within 3 s. This does **not** mean the command failed. |

## Hardware checks

`build/mavlink_capture --ip 192.168.55.1 --seconds 30 --out capture` connects through the same code
as the GUI, records `capture.tlog` and `capture.jsonl`, and prints the components seen, the message
rates, the RX sequence statistics, and the result of each request. It sends only what the GUI sends
on connect, so nothing moves.

Results from a VIO over USB-C (RNDIS, laptop at `192.168.55.100`, 2026-10-22):

- **Device:** Gremsy JSC "Vio", software 3.1.4.18, hardware V42A.
- **Network:** WSL2 NAT works for this link; mirrored networking was not needed. Over 20–30 s
  captures there were 0 CRC errors, 0 junk bytes, and 0 capture drops.
- **Components and rates:**
  - Payload core 1/26: HEARTBEAT ~2 Hz; DEBUG parameters ~40 Hz once the streams are requested.
  - Camera 1/100 (`MAV_COMP_ID_CAMERA`; the command list PDF says 101): HEARTBEAT,
    STORAGE_INFORMATION, and CAMERA_CAPTURE_STATUS at ~1 Hz.
  - Gimbal 1/154: GIMBAL_DEVICE_ATTITUDE_STATUS, MOUNT_ORIENTATION, and RAW_IMU at ~10 Hz;
    HEARTBEAT and MOUNT_STATUS at 1 Hz.
- **Acknowledgements:**
  - `MAV_CMD_REQUEST_CAMERA_*` and `MAV_CMD_REQUEST_STORAGE_INFORMATION` are acknowledged in
    3–250 ms; `MAV_CMD_SET_MESSAGE_INTERVAL` in 1–3 ms.
  - `MAV_CMD_REQUEST_MESSAGE` is answered with the requested message within ~4 ms but never with a
    COMMAND_ACK. The tracker reports this as "Answered (message, no ACK)".
- **Sequence numbers:** the payload core and the camera sometimes send seq N+1 before N (two sender
  threads share one counter). No frame is missing; the statistics count this as *reordered*.
- **Commands** (`build/hw_vio_commands --ip 192.168.55.1`; small motions, each undone; no photo or
  recording):
  - **Camera:** `PARAM_EXT_SET C_SOURCE`, continuous zoom, and auto focus are all acknowledged by
    1/100 within 1–4 ms. `EO_ZOOM` went 1 → 2.16 → 1.
  - **Gimbal angle:** pitch −10° and back were reached in 100–300 ms. `GIMBAL_DEVICE_SET_ATTITUDE`
    has no ACK, so telemetry is the confirmation.
  - **Gimbal rate:** a yaw rate of 10°/s re-sent for 1 s gave a reported peak of only ~1.7°/s and
    ~1.9° of motion. The commands arrive and the gimbal reacts, but at about a sixth of the request.
    The SDK's `gimbal_move_speed` example switches `RC_MODE` to Standard first; that persistent
    setting was not changed in this check.
  - **Modes:** `GB_MODE` = FOLLOW, then LOCK, was acknowledged in 1–2 ms. The flags changed
    `0x3C` → `0x2C` → `0x3C` within ~100 ms, and the `GB_MODE` parameter followed. The flag route
    (`YAW_LOCK` cleared in `GIMBAL_DEVICE_SET_ATTITUDE`) did not change the mode.
  - **Not exercised:** OFF, RESET, MAPPING, photo, and recording.
- **GUI:** `HW_IP=192.168.55.1 SCREENSHOT_DIR=/tmp/shots QT_QPA_PLATFORM=offscreen
  build/test_main_window hardwareScreenshots` connects read-only and saves a screenshot of each tab.
- **Rangefinder:** the VIO sends no `DISTANCE_SENSOR`; it streams the `LRF_RANGE` parameter (about
  2–4 on a desk; the unit is not documented).
- **Firmware quirks:**
  - `CAMERA_INFORMATION.firmware_version` is `0x03010412` for software 3.1.4.18, which is the
    reverse of the MAVLink byte order. The Telemetry tab shows both readings.
  - The bytes after the terminating NUL in `vendor_name` and `model_name` contain leftover text
    (for example `/dev/ttyTHS1`, `Baud=115200`). The app stops at the first NUL.

## Live video

The **Video** tab plays the payload's RTSP stream:

- **URL:** built from the payload IP using Gremsy's format, `rtsp://<IP>:8554/payload`, until you
  edit it.
- **Independent of MAVLink:** it has its own Start/Stop and does not need the MAVLink connection,
  or affect it.
- **Pipeline:** `rtspsrc ! decodebin ! videoconvert ! appsink`. The URL is set as an element
  property, never parsed as pipeline syntax.
- **Threading:** frames are copied on GStreamer's streaming thread; the GUI paints only the latest
  one. Errors and end-of-stream are shown as text.
- **Transport:**
  - **TCP** (default): works through WSL2 NAT.
  - **UDP**: RTP from the VIO does not get through WSL2 NAT ("no protocols left"). Use it on native
    Linux.
  - **UDP, then TCP**: tries UDP first.
- **Measured on the VIO:** H.264, 1280×720, about 31 fps decoded over TCP while the MAVLink link
  ran at about 90 frames/s, with no errors on either.

## Threading

- The SDK's I/O threads only copy each datagram into a bounded queue (20,000 entries). Overflow is
  counted and shown as "capture queue drops"; it is never silent.
- A 100 ms timer on the GUI thread drains the queue, parses the frames, and updates the link state
  and the table (100,000 rows; the oldest are evicted).
- Connect (the SDK blocks for about 1 s) and disconnect run on a worker thread.
- Closing the window waits for that worker and joins the SDK threads.

## Agreed design (Stage 1)

- **Platform:** Ubuntu 24.04. Developed under WSL2 with WSLg. The SDK is POSIX-only, so there is
  no native Windows build.
- **First payload:** VIO. The SDK library is model-agnostic. The `VIO/ZIO/MB1/ORUSL` defines only
  pick a header of parameter names and enum values, so models will be selected at runtime from
  capability tables built from those headers.
- **One connection owner:** the SDK's `UDP_Port` (client mode: target IP and port are set at runtime
  via `T_ConnInfo`, and the local port is ephemeral). The app never opens a second command channel.
- **Inspector source:** the patch adds `PayloadSdkInterface::regTransportObserver()`. It receives
  every raw RX datagram (with the sender's endpoint) and every TX frame (with the `sendto` result),
  before the SDK parses anything. Frames from every component on the link (gimbal, camera, payload
  core, and others) are seen, including ones the SDK parser would silently drop.
- **Capturing other devices' traffic** (for example payload ↔ autopilot) is planned as a later,
  optional libpcap capture. It needs root or `CAP_NET_RAW`.
- **First release:** connection management, MAVLink Inspector, gimbal, core camera,
  telemetry/info pages, and live RTSP video (GStreamer `appsink` → Qt, with its own start/stop
  lifecycle).

## SDK patch (`patches/payloadsdk-transport-observer.patch`, +48/−10 lines)

1. **Transport observer:** `transport_observer_t` on `Generic_Port`, called from `UDP_Port` read
   and write.
2. **Shutdown fix:** `UDP_Port::stop()` called `close()` while the read thread was blocked in
   `recvfrom()`, so with a silent payload the thread join could hang. It now calls
   `shutdown(SHUT_RD)` to wake the reader, and the socket is closed in the destructor.
3. **`sdkQuit()`:** is idempotent and joins the SDK receive thread, which was never joined before.
4. **`~PayloadSdkInterface()`:** frees the port and interface objects, which used to leak on every
   reconnect.

The patch is verified by `tests/test_sdk_transport.cpp`. It connects and disconnects twice against a
loopback fake payload and checks that `sdkQuit()` returns in under 2 s (measured: 15 ms).

## MAVLink parsing

- Each UDP datagram is split independently into frames.
- Statuses: `OK`, `BAD_CRC`, `UNKNOWN_ID` (CRC cannot be verified without the message's
  CRC_EXTRA), `TRUNCATED`, and `GARBAGE` (bytes outside any frame).
- MAVLink 1 and 2 are supported, including 24-bit message IDs, v2 trailing-zero payloads, and
  extension fields.
- CRC_EXTRA values and field tables come from the SDK's generated `ardupilotmega` dialect.
- Signatures are **detected but not verified** (no key is configured).

## SDK behaviour observed through the inspector

- **Automatic stream requests:** on the first message from the payload core (compid 26,
  `MAV_COMP_ID_USER2`), the SDK sends a burst of `COMMAND_LONG` stream-interval requests, one every
  20 ms (`requestMessageStreamInterval()`). The user did not trigger these.
- **Sequence numbers:** the app's TX frames (1/193) carry more than one independent sequence
  counter. MAVLink keeps channel state per C++ translation unit, and the SDK encodes heartbeats and
  commands in different files. TX sequence gaps therefore do not indicate packet loss.
- **Send queue:** the SDK writes at most one queued message every 20 ms, so a command appears in TX
  slightly after the API call.

## Known limitations

- **Partly tested on hardware:** gimbal OFF, RESET and MAPPING, photo capture, and recording have
  not been run on a VIO. The gimbal rate is executed far slower than commanded under the payload's
  current RC settings.
- **VIO only:** other models need their own `payload_profile_<model>.cpp`. Zoom to an absolute level
  (`setCameraZoomTarget`, a custom `MAV_CMD_USER_4`) is not exposed because its range is not
  documented per model.
- **Live network capture:** checked only for its error path here (no root under WSL). The decoding
  and file path are tested with `.pcap` files written by libpcap.
- **Video:** there is no recording or snapshot from the app (use the payload's own recording).
  Under WSL2 NAT only TCP transport works.

- **No time-range filter:** the inspector shows the live session only. A log viewer that loads
  recorded files is not implemented.
- **XML location:** units and enum names come from `message_definitions/` (override with
  `MAVLINK_XML_DIR`). The build uses the SDK copy; an installed binary must ship that folder next to
  it.

- **WSL2 networking:** WSL2 defaults to NAT networking. If replies from the payload do not arrive,
  enable mirrored networking by adding `networkingMode=mirrored` to `%UserProfile%\.wslconfig`,
  then run `wsl --shutdown`.
- **SDK `checkPayloadConnection()`** blocks forever. The app does not call it; it judges link state
  from the traffic it observes.
- **Licence:** the PayloadSDK is GPL-3.0. Distributing this app requires releasing it under
  GPL-3.0.
