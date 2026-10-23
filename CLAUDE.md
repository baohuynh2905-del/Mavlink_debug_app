# Project notes for Claude Code

Gremsy MAVLink debug app: a Qt 6 / C++17 desktop tool that controls Gremsy payloads through the
official PayloadSDK and inspects the real MAVLink traffic. Read `README.md` first (design, hardware
results, acceptance checklist, limitations). For setup on native Ubuntu, see
`docs/INSTALL_UBUNTU.md`.

## Status (2026-10-22)

- Stages 1–6 are done: connection and profiles, the inspector with network capture,
  VIO gimbal and camera control, telemetry, command/ACK tracking, video, clean-build validation, and
  install.
- Development so far was on Windows + WSL2 (Ubuntu 24.04). Work is moving to the user's native
  dual-boot Ubuntu 24.04 to finish the hardware testing. The native build passes 9/9.
- UI redesign (2026-10-22) is done:
  - Dark station theme (`src/theme.*`).
  - One Operate workspace (gimbal | video with HUD | camera), plus an Analyze view.
  - A collapsible MAVLink monitor and a SETTINGS drawer.
  - Object tracking control added.
- 2026-10-23 (native Ubuntu, VIO over USB-C):
  - Tracking fixed and verified (see facts).
  - Gimbal speed slider with the VIO rate scale.
  - Camera settings dialog (30 settings).
  - Boot splash.
  - Fixed a crash on exit while the video plays, and HEARTBEAT being matched as a command's answer.
  - `hw_vio_commands --steps camera,angle,rate,mode-param,tracking,settings`: 59/59 checks passed (on 3.1.4.18).
- 2026-10-23, later: the VIO was updated to software **3.1.8.21**:
  - Modes now use the flag route.
  - The gimbal panel shows and switches the control source (`GB_FW_FLAG`).
  - Rate compensation removed.
  - The automatic info request now waits for the camera's heartbeat.
  - `--steps control-onboard,camera,rate,mode-flags` passed.

## Next steps

1. Native build: `scripts/build.sh` should report 9/9 tests passing.
2. On native Linux, verify RTSP over UDP and live network capture (both impossible under WSL2).
   `ffmpeg -rtsp_transport udp` got only 3 frames on 2026-10-23 (TCP fine). The laptop then had two
   addresses in 192.168.55.0/24 (`.3` and `.100`), which may matter.
3. Ask the user for the original `C_V_RGAIN`, `C_V_BGAIN` and `C_V_ICR_THR`. They were set to 0 by
   mistake on 2026-10-23 (originals were 128–255, unknown). They have no effect while white balance is
   Auto and ICR is Manual.
4. Optional: payload profiles for ZIO / MB1 / OrusL (`src/payload_profile_<model>.cpp`, values only
   from `third_party/PayloadSdk/libs/payload-define/<model>_sdk.h`).

## Rules the user set

- **Ask first** before any command that moves the gimbal or changes the payload's state.
  - Approved so far: camera zoom/focus/settings, small gimbal angle/rate moves, and LOCK/FOLLOW mode
    changes, each undone afterwards.
  - Approved on 2026-10-23: object tracking (the gimbal follows), and writing camera settings, each
    restored afterwards.
  - **Not approved:** photo capture or recording (SD writes), gimbal OFF / RESET / MAPPING, and
    changing `RC_MODE` (a persistent setting). Re-ask for these.
  - Never write a camera setting value that was not read back through a decoder already checked
    against the camera (`--steps settings-read`).
- Commit and push to GitHub when a stage or fix is finished.
  - Author: `Huynh Gia Bao <baohuynh2905@gmail.com>`. Set it per clone with `git config user.name` /
    `git config user.email`; it is not stored in the repo.
  - Never use support@gremsy.com for commits.
- Keep changes small, match the existing code style, add a test for new logic, and do not claim a
  test passed without running it.

## Facts that are easy to get wrong

- **Transport:** the SDK's `UDP_Port` is a UDP client (ephemeral local port).
  `patches/payloadsdk-transport-observer.patch` (applied by CMake) adds the TX/RX observer and fixes
  the `sdkQuit()` hang. Never edit the submodule directly; change the patch.
- **Gimbal modes:** on 3.1.8.21 only the flags in `GIMBAL_DEVICE_SET_ATTITUDE` work (Gremsy's way since
  v307.54); `PARAM_EXT_SET GB_MODE` is rejected. On 3.1.4.18 it was the other way round.
- **Gimbal control source `GB_FW_FLAG`** (3.1.8.21):
  - 0 = Autopilot (the user's setting): the payload drops all our `GIMBAL_DEVICE_SET_ATTITUDE`.
  - 1 = Onboard computer: modes and rates work.
  - It is a stored setting. The user chose to switch it from the gimbal panel themselves; tests restore it.
- **Gimbal rate:** in real units on 3.1.8.21 (10°/s gives about 12°/s peak). 3.1.4.18 turned at
  × π/18; that compensation is gone.
- **Gimbal angle:** works on 3.1.4.18 (yaw lands about 2.7° off); no effect at all on 3.1.8.21 by any route
  tried. Ask Gremsy.
- **Gimbal flags on every command:** each `GIMBAL_DEVICE_SET_ATTITUDE` (rate, angle) carries mode flags, and
  3.1.8.21 applies them. The SDK never initialised its copy (`current_gb_device_flags`) and refreshes it only
  with a `regPayloadParamChanged()` callback. The app patch adds `setGimbalDeviceFlags()`;
  `PayloadLink::setMotionFlags()` sends the reported mode, or for 1 s the mode just commanded.
- **Camera settings list:** the reply is a burst, and about a third of it can be lost on the USB link. Pages call
  `readMissingCameraSetting()`, which re-asks by ID every 2 s.
- **SDK camera compid:** the SDK addresses the camera at 101 until it hears compid 100, so the automatic
  requests wait for the camera's heartbeat.
- **Tracking:**
  - The gimbal follows only if the last gimbal input was not an angle command, so
    `trackingTarget()` sends rate 0 first.
  - `TRK_POS_X/Y` is the box's top-left corner.
  - The tracker space is 1920×1080; this was checked against the OSD's own box.
- **Camera settings:**
  - The `PARAM_EXT_ACK` carries no value.
  - The camera lists `C_V_GAIN` and `C_V_BrP` and rejects the header's `_HS`/`_LS` IDs.
  - It answered parameter messages only from source component IDs it had already seen (new compids
    195/196 got no reply, 191/193/194 did). The app's SDK compid works.
- **Camera component:** compid 100 (not 101).
- **REQUEST_MESSAGE:** answered with the message itself but no COMMAND_ACK.
- **Sequence numbers:** the VIO payload core and camera reorder them (N+1 before N). This is not
  loss.
- **Video:** `rtsp://<IP>:8554/payload`, H.264 1280×720 at about 30 fps. Under WSL2 NAT only TCP
  works.
- **Build:** a full parallel build can exhaust memory. Use `JOBS=2 scripts/build.sh` if the machine
  freezes or a compile is killed.
