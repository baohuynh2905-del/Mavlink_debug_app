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
  - Object tracking control added. It is **untested on hardware**, and the 1920×1080 tracker pixel
    space is an assumption.

## Next steps

1. Native build: `scripts/build.sh` should report 9/9 tests passing.
2. Run the open hardware checks in `docs/INSTALL_UBUNTU.md` §7 (a: passive capture, b: screenshots,
   c: control path) and analyse their outputs. Compare them with the WSL2 results in the README's
   *Hardware checks*.
3. On native Linux, verify RTSP over UDP and live network capture (both impossible under WSL2).
4. Optional: payload profiles for ZIO / MB1 / OrusL (`src/payload_profile_<model>.cpp`, values only
   from `third_party/PayloadSdk/libs/payload-define/<model>_sdk.h`).

## Rules the user set

- **Ask first** before any command that moves the gimbal or changes the payload's state.
  - Approved so far: camera zoom/focus/settings, small gimbal angle/rate moves, and LOCK/FOLLOW mode
    changes, each undone afterwards.
  - **Not approved:** photo capture or recording (SD writes), gimbal OFF / RESET / MAPPING,
    changing `RC_MODE` (a persistent setting), and object tracking (Arm changes `TRACK_MODE`; tracking
    moves the gimbal). Re-ask for these.
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
- **Gimbal modes on VIO software 3.1.4.18:** only `PARAM_EXT_SET GB_MODE` works. The SDK's flag
  route (`GIMBAL_DEVICE_SET_ATTITUDE` flags) has no effect.
- **Gimbal rate:** a commanded 10°/s gave about 1.7°/s, probably because of `RC_MODE`; not verified.
- **Camera component:** compid 100 (not 101).
- **REQUEST_MESSAGE:** answered with the message itself but no COMMAND_ACK.
- **Sequence numbers:** the VIO payload core and camera reorder them (N+1 before N). This is not
  loss.
- **Video:** `rtsp://<IP>:8554/payload`, H.264 1280×720 at about 30 fps. Under WSL2 NAT only TCP
  works.
- **Build:** a full parallel build can exhaust memory. Use `JOBS=2 scripts/build.sh` if the machine
  freezes or a compile is killed.
