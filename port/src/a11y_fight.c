/*
 * Accessibility: the fight's alerts. Where the rival is (distance and height), the stage's edge, health, and the
 * cues a11y_game.c already had (a transformation or the Ultimate Blast within reach), each said by a sound
 * (gs/a11y_tones.c, gs/a11y.c), by the screen reader, by both or not at all, as the player sets it in the settings
 * window's Accessibility tab (gs/a11y_ui.cpp). All of them are off by default except the two cues, which sound as
 * before; the gauges asked for with the touchpad (or F3 / F4) are two beeps by default, and can be the spoken
 * gauges again.
 *
 *   distance  a tick that repeats faster as the rival is nearer, higher when the rival is above and lower when
 *             below (Street Fighter 6's distance sound, with the height of a fight in the air); two ticks while
 *             the game itself counts the rival as close (fighter flag 0x13, BtlCharApi_IsClose). By voice: near,
 *             middle distance or far, and above or below, when that changes.
 *   edge      what the game shows as the wall of the stage (src/battle/eft_g.c, EftBound_BuildWall): from
 *             param->range away from the stage's cylinder a hum, faster as the fighter nears it; the ceiling when
 *             the fighter is held under it (flag 0x15, BtlMove_ClampToStage); on the World Tournament stages,
 *             being out over the grass, where landing loses (src/battle/btl_char_coll_b.c, flag 7). By voice,
 *             when it starts.
 *   health    two beeps as each player's health falls under three quarters, a half and a quarter, louder the less
 *             is left; under a quarter, the player's own every three seconds, as the low-health alarm of the
 *             Pokémon games. By voice, the percent.
 *
 * Only the local player's distance and edge are told (online: that player's side; else player 1); health is told
 * for both, each on its side (player 1 left, player 2 right).
 *
 * It only reads, by the game's own read-only calls and fields; nothing of the game is written or drawn. a11y_game.c
 * calls it once per blank that is not run again after a rollback, and only while a fight is on with no menu over it.
 * Built soft-float: only integers go to gs/.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__x86_64__) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr
#else
#define GAME_PTR *
#endif

#define A11Y_INTERRUPT 1 /* gs/a11y.c */
extern const char *PortA11y_Tr(const char *label) __attribute__((format_arg(1)));
#define tr(label) PortA11y_Tr(label)
extern void PortA11y_Say(const char *text, int flags);
extern void PortA11y_Note(const char *text);
extern void PortA11y_Cue(int side, int kind);                     /* gs/a11y.c */
extern void PortA11y_Tone(int kind, int side, int pitch, int loud); /* gs/a11y_tones.c */
extern int Port_Setting(const char *name, int def);               /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern int Port_NetActive(void), Port_NetMe(void);                 /* gs/net.c */

/* The game's read-only calls (src/battle). The object id of a side's active fighter is the side. */
typedef struct { float x, y, z, w; } A11yVec;
/* (BtlCharApi_GetPos is not used: it writes into the caller's vector, which would have to be in the game's memory) */
extern A11yVec GAME_PTR BtlChar_GetPos(void GAME_PTR chr); /* src/battle/btl_char_get.c: the fighter's position */
extern float BtlCharApi_GetRadius(int objId);
extern int BtlCharApi_IsClose(int objId); /* fighter flag 0x13: the rival within the game's close range */
extern int BtlCharApi_GetHp(int objId), BtlCharApi_GetHpMax(int objId);
extern int BtlCharApi_GetChara(int objId), BtlCharApi_GetActiveMember(int objId);
extern void GAME_PTR BtlChar_FindBySide(int side);
extern int BtlChar_TestFlag(void GAME_PTR chr, unsigned n);
extern int BtlStage_IsReady(void);
extern int Battle_GetStage(void);

/* include/battle/stg_a.h: BtlStage and StgBounds (+Y is down: `top` is the smallest y). */
typedef struct {
    float radius, top, bottom;
} A11yStgBounds;
typedef struct {
    char unk0[0x40];
    A11yStgBounds GAME_PTR bounds; /* 0x40 */
} A11yStage;
_Static_assert(offsetof(A11yStage, bounds) == 0x40, "BtlStage");
extern A11yStage GAME_PTR gBtlStage;
/* include/battle/eft_g.h: EftBound and EftBoundParam, the stage wall's drawing. */
typedef struct {
    float range;     /* 0x00: how far from the wall it starts to show */
    float height, unk8;
    float radiusAdd; /* 0x0C: the wall's cylinder is the fighters' limit plus this */
} A11yBoundParam;
typedef struct {
    char unk0[0x218];
    A11yBoundParam GAME_PTR param; /* 0x218 */
} A11yBound;
_Static_assert(offsetof(A11yBound, param) == 0x218, "EftBound");
extern A11yBound GAME_PTR gEftBound;

#define CHR_AT_TOP 0x15   /* held under the stage's ceiling (BtlMove_ClampToStage) */
#define STAGE_TOURNAMENT_A 4 /* the World Tournament stages, with the ring (BtlChar_IsStage4Or27) */
#define STAGE_TOURNAMENT_B 27
#define RING_HALF 185.0f  /* inferred: btl_char_move.c keeps a fighter in this square there (under flag 2) */
#define INNER_MARGIN 100.0f /* BtlStage_GetInnerRadius: the stage radius less this */
#define MIDDLE 400.0f     /* inferred, to be tuned in a fight: "middle distance" up to this, "far" beyond */
#define HIGH 60.0f        /* inferred: about three fighters' height, "above" / "below" from here */

/* gs/a11y_tones.c */
enum { TONE_DISTANCE, TONE_DISTANCE_CLOSE, TONE_EDGE_WALL, TONE_EDGE_TOP, TONE_EDGE_FLOOR, TONE_RING, TONE_HEALTH };

/* The alerts and how each is told (a bit each: 1 a sound, 2 the screen reader). */
enum { ALERT_DISTANCE, ALERT_EDGE, ALERT_HEALTH, ALERT_POWER, ALERT_GAUGES, ALERTS };
#define BY_SOUND 1
#define BY_VOICE 2
static const char *const kKeys[ALERTS] = {"a11y_alert_distance", "a11y_alert_edge", "a11y_alert_health", "a11y_alert_power",
                                          "a11y_gauges"};
static const int kDefaults[ALERTS] = {0, 0, 0, BY_SOUND, BY_SOUND};
static int sMode[ALERTS] = {-1, -1, -1, -1, -1};

static int mode(int alert) {
    if (sMode[alert] < 0) {
        int v = Port_Setting(kKeys[alert], kDefaults[alert]), lo = alert == ALERT_GAUGES ? 1 : 0;
        sMode[alert] = v < lo || v > 3 ? kDefaults[alert] : v; /* (the gauges asked for are always told some way) */
    }
    return sMode[alert];
}

/* For the settings window (gs/a11y_ui.cpp): how an alert is told, 0..3 (the gauges: 1..3). */
int PortA11y_AlertMode(int alert) {
    return alert >= 0 && alert < ALERTS ? mode(alert) : 0;
}

void PortA11y_SetAlertMode(int alert, int value) {
    if (alert >= 0 && alert < ALERTS) {
        sMode[alert] = value < (alert == ALERT_GAUGES ? 1 : 0) ? (alert == ALERT_GAUGES ? 1 : 0) : value > 3 ? 3 : value;
        Port_SettingSave(kKeys[alert], sMode[alert]);
    }
}

static int sBlank; /* blanks of fights told so far */

/* How loud the health beeps are for a side: from a third at full health to all at none. */
static int health_loud(int hp, int max) {
    int pct = max > 0 ? hp * 100 / max : 100;
    return 35 + 65 * (100 - (pct < 0 ? 0 : pct > 100 ? 100 : pct)) / 100;
}

/* The gauges asked for (a11y_game.c, fight_gauges): the beeps when they go by sound; returns how they are told, so
   that the caller says them when that includes the voice. */
int PortA11y_GaugesQuery(int side) {
    extern int PortA11y_CueVolume(void); /* gs/a11y.c */
    int m = mode(ALERT_GAUGES);

    if (m == BY_SOUND && PortA11y_CueVolume() <= 0) {
        m = BY_VOICE; /* (the cues turned off: the beeps would be silent, and a question gets an answer) */
    }

    if (m & BY_SOUND) {
        PortA11y_Tone(TONE_HEALTH, side, 0, health_loud(BtlCharApi_GetHp(side), BtlCharApi_GetHpMax(side)));
    }
    return m;
}

/* A transformation or the Ultimate Blast within reach (a11y_game.c, fight_cues): kind 0 with `count`
   transformations, kind 1. */
void PortA11y_PowerCue(int side, int kind, int count) {
    char text[128];
    int m = mode(ALERT_POWER);

    if (m & BY_SOUND) {
        PortA11y_Cue(side, kind);
    }
    if (m & BY_VOICE) {
        if (kind == 0) {
            snprintf(text, sizeof(text), count > 1 ? tr("Player %d can take %d transformations") : tr("Player %d can transform"),
                     side + 1, count);
        } else {
            snprintf(text, sizeof(text), tr("Player %d can fire the Ultimate Blast"), side + 1);
        }
        PortA11y_Say(text, 0);
    }
}

/* ---- distance and height ---- */

static void distance(int me, A11yVec *a, A11yVec *b) {
    static int next, zone = -1, heard = -1, held;
    float dx = b->x - a->x, dy = a->y - b->y, dz = b->z - a->z, d = sqrtf(dx * dx + dy * dy + dz * dz);
    int m = mode(ALERT_DISTANCE), close = BtlCharApi_IsClose(me) != 0, gap, pitch, now;
    float x;
    char text[128];

    if (m & BY_SOUND) {
        /* 80 ms apart close by to 1.2 s far away; half an octave up or down at three fighters' height, an octave at most */
        gap = (int)((80.0f + 1120.0f * d / (d + MIDDLE)) * 60.0f / 1000.0f);
        gap = gap < 5 ? 5 : gap; /* (the double tick lasts 75 ms: never put a new one over it) */
        if (next - sBlank > gap) {
            next = sBlank + gap; /* (nearer now: the next tick comes sooner) */
        }
        if (sBlank >= next) {
            x = 12.0f * dy / (fabsf(dy) + HIGH);
            pitch = (int)(x < 0.0f ? x - 0.5f : x + 0.5f); /* (rounded here: the C library's lrintf is the host's, SSE) */
            PortA11y_Tone(close ? TONE_DISTANCE_CLOSE : TONE_DISTANCE, -1, pitch, 100);
            next = sBlank + gap;
        }
    }
    if (m & BY_VOICE) {
        now = (close ? 0 : d < MIDDLE ? 1 : 2) * 3 + (dy > HIGH ? 1 : dy < -HIGH ? 2 : 0);
        if (now != zone) {
            zone = now;
            held = 0;
        } else if (++held == 30 && now != heard) { /* (half a second the same: not every step at a border) */
            static const char *const kZones[] = {"Rival close", "Rival at middle distance", "Rival far"};
            static const char *const kHeights[] = {"", ", above", ", below"};
            heard = now;
            snprintf(text, sizeof(text), "%s%s", tr(kZones[now / 3]), now % 3 ? tr(kHeights[now % 3]) : "");
            PortA11y_Say(text, 0);
        }
    }
    if (sBlank % 120 == 0) {
        snprintf(text, sizeof(text), "distance %d, height %d%s", (int)d, (int)dy, close ? ", close" : "");
        PortA11y_Note(text);
    }
}

/* ---- the stage's edge ---- */

static void edge(int me, void GAME_PTR chr, A11yVec *a) {
    static int next, said = -1;
    A11yBoundParam GAME_PTR param = gEftBound != NULL ? gEftBound->param : NULL;
    float range = param != NULL && param->range > 0.0f ? param->range : 300.0f;
    float wall, margin, r = sqrtf(a->x * a->x + a->z * a->z);
    int m = mode(ALERT_EDGE), stage = Battle_GetStage(), kind = -1, gap = 0, now;
    char text[96];

    if (!BtlStage_IsReady() || gBtlStage->bounds == NULL) {
        return;
    }
    wall = gBtlStage->bounds->radius - INNER_MARGIN + (param != NULL ? param->radiusAdd : 0.0f);
    margin = wall - r - BtlCharApi_GetRadius(me);
    if ((stage == STAGE_TOURNAMENT_A || stage == STAGE_TOURNAMENT_B) && (fabsf(a->x) > RING_HALF || fabsf(a->z) > RING_HALF)) {
        kind = TONE_RING;
        gap = 60;
    } else if (BtlChar_TestFlag(chr, CHR_AT_TOP)) {
        kind = TONE_EDGE_TOP;
        gap = 30;
    } else if (margin < range) {
        kind = TONE_EDGE_WALL; /* every half second at the edge of the range down to every tenth at the wall */
        gap = 6 + (int)(24.0f * (margin < 0.0f ? 0.0f : margin) / range);
    }
    if ((m & BY_SOUND) && kind >= 0 && sBlank >= next) {
        PortA11y_Tone(kind, -1, 0, 100);
        next = sBlank + gap;
    }
    now = kind;
    if (now != said) {
        said = now;
        if ((m & BY_VOICE) && now >= 0) {
            PortA11y_Say(tr(now == TONE_RING ? "Out of the ring" : now == TONE_EDGE_TOP ? "Ceiling of the stage" : "Edge of the stage"), 0);
        }
        snprintf(text, sizeof(text), "edge: %s (wall %d away)", now < 0 ? "none" : now == TONE_RING ? "out of the ring" :
                 now == TONE_EDGE_TOP ? "ceiling" : "wall", (int)margin);
        PortA11y_Note(text);
    }
}

/* ---- health ---- */

static void health(int me) {
    static int level[2] = {-1, -1}, who[2], next;
    int side, hp, max, pct, now, m = mode(ALERT_HEALTH), id;
    char text[128];

    for (side = 0; side < 2; side++) {
        hp = BtlCharApi_GetHp(side);
        max = BtlCharApi_GetHpMax(side);
        if (max <= 0) {
            continue;
        }
        pct = hp * 100 / max;
        now = pct > 75 ? 3 : pct > 50 ? 2 : pct > 25 ? 1 : 0;
        id = (BtlCharApi_GetActiveMember(side) << 16) | (BtlCharApi_GetChara(side) + 1);
        if (id != who[side]) { /* another fighter (a team's next member, a transformation, the next fight) */
            who[side] = id;
            level[side] = now;
            continue;
        }
        if (now < level[side]) {
            if (m & BY_SOUND) {
                PortA11y_Tone(TONE_HEALTH, side, 0, health_loud(hp, max));
            }
            if (m & BY_VOICE) {
                if (now == 0) {
                    snprintf(text, sizeof(text), tr("Player %d: health in danger, %d percent"), side + 1, pct < 0 ? 0 : pct);
                } else {
                    snprintf(text, sizeof(text), tr("Player %d: health %d percent"), side + 1, pct);
                }
                PortA11y_Say(text, 0);
            }
            if (side == me) {
                next = sBlank + 180;
            }
        }
        level[side] = now;
        if (side == me && now == 0 && hp > 0 && (m & BY_SOUND) && sBlank >= next) {
            PortA11y_Tone(TONE_HEALTH, side, 0, health_loud(hp, max)); /* the alarm, every three seconds */
            next = sBlank + 180;
        }
    }
}

/* Once per blank from a11y_game.c: `fight` while a fight is on with no menu over it. */
void PortA11y_FightAlerts(int fight) {
    static int was;
    A11yVec a, b;
    void GAME_PTR chr;
    void GAME_PTR foe;
    int me;

    if (!fight || BtlChar_FindBySide(0) == NULL || BtlChar_FindBySide(1) == NULL) {
        was = 0;
        return;
    }
    if (!was) {
        was = 1;
        sBlank += 600; /* (whatever was waiting from the last fight is due) */
    }
    sBlank++;
    me = Port_NetActive() ? Port_NetMe() & 1 : 0;
    chr = BtlChar_FindBySide(me);
    foe = BtlChar_FindBySide(me ^ 1);
    if ((mode(ALERT_DISTANCE) | mode(ALERT_EDGE)) != 0) {
        a = *BtlChar_GetPos(chr);
        b = *BtlChar_GetPos(foe);
        if (mode(ALERT_DISTANCE)) {
            distance(me, &a, &b);
        }
        if (mode(ALERT_EDGE)) {
            edge(me, chr, &a);
        }
    }
    if (mode(ALERT_HEALTH)) {
        health(me);
    }
}
