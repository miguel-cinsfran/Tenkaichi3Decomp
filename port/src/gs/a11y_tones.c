/*
 * Accessibility: the fight's spatial and health tones (port/src/a11y_fight.c decides when; this makes and plays them).
 *
 *   distance   a short tick, repeated: a11y_fight.c spaces the ticks by the distance to the rival (closer, faster)
 *              and gives each its pitch by the rival's height (above, higher; below, lower), as Street Fighter 6's
 *              distance sound; within the game's own close range two ticks in a row
 *   edge       a low hum, repeated faster as the fighter nears the stage's wall; higher for its ceiling, lower
 *              for its floor
 *   ring       two low knocks: out over the grass of the World Tournament ring, where landing loses
 *   health     two beeps, high then low, as the low-health alarm of the Pokémon games: the less health is left,
 *              the louder (a11y_fight.c gives the loudness)
 *
 * Made here from sine waves (nothing of the game's sounds), on a stream of their own for each kind (they can sound
 * together), at the cues' volume (a11y_cue_volume). Player 1's side is on the left and player 2's on the right, as
 * the transformation and Ultimate Blast cues (a11y.c). Only integers come from a11y_fight.c: it is built soft-float.
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

extern SDL_AudioDeviceID Port_AudioDevice(void); /* snd_adx.c */
extern int PortA11y_CueVolume(void);              /* a11y.c */
extern int PortA11y_On(void);

#define TONE_RATE 48000
#define TONE_MAX (TONE_RATE * 300 / 1000) /* the longest tone */
#define TONE_EDGE (TONE_RATE * 3 / 1000)  /* fade in and out: no click */
#define TONE_TAU 6.2831853f

enum { TONE_DISTANCE, TONE_DISTANCE_CLOSE, TONE_EDGE_WALL, TONE_EDGE_TOP, TONE_EDGE_FLOOR, TONE_RING, TONE_HEALTH, TONE_KINDS };

static SDL_AudioStream *sStream[TONE_KINDS];
static int sTried;

/* A partial of `freq` Hz from sample `at`, `len` samples long, fading in and out, added to out. */
static void add_tone(float *out, int at, int len, float freq, float amp, int bright) {
    int i;
    for (i = 0; i < len && at + i < TONE_MAX; i++) {
        float x = (float)i / TONE_RATE, env = 1.0f, s;
        if (i < TONE_EDGE) {
            env = (float)i / TONE_EDGE;
        } else if (len - i < TONE_EDGE * 4) {
            env = (float)(len - i) / (TONE_EDGE * 4);
        }
        s = sinf(TONE_TAU * freq * x);
        if (bright) {
            s += 0.3f * sinf(TONE_TAU * 2.0f * freq * x) + 0.15f * sinf(TONE_TAU * 3.0f * freq * x);
        }
        out[at + i] += amp * env * s;
    }
}

/* The wave of a kind, -1..1 at most, into out (TONE_MAX samples); returns its length. `pitch` in semitones from the
   kind's own note. */
static int tone_wave(int kind, int pitch, float *out) {
    float base = 880.0f * powf(2.0f, (float)pitch / 12.0f); /* A5, moved */
    int ms = TONE_RATE / 1000, len = 0;

    memset(out, 0, sizeof(float) * TONE_MAX);
    switch (kind) {
    case TONE_DISTANCE: /* a tick: 25 ms, a little bright */
        add_tone(out, 0, 25 * ms, base, 0.6f, 1);
        len = 25 * ms;
        break;
    case TONE_DISTANCE_CLOSE: /* two ticks 50 ms apart */
        add_tone(out, 0, 25 * ms, base, 0.6f, 1);
        add_tone(out, 50 * ms, 25 * ms, base, 0.6f, 1);
        len = 75 * ms;
        break;
    case TONE_EDGE_WALL: /* a hum of 120 ms: A2 with a fifth over it */
    case TONE_EDGE_TOP:
    case TONE_EDGE_FLOOR: {
        float f = kind == TONE_EDGE_WALL ? 110.0f : kind == TONE_EDGE_TOP ? 220.0f : 73.42f;
        add_tone(out, 0, 120 * ms, f, 0.5f, 1);
        add_tone(out, 0, 120 * ms, f * 1.5f, 0.25f, 0);
        len = 120 * ms;
        break;
    }
    case TONE_RING: /* two low knocks */
        add_tone(out, 0, 60 * ms, 146.83f, 0.6f, 1);
        add_tone(out, 110 * ms, 60 * ms, 110.0f, 0.6f, 1);
        len = 170 * ms;
        break;
    case TONE_HEALTH: /* E6 then C6, 90 ms each, 40 ms apart */
        add_tone(out, 0, 90 * ms, 1318.5f, 0.45f, 0);
        add_tone(out, 130 * ms, 90 * ms, 1046.5f, 0.45f, 0);
        len = 220 * ms;
        break;
    }
    return len;
}

/* Plays a tone: kind as above, side 0 player 1 (left), 1 player 2 (right), -1 the middle; pitch in semitones;
   loud in percent of the cues' volume. A tone of the same kind still waiting is dropped: the newest says the most. */
void PortA11y_Tone(int kind, int side, int pitch, int loud) {
    static float wave[TONE_MAX];
    static int16_t pcm[TONE_MAX * 2];
    SDL_AudioDeviceID dev;
    SDL_AudioSpec spec;
    float gain, l, r;
    int len, i, k;

    if (!PortA11y_On() || kind < 0 || kind >= TONE_KINDS || PortA11y_CueVolume() <= 0 || loud <= 0) {
        return;
    }
    if (!sTried) {
        if ((dev = Port_AudioDevice()) == 0) {
            return; /* (no sound device, or none yet) */
        }
        sTried = 1;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = TONE_RATE;
        for (k = 0; k < TONE_KINDS; k++) {
            if ((sStream[k] = SDL_CreateAudioStream(&spec, NULL)) != NULL && !SDL_BindAudioStream(dev, sStream[k])) {
                SDL_DestroyAudioStream(sStream[k]);
                sStream[k] = NULL;
            }
        }
    }
    if (sStream[kind] == NULL) {
        return;
    }
    gain = 0.25f * (float)PortA11y_CueVolume() / 100.0f * (float)(loud > 100 ? 100 : loud) / 100.0f;
    l = side == 1 ? 0.35f : 1.0f; /* (the other side at a third, as a11y.c's cues have it) */
    r = side == 0 ? 0.35f : 1.0f;
    len = tone_wave(kind, pitch < -24 ? -24 : pitch > 24 ? 24 : pitch, wave);
    for (i = 0; i < len; i++) {
        pcm[i * 2] = (int16_t)(wave[i] * gain * l * 32767.0f);
        pcm[i * 2 + 1] = (int16_t)(wave[i] * gain * r * 32767.0f);
    }
    if (SDL_GetAudioStreamQueued(sStream[kind]) > 0) {
        SDL_ClearAudioStream(sStream[kind]);
    }
    SDL_PutAudioStreamData(sStream[kind], pcm, len * 4);
}

/* For herramientas/avisos-wav.sh-like checks without the game: the 16-bit stereo samples of a tone at full volume,
   into out (TONE_MAX frames); returns the frames. */
int PortA11y_TonePcm(int kind, int pitch, int16_t *out) {
    static float wave[TONE_MAX];
    int len, i;

    if (kind < 0 || kind >= TONE_KINDS) {
        return 0;
    }
    len = tone_wave(kind, pitch, wave);
    for (i = 0; i < len; i++) {
        out[i * 2] = out[i * 2 + 1] = (int16_t)(wave[i] * 0.25f * 32767.0f);
    }
    return len;
}
