/*
 * Accessibility: the controller's buttons by the names of the controller in the player's hands.
 *
 * The port takes every controller by where its buttons are (gs_input.c: SDL's south, east, west and north buttons
 * are the game's Cross, Circle, Square and Triangle), and the game's own texts say PlayStation names. An Xbox
 * controller (and any XInput one) has A, B, X and Y in those places, a Nintendo one B, A, Y and X, and their
 * shoulders, triggers and middle buttons have names of their own. So whatever the reader says about a button is
 * said here: the button the player has bound to that action (the Controls tab can move them), named as that
 * controller's maker names it; with no controller, the key bound to it.
 *
 * The controller is the player's (the Controls tab's controller of that player); for "whoever is holding one" (the
 * settings window, text with no player), the one a button was last pressed on, and else player 1's.
 *
 * Called from the game's thread (a11y_text.c, a11y_skills.c) and the render thread (a11y_ui.cpp); everything here
 * only asks SDL and the bindings, and the last controller used is one atomic value.
 *
 * Here too the left stick's click (L3 on a PlayStation controller, LS on an Xbox one): every controller has it, and in a
 * fight the game reads it only together with R3 (src/battle/btl_input.c, BtlInput_BuildMaskTable: L3 is only in the
 * mask of BTLB_L3R3, which needs both down); its menus read neither PADG_L3 nor the raw PAD_L3. (Select is not free:
 * in a fight it steps the camera's distance, btl_char_cam_modes.c ChrCam_UpdateInput.) A tap of it, pressed and let
 * go within half a second with no other button of that controller pressed in between and the settings window closed,
 * asks for the gauges in a fight and a second tap within 0.4 s for the rival's; outside a fight it says the last
 * message again (a11y.c, PortA11y_StickTap). The game still gets the button, as it would without this.
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

extern const char *PortA11y_TrSense(const char *sense, const char *label); /* a11y.c */
extern void PortA11y_Note(const char *text);

/* The families, as the reader names their buttons. */
enum { FAMILY_NONE, FAMILY_PS3, FAMILY_PS4, FAMILY_PS5, FAMILY_XBOX360, FAMILY_XBOX, FAMILY_NINTENDO, FAMILY_GAMECUBE };

static SDL_AtomicInt sLastPad; /* the SDL id of the controller a button was last pressed on, 0 = none yet */
extern void PortA11y_StickTap(int player, int taps); /* a11y.c */

/* The player (0 or 1) whose controller this is in the Controls tab, -1 none. */
static int player_of(SDL_JoystickID id) {
    SDL_JoystickID *ids;
    int n = 0, slot, p, found = -1;

    ids = SDL_GetGamepads(&n);
    for (slot = 0; ids != NULL && slot < n && ids[slot] != id; slot++) {
    }
    for (p = 0; ids != NULL && slot < n && p < 2 && found < 0; p++) {
        found = *PortInput_PadSlot(p) == slot ? p : -1;
    }
    SDL_free(ids);
    return found;
}

/* The render thread's events (a11y.c, PortA11y_Event): a button pressed tells which controller is in use, and the
   left stick's click alone is a tap (see the top). Each controller's click is followed on its own. */
void PortA11y_PadUsed(const SDL_Event *ev) {
    static struct {
        SDL_JoystickID id; /* 0: free */
        int alone;
        Uint64 down;
    } held[4];
    static int taps, tapPlayer;
    static Uint64 lastTap;
    SDL_JoystickID id = ev->gbutton.which;
    Uint64 now = SDL_GetTicks();
    int i, slot = -1, player;

    if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (i = 0; i < 4; i++) {
            held[i].id = held[i].id == ev->gdevice.which ? 0 : held[i].id; /* (its buttons let go by SDL are no tap) */
        }
        return;
    }
    if (ev->type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && ev->type != SDL_EVENT_GAMEPAD_BUTTON_UP) {
        return;
    }
    for (i = 0; i < 4; i++) {
        slot = slot < 0 && held[i].id == id ? i : slot;
    }
    if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        SDL_SetAtomicInt(&sLastPad, (int)id);
        if (ev->gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            for (i = 0; slot < 0 && i < 4; i++) {
                slot = held[i].id == 0 ? i : slot;
            }
            if (slot >= 0) {
                SDL_Gamepad *g = SDL_GetGamepadFromID(id);
                held[slot].id = id;
                held[slot].alone = !gPortOverlayOpen && !(g != NULL && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
                held[slot].down = now;
            }
        } else if (slot >= 0) {
            held[slot].alone = 0; /* (another button with it: not a tap; R3 with it is the game's L3 + R3) */
        }
    } else if (ev->gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK && slot >= 0) {
        held[slot].id = 0;
        player = player_of(id); /* (-1: not a player's controller, or no longer there) */
        if (held[slot].alone && now - held[slot].down < 500 && !gPortOverlayOpen && player >= 0) {
            taps = now - lastTap < 400 && player == tapPlayer ? taps + 1 : 1;
            lastTap = now;
            tapPlayer = player;
            PortA11y_StickTap(player, taps);
        }
    }
}

static int family_of(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
        return FAMILY_PS3;
    case SDL_GAMEPAD_TYPE_PS4:
        return FAMILY_PS4;
    case SDL_GAMEPAD_TYPE_PS5:
        return FAMILY_PS5;
    case SDL_GAMEPAD_TYPE_XBOX360:
        return FAMILY_XBOX360;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return FAMILY_NINTENDO;
    case SDL_GAMEPAD_TYPE_GAMECUBE:
        return FAMILY_GAMECUBE;
    default: /* Xbox One and Series, and the rest: SDL takes an unknown controller as an XInput one */
        return FAMILY_XBOX;
    }
}

/* The controller of a player (0 or 1), or with player < 0 the one last used (else player 1's): its SDL id, 0 = none. */
static SDL_JoystickID pad_id(int player) {
    SDL_JoystickID *ids, id = 0, last = (SDL_JoystickID)SDL_GetAtomicInt(&sLastPad);
    int n = 0, slot, i;

    ids = SDL_GetGamepads(&n);
    if (player < 0) {
        for (i = 0; ids != NULL && i < n && ids[i] != last; i++) {
        }
        if (last != 0 && ids != NULL && i < n) {
            SDL_free(ids);
            return last;
        }
        player = 0;
    }
    slot = *PortInput_PadSlot(player);
    if (ids != NULL && slot >= 0 && slot < n) {
        id = ids[slot];
    }
    SDL_free(ids);
    return id;
}

static SDL_GamepadType sType; /* (the type of the controller family() last looked at: its face buttons' labels) */

/* The family of a player's controller (player < 0: the one in use); FAMILY_NONE: the keyboard. */
static int family(int player) {
    SDL_JoystickID id = pad_id(player);

    sType = id == 0 ? SDL_GAMEPAD_TYPE_UNKNOWN : SDL_GetGamepadTypeForID(id);
    return id == 0 ? FAMILY_NONE : family_of(sType);
}

/* 1 PlayStation, 2 Xbox, 3 Nintendo, 0 none: for the log and the settings window. */
int PortA11y_PadFamily(int player) {
    int f = family(player);

    return f == FAMILY_NONE ? 0 : f <= FAMILY_PS5 ? 1 : f == FAMILY_NINTENDO || f == FAMILY_GAMECUBE ? 3 : 2;
}

static const char *tr_button(const char *label) {
    return PortA11y_TrSense("button", label);
}

/* An SDL controller button (or PORT_PAD_LT / PORT_PAD_RT) as the family's maker names it. */
static const char *source_name(int f, int src) {
    int ps = f >= FAMILY_PS3 && f <= FAMILY_PS5, nin = f == FAMILY_NINTENDO, gc = f == FAMILY_GAMECUBE;

    switch (src) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_WEST:
    case SDL_GAMEPAD_BUTTON_NORTH: {
        /* the letter or shape printed on the button in that place, as SDL knows it for the controller's type (a
           GameCube controller has A, X, B, Y there); by family when SDL has no label */
        static const char *const kLabels[] = {NULL, "A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle"};
        static const char *const kPs[] = {"Cross", "Circle", "Square", "Triangle"}, *const kXbox[] = {"A", "B", "X", "Y"},
                                 *const kNin[] = {"B", "A", "Y", "X"};
        int i = src - SDL_GAMEPAD_BUTTON_SOUTH, label = (int)SDL_GetGamepadButtonLabelForType(sType, (SDL_GamepadButton)src);
        if (label > 0 && label < (int)(sizeof(kLabels) / sizeof(kLabels[0]))) {
            return tr_button(kLabels[label]);
        }
        return tr_button(ps ? kPs[i] : nin ? kNin[i] : kXbox[i]);
    }
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return tr_button(ps ? "L1" : nin ? "L" : gc ? "Z" : "LB");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: /* (a GameCube controller's Z: SDL puts its L and R on the triggers) */
        return tr_button(ps ? "R1" : nin ? "R" : gc ? "Z" : "RB");
    case PORT_PAD_LT:
        return tr_button(ps ? "L2" : nin ? "ZL" : gc ? "L" : "LT");
    case PORT_PAD_RT:
        return tr_button(ps ? "R2" : nin ? "ZR" : gc ? "R" : "RT");
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return tr_button(ps ? "L3" : "LS");
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return tr_button(ps ? "R3" : "RS");
    case SDL_GAMEPAD_BUTTON_BACK:
        return tr_button(f == FAMILY_PS5 ? "Create" : f == FAMILY_PS4 ? "Share" : f == FAMILY_PS3 ? "Select"
                         : nin || gc ? "Minus" : f == FAMILY_XBOX360 ? "Back" : "View");
    case SDL_GAMEPAD_BUTTON_START:
        return tr_button(f == FAMILY_PS5 || f == FAMILY_PS4 ? "Options" : f == FAMILY_PS3 || gc ? "Start"
                         : nin ? "Plus" : f == FAMILY_XBOX360 ? "Start" : "Menu");
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return tr_button(ps ? "PS button" : nin || gc ? "Home button" : "Xbox button");
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return tr_button("D-pad up");
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return tr_button("D-pad down");
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return tr_button("D-pad left");
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return tr_button("D-pad right");
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        return tr_button("Touchpad");
    case SDL_GAMEPAD_BUTTON_MISC1:
        return tr_button(ps ? "Mute button" : nin || gc ? "Capture button" : "Share button");
    default:
        return NULL;
    }
}

/* A controller button of the settings window's list (SDL button or trigger) for the controller in use; NULL when it
   is none of those (gs/a11y_ui.cpp says the window's own name then). */
const char *PortA11y_PadSourceName(int src) {
    int f = family(-1);

    return f == FAMILY_NONE ? NULL : source_name(f, src);
}

/* What a game action (gs_input.c's order: 0..15 the pad's buttons, 16..23 the sticks' directions) is for a player
   (0 or 1; < 0: whoever is holding a controller): the button bound to it on that player's controller, by its
   maker's name, or the key bound to it when there is no controller. NULL when nothing is bound. */
const char *PortA11y_ActionName(int player, int action) {
    static const char *const kSticks[8] = {"Left stick up", "Left stick down", "Left stick left", "Left stick right",
                                           "Right stick up", "Right stick down", "Right stick left", "Right stick right"};
    int p = player & 1, f, src, key;

    if (player < 0) { /* (the player whose controller is in use, so that its name and its bindings agree) */
        p = player_of(pad_id(-1));
        player = p < 0 ? 0 : p;
        p = player;
    }
    f = family(player);

    if (action < 0 || action >= PORT_ACTIONS) {
        return NULL;
    }
    if (f != FAMILY_NONE) {
        if (action >= PORT_PAD_BUTTONS) {
            return tr_button(kSticks[action - PORT_PAD_BUTTONS]);
        }
        src = PortInput_PadButtons(p)[action];
        return src < 0 ? NULL : source_name(f, src);
    }
    key = PortInput_Keys(p)[action];
    return key == 0 ? NULL : SDL_GetScancodeName((SDL_Scancode)key);
}
