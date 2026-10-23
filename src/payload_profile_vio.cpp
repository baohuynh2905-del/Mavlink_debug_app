#include "payload_profile.h"

#include "vio_sdk.h"  // SDK libs/payload-define: the only source of these IDs and values

#include <initializer_list>

// Options numbered 0, 1, 2 ... in order, for the header's sequential zoom enums.
static std::vector<ParamOption> sequence(std::initializer_list<const char *> labels)
{
    std::vector<ParamOption> o;
    for (const char *l : labels) o.push_back({unsigned(o.size()), l});
    return o;
}
static_assert(ZOOM_IR_8X == 7 && ZOOM_COMBINE_240X == 21 && ZOOM_SUPER_RESOLUTION_30X == 11, "vio_sdk.h zoom enums changed");

const PayloadProfile &vioProfile()
{
    static const std::vector<ParamOption> viewSources{
        {PAYLOAD_CAMERA_VIEW_EOIR, "EO / IR (picture-in-picture)"},
        {PAYLOAD_CAMERA_VIEW_EO, "EO only"},
        {PAYLOAD_CAMERA_VIEW_IR, "IR only"},
        {PAYLOAD_CAMERA_VIEW_IREO, "IR / EO (picture-in-picture)"},
        {PAYLOAD_CAMERA_VIEW_SYNC, "Sync"},
        {PAYLOAD_CAMERA_VIEW_SIDE_BY_SIDE, "Side by side"}};
    static const std::vector<ParamOption> recordSources{
        {PAYLOAD_CAMERA_RECORD_BOTH, "EO + IR"},
        {PAYLOAD_CAMERA_RECORD_EO, "EO"},
        {PAYLOAD_CAMERA_RECORD_IR, "IR"},
        {PAYLOAD_CAMERA_RECORD_OSD, "With OSD"}};
    // The SDK header names each index differently per IR core (F1 / G1), so both are shown.
    static const std::vector<ParamOption> irPalettes{
        {PAYLOAD_CAMERA_IR_PALETTE_1, "1  (F1 WhiteHot | G1 WhiteHot)"},
        {PAYLOAD_CAMERA_IR_PALETTE_2, "2  (F1 BlackHot | G1 Fulgurite)"},
        {PAYLOAD_CAMERA_IR_PALETTE_3, "3  (F1 Rainbow | G1 IronRed)"},
        {PAYLOAD_CAMERA_IR_PALETTE_4, "4  (F1 RainbowHC | G1 HotIron)"},
        {PAYLOAD_CAMERA_IR_PALETTE_5, "5  (F1 Ironbow | G1 Medical)"},
        {PAYLOAD_CAMERA_IR_PALETTE_6, "6  (F1 Lava | G1 Arctic)"},
        {PAYLOAD_CAMERA_IR_PALETTE_7, "7  (F1 Arctic | G1 Rainbow1)"},
        {PAYLOAD_CAMERA_IR_PALETTE_8, "8  (F1 Globow | G1 Rainbow2)"},
        {PAYLOAD_CAMERA_IR_PALETTE_9, "9  (F1 Gradedfire | G1 Tint)"},
        {PAYLOAD_CAMERA_IR_PALETTE_10, "10 (F1 Hottest | G1 BlackHot)"}};
    auto onOff = [](unsigned on, unsigned off) { return std::vector<ParamOption>{{on, "On"}, {off, "Off"}}; };

    static const PayloadProfile p{
        "VIO",
        PAYLOAD_CAMERA_VIEW_SRC,
        viewSources,
        PAYLOAD_CAMERA_RECORD_SRC,
        recordSources,
        PAYLOAD_CAMERA_IR_PALETTE,
        irPalettes,
        PAYLOAD_CAMERA_GIMBAL_MODE,
        {{PAYLOAD_CAMERA_GIMBAL_MODE_OFF, "OFF"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_LOCK, "LOCK"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_FOLLOW, "FOLLOW"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_MAPPING, "MAPPING"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_RESET, "RESET"}},
        PAYLOAD_CAMERA_TRACKING_MODE,
        PAYLOAD_CAMERA_TRACKING_OBJ_TRACKING,
        {
            {"Exposure", "Exposure mode", PAYLOAD_CAMERA_VIDEO_AUTO_EXPOSURE,
             {{PAYLOAD_CAMERA_VIDEO_EXPOSURE_AUTO, "Auto"},
              {PAYLOAD_CAMERA_VIDEO_EXPOSURE_MANUAL, "Manual"},
              {PAYLOAD_CAMERA_VIDEO_EXPOSURE_SHUTTER, "Shutter priority"},
              {PAYLOAD_CAMERA_VIDEO_EXPOSURE_IRIS, "Iris priority"},
              {PAYLOAD_CAMERA_VIDEO_EXPOSURE_BRIGHT, "Bright"},
              {PAYLOAD_CAMERA_VIDEO_EXPOSURE_GAIN, "Gain priority"}}},
            {"Exposure", "Shutter", PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED,
             {{PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_1, "1/1"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_2_3, "2/3"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_2, "1/2"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_3, "1/3"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_4, "1/4"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_6, "1/6"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_8, "1/8"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_10, "1/10"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_15, "1/15"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_20, "1/20"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_30, "1/30"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_50, "1/50"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_60, "1/60"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_90, "1/90"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_100, "1/100"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_125, "1/125"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_180, "1/180"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_250, "1/250"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_350, "1/350"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_500, "1/500"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_725, "1/725"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_1000, "1/1000"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_1500, "1/1500"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_2000, "1/2000"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_3000, "1/3000"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_4000, "1/4000"},
              {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_6000, "1/6000"}, {PAYLOAD_CAMERA_VIDEO_SHUTTER_SPEED_1_10000, "1/10000"}}},
            {"Exposure", "Slowest shutter (auto)", PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT,
             {{PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_10, "1/10"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_15, "1/15"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_20, "1/20"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_30, "1/30"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_50, "1/50"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_60, "1/60"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_90, "1/90"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_100, "1/100"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_125, "1/125"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_180, "1/180"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_250, "1/250"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_350, "1/350"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_500, "1/500"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_725, "1/725"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_1000, "1/1000"}, {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_1500, "1/1500"},
              {PAYLOAD_CAMERA_EO_SHUTTER_MIN_LIMIT_1_2000, "1/2000"}}},
            {"Exposure", "Iris", PAYLOAD_CAMERA_VIDEO_APERTURE_VALUE,
             {{PAYLOAD_CAMERA_EO_APERTURE_F2_0, "F2.0"}, {PAYLOAD_CAMERA_EO_APERTURE_F2_2, "F2.2"},
              {PAYLOAD_CAMERA_EO_APERTURE_F2_4, "F2.4"}, {PAYLOAD_CAMERA_EO_APERTURE_F2_6, "F2.6"},
              {PAYLOAD_CAMERA_EO_APERTURE_F2_8, "F2.8"}, {PAYLOAD_CAMERA_EO_APERTURE_F3_1, "F3.1"},
              {PAYLOAD_CAMERA_EO_APERTURE_F3_4, "F3.4"}, {PAYLOAD_CAMERA_EO_APERTURE_F4_0, "F4.0"},
              {PAYLOAD_CAMERA_EO_APERTURE_F5_2, "F5.2"}, {PAYLOAD_CAMERA_EO_APERTURE_F6_8, "F6.8"},
              {PAYLOAD_CAMERA_EO_APERTURE_F7_3, "F7.3"}, {PAYLOAD_CAMERA_EO_APERTURE_F8_7, "F8.7"},
              {PAYLOAD_CAMERA_EO_APERTURE_F9_6, "F9.6"}, {PAYLOAD_CAMERA_EO_APERTURE_F10_0, "F10"},
              {PAYLOAD_CAMERA_EO_APERTURE_F11_0, "F11"}, {PAYLOAD_CAMERA_EO_APERTURE_CLOSE, "Closed"}}},
            {"Exposure", "High sensitivity", PAYLOAD_CAMERA_EO_HS, onOff(PAYLOAD_CAMERA_EO_HS_ON, PAYLOAD_CAMERA_EO_HS_OFF)},
            // vio_sdk.h names C_V_GAIN_HS / _LS and C_V_BrP_HS / _LS. A VIO on software 3.1.4.18 answers both with
            // PARAM_ACK_FAILED and lists C_V_GAIN / C_V_BrP itself, so the IDs are the camera's; the values are the
            // header's (the HS gain list holds the LS one: 1 = 0 dB ... 13 = 36 dB; 14-17 need high sensitivity on).
            {"Exposure", "Gain", "C_V_GAIN",
             {{PAYLOAD_CAMERA_EO_GAIN_HS_0DB, "0 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_3DB, "3 dB"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_6DB, "6 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_9DB, "9 dB"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_12DB, "12 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_15DB, "15 dB"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_18DB, "18 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_21DB, "21 dB"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_24DB, "24 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_27DB, "27 dB"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_30DB, "30 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_33DB, "33 dB"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_36DB, "36 dB"}, {PAYLOAD_CAMERA_EO_GAIN_HS_39DB, "39 dB (high sensitivity)"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_42DB, "42 dB (high sensitivity)"}, {PAYLOAD_CAMERA_EO_GAIN_HS_45DB, "45 dB (high sensitivity)"},
              {PAYLOAD_CAMERA_EO_GAIN_HS_48DB, "48 dB (high sensitivity)"}}},
            {"Exposure", "Bright (0-37, 0-41 with high sensitivity)", "C_V_BrP", {}, 0, 41},

            {"Image", "White balance", PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE,
             {{PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_AUTO, "Auto"},
              {PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_INDOOR, "Indoor"},
              {PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_OUTDOOR, "Outdoor"},
              {PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_ONE_PUSH, "One push"},
              {PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_ATW, "ATW (auto tracing)"},
              {PAYLOAD_CAMERA_VIDEO_WHITE_BALANCE_MANUAL, "Manual"}}},
            {"Image", "Red gain (manual white balance)", PAYLOAD_CAMERA_EO_R_GAIN, {}, 0, 255},
            {"Image", "Blue gain (manual white balance)", PAYLOAD_CAMERA_EO_B_GAIN, {}, 0, 255},
            {"Image", "Defog", PAYLOAD_CAMERA_VIDEO_DEFOG, onOff(PAYLOAD_CAMERA_VIDEO_DEFOG_ON, PAYLOAD_CAMERA_VIDEO_DEFOG_OFF)},
            {"Image", "Defog level", PAYLOAD_CAMERA_VIDEO_DEFOG_LEVEL,
             {{PAYLOAD_CAMERA_VIDEO_DEFOG_LOWEST, "Lowest"},
              {PAYLOAD_CAMERA_VIDEO_DEFOG_LOW, "Low"},
              {PAYLOAD_CAMERA_VIDEO_DEFOG_MID, "Mid"},
              {PAYLOAD_CAMERA_VIDEO_DEFOG_HIGH, "High"}}},
            {"Image", "IR-cut filter (ICR) mode", PAYLOAD_CAMERA_EO_ICR_MODE,
             {{PAYLOAD_CAMERA_EO_ICR_MODE_AUTO, "Auto"}, {PAYLOAD_CAMERA_EO_ICR_MODE_MANUAL, "Manual"}}},
            {"Image", "ICR auto threshold", PAYLOAD_CAMERA_EO_ICR_MODE_AUTO_THRESHOLD, {}, 0, 255},
            {"Image", "ICR manual (night mode)", PAYLOAD_CAMERA_EO_ICR_MANUAL,
             onOff(PAYLOAD_CAMERA_EO_ICR_MANUAL_ON, PAYLOAD_CAMERA_EO_ICR_MANUAL_OFF)},
            {"Image", "Flip", PAYLOAD_CAMERA_VIDEO_FLIP, onOff(PAYLOAD_CAMERA_VIDEO_FLIP_ON, PAYLOAD_CAMERA_VIDEO_FLIP_OFF)},
            {"Image", "Freeze", PAYLOAD_CAMERA_EO_FREEZE, onOff(PAYLOAD_CAMERA_EO_FREEZE_ON, PAYLOAD_CAMERA_EO_FREEZE_OFF)},

            {"Focus / Zoom", "Focus mode", PAYLOAD_CAMERA_VIDEO_FOCUS_MODE,
             {{PAYLOAD_CAMERA_VIDEO_FOCUS_MODE_MANUAL, "Manual"},
              {PAYLOAD_CAMERA_VIDEO_FOCUS_MODE_AUTO_FOCUS, "Auto"},
              {PAYLOAD_CAMERA_VIDEO_FOCUS_MODE_AUTO_FOCUS_ONEPUSH, "One-push auto"}}},
            {"Focus / Zoom", "Manual focus position", PAYLOAD_CAMERA_VIDEO_FOCUS_VALUE, {}, 0, 61440},
            {"Focus / Zoom", "Focus speed", PAYLOAD_CAMERA_EO_FOCUS_SPEED, {}, 0, 7},
            {"Focus / Zoom", "Zoom mode", PAYLOAD_CAMERA_VIDEO_ZOOM_MODE,
             {{PAYLOAD_CAMERA_VIDEO_ZOOM_MODE_COMBINE, "Combine"},
              {PAYLOAD_CAMERA_VIDEO_ZOOM_MODE_SUPER_RESOLUTION, "Super resolution"}}},
            {"Focus / Zoom", "Combine zoom level", PAYLOAD_CAMERA_VIDEO_ZOOM_COMBINE_FACTOR,
             sequence({"1x", "2x", "4x", "6x", "8x", "10x", "12x", "14x", "16x", "18x", "20x", "40x", "60x", "80x", "100x",
                       "120x", "140x", "160x", "180x", "200x", "220x", "240x"})},
            {"Focus / Zoom", "Super-resolution zoom level", PAYLOAD_CAMERA_VIDEO_ZOOM_SUPER_RESOLUTION_FACTOR,
             sequence({"1x", "2x", "4x", "6x", "8x", "10x", "12x", "14x", "16x", "18x", "20x", "30x"})},
            {"Focus / Zoom", "Zoom speed", PAYLOAD_CAMERA_EO_ZOOM_SPEED, {}, 0, 7},

            {"Thermal", "IR palette", PAYLOAD_CAMERA_IR_PALETTE, irPalettes},
            {"Thermal", "IR zoom", PAYLOAD_CAMERA_IR_ZOOM_FACTOR, sequence({"1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x"})},

            {"Video / OSD / LRF", "View source", PAYLOAD_CAMERA_VIEW_SRC, viewSources},
            {"Video / OSD / LRF", "Record source", PAYLOAD_CAMERA_RECORD_SRC, recordSources},
            {"Video / OSD / LRF", "On-screen display", PAYLOAD_CAMERA_VIDEO_OSD_MODE,
             {{PAYLOAD_CAMERA_VIDEO_OSD_MODE_DISABLE, "Off"},
              {PAYLOAD_CAMERA_VIDEO_OSD_MODE_DEBUG, "Debug"},
              {PAYLOAD_CAMERA_VIDEO_OSD_MODE_STATUS, "Status"}}},
            {"Video / OSD / LRF", "Rangefinder", PAYLOAD_LRF_MODE,
             {{PAYLOAD_LRF_MODE_OFF, "Off"}, {PAYLOAD_LRF_MODE_1HZ, "1 Hz"}, {PAYLOAD_LRF_MODE_4HZ, "4 Hz"},
              {PAYLOAD_LRF_MODE_10HZ, "10 Hz"}}},
        },
        // Not in vio_sdk.h: from the payload's own camera definition (vio_camera_g1_def.xml, software 3.1.8.21).
        // With Autopilot, the VIO drops this app's GIMBAL_DEVICE_SET_ATTITUDE (mode flags and rates).
        "GB_FW_FLAG",
        {{0, "Autopilot"}, {1, "Onboard computer"}, {2, "SBUS Convert"}},
        1,
    };
    return p;
}
