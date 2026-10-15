// The setup speaking for itself, for a player who cannot see its window, and finding the disc image by itself.
//
// Speech: Prism (https://github.com/ethindp/prism, MPL-2.0), the library the game's screen reader uses
// (port/src/gs/a11y.c), loaded from prism.dll (libprism.so) in the setup's folder only; without it nothing is said
// and the setup is what it was. The first screen reader that really starts wins, NVDA and JAWS before the rest
// (the system's own voice last), as in the game.
//
// Language and on/off: as the game's screen reader has them (port/src/gs/a11y.c, start and set_lang): off with a11y=0
// in bt3_settings.txt or $BT3_A11Y=0; the language $BT3_A11Y_LANG, else a11y_lang="xx", else the system's. The settings
// file is the game's ($BT3_SETTINGS, else bt3_settings.txt in the program's folder, where the game works).
// Every English text said here has its line in port/lang/a11y_<code>.txt (shipped next to the program), found by the
// FNV-1a hash of the English, a11y_<code>_own.txt first; English when there is none.
//
// The disc image: the .iso files in the setup's folder and in the two folders above it (the zip unpacks into a folder
// of its own, and Windows' "Extract All" into one more: an image kept next to the zip is two folders up), those whose
// name says Tenkaichi or BT3 first, then the largest (the game's disc is the largest file a player keeps there).
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>
#include "setup_voice.h"

#ifdef _WIN32
#define PRISM_CALL __cdecl
#define PRISM_LIB "prism.dll"
#else
#define PRISM_CALL
#define PRISM_LIB "libprism.so"
#endif

// Prism's C interface, the part used (include/prism.h of release 0.18.3; the same as port/src/gs/a11y.c).
typedef struct PrismContext PrismContext;
typedef struct PrismBackend PrismBackend;
typedef union {
    unsigned char room[512]; // PrismConfig, inside room a later and larger one may take (see a11y.c)
    uint64_t align;
} PrismConfig;
#define PRISM_OK 0
#define PRISM_SUPPORTS_SPEAK (1ULL << 2)
#define PRISM_SUPPORTS_OUTPUT (1ULL << 5)
#define PRISM_NVDA 0x89CC19C5C4AC1A56ULL
#define PRISM_JAWS 0xAC3D60E9BD84B53EULL

static struct {
    PrismConfig(PRISM_CALL *config_init)(void);
    PrismContext *(PRISM_CALL *init)(PrismConfig *cfg);
    size_t(PRISM_CALL *count)(PrismContext *ctx);
    uint64_t(PRISM_CALL *id_at)(PrismContext *ctx, size_t index);
    PrismBackend *(PRISM_CALL *create)(PrismContext *ctx, uint64_t id);
    void(PRISM_CALL *free_backend)(PrismBackend *backend);
    uint64_t(PRISM_CALL *features)(PrismBackend *backend);
    int(PRISM_CALL *initialize)(PrismBackend *backend);
    int(PRISM_CALL *output)(PrismBackend *backend, const char *text, bool interrupt);
    int(PRISM_CALL *speak)(PrismBackend *backend, const char *text, bool interrupt);
} P;

static PrismBackend *sBackend;
static std::unordered_map<uint64_t, std::string> sLines;

static uint64_t fnv1a(const char *s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 0x100000001B3ull;
    }
    return h;
}

static bool load_prism(const std::string &root) {
    static const char *const names[] = {"prism_config_init", "prism_init", "prism_registry_count", "prism_registry_id_at",
                                        "prism_registry_create", "prism_backend_free", "prism_backend_get_features",
                                        "prism_backend_initialize", "prism_backend_output", "prism_backend_speak"};
    SDL_SharedObject *lib = SDL_LoadObject((root + PRISM_LIB).c_str());
    SDL_FunctionPointer *slot = (SDL_FunctionPointer *)&P;

    if (lib == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if ((slot[i] = SDL_LoadFunction(lib, names[i])) == NULL) {
            SDL_UnloadObject(lib);
            return false;
        }
    }
    return true;
}

static PrismBackend *try_backend(PrismContext *ctx, uint64_t id) {
    PrismBackend *b = P.create(ctx, id);
    if (b != NULL && (P.initialize(b) != PRISM_OK || !(P.features(b) & (PRISM_SUPPORTS_OUTPUT | PRISM_SUPPORTS_SPEAK)))) {
        P.free_backend(b);
        b = NULL;
    }
    return b;
}

// The game's settings file (port/src/plat_settings.c, load): lines `name="text"` (one character at least) and
// `name=number` (as sscanf's %d reads it: spaces, a sign, digits), two kinds the game keeps apart (Port_SettingText,
// Port_Setting): `text` asks for the first. The last line of the name that reads as that kind counts; a line that does
// not is left out, as the game leaves it. Returns false when no line of the name reads as that kind.
static bool setting(const std::string &root, const char *name, bool text, std::string &value) {
    const char *env = getenv("BT3_SETTINGS");
    std::string path = env == NULL ? root + "bt3_settings.txt" : env;
    size_t n = 0, len = strlen(name);
    char *file = env != NULL && env[0] == '\0' ? NULL : (char *)SDL_LoadFile(path.c_str(), &n);
    bool found = false;

    for (char *line = file; line != NULL && *line != '\0';) {
        char *end = strchr(line, '\n');
        std::string got;
        while (*line == ' ' || *line == '\t' || *line == '\r') {
            line++;
        }
        if (strncmp(line, name, len) == 0 && line[len] == '=') {
            const char *v = line + len + 1;
            if (*v == '"') {
                for (v++; text && *v != '\0' && *v != '"' && *v != '\n' && got.size() < 63; v++) {
                    got += *v;
                }
            } else if (!text) {
                while (*v == ' ' || *v == '\t') {
                    v++;
                }
                const char *digits = *v == '-' || *v == '+' ? v + 1 : v;
                if (*digits >= '0' && *digits <= '9') {
                    for (got = *v == '-' ? "-" : ""; *digits >= '0' && *digits <= '9'; digits++) {
                        got += *digits;
                    }
                }
            }
            if (!got.empty()) {
                value = got;
                found = true;
            }
        }
        line = end != NULL ? end + 1 : NULL;
    }
    SDL_free(file);
    return found;
}

// The language's code from one like "pt-BR" or "ES": its first letters, lowercase, at most three (a11y.c, set_lang).
static std::string lang_code(const char *code) {
    std::string out;
    for (; code != NULL && out.size() < 3; code++) {
        char c = (char)(*code | 0x20);
        if (c < 'a' || c > 'z') {
            break;
        }
        out += c;
    }
    return out;
}

static std::string language(const std::string &root) {
    std::string code = lang_code(getenv("BT3_A11Y_LANG")), saved;
    if (getenv("BT3_A11Y_LANG") == NULL && setting(root, "a11y_lang", true, saved) && !saved.empty()) {
        code = lang_code(saved.c_str());
    } else if (getenv("BT3_A11Y_LANG") == NULL) {
        int n = 0;
        SDL_Locale **loc = SDL_GetPreferredLocales(&n);
        if (loc != NULL && n > 0 && loc[0]->language != NULL) {
            code = lang_code(loc[0]->language);
        }
        SDL_free(loc);
    }
    return code.empty() ? "en" : code;
}

// The lines of one language file ("hash<tab>text", '#' notes), the first one read of a hash kept.
static void load_lines(const std::string &path) {
    size_t n = 0;
    char *text = (char *)SDL_LoadFile(path.c_str(), &n);
    char *line = text != NULL && n >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0 ? text + 3 : text; // (the mark Notepad writes)

    while (line != NULL && *line != '\0') {
        char *end = strchr(line, '\n');
        if (end != NULL) {
            *end = '\0';
        }
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\r') {
            line[--len] = '\0';
        }
        if (len > 17 && line[16] == '\t' && line[0] != '#') {
            char hex[17];
            memcpy(hex, line, 16);
            hex[16] = '\0';
            sLines.emplace((uint64_t)strtoull(hex, NULL, 16), std::string(line + 17));
        }
        line = end != NULL ? end + 1 : NULL;
    }
    SDL_free(text);
}

void Voice_Start(const std::string &root) {
    std::string lang = language(root), on;

    if (lang != "en") {
        load_lines(root + "a11y_" + lang + "_own.txt"); // (the player's own first: they win)
        load_lines(root + "a11y_" + lang + ".txt");
    }
    if ((getenv("BT3_A11Y") != NULL && strcmp(getenv("BT3_A11Y"), "0") == 0) || (setting(root, "a11y", false, on) && atoi(on.c_str()) == 0)) {
        return; // (switched off for the game: off here too)
    }
    if (!load_prism(root)) {
        return;
    }
    PrismConfig cfg = P.config_init();
    PrismContext *ctx = P.init(&cfg);
    if (ctx == NULL) {
        return;
    }
    for (uint64_t id : {PRISM_NVDA, PRISM_JAWS}) {
        if (sBackend == NULL) {
            sBackend = try_backend(ctx, id);
        }
    }
    for (size_t i = 0, n = P.count(ctx); sBackend == NULL && i < n; i++) {
        uint64_t id = P.id_at(ctx, i);
        if (id != PRISM_NVDA && id != PRISM_JAWS) {
            sBackend = try_backend(ctx, id);
        }
    }
}

bool Voice_On(void) {
    return sBackend != NULL;
}

void Voice_Say(const std::string &text, bool interrupt) {
    if (sBackend == NULL || text.empty()) {
        return;
    }
    if (!(P.features(sBackend) & PRISM_SUPPORTS_OUTPUT) || P.output(sBackend, text.c_str(), interrupt) != PRISM_OK) {
        P.speak(sBackend, text.c_str(), interrupt);
    }
}

const char *Voice_Tr(const char *en) {
    auto it = sLines.find(fnv1a(en));
    return it != sLines.end() ? it->second.c_str() : en;
}

// 1: a and b have the same printf conversions in the same order, each the same whole spec and one of cdiouxXs: the
// game's same_conversions (port/src/gs/a11y.c), so that a line the game would not use is not used here either.
static bool same_conversions(const char *a, const char *b) {
    auto next = [](const char *s) -> const char * { // the next conversion from s on ("%%" is none), or NULL
        for (; (s = strchr(s, '%')) != NULL; s += 2) {
            if (s[1] != '%') {
                return s;
            }
        }
        return NULL;
    };
    for (;;) {
        a = next(a);
        b = next(b);
        if (a == NULL || b == NULL) {
            return a == b;
        }
        size_t n = 1 + strspn(a + 1, "-+ #0123456789.hlz");
        if (a[n] == '\0' || strchr("cdiouxXs", a[n]) == NULL || strncmp(a, b, n + 1) != 0) {
            return false;
        }
        a += n + 1;
        b += n + 1;
    }
}

const char *Voice_TrFormat(const char *en) {
    const char *line = Voice_Tr(en);
    return line != en && same_conversions(en, line) ? line : en; // (a line that does not match is not used)
}

std::vector<std::string> Voice_FindIsos(const std::string &root) {
    struct Found { std::string path; bool named; Sint64 size; };
    std::vector<Found> found;
    std::vector<std::string> out;
    std::string dir = root;

    for (int up = 0; up < 3 && !dir.empty(); up++) {
        int n = 0;
        char **names = SDL_GlobDirectory(dir.c_str(), "*.iso", SDL_GLOB_CASEINSENSITIVE, &n);
        for (int i = 0; names != NULL && i < n; i++) {
            std::string lower = names[i];
            SDL_PathInfo info;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
            if (SDL_GetPathInfo((dir + names[i]).c_str(), &info) && info.type == SDL_PATHTYPE_FILE) {
                found.push_back({dir + names[i], lower.find("tenkaichi") != std::string::npos || lower.find("bt3") != std::string::npos,
                                 (Sint64)info.size});
            }
        }
        SDL_free(names);
        // the folder above: dir without its last name ("C:\\Games\\BT3\\" -> "C:\\Games\\"; a root stays a root and ends it)
        size_t last = dir.find_last_of("/\\", dir.size() - 2);
        dir = last == std::string::npos || dir.size() < 2 ? "" : dir.substr(0, last + 1);
    }
    std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        return a.named != b.named ? a.named : a.size > b.size;
    });
    for (auto &f : found) {
        out.push_back(f.path);
    }
    return out;
}
