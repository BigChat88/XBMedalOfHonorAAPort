/*
 * xbox_paddefaults.cpp -- controller defaults applied after the saved config.
 *
 * The saved config starts with "unbindall" and carries every archived cvar, so
 * defaults executed before it (default.cfg, xbox_default.cfg) are lost when the
 * config was written by an older build. Com_Init / Com_ExecuteCfg call
 * Xbox_ApplyPadDefaults once the config has run:
 *   - the controller's buttons get the layout below. It is imposed, not a
 *     default: the Controls menu has no way to change it, and a binding left in
 *     a saved config would make the game's texts name the wrong button;
 *   - the joystick cvars that must be on for the pad are forced on;
 *   - stick sensitivities still at the PC (ioquake3) defaults get the Xbox ones.
 *
 * Default layout:
 *   left stick         move / strafe (analog)
 *   right stick        look (analog)
 *   right trigger      fire
 *   left trigger       secondary attack (melee, scope)
 *   A                  jump
 *   B                  crouch
 *   X                  reload
 *   Y                  use (doors, mounted guns)
 *   white / black      lean left / right
 *   left stick click   walk (cl_run is on)
 *   right stick click  binoculars / items
 *   D-pad left/right   previous / next weapon
 *   D-pad up           grenade
 *   D-pad down         holster
 *   BACK               objectives / scores
 *   START              menu (always ESC, see xbox_uipad.cpp)
 */
#include "../../../third_party/openmohaa/code/client/client.h"

static const struct {
    const char *key;
    const char *command;
} pad_bindings[] = {
    {"PAD0_LEFTSTICK_UP",      "+forward"              },
    {"PAD0_LEFTSTICK_DOWN",    "+back"                 },
    {"PAD0_LEFTSTICK_LEFT",    "+moveleft"             },
    {"PAD0_LEFTSTICK_RIGHT",   "+moveright"            },
    {"PAD0_RIGHTSTICK_LEFT",   "+left"                 },
    {"PAD0_RIGHTSTICK_RIGHT",  "+right"                },
    {"PAD0_RIGHTSTICK_UP",     "+lookup"               },
    {"PAD0_RIGHTSTICK_DOWN",   "+lookdown"             },
    {"PAD0_RIGHTTRIGGER",      "+attackprimary"        },
    {"PAD0_LEFTTRIGGER",       "+attacksecondary"      },
    {"PAD0_A",                 "+moveup"               },
    {"PAD0_B",                 "+movedown"             },
    {"PAD0_X",                 "reload"                },
    {"PAD0_Y",                 "+use"                  },
    {"PAD0_LEFTSHOULDER",      "+leanleft"             },
    {"PAD0_RIGHTSHOULDER",     "+leanright"            },
    {"PAD0_LEFTSTICK_CLICK",   "+speed"                },
    {"PAD0_RIGHTSTICK_CLICK",  "toggleitem"            },
    {"PAD0_DPAD_LEFT",         "weapprev"              },
    {"PAD0_DPAD_RIGHT",        "weapnext"              },
    {"PAD0_DPAD_UP",           "useweaponclass grenade"},
    {"PAD0_DPAD_DOWN",         "holster"               },
    {"PAD0_BACK",              "+scores"               },
};

// Stick sensitivity: the PC default and earlier Xbox defaults -> the current
// Xbox default (see xbox_default.cfg); values the player changed are kept
static const struct {
    const char *name;
    float       oldValues[2];
    const char *xboxValue;
} pad_sensitivity[] = {
    {"j_yaw",     {-0.022f, -0.0055f}, "-0.0046"},
    {"j_pitch",   {0.022f, 0.004f},    "0.0032" },
    {"j_forward", {-0.25f, -0.25f},    "-0.0039"},
    {"j_side",    {0.25f, 0.25f},      "0.0039" },
};

// Detail settings: the engine's default -> the original game's "high" profile
// (high.cfg). The engine's defaults are above what the game ever shipped with;
// values the player changed are kept. See xbox_default.cfg.
static const struct {
    const char *name;
    float       engineValue;
    const char *xboxValue;
} detail_settings[] = {
    {"r_lodscale",      1.1f, "0.55"},
    {"r_lodcap",        1.0f, "0.55"},
    {"r_subdivisions",  3.0f, "4"   },
    {"ter_error",       4.0f, "9"   },
    {"ter_maxlod",      6.0f, "4"   },
    {"cg_effectdetail", 1.0f, "0.8" },
};

const char *UI_Xbox_PadFocusName(void);

// For test ISOs: "xbox_padkey PAD0_A 1", then "xbox_padkey PAD0_A 0" a few frames
// later, drives the game from a cfg as the controller would, without any input
static void Xbox_PadKey_f(void)
{
    const int key = Key_StringToKeynum(Cmd_Argv(1));

    if (Cmd_Argc() != 3 || key < 0) {
        Com_Printf("Usage: xbox_padkey <PAD0_name> <1 down, 0 up>\n");
        return;
    }
    Com_Printf("PADKEY %s %s %s\n", Cmd_Argv(1), Cmd_Argv(2), UI_Xbox_PadFocusName());
    Com_QueueEvent(0, SE_KEY, key, atoi(Cmd_Argv(2)) ? qtrue : qfalse, 0, NULL);
}

extern "C" void Xbox_ApplyPadDefaults(void)
{
    static qboolean commandAdded;
    int             bound = 0;

    if (!commandAdded) {
        Cmd_AddCommand("xbox_padkey", Xbox_PadKey_f);
        commandAdded = qtrue;
    }

    for (size_t i = 0; i < ARRAY_LEN(pad_bindings); i++) {
        const int   key     = Key_StringToKeynum(pad_bindings[i].key);
        const char *binding = key >= 0 ? Key_GetBinding(key) : NULL;

        if (key >= 0 && (!binding || Q_stricmp(binding, pad_bindings[i].command))) {
            Key_SetBinding(key, pad_bindings[i].command);
            bound++;
        }
    }

    Cvar_Set("in_joystick", "1");
    Cvar_Set("in_joystickUseAnalog", "1");
    // characters lit from the light grid: a config saved before it was the
    // Xbox default has 0, and no menu sets it (see xbox_default.cfg)
    Cvar_Set("r_fastentlight", "1");

    for (size_t i = 0; i < ARRAY_LEN(pad_sensitivity); i++) {
        const float value = Cvar_VariableValue(pad_sensitivity[i].name);
        for (size_t j = 0; j < ARRAY_LEN(pad_sensitivity[i].oldValues); j++) {
            if (value == pad_sensitivity[i].oldValues[j])
                Cvar_Set(pad_sensitivity[i].name, pad_sensitivity[i].xboxValue);
        }
    }

    for (size_t i = 0; i < ARRAY_LEN(detail_settings); i++) {
        if (Cvar_VariableValue(detail_settings[i].name) == detail_settings[i].engineValue)
            Cvar_Set(detail_settings[i].name, detail_settings[i].xboxValue);
    }

    if (bound)
        Com_Printf("Xbox: %d controller buttons got their binding\n", bound);
}
