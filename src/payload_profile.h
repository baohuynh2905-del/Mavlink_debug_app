#pragma once
// What a payload model supports, with parameter IDs and values taken from the SDK's model header.
// The SDK's model headers reuse macro names with different values, so each profile lives in its
// own translation unit (payload_profile_<model>.cpp) that includes only its header.

#include <vector>

struct ParamOption {
    unsigned value;
    const char *label;
};

// One PARAM_EXT camera setting, for the camera settings dialog.
struct CameraSetting {
    const char *group;  // dialog tab
    const char *label;
    const char *id;
    std::vector<ParamOption> options;  // empty: a number from min to max
    unsigned min = 0, max = 0;
};

struct PayloadProfile {
    const char *name;
    const char *viewSourceId;            // PARAM_EXT id, nullptr = unsupported
    std::vector<ParamOption> viewSources;
    const char *recordSourceId;
    std::vector<ParamOption> recordSources;
    const char *irPaletteId;
    std::vector<ParamOption> irPalettes;
    const char *gimbalModeId;  // PARAM_EXT_SET gimbal mode (older route than the attitude flags)
    std::vector<ParamOption> gimbalModes;
    const char *trackingModeId;  // PARAM_EXT_SET that selects object tracking before arming the tracker
    unsigned trackingObjectMode;
    std::vector<CameraSetting> cameraSettings;
    // Gimbal rate commands are multiplied by this before sending (1 = MAVLink's rad/s as the SDK sends it).
    double gimbalRateScale = 1;
};

const PayloadProfile &vioProfile();
