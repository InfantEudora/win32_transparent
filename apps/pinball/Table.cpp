#include "Table.h"

/*
    The feature list, in the order the design document walks the machine: bottom, right edge, top,
    middle, ramps. Names are the stable ids - the MCP layout tool prints them, and from stage 2 the
    switch log will report hits by them - so they are worth keeping tidy and worth NOT renaming
    once anything has been recorded against them.

    Every position here is a reference to a #define in Table.h rather than a repeated literal. That
    is the whole discipline of this pair of files: a coordinate exists once, and moving it moves
    everything that reads it.
*/
const PinFeature PIN_FEATURES[] = {
    //--- bottom, the flipper end -----------------------------------------------------------------
    { "drain",              PIN_KIND_DRAIN,      PIN_DRAIN_X,               0.0f, PIN_DRAIN_Z              },
    { "flipper_left",       PIN_KIND_FLIPPER,    PIN_FLIPPER_L_X,           0.0f, PIN_FLIPPER_L_Z          },
    { "flipper_right",      PIN_KIND_FLIPPER,    PIN_FLIPPER_R_X,           0.0f, PIN_FLIPPER_R_Z          },
    { "flipper_upper",      PIN_KIND_FLIPPER,    PIN_FLIPPER_U_X,           0.0f, PIN_FLIPPER_U_Z          },
    //A slingshot is listed at the MIDDLE of its face; the two endpoints it is really built from
    //are in Table.h, because the face normal is what decides which way it kicks.
    { "sling_left",         PIN_KIND_SLINGSHOT,  (PIN_SLING_L_AX + PIN_SLING_L_BX) * 0.5f,
                                                                            0.0f, (PIN_SLING_L_AZ + PIN_SLING_L_BZ) * 0.5f },
    { "sling_right",        PIN_KIND_SLINGSHOT,  PIN_MIRROR_X((PIN_SLING_L_AX + PIN_SLING_L_BX) * 0.5f),
                                                                            0.0f, (PIN_SLING_L_AZ + PIN_SLING_L_BZ) * 0.5f },
    { "inlane_left",        PIN_KIND_ROLLOVER,   PIN_INLANE_L_X,            0.0f, PIN_INLANE_L_Z           },
    { "inlane_right",       PIN_KIND_ROLLOVER,   PIN_MIRROR_X(PIN_INLANE_L_X),
                                                                            0.0f, PIN_INLANE_L_Z           },
    { "save_left",          PIN_KIND_KICKER,     PIN_SAVE_L_X,              0.0f, PIN_SAVE_L_Z             },
    { "save_right",         PIN_KIND_KICKER,     PIN_MIRROR_X(PIN_SAVE_L_X), 0.0f, PIN_SAVE_L_Z            },

    //--- right edge, the launcher ----------------------------------------------------------------
    { "plunger",            PIN_KIND_PLUNGER,    PIN_PLUNGER_X,             0.0f, PIN_PLUNGER_Z            },
    { "skill_low",          PIN_KIND_ROLLOVER,   PIN_CHUTE_X,               0.0f, PIN_SKILL_Z_0            },
    { "skill_mid",          PIN_KIND_ROLLOVER,   PIN_CHUTE_X,               0.0f, PIN_SKILL_Z_1            },
    { "skill_high",         PIN_KIND_ROLLOVER,   PIN_CHUTE_X,               0.0f, PIN_SKILL_Z_2            },
    { "gate_orbit",         PIN_KIND_GATE,       PIN_GATE_X,                0.0f, PIN_GATE_Z               },

    //--- top, the orbit and the wormholes --------------------------------------------------------
    { "fuel_f",             PIN_KIND_ROLLOVER,   PIN_FUEL_X_0,              0.0f, PIN_FUEL_Z               },
    { "fuel_u",             PIN_KIND_ROLLOVER,   PIN_FUEL_X_1,              0.0f, PIN_FUEL_Z               },
    { "fuel_e",             PIN_KIND_ROLLOVER,   PIN_FUEL_X_2,              0.0f, PIN_FUEL_Z               },
    { "wormhole_left",      PIN_KIND_SAUCER,     PIN_WORM_0_X,              0.0f, PIN_WORM_0_Z             },
    { "wormhole_centre",    PIN_KIND_SAUCER,     PIN_WORM_1_X,              0.0f, PIN_WORM_1_Z             },
    { "wormhole_right",     PIN_KIND_SAUCER,     PIN_WORM_2_X,              0.0f, PIN_WORM_2_Z             },

    //--- middle, the scoring cluster -------------------------------------------------------------
    { "bumper_left",        PIN_KIND_BUMPER,     PIN_BUMPER_0_X,            0.0f, PIN_BUMPER_0_Z           },
    { "bumper_right",       PIN_KIND_BUMPER,     PIN_BUMPER_1_X,            0.0f, PIN_BUMPER_1_Z           },
    { "bumper_low",         PIN_KIND_BUMPER,     PIN_BUMPER_2_X,            0.0f, PIN_BUMPER_2_Z           },
    { "drop_m",             PIN_KIND_DROP_TARGET,PIN_DROP_X,               0.0f, PIN_DROP_Z_0             },
    { "drop_i",             PIN_KIND_DROP_TARGET,PIN_DROP_X,               0.0f, PIN_DROP_Z_1             },
    { "drop_s",             PIN_KIND_DROP_TARGET,PIN_DROP_X,               0.0f, PIN_DROP_Z_2             },
    { "standup_upper",      PIN_KIND_STANDUP,    PIN_STANDUP_0_X,           0.0f, PIN_STANDUP_0_Z          },
    { "standup_lower",      PIN_KIND_STANDUP,    PIN_STANDUP_1_X,           0.0f, PIN_STANDUP_1_Z          },
    { "spinner",            PIN_KIND_SPINNER,    PIN_SPINNER_X,             0.0f, PIN_SPINNER_Z            },
    { "gravity_well",       PIN_KIND_SAUCER,     PIN_WELL_X,                0.0f, PIN_WELL_Z               },

    //--- ramps -----------------------------------------------------------------------------------
    //Listed at their ENTRY, at deck level, because that is the thing a player aims at and the
    //thing a scripted test fires a ball at. The full centrelines are in ApplicationPinball, where
    //they are sampled into geometry.
    { "ramp_left",          PIN_KIND_RAMP_ENTRY, PIN_RAMP_L_ENTRY_X,        0.0f, PIN_RAMP_L_ENTRY_Z       },
    { "ramp_right",         PIN_KIND_RAMP_ENTRY, PIN_RAMP_R_ENTRY_X,        0.0f, PIN_RAMP_R_ENTRY_Z       },
};

const int PIN_FEATURE_COUNT = (int)(sizeof(PIN_FEATURES) / sizeof(PIN_FEATURES[0]));

const char* PinFeatureKindName(int kind){
    switch (kind){
        case PIN_KIND_DRAIN:        return "drain";
        case PIN_KIND_FLIPPER:      return "flipper";
        case PIN_KIND_SLINGSHOT:    return "slingshot";
        case PIN_KIND_ROLLOVER:     return "rollover";
        case PIN_KIND_KICKER:       return "kicker";
        case PIN_KIND_PLUNGER:      return "plunger";
        case PIN_KIND_GATE:         return "gate";
        case PIN_KIND_BUMPER:       return "bumper";
        case PIN_KIND_DROP_TARGET:  return "drop_target";
        case PIN_KIND_STANDUP:      return "standup";
        case PIN_KIND_SPINNER:      return "spinner";
        case PIN_KIND_SAUCER:       return "saucer";
        case PIN_KIND_POST:         return "post";
        case PIN_KIND_RAMP_ENTRY:   return "ramp_entry";
    }
    return "?";
}
