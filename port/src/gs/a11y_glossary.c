/*
 * Accessibility: the glossary of the fight's sounds. Each entry is a sound the accessibility makes (gs/a11y_tones.c,
 * the cues of gs/a11y.c) and what it means. It is played from the settings window (a11y_ui.cpp: the Accessibility
 * tab's "Sound glossary", one button per sound) and in a fight's pause menu (a11y.c: Select steps through it).
 *
 * The sound is played at once and its meaning said a little after (a timer of SDL's), so that the two are not heard
 * over each other. The meanings are said in the language of the screen reader (port/lang/a11y_<code>.txt).
 */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

extern void PortA11y_Tone(int kind, int side, int pitch, int loud); /* a11y_tones.c */
extern void PortA11y_Cue(int side, int kind);                       /* a11y.c */
extern void PortA11y_Say(const char *text, int flags);
extern const char *PortA11y_TrSense(const char *sense, const char *label);

#define A11Y_INTERRUPT 1 /* (a11y.c) */
enum { TONE_DISTANCE, TONE_DISTANCE_CLOSE, TONE_EDGE_WALL, TONE_EDGE_TOP, TONE_EDGE_FLOOR, TONE_RING, TONE_HEALTH, TONE_KI };
#define CUE 100 /* kind CUE + n: a11y.c's cue n (0 a transformation, 1 the Ultimate Blast) */

static const struct {
    const char *name, *meaning;
    int kind, pitch;
} kEntries[] = {
    {"Tick", "Distance to the rival: it repeats faster the nearer the rival is; higher when the rival is above you, lower "
             "when below.", TONE_DISTANCE, 0},
    {"Two ticks", "The rival is within close range.", TONE_DISTANCE_CLOSE, 0},
    {"Hum", "You are near the stage's wall; faster the nearer.", TONE_EDGE_WALL, 0},
    {"High hum", "You are under the stage's ceiling.", TONE_EDGE_TOP, 0},
    {"Two low knocks", "You are out of the World Tournament ring.", TONE_RING, 0},
    {"Two beeps, high then low", "Health: louder the less is left. Player 1 on the left, player 2 on the right.", TONE_HEALTH, 0},
    {"Three notes going up", "The ki is full.", TONE_KI, 0},
    {"Three higher notes going up", "Max power.", TONE_KI, 7},
    {"A tone that slides up", "One more transformation can be taken.", CUE + 0, 0},
    {"A small bell, twice", "The Ultimate Blast can be fired.", CUE + 1, 0},
};
#define ENTRIES ((int)(sizeof(kEntries) / sizeof(kEntries[0])))

int PortA11y_GlossaryCount(void) {
    return ENTRIES;
}

/* An entry's name (English: a11y_ui.cpp says it in the language). */
const char *PortA11y_GlossaryName(int i) {
    return i >= 0 && i < ENTRIES ? kEntries[i].name : "";
}

static SDL_TimerID sPending; /* the meaning waiting to be said (one at a time: a newer entry replaces it) */
static char *sPendingText;

static Uint32 SDLCALL say_later(void *text, SDL_TimerID id, Uint32 interval) {
    (void)id;
    (void)interval;
    PortA11y_Say((const char *)text, A11Y_INTERRUPT);
    SDL_free(text);
    return 0;
}

/* Plays entry i and says what it means 0.45 s after; `numbered`: "3 of 10." before it (the pause menu, where the
   entries are gone through one at a time). */
void PortA11y_GlossaryPlay(int i, int numbered) {
    char text[512], head[48] = "";
    char *copy;

    if (i < 0 || i >= ENTRIES) {
        return;
    }
    if (kEntries[i].kind >= CUE) {
        PortA11y_Cue(0, kEntries[i].kind - CUE);
    } else {
        PortA11y_Tone(kEntries[i].kind, kEntries[i].kind == TONE_HEALTH ? 0 : -1, kEntries[i].pitch, 100);
    }
    if (numbered) {
        snprintf(head, sizeof(head), PortA11y_TrSense(NULL, "%d of %d"), i + 1, ENTRIES);
    }
    snprintf(text, sizeof(text), "%s%s%s. %s", head, head[0] != '\0' ? ". " : "", PortA11y_TrSense(NULL, kEntries[i].name),
             PortA11y_TrSense(NULL, kEntries[i].meaning));
    if (sPending != 0 && SDL_RemoveTimer(sPending)) {
        SDL_free(sPendingText); /* (not said yet: the newer one replaces it) */
    }
    sPending = 0;
    if ((copy = SDL_strdup(text)) != NULL && (sPending = SDL_AddTimer(450, say_later, copy)) == 0) {
        SDL_free(copy);
        PortA11y_Say(text, A11Y_INTERRUPT); /* (no timer: said at once) */
    }
    sPendingText = copy;
}

/* The pause menu's way through it (a11y.c, a Select tap): step +1 the next entry, -1 the one before; from the first
   step of a run, the first entry. */
void PortA11y_GlossaryStep(int step) {
    static int at = -1;

    at = at < 0 ? 0 : ((at + step) % ENTRIES + ENTRIES) % ENTRIES;
    PortA11y_GlossaryPlay(at, 1);
}
