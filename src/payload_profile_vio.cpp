#include "payload_profile.h"

#include "vio_sdk.h"  // SDK libs/payload-define: the only source of these IDs and values

const PayloadProfile &vioProfile()
{
    static const PayloadProfile p{
        "VIO",
        PAYLOAD_CAMERA_VIEW_SRC,
        {{PAYLOAD_CAMERA_VIEW_EOIR, "EO / IR (picture-in-picture)"},
         {PAYLOAD_CAMERA_VIEW_EO, "EO only"},
         {PAYLOAD_CAMERA_VIEW_IR, "IR only"},
         {PAYLOAD_CAMERA_VIEW_IREO, "IR / EO (picture-in-picture)"},
         {PAYLOAD_CAMERA_VIEW_SYNC, "Sync"},
         {PAYLOAD_CAMERA_VIEW_SIDE_BY_SIDE, "Side by side"}},
        PAYLOAD_CAMERA_RECORD_SRC,
        {{PAYLOAD_CAMERA_RECORD_BOTH, "EO + IR"},
         {PAYLOAD_CAMERA_RECORD_EO, "EO"},
         {PAYLOAD_CAMERA_RECORD_IR, "IR"},
         {PAYLOAD_CAMERA_RECORD_OSD, "With OSD"}},
        PAYLOAD_CAMERA_IR_PALETTE,
        // The SDK header names each index differently per IR core (F1 / G1), so both are shown.
        {{PAYLOAD_CAMERA_IR_PALETTE_1, "1  (F1 WhiteHot | G1 WhiteHot)"},
         {PAYLOAD_CAMERA_IR_PALETTE_2, "2  (F1 BlackHot | G1 Fulgurite)"},
         {PAYLOAD_CAMERA_IR_PALETTE_3, "3  (F1 Rainbow | G1 IronRed)"},
         {PAYLOAD_CAMERA_IR_PALETTE_4, "4  (F1 RainbowHC | G1 HotIron)"},
         {PAYLOAD_CAMERA_IR_PALETTE_5, "5  (F1 Ironbow | G1 Medical)"},
         {PAYLOAD_CAMERA_IR_PALETTE_6, "6  (F1 Lava | G1 Arctic)"},
         {PAYLOAD_CAMERA_IR_PALETTE_7, "7  (F1 Arctic | G1 Rainbow1)"},
         {PAYLOAD_CAMERA_IR_PALETTE_8, "8  (F1 Globow | G1 Rainbow2)"},
         {PAYLOAD_CAMERA_IR_PALETTE_9, "9  (F1 Gradedfire | G1 Tint)"},
         {PAYLOAD_CAMERA_IR_PALETTE_10, "10 (F1 Hottest | G1 BlackHot)"}},
        PAYLOAD_CAMERA_GIMBAL_MODE,
        {{PAYLOAD_CAMERA_GIMBAL_MODE_OFF, "OFF"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_LOCK, "LOCK"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_FOLLOW, "FOLLOW"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_MAPPING, "MAPPING"},
         {PAYLOAD_CAMERA_GIMBAL_MODE_RESET, "RESET"}},
    };
    return p;
}
