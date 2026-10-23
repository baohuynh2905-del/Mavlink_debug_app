#pragma once
// What a payload model supports, with parameter IDs and values taken from the SDK's model header.
// The SDK's model headers reuse macro names with different values, so each profile lives in its
// own translation unit (payload_profile_<model>.cpp) that includes only its header.

#include <QString>

#include <initializer_list>
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
    unsigned min = 0, max = 0, step = 1;
};

struct PayloadProfile {
    const char *name;
    const char *viewSourceId;            // PARAM_EXT id, nullptr = unsupported
    std::vector<ParamOption> viewSources;
    const char *recordSourceId;
    std::vector<ParamOption> recordSources;
    const char *irPaletteId;
    std::vector<ParamOption> irPalettes;
    const char *gimbalModeId;  // GB_MODE: names the mode in the DEBUG stream; as PARAM_EXT_SET only up to VIO 3.1.4.18
    std::vector<ParamOption> gimbalModes;
    const char *trackingModeId;  // PARAM_EXT_SET that selects object tracking before arming the tracker
    unsigned trackingObjectMode;
    std::vector<CameraSetting> cameraSettings;
    // The payload's gimbal control source: gimbal commands from this app pass only when it is gimbalControlOnboard
    const char *gimbalControlId = nullptr;
    std::vector<ParamOption> gimbalControls;
    unsigned gimbalControlOnboard = 0;
};

// One per model, each in its own payload_profile_<model>.cpp with only that model's SDK header
const PayloadProfile &vioProfile();
const PayloadProfile &zioProfile();
const PayloadProfile &mb1Profile();
const PayloadProfile &oruslProfile();

inline const std::vector<const PayloadProfile *> &payloadProfiles()
{
    static const std::vector<const PayloadProfile *> all{&vioProfile(), &zioProfile(), &mb1Profile(), &oruslProfile()};
    return all;
}

inline const PayloadProfile &profileByName(const QString &name)  // VIO when unknown
{
    for (const PayloadProfile *p : payloadProfiles())
        if (name.compare(QLatin1String(p->name), Qt::CaseInsensitive) == 0) return *p;
    return vioProfile();
}

// Options numbered 0, 1, 2 ... in order, for the SDK headers' sequential enums
inline std::vector<ParamOption> optionSequence(std::initializer_list<const char *> labels)
{
    std::vector<ParamOption> o;
    for (const char *l : labels) o.push_back({unsigned(o.size()), l});
    return o;
}

inline std::vector<ParamOption> onOff(unsigned on, unsigned off) { return {{on, "On"}, {off, "Off"}}; }
