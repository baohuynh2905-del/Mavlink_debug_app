# Gremsy MAVLink Debug App

Desktop tool (C++17, Qt 6 Widgets, CMake) for controlling Gremsy payloads through the official
PayloadSDK and inspecting the real MAVLink traffic of that connection.

**Status: Stage 3.** Connection management and the MAVLink Inspector are complete: live table,
decoded details, filters, statistics, export, and recording. The payload control pages (gimbal,
camera, telemetry, video) are not built yet.

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
| `src/main_window.*`, `src/inspector_widget.*`, `src/stats_widget.*` | GUI |
| `tests/` | Hardware-free tests (CTest). `fake_payload.h` is a loopback UDP peer used in place of a payload. |
| `tools/check_tlog.py` | Reads a `.tlog` file with pymavlink, an independent check of recorded files |

## Dependencies (Ubuntu 24.04, native or WSL2)

```bash
sudo apt install build-essential cmake git pkg-config \
    qt6-base-dev libgl-dev libpcap-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
```

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

Enter the payload IP and UDP port (default `192.168.55.1:14566`), then click **Connect**. The last
valid IP, port, and timeout are saved and validated again on start-up. Connecting never sends a
movement or camera command.

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
