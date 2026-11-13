#include "pip.h"
#include "face.h"
#include "imu.h"
#include "mic.h"
#include "theme.h"
#include <stdio.h>
#include "doodle.h"
#include "pet.h"
#include "voice.h"
#include <math.h>
#include <string.h>

#define SCR 466
#define TAP_MS 300
#define SPREAD 0.55f  /* angle of each eye from the front of the (virtual, round) head, rad */
#define REST_SPACING 237.0f /* px between the eyes when looking straight */
#define HEAD_R (REST_SPACING / (2 * 0.5227f)) /* sin(SPREAD); turning the head slides the far eye behind the near one */
#define EYE_SCALE 1.45f /* mood table sizes -> screen */

typedef struct { float w, h, roll, taper, lid, tilt, smile, pw, ph, square, heart, glint, spiral, veins, wet; } shape_t;
#define SHAPE_N (int)(sizeof(shape_t) / sizeof(float))

/* One mood = eye shape + how Pip behaves in it. Anything left out is "neutral":
   w/h/pw/ph of 0 mean the neutral size, look_rate/look_amp/blink of 0 mean 1.
   roll/tilt are for the left eye (+ = inner corner down), mirrored for the right one. */
typedef struct {
    const char *name;
    shape_t s;
    float closed;              /* 0 open .. 1 shut */
    float gx, gy;              /* where it tends to look, -1..1 */
    float head_roll;
    float lid_asym, size_asym; /* right eye: extra lid, extra size */
    float cross;               /* pupils turned inward (+) */
    uint32_t tint;             /* white of the eye, 0 = white */
    float blush;
    float look_rate, look_amp; /* glances: how often, how far */
    float slow;                /* 0..1 sluggish */
    float tremble;             /* px */
    float sway_x, sway_y;      /* slow head circles / nods, rad */
    float nod, wag;            /* answers: the head nods yes / shakes no, rad */
    float blink;               /* blink frequency multiplier */
    float wink;                /* seconds between winks of the right eye, 0 = never */
    uint8_t fx;                /* scene effect around the face */
    const char *text;          /* drawn above the eyes */
    uint32_t text_color;
} mood_def_t;

enum { FX_NONE, FX_SWEAR, FX_RAIN };

#define S(...) .s = {__VA_ARGS__}
static const mood_def_t moods[PIP_MOOD_COUNT] = {
    [PIP_NEUTRAL]     = {.name = "neutral"},
    [PIP_HAPPY]       = {"happy", S(.w = 118, .h = 124, .roll = -.06f, .taper = -.1f, .smile = .42f, .pw = .36f, .ph = .38f), .gy = -.8f, .head_roll = .12f},
    [PIP_SAD]         = {"sad", S(.w = 112, .h = 126, .roll = -.14f, .taper = .15f, .lid = .22f, .tilt = -.35f, .pw = .52f, .ph = .56f), .gy = .55f, .head_roll = -.1f, .slow = .3f},
    [PIP_ANGRY]       = {"angry", S(.w = 114, .h = 114, .roll = .16f, .taper = .3f, .lid = .3f, .tilt = .45f, .pw = .2f, .ph = .4f), .gy = .1f},
    [PIP_SURPRISED]   = {"surprised", S(.w = 124, .h = 160, .taper = -.05f, .pw = .24f, .ph = .26f), .gy = -.1f, .look_rate = 2},
    [PIP_SLEEPY]      = {"sleepy", S(.w = 122, .h = 124, .lid = .5f, .tilt = -.05f, .pw = .44f, .ph = .44f), .gy = .45f, .head_roll = .08f, .slow = .5f, .blink = 1.6f},
    [PIP_ASLEEP]      = {"asleep", S(.w = 122, .h = 124, .pw = .44f, .ph = .44f), .closed = 1, .gy = .4f, .head_roll = .1f},
    [PIP_SKEPTICAL]   = {"skeptical", S(.w = 128, .h = 76, .roll = .04f, .lid = .08f, .pw = .52f, .ph = .3f), .lid_asym = .28f, .head_roll = -.06f},
    [PIP_CURIOUS]     = {"curious", S(.w = 124, .h = 148, .taper = -.05f, .pw = .5f, .ph = .54f), .lid_asym = -.1f, .gy = -.1f, .head_roll = .22f},
    [PIP_LOVE]        = {"love", S(.w = 120, .h = 128, .taper = -.08f, .smile = .2f, .pw = .46f, .ph = .46f, .heart = 1), .gy = -.15f, .head_roll = .1f, .blush = .6f},
    [PIP_DIZZY]       = {"dizzy", S(.w = 120, .h = 128, .lid = .12f, .pw = .34f, .ph = .34f), .lid_asym = .1f},
    [PIP_OUCH]        = {"ouch", S(.w = 126, .h = 26, .roll = .3f, .pw = .2f, .ph = .2f)},

    [PIP_STONED]      = {"stoned", S(.w = 124, .h = 122, .roll = -.04f, .lid = .42f, .tilt = -.04f, .smile = .24f, .pw = .46f, .ph = .48f, .veins = 1),
                         .gy = .1f, .head_roll = .1f, .tint = 0xFFE4E0, .look_amp = .5f, .slow = .75f, .sway_x = .06f, .sway_y = .04f, .blink = .6f},
    [PIP_PINKEYE]     = {"pinkeye", S(.w = 118, .h = 128, .lid = .25f, .tilt = -.1f), .gy = .2f, .lid_asym = .4f, .size_asym = .08f, .tint = 0xFF8C88, .slow = .2f, .blink = 2.5f},
    [PIP_NAUSEOUS]    = {"nauseous", S(.w = 116, .h = 124, .roll = -.06f, .lid = .3f, .tilt = -.2f, .pw = .38f, .ph = .4f), .gy = .2f, .lid_asym = .12f,
                         .tint = 0xD2F2B4, .slow = .4f, .sway_x = .12f, .sway_y = .08f},
    [PIP_SICK]        = {"sick", S(.w = 112, .h = 118, .lid = .38f, .tilt = -.15f, .pw = .36f, .ph = .38f), .gy = .35f, .tint = 0xC6D0DA, .slow = .5f, .tremble = 1, .blink = 1.5f},
    [PIP_TIRED]       = {"tired", S(.w = 120, .h = 120, .lid = .45f, .tilt = -.12f, .pw = .42f, .ph = .44f), .gy = .3f, .look_amp = .6f, .slow = .45f, .blink = 1.8f},
    [PIP_EXHAUSTED]   = {"exhausted", S(.w = 118, .h = 112, .roll = -.06f, .lid = .62f, .tilt = -.15f), .gy = .45f, .head_roll = .12f, .slow = .7f, .sway_y = .05f, .blink = 2},

    [PIP_ANNOYED]     = {"annoyed", S(.w = 116, .h = 124, .roll = .06f, .lid = .2f, .tilt = .22f, .pw = .36f, .ph = .4f), .gx = .5f, .look_rate = .5f, .look_amp = .3f},
    [PIP_FURIOUS]     = {"furious", S(.w = 112, .h = 110, .roll = .2f, .taper = .35f, .lid = .36f, .tilt = .6f, .pw = .16f, .ph = .34f), .tint = 0xFFD6CC, .look_amp = .3f, .tremble = 1.2f},
    [PIP_RAGE]        = {"rage", S(.w = 110, .h = 106, .roll = .24f, .taper = .4f, .lid = .38f, .tilt = .7f, .pw = .14f, .ph = .3f), .tint = 0xFF3A28,
                         .look_rate = .3f, .look_amp = .2f, .tremble = 3},
    [PIP_SWEARING]    = {"swearing", S(.w = 118, .h = 118, .roll = .16f, .taper = .28f, .lid = .3f, .tilt = .5f, .pw = .2f, .ph = .38f), .look_rate = .5f,
                         .tremble = 1.5f, .fx = FX_SWEAR},
    [PIP_INDIGNANT]   = {"indignant", S(.w = 122, .h = 146, .roll = .08f, .lid = .12f, .tilt = .35f, .pw = .26f, .ph = .3f), .gy = -.2f, .head_roll = -.08f, .look_rate = 1.5f},
    [PIP_BELLIGERENT] = {"belligerent", S(.w = 116, .h = 112, .roll = .18f, .taper = .25f, .lid = .34f, .tilt = .5f, .pw = .3f, .ph = .42f), .gy = -.5f,
                         .look_rate = .4f, .look_amp = .2f},
    [PIP_HURT]        = {"hurt", S(.w = 114, .h = 124, .roll = -.12f, .lid = .2f, .tilt = -.32f, .pw = .5f, .ph = .54f, .glint = 1, .wet = 1),
                         .gx = -.3f, .gy = .3f, .head_roll = -.12f, .look_amp = .3f, .slow = .3f, .tremble = .4f},

    [PIP_DISGUSTED]   = {"disgusted", S(.w = 118, .h = 104, .roll = -.1f, .lid = .3f, .tilt = -.1f, .smile = .25f, .pw = .32f, .ph = .34f), .gx = -.8f, .gy = .35f,
                         .head_roll = -.15f, .lid_asym = .2f, .tint = 0xE8F4D4},
    [PIP_SUSPICIOUS]  = {"suspicious", S(.w = 124, .h = 86, .lid = .44f, .tilt = .08f, .pw = .32f, .ph = .3f), .head_roll = .08f, .lid_asym = .08f,
                         .look_rate = .25f, .look_amp = .12f, .slow = .3f}, /* stares right at you */
    [PIP_CYNICAL]     = {"cynical", S(.w = 120, .h = 108, .lid = .42f, .tilt = -.08f, .pw = .34f, .ph = .36f), .gx = .3f, .gy = -.85f, .head_roll = .1f, .lid_asym = .1f,
                         .look_rate = .3f},
    [PIP_AFRAID]      = {"afraid", S(.w = 126, .h = 160, .roll = -.1f, .taper = -.1f, .lid = .08f, .tilt = -.3f, .pw = .18f, .ph = .2f), .gy = .1f,
                         .look_rate = 2.5f, .look_amp = .5f, .tremble = 2.2f},
    [PIP_NERVOUS]     = {"nervous", S(.w = 118, .h = 140, .lid = .06f, .tilt = -.15f, .pw = .3f, .ph = .32f), .look_rate = 4, .look_amp = .7f, .tremble = .8f, .blink = 2.2f},
    [PIP_DESPONDENT]  = {"despondent", S(.w = 108, .h = 114, .roll = -.12f, .lid = .34f, .tilt = -.4f, .pw = .42f, .ph = .46f), .gy = .7f, .head_roll = -.1f,
                         .look_rate = .4f, .look_amp = .4f, .slow = .6f},
    [PIP_LONELY]      = {"lonely", S(.w = 100, .h = 112, .roll = -.08f, .lid = .18f, .tilt = -.25f, .pw = .46f, .ph = .5f), .gx = -.6f, .gy = .5f, .tint = 0xD4DCE8,
                         .look_rate = .4f, .slow = .5f},
    [PIP_GLOOMY]      = {"gloomy", S(.w = 118, .h = 104, .lid = .5f, .tilt = .02f, .pw = .36f, .ph = .38f), .gy = .45f, .tint = 0x9EA3AA, .look_rate = .3f, .slow = .5f,
                         .fx = FX_RAIN},
    [PIP_DULL]        = {"dull", S(.w = 122, .h = 126, .lid = .35f, .pw = .3f, .ph = .32f), .head_roll = .15f, .lid_asym = .1f, .cross = .7f, .look_rate = .3f,
                         .slow = .6f, .blink = .5f},
    [PIP_SHY]         = {"shy", S(.w = 112, .h = 124, .roll = -.06f, .lid = .12f, .tilt = -.15f, .smile = .15f, .pw = .44f, .ph = .46f), .gx = -.7f, .gy = .55f,
                         .head_roll = -.15f, .blush = .9f, .look_rate = .6f, .look_amp = .4f},
    [PIP_SMUG]        = {"smug", S(.w = 124, .h = 104, .lid = .38f, .tilt = -.06f, .smile = .3f, .pw = .36f, .ph = .38f), .gx = .35f, .gy = -.1f, .head_roll = .14f,
                         .lid_asym = .08f, .look_rate = .4f, .slow = .2f},
    [PIP_BORED]       = {"bored", S(.w = 122, .h = 114, .lid = .45f, .tilt = -.04f, .pw = .34f, .ph = .36f), .gx = .5f, .gy = -.5f, .look_rate = .3f, .slow = .5f, .blink = .7f},
    [PIP_SHOCKED]     = {"shocked", S(.w = 138, .h = 172, .taper = -.08f, .pw = .14f, .ph = .15f), .look_rate = .02f, .look_amp = .05f, .tremble = 1, .blink = .05f,
                         .text = "!", .text_color = 0xFFFFFF},
    [PIP_DISMAYED]    = {"dismayed", S(.w = 118, .h = 146, .roll = -.12f, .lid = .12f, .tilt = -.4f, .pw = .3f, .ph = .32f), .gy = .1f, .look_rate = 2.5f,
                         .look_amp = .6f, .tremble = .6f},
    [PIP_PUZZLED]     = {"puzzled", S(.w = 120, .h = 132, .pw = .36f, .ph = .38f), .gx = .3f, .gy = -.3f, .head_roll = -.22f, .lid_asym = .3f, .size_asym = -.15f,
                         .look_rate = .5f, .text = "?", .text_color = 0xFFFFFF},

    [PIP_AMAZED]      = {"amazed", S(.w = 132, .h = 160, .taper = -.06f, .pw = .48f, .ph = .5f, .glint = 1), .gy = -.25f, .look_rate = .5f, .look_amp = .4f},
    /* one brow down, the other eye wide, chin up, looking right at you, and a wink now and then */
    [PIP_CHEEKY]      = {"cheeky", S(.w = 120, .h = 126, .lid = .22f, .tilt = .35f, .smile = .12f, .pw = .36f, .ph = .4f), .gy = -.25f, .head_roll = -.18f,
                         .lid_asym = -.3f, .size_asym = .1f, .look_rate = 1.2f, .look_amp = .3f, .wink = 2.8f},
    [PIP_CONTENT]     = {"content", S(.w = 124, .h = 124, .roll = -.06f, .taper = -.1f, .smile = .72f, .pw = .36f, .ph = .38f), .gy = -.3f, .head_roll = .1f, .look_rate = .3f, .slow = .4f, .blink = .6f},
    [PIP_COMFY]       = {"comfy", S(.w = 124, .h = 118, .lid = .38f, .smile = .2f, .pw = .4f, .ph = .42f), .gy = .1f, .look_rate = .3f, .slow = .6f, .blink = .5f},

    /* looking right at you, the head nods / shakes */
    [PIP_YES]         = {"yes", S(.w = 120, .h = 130, .taper = -.05f, .smile = .25f, .pw = .42f, .ph = .44f, .glint = .4f), .head_roll = .06f,
                         .look_rate = .1f, .look_amp = .1f, .nod = .13f},
    [PIP_NO]          = {"no", S(.w = 118, .h = 120, .roll = .04f, .lid = .2f, .tilt = .2f, .pw = .36f, .ph = .4f), .look_rate = .1f, .look_amp = .1f,
                         .wag = .2f},
};
#undef S

static shape_t mood_shape(const mood_def_t *m)
{
    shape_t s = m->s;
    if (!s.w) s.w = 118;
    if (!s.h) s.h = 138;
    if (!s.pw) s.pw = .40f;
    if (!s.ph) s.ph = .44f;
    return s;
}
static float or1(float v) { return v ? v : 1; }

/* Damped spring: zeta < 1 overshoots, which is where the cartoon bounce comes from. */
typedef struct { float x, v; } spring_t;
static void spring(spring_t *s, float to, float stiff, float zeta, float dt)
{
    s->v += ((to - s->x) * stiff - s->v * 2.0f * sqrtf(stiff) * zeta) * dt;
    s->x += s->v * dt;
}

static struct {
    lv_obj_t *canvas, *text;
    lv_draw_buf_t *buf, *back; /* back: a second canvas when the face renders in the background (face_start) */
    face_t rface;              /* the frame being drawn in the background: a snapshot of face */
    face_rect_t front_rects[4], back_rects[4];
    bool pending;
    uint32_t last_swap;
    face_t face;
    imu_t imu;
    int mood, shown;
    /* reaction: a mood shown for a while on top of the base mood */
    int react_mood;
    float react_left, annoy, pet_total, pet_recent;
    const char *react_name;
    bool meet_pending;
    spring_t shape[2][SHAPE_N];
    spring_t open, squash, look_x, look_y, head_yaw, head_pitch, head_roll, dizzy;
    spring_t tint[3], blush, tremble, nod, wag;
    const char *text_shown;
    float wink_t, next_wink;
    /* scene effects */
    lv_obj_t *rain_cloud;
    /* the pet behind the face */
    pet_t pet;
    bool auto_mood, bat_charging, no_shakes;
    unsigned feat_off; /* PIP_F_* bits switched off */
    float intro;       /* seconds into waking up after power-on; < 0: done */
    int bat_pct;
    int tz;
    float stroke_t, shakes, last_voice, greet_t;
    float dark_in;  /* > 0: the screen goes dark in this many seconds (lying face down) */
    bool dark;      /* the screen is off: nothing to draw */
    float room_db, mic_t, bang_wait, bang_db, last_bang; /* the room's level, and a bang waiting to be sure it is no breath */
    float notes[8];                                      /* when the last notifications came */
    int note_i;
    float sx, sy; /* where the finger went down */
    float glance_x, glance_y, glance_left; /* something on screen worth a look, e.g. a notification */
    void (*on_swipe)(int dir);
    int fx_shown;
    float spawn_t;
    float t, blink_t, next_blink, next_look, tgx, tgy, idle_roll, beat;
    float micro_x, micro_y, next_micro;
    bool touching, petting;
    float fx, fy, path, frame_ms;
    uint32_t last_tick, press_tick;
} s = {.bat_pct = -1, .intro = -1, .last_bang = -10}; /* the battery is unknown until the power chip says */

static float frand(float a, float b) { return a + (b - a) * (float)lv_rand(0, 10000) / 10000.0f; }
static void blink(void) { s.blink_t = 0; }
const char *pip_mood_name(int mood) { return moods[mood].name; }

int pip_find_mood(const char *name)
{
    for (int i = 0; i < PIP_MOOD_COUNT; i++)
        if (!strcmp(moods[i].name, name)) return i;
    return -1;
}

/* what Pip says for a mood, -1 = nothing */
static int mood_voice(int mood)
{
    switch (mood) {
    case PIP_OUCH: return VOICE_POKE;
    case PIP_HAPPY: case PIP_CONTENT: case PIP_CHEEKY: case PIP_YES: return VOICE_HAPPY;
    case PIP_SAD: case PIP_HURT: case PIP_DESPONDENT: case PIP_LONELY: case PIP_GLOOMY: return VOICE_SAD;
    case PIP_ANGRY: case PIP_ANNOYED: case PIP_FURIOUS: case PIP_RAGE: case PIP_BELLIGERENT: case PIP_INDIGNANT: return VOICE_GROWL;
    case PIP_SURPRISED: case PIP_SHOCKED: case PIP_AMAZED: case PIP_AFRAID: case PIP_DISMAYED: return VOICE_OH;
    case PIP_LOVE: return VOICE_LOVE;
    case PIP_NAUSEOUS: return VOICE_BURP;
    case PIP_ASLEEP: case PIP_SLEEPY: case PIP_TIRED: case PIP_EXHAUSTED: return VOICE_YAWN;
    case PIP_DIZZY: case PIP_STONED: return VOICE_DIZZY;
    case PIP_DULL: return VOICE_DERP;
    case PIP_CURIOUS: case PIP_PUZZLED: case PIP_SUSPICIOUS: return VOICE_HM;
    case PIP_SWEARING: return VOICE_CENSOR;
    case PIP_SKEPTICAL: case PIP_CYNICAL: case PIP_DISGUSTED: case PIP_SMUG: case PIP_NO: return VOICE_HMPH;
    }
    return -1;
}

#define FEAT(f) (!(s.feat_off >> (f) & 1))
void pip_feature(int f, bool on) { s.feat_off = on ? s.feat_off & ~(1u << f) : s.feat_off | 1u << f; }
bool pip_feature_on(int f) { return FEAT(f); }
void *pip_pet(void) { return &s.pet; }

static void say(int sound)
{
    if (sound < 0 || !FEAT(PIP_F_VOICE)) return;
    voice_say(sound);
    s.last_voice = s.t;
}

static void react(int mood, float secs, const char *name)
{
    bool fresh = mood != s.react_mood || name != s.react_name;
    s.react_mood = mood;
    s.react_left = secs;
    s.react_name = name;
    if (!fresh) return; /* petting refreshes the same reaction every frame */
    static const struct { const char *name; int sound; } said[] = {
        {"yum", VOICE_NOM}, {"charging", VOICE_NOM}, {"blown", VOICE_OH}, {"too full", VOICE_BURP}, {"dislikes you", VOICE_HMPH}, {"time's up", VOICE_ALARM},
        {"fed up", VOICE_GROWL}, {"pet", VOICE_PURR}, {"meet", VOICE_HM}, {"face down", VOICE_YAWN}, {"won", VOICE_HAPPY}, {"lost", VOICE_SAD},
    };
    for (unsigned i = 0; i < sizeof said / sizeof said[0]; i++)
        if (!strcmp(name, said[i].name)) { say(said[i].sound); return; }
    say(mood_voice(mood));
}

void pip_react(int mood, float secs, const char *name) { react(mood, secs, name); }
void pip_on_swipe(void (*cb)(int dir)) { s.on_swipe = cb; }

void pip_set_mood(int mood)
{
    s.mood = (mood % PIP_MOOD_COUNT + PIP_MOOD_COUNT) % PIP_MOOD_COUNT;
    s.auto_mood = false;
    say(mood_voice(s.mood)); /* picked by hand: he says it */
}

void pip_auto(bool on) { s.auto_mood = on; }

void pip_time(int64_t now, int tz_min)
{
    s.tz = tz_min;
    if (s.pet.version != PET_VERSION) pet_init(&s.pet, now, tz_min, lv_rand(1, 0x7FFFFFFF));
    if (FEAT(PIP_F_TIME)) pet_update(&s.pet, now, tz_min);
    else s.pet.last = now; /* time stands still for him: nothing drops while filming */
    bool bat = FEAT(PIP_F_BATTERY), charging = bat && s.bat_charging; /* what the mood tree reads of the battery */
    if (charging && !s.pet.charging) s.pet.charge_since = s.pet.last;
    s.pet.charging = charging, s.pet.bat = (int8_t)(bat ? s.bat_pct : -1);
    if (s.auto_mood) s.mood = pet_settle(&s.pet, tz_min);
}

/* the battery is his dinner: plugged in he cheers up and eats, a little every reading, until the charger goes */
void pip_shake_ok(bool ok) { s.no_shakes = !ok; }
void pip_wake_up(void) { s.intro = 0, s.open.x = 0, s.open.v = 0; }

void pip_battery(int pct, bool charging)
{
    if (charging && !s.bat_charging && s.bat_pct >= 0 && FEAT(PIP_F_BATTERY)) react(PIP_HAPPY, 4.0f, "charging");
    if (charging && FEAT(PIP_F_BATTERY)) s.pet.food = fminf(1, s.pet.food + 0.005f);
    s.bat_pct = pct, s.bat_charging = charging;
}

void pip_action(int act)
{
    int r = pet_event(&s.pet, act == PIP_ACT_FEED ? PET_FEED : PET_PLAY);
    if (r >= 0)
        react(r, 2.0f, act == PIP_ACT_PLAY ? (r == PIP_NO ? "worn out" : "play")
                       : r == PIP_NAUSEOUS ? "too full" : r == PIP_DISGUSTED ? "enough" : "yum");
}

void pip_game(int result)
{
    static const struct { int ev; const char *name; } g[] = {{PET_WIN, "won"}, {PET_LOSE, "lost"}, {PET_RECORD, "record"}};
    react(pet_event(&s.pet, g[result].ev), 2.5f, g[result].name);
}

void pip_phone(int what)
{
    static const struct { int mood; const char *name; } r[] = {
        {PIP_YES, "answered"}, {PIP_NO, "declined"}, {PIP_PUZZLED, "missed call"}, {PIP_YES, "paired"}};
    if (!s.pet.asleep) react(r[what].mood, 2.5f, r[what].name);
}

/* a pile of notifications at once dismays him; one by one they are just a glance */
static void light_up(void);
void pip_wake(void) { light_up(); } /* something to look at came: a notification, a call */

void pip_note(void)
{
    light_up();
    s.notes[s.note_i++ % 8] = s.t;
    int n = 0;
    for (int i = 0; i < 8; i++) n += s.notes[i] > 0 && s.t - s.notes[i] < pet_tune.flood_s;
    if (n < pet_tune.flood_n || s.pet.asleep) return;
    react(pet_event(&s.pet, PET_FLOOD), 3.0f, "flood");
    memset(s.notes, 0, sizeof s.notes);
}

/* lying face down the screen goes dark a second after he falls asleep; turned over, or a button, wakes it */
#define DARK_AFTER_S 1.0f
#define DARK_AGAIN_S 5.0f /* woken by a button while still face down: dark again after this */
bool pip_display_on(void) { return !s.dark; }

/* Power: left alone for a while (no touch, no button, not picked up, nothing from the phone) the screen dims, then goes
   dark and the face stops drawing - the face and the panel are most of the battery. A touch, a button, picking him up
   (turned more than 25 degrees from how he lay) or a notification wakes him. Apps that are there to be looked at
   (the badge, a QR code, the torch...) hold him awake while open. */
#define DIM_BEFORE_S 5.0f
static struct {
    int after;      /* seconds to sleep, 0 = never */
    float idle, lp[3], g[3];
    bool dozed, dim, hold;
} pw = {.after = 30};
void pip_power(int seconds) { pw.after = seconds < 0 ? 0 : seconds, pw.idle = 0; }
int pip_power_after(void) { return pw.after; }
void pip_hold_awake(bool on) { pw.hold = on; }
int pip_display_level(void) { return s.dark ? 0 : pw.dim ? 25 : 100; }

static void light_up(void)
{
    pw.idle = 0, pw.dozed = pw.dim = false;
    s.dark = false;
    s.dark_in = s.imu.face_down && FEAT(PIP_F_FACEDOWN) ? DARK_AGAIN_S : 0;
}

void pip_seen(void)
{
    light_up();
    int r = pet_event(&s.pet, PET_SEEN); /* back after hours alone: glad, or sulking if he felt left */
    if (r >= 0) react(r, 3.0f, r == PIP_HURT ? "sulks" : "glad you're back"), s.greet_t = 1.2f;
}

const void *pip_pet_state(size_t *size)
{
    *size = sizeof s.pet;
    return &s.pet;
}

bool pip_pet_restore(const void *data, size_t size)
{
    const pet_t *p = data;
    if (size != sizeof *p || p->version != PET_VERSION) return false;
    s.pet = *p;
    return true;
}

static void meet(void)
{
    react(PIP_CURIOUS, 1.4f, "meet");
    s.meet_pending = true;
}

static void poke(void)
{
    blink();
    if (s.pet.asleep) { /* woken up: grumpy unless he has slept enough */
        int r = pet_event(&s.pet, PET_WAKE);
        react(r, 2.5f, "woken up");
        return;
    }
    if (s.greet_t > 0) return; /* the touch that brought him back is not a poke */
    pet_event(&s.pet, PET_POKE);
    if ((s.annoy += 1) >= 2.5f) { /* about three pokes in a row; keep at it and he goes furious, red, swearing */
        int r = pet_event(&s.pet, PET_FED_UP);
        react(r, r == PIP_SWEARING ? 4.0f : 2.5f, "fed up");
        s.annoy = 0;
    } else {
        react(PIP_OUCH, 0.45f, "poke");
    }
}

void pip_meet(void) { meet(); }

void pip_button(int id)
{
    if (id == PIP_BTN_PWR) meet();
    else blink(); /* BOOT: free for now */
}

void pip_key(char k)
{
    static const char keys[] = "1234567890-=";
    for (int i = 0; i < (int)sizeof keys - 1 && i < PIP_MOOD_COUNT; i++)
        if (k == keys[i]) { pip_set_mood(i); s.react_mood = -1; }
    if (k == ' ') { pip_set_mood(s.mood + 1); s.react_mood = -1; }
    if (k == 'b') blink();
    if (k == 'm') meet();
}

void pip_imu(float ax, float ay, float az, float dt)
{
    int ev = imu_feed(&s.imu, ax, ay, az, dt);
    /* picked up? gravity, smoothed, against how he lay when he dozed off */
    float k = fminf(1, dt * 5);
    pw.lp[0] += (ax - pw.lp[0]) * k, pw.lp[1] += (ay - pw.lp[1]) * k, pw.lp[2] += (az - pw.lp[2]) * k;
    if (s.imu.energy > 0.35f) pw.idle = 0; /* handled, not just carried */
    if (pw.dozed) {
        float d = pw.lp[0] * pw.g[0] + pw.lp[1] * pw.g[1] + pw.lp[2] * pw.g[2];
        float n = sqrtf((pw.lp[0] * pw.lp[0] + pw.lp[1] * pw.lp[1] + pw.lp[2] * pw.lp[2]) * (pw.g[0] * pw.g[0] + pw.g[1] * pw.g[1] + pw.g[2] * pw.g[2]));
        if (n > 0.1f && d / n < 0.906f) light_up(); /* turned more than 25 degrees */
    }
    if (!FEAT(PIP_F_FACEDOWN)) ev &= ~(IMU_FACE_DOWN | IMU_FACE_UP);
    if (ev & IMU_FACE_DOWN) { react(PIP_ASLEEP, INFINITY, "face down"); pet_event(&s.pet, PET_SLEEP); s.dark_in = DARK_AFTER_S; }
    if (ev & IMU_FACE_UP) {
        s.dark = false, s.dark_in = 0;
        int r = pet_event(&s.pet, PET_WAKE);
        react(r >= 0 ? r : PIP_SURPRISED, 1.5f, "woke up");
        blink();
    }
    if ((ev & IMU_UPSIDE) && !s.pet.asleep) react(PIP_PUZZLED, INFINITY, "upside down"); /* the world turned over */
    if ((ev & IMU_UPRIGHT) && s.react_mood >= 0 && !strcmp(s.react_name, "upside down")) s.react_left = 0;
    if ((ev & IMU_FALL) && !s.pet.asleep) { pet_event(&s.pet, PET_FRIGHT); react(PIP_SHOCKED, 2.0f, "dropped"); }
    if ((ev & IMU_SHAKE) && !s.imu.face_down && !s.no_shakes && FEAT(PIP_F_SHAKE)) {
        pet_event(&s.pet, PET_SHAKE);
        if ((s.shakes += 1) >= 2.5f) { react(PIP_DULL, 40, "shaken silly"); s.shakes = 0; } /* about three in a row */
        else react(PIP_DIZZY, 3.0f, "dizzy");
    }
    if (ev & IMU_STEP) pet_event(&s.pet, PET_STEP);
}

void pip_get_info(pip_info_t *o)
{
    *o = (pip_info_t){moods[s.shown].name, s.react_mood >= 0 ? s.react_name : "", (int)s.pet.steps_today,
                      s.imu.tilt_x, s.imu.tilt_y, s.imu.energy, s.imu.face_down, s.frame_ms,
                      s.pet.food, s.pet.energy, s.pet.fun, s.pet.love, s.pet.walk, s.pet.day_mood,
                      s.pet.asleep, s.auto_mood, pet_hour(s.pet.last, s.tz)};
}

/* ---------- scene effects: particles and sprites drawn over the face ---------- */
typedef struct { lv_obj_t *o; float x, y, vx, vy, life, max; } part_t;
#define N_SWEAR 2
#define N_DROPS 28
static part_t swear[N_SWEAR], drops[N_DROPS];

static part_t *free_part(part_t *pool, int n)
{
    for (int i = 0; i < n; i++)
        if (pool[i].life <= 0) return &pool[i];
    return NULL;
}

static void launch(part_t *p, float x, float y, float vx, float vy, float life)
{
    *p = (part_t){p->o, x, y, vx, vy, life, life};
    lv_obj_remove_flag(p->o, LV_OBJ_FLAG_HIDDEN);
}

/* move and fade (no shrinking: a scaled object is drawn through a layer, slow on the board) */
static void animate(part_t *pool, int n, float dt)
{
    for (int i = 0; i < n; i++) {
        part_t *p = &pool[i];
        if (p->life <= 0) continue;
        if ((p->life -= dt) <= 0) { lv_obj_add_flag(p->o, LV_OBJ_FLAG_HIDDEN); continue; }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        float k = p->life / p->max, age = p->max - p->life;
        float opa = fminf(1, age / 0.12f) * fminf(1, k / 0.4f);
        lv_obj_set_style_opa(p->o, (lv_opa_t)(opa * 255), 0);
        lv_obj_set_pos(p->o, (int32_t)p->x - lv_obj_get_width(p->o) / 2, (int32_t)p->y - lv_obj_get_height(p->o) / 2);
    }
}

static lv_obj_t *sprite(lv_obj_t *parent, int w, int h, void (*draw)(uint32_t *, int, int))
{
    lv_draw_buf_t *b = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
    LV_ASSERT_MALLOC(b);
    draw((uint32_t *)b->data, w, h);
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, b);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    return img;
}

static void effects(int fx, float dt)
{
    if (fx != s.fx_shown) {
        lv_obj_set_flag(s.rain_cloud, LV_OBJ_FLAG_HIDDEN, fx != FX_RAIN);
        s.fx_shown = fx, s.spawn_t = 0;
    }
    s.spawn_t -= dt;
    part_t *p;
    if (fx == FX_SWEAR && s.spawn_t <= 0 && (p = free_part(swear, N_SWEAR))) {
        /* grawlix out of where a mouth would be, drifting up and away */
        static const char glyphs[] = "%$#@!&*";
        static const uint32_t colors[] = {0xFF4A3A, 0xFFD23A, 0xFFFFFF, 0xFF8A2A};
        char t[2] = {glyphs[lv_rand(0, sizeof glyphs - 2)], 0};
        lv_label_set_text(p->o, t);
        lv_obj_set_style_text_color(p->o, lv_color_hex(colors[lv_rand(0, 3)]), 0);
        launch(p, SCR / 2 + frand(-35, 35), 372, frand(-55, 55), frand(-130, -90), frand(1.2f, 1.7f));
        s.spawn_t = 5.0f; /* one curse at a time, with its beep */
        if (s.t - s.last_voice > 1) say(VOICE_CENSOR);
    }
    if (fx == FX_RAIN && s.spawn_t <= 0 && (p = free_part(drops, N_DROPS))) {
        float cx = lv_obj_get_x(s.rain_cloud);
        launch(p, cx + frand(30, 140), 92, -25, frand(300, 360), 1.0f);
        s.spawn_t = frand(0.03f, 0.08f);
    }
    animate(swear, N_SWEAR, dt);
    animate(drops, N_DROPS, dt);
    lv_obj_set_pos(s.rain_cloud, SCR / 2 - 85 + (int32_t)(sinf(s.t * 0.7f) * 10), 26); /* the cloud drifts a little */
}

static void step(float dt, const mood_def_t *m, bool dizzy)
{
    float slow = m->slow;
    for (int i = 0; i < 2; i++) {
        shape_t to = mood_shape(m);
        if (i) to.lid += m->lid_asym;
        const float *tf = (const float *)&to;
        for (int j = 0; j < SHAPE_N; j++) spring(&s.shape[i][j], tf[j], 220 * (1 - 0.6f * slow), 0.5f, dt);
    }
    spring(&s.open, 1 - m->closed, 30, 1.0f, dt);
    spring(&s.squash, 0, 300, 0.25f, dt);
    spring(&s.dizzy, dizzy, 20, 1.0f, dt);
    uint32_t tint = m->tint ? m->tint : 0xFFFFFF;
    for (int c = 0; c < 3; c++) spring(&s.tint[c], ((tint >> (16 - 8 * c)) & 255) / 255.0f, 20, 1.0f, dt);
    spring(&s.blush, m->blush, 20, 1.0f, dt);
    spring(&s.tremble, m->tremble, 20, 1.0f, dt);
    spring(&s.nod, m->nod, 20, 1.0f, dt);
    spring(&s.wag, m->wag, 20, 1.0f, dt);

    /* the eyes jump to the target, the head follows slowly and the eyes counter-rotate */
    float tx = s.tgx + m->gx + s.imu.tilt_x * 1.1f, ty = s.tgy + m->gy + s.imu.tilt_y * 0.9f; /* eyes roll downhill */
    tx = fmaxf(-1.2f, fminf(1.2f, tx));
    ty = fmaxf(-1.0f, fminf(1.0f, ty));
    float eye_k = 900 * (1 - 0.85f * slow), head_k = 45 * (1 - 0.7f * slow);
    float sway = s.t * 1.4f; /* nausea circles, respectful nods */
    spring(&s.look_x, tx * 0.9f + s.micro_x, eye_k, 0.65f, dt); /* micro-saccades move the pupils only */
    spring(&s.look_y, ty * 0.6f + s.micro_y, eye_k, 0.65f, dt);
    spring(&s.head_yaw, tx * 0.7f + m->sway_x * sinf(sway), head_k, 0.7f, dt);
    spring(&s.head_pitch, ty * 0.3f + m->sway_y * cosf(sway), head_k, 0.7f, dt);
    spring(&s.head_roll, m->head_roll + s.idle_roll, 30, 0.6f, dt);
}

static void power(float dt)
{
    if (lv_display_get_inactive_time(NULL) < 300) { /* a finger on the glass */
        if (pw.dozed) light_up();
        pw.idle = 0;
    }
    pw.idle += dt;
    if (pw.hold) pw.idle = 0;
    if (s.dark || pw.after <= 0) { pw.dim = false; return; }
    pw.dim = pw.idle > pw.after - fminf(DIM_BEFORE_S, pw.after * 0.5f);
    if (pw.idle > pw.after) {
        s.dark = true, pw.dozed = true, pw.dim = false;
        memcpy(pw.g, pw.lp, sizeof pw.g);
    }
}

static void update(float dt)
{
    power(dt);
    /* reactions */
    s.annoy = fmaxf(0, s.annoy - dt * 0.4f);
    s.shakes = fmaxf(0, s.shakes - dt / 20); /* three shakes within about a minute */
    if (s.petting && s.touching && (s.pet_recent -= dt) > 0) {
        s.pet_total += dt;
        if ((s.stroke_t += dt) > 0.5f) { s.stroke_t = 0; pet_event(&s.pet, PET_STROKE); }
        if (s.pet_total > 2.5f) react(PIP_LOVE, 2.5f, "love");
        else if (s.react_mood != PIP_LOVE) react(PIP_HAPPY, 1.0f, "pet");
        if (s.react_mood == PIP_HAPPY && !voice_busy()) say(VOICE_PURR); /* keeps purring while stroked */
    }
    /* the mic hears his own voice from the speaker: nothing it picks up then (or just after) is someone else's */
    static float hush;
    hush = voice_busy() ? 0 : hush + dt;
    bool ears = hush > 0.4f;
    if (!ears) s.bang_wait = 0;
    if (ears && mic_blowing() && FEAT(PIP_F_BLOW) && !s.pet.asleep) react(PIP_OUCH, 0.6f, "blown"); /* someone blows on him: he screws his eyes up */
    if (mic_on() && FEAT(PIP_F_NOISE)) { /* a bang: far over the room's level, all at once */
        float db = mic_db();
        if ((s.mic_t += dt) < 2) s.room_db = db; /* the meter settles first */
        if (s.bang_wait > 0) { /* held back a moment: a breath on the mic starts the same way */
            s.bang_db = fmaxf(s.bang_db, db);
            if ((s.bang_wait -= dt) <= 0 && !mic_blowing() && !s.pet.asleep) {
                pet_event(&s.pet, PET_FRIGHT);
                react(s.bang_db >= pet_tune.shock_db ? PIP_SHOCKED : PIP_AFRAID, 2.0f, "bang");
            }
        } else if (ears && s.mic_t >= 2 && db >= pet_tune.loud_db && db - s.room_db >= pet_tune.loud_jump && s.t - s.last_bang > 3 && !mic_blowing())
            s.bang_wait = 0.45f, s.bang_db = db, s.last_bang = s.t;
        s.room_db += (db - s.room_db) * (1 - expf(-dt / 4)); /* a loud room becomes the room */
    }
    s.greet_t -= dt;
    if (s.dark_in > 0 && (s.dark_in -= dt) <= 0) s.dark = true;
    if (s.react_mood >= 0 && (s.react_left -= dt) <= 0) {
        s.react_mood = -1;
        if (s.meet_pending) {
            s.meet_pending = false;
            static const char *const said[PIP_MOOD_COUNT] = {[PIP_LOVE] = "likes you", [PIP_SHY] = "shy", [PIP_SUSPICIOUS] = "wary",
                                                             [PIP_ANGRY] = "dislikes you", [PIP_BELLIGERENT] = "dislikes you"};
            int r = pet_meet(&s.pet, (int)lv_rand(0, 99));
            react(r, 3.0f, said[r]);
        }
    }
    int mood = s.react_mood >= 0 ? s.react_mood : s.mood;
    if (mood != s.shown) {
        /* his own mood swings get a comment now and then, not every time */
        if (s.react_mood < 0 && s.t - s.last_voice > 15 && lv_rand(0, 100) < 35) say(mood_voice(mood));
        s.shown = mood;
        s.next_look = 0;
        s.squash.v -= 3.0f; /* anticipation: squash first, the spring overshoots into a stretch */
    }
    const mood_def_t *m = &moods[mood];
    /* waking up after power-on: out of black, sleepy and lopsided, a shake, then off he goes */
    static mood_def_t wake;
    if (s.intro >= 0) {
        float t = s.intro += dt;
        wake = moods[PIP_SLEEPY];
        wake.slow = 0.8f, wake.blink = 0.01f, wake.gy = 0.3f;
        if (t < 0.8f) wake.closed = 1;                                        /* still dark */
        else if (t < 3.0f) wake.closed = 1 - 0.6f * fminf(1, (t - 0.8f) / 1.6f), wake.lid_asym = 0.32f; /* one eye opens more */
        else if (t < 3.8f) {                                                  /* a shake to wake up */
            wake.closed = 0.25f, wake.lid_asym = 0.1f, wake.tremble = 3;
        } else {
            s.intro = -1;
            react(PIP_CHEEKY, 3.0f, "awake");
        }
        if (t >= 0.8f && t - dt < 0.8f) say(VOICE_YAWN);
        if (t >= 3.0f && t - dt < 3.0f) s.squash.v -= 4, say(VOICE_DIZZY);
        if (s.intro >= 0) m = &wake;
    }
    bool awake = m->closed < 1 && s.intro < 0;
    float rate = or1(m->look_rate), amp = or1(m->look_amp);

    if (awake && (s.next_blink -= dt) <= 0) {
        blink();
        s.next_blink = lv_rand(0, 100) < 15 ? 0.3f : frand(1.8f, 5.0f) / or1(m->blink);
    }
    float shut = 0;
    if (s.blink_t >= 0) {
        const float close = 0.07f, open = 0.14f;
        shut = fmaxf(s.blink_t < close ? s.blink_t / close : 1.0f - (s.blink_t - close) / open, 0);
        if ((s.blink_t += dt) > close + open) s.blink_t = -1;
    }
    float wink = 0;
    if (awake && m->wink > 0 && (s.next_wink -= dt) <= 0) { s.wink_t = 0; s.next_wink = m->wink * frand(0.7f, 1.3f); }
    if (s.wink_t >= 0) {
        wink = fmaxf(0, s.wink_t < 0.08f ? s.wink_t / 0.08f : s.wink_t < 0.25f ? 1 : 1 - (s.wink_t - 0.25f) / 0.15f);
        if ((s.wink_t += dt) > 0.4f) s.wink_t = -1;
    }
    if (mood == PIP_LOVE && (s.beat -= dt) <= 0) { s.squash.v += 2.2f; s.beat = 0.7f; } /* heartbeat */

    if (s.touching && !s.petting) {
        s.tgx = (s.fx - SCR / 2) / 160.0f;
        s.tgy = (s.fy - SCR / 2) / 160.0f;
    } else if (s.glance_left > 0 && awake) {
        s.glance_left -= dt;
        s.tgx = (s.glance_x - SCR / 2) / 160.0f;
        s.tgy = (s.glance_y - SCR / 2) / 160.0f;
        if (s.glance_left <= 0) s.next_look = frand(0.3f, 0.8f);
    } else if (mood == PIP_DIZZY) {
        s.tgx = cosf(s.t * 6) * 0.6f;
        s.tgy = sinf(s.t * 6) * 0.4f;
        s.idle_roll = sinf(s.t * 2.5f) * 0.25f;
    } else if ((s.next_look -= dt) <= 0) {
        bool centre = lv_rand(0, 100) < 25 || !awake;
        float nx = centre ? 0 : (lv_rand(0, 1) ? 1 : -1) * frand(0.4f, 1.1f) * amp, ny = centre ? 0 : frand(-0.6f, 0.6f) * amp;
        if (awake && fabsf(nx - s.tgx) > 0.9f && lv_rand(0, 100) < 50) blink(); /* big gaze shifts come with a blink */
        s.tgx = nx;
        s.tgy = ny;
        s.idle_roll = lv_rand(0, 100) < 25 ? frand(-0.12f, 0.12f) : 0; /* curious head tilt now and then */
        s.next_look = frand(0.5f, 2.2f) / rate;
    }
    if (awake && rate >= 0.2f && (s.next_micro -= dt) <= 0) {
        s.micro_x = frand(-0.08f, 0.08f);
        s.micro_y = frand(-0.06f, 0.06f);
        s.next_micro = frand(0.15f, 0.5f);
    }
    s.tgx = fmaxf(-1.2f, fminf(1.2f, s.tgx));
    s.tgy = fmaxf(-1.0f, fminf(1.0f, s.tgy));

    for (float left = dt; left > 0; left -= 1.0f / 120) step(fminf(left, 1.0f / 120), m, mood == PIP_DIZZY && s.intro < 0);
    lv_obj_set_flag(s.canvas, LV_OBJ_FLAG_HIDDEN, s.intro >= 0 && s.intro < 0.8f); /* black until he stirs */
    if (s.intro >= 3.0f) { /* the shake: the head whips side to side, fading out */
        float k = 1 - (s.intro - 3.0f) / 0.8f;
        s.head_yaw.x = sinf(s.intro * 34) * 0.45f * k, s.head_roll.x = sinf(s.intro * 27) * 0.25f * k;
    }
    s.t += dt;

    float cx = SCR / 2, cy = SCR / 2 + sinf(s.t * (awake ? 1.6f : 0.9f)) * (awake ? 2.5f : 5.0f);
    float swing = sinf(s.t * 13); /* yes / no: the head swings about twice a second, the pupils stay on you */
    float hy = s.head_yaw.x + s.wag.x * swing, hp = s.head_pitch.x + s.nod.x * swing, hr = s.head_roll.x;
    float open = fmaxf(s.open.x, 0) * (1 - shut), sq = s.squash.x;
    float trem = fmaxf(s.tremble.x, 0), jx = frand(-1, 1) * trem, jy = frand(-1, 1) * trem;
    for (int i = 0; i < 2; i++) {
        float side = i ? 1.0f : -1.0f;
        shape_t cur;
        for (int j = 0; j < SHAPE_N; j++) ((float *)&cur)[j] = s.shape[i][j].x;
        const shape_t *sh = &cur;
        eye_t *e = &s.face.eye[i];
        float phi = hy + side * SPREAD;
        float k = EYE_SCALE * (1 + 0.35f * (cosf(phi) - 0.8525f)); /* cos(SPREAD): the far eye shrinks */
        float x0 = HEAD_R * sinf(phi), y0 = 240.0f * sinf(hp);

        if (i) k *= 1 + m->size_asym;
        e->w = sh->w * k * (1 + 0.12f * (1 - open)) * (1 - 0.5f * sq);
        e->h = sh->h * k * open * (1 + sq) * (i ? 1 - 0.95f * wink : 1);
        e->depth = sh->w * k * 0.5f; /* flatter discs foreshorten more when turned away */
        y0 += (sh->h * k - e->h) * 0.25f; /* closing settles toward the lower half */
        e->x = cx + x0 * cosf(hr) - y0 * sinf(hr) + jx + frand(-0.3f, 0.3f) * trem;
        e->y = cy + x0 * sinf(hr) + y0 * cosf(hr) + jy + frand(-0.3f, 0.3f) * trem;
        e->yaw = hy + side * SPREAD * 0.5f;
        e->pitch = hp;
        e->roll = hr - side * sh->roll;
        e->taper = sh->taper;
        e->lid = sh->lid;
        e->tilt = -side * sh->tilt;
        e->smile = sh->smile;
        /* dizzy: each pupil circles on its own, in opposite directions */
        float dz = s.dizzy.x * 0.45f, ph = s.t * 9 * side;
        e->look_x = fmaxf(-0.85f, fminf(0.85f, s.look_x.x - e->yaw + cosf(ph) * dz - side * m->cross));
        e->look_y = fmaxf(-0.7f, fminf(0.7f, s.look_y.x - hp + sinf(ph) * dz));
        e->pw = sh->pw;
        e->ph = sh->ph;
        e->square = sh->square;
        e->heart = sh->heart;
        e->glint = sh->glint;
        e->spiral = sh->spiral;
        e->veins = sh->veins;
        e->wet = sh->wet;
        e->wobble = 0.022f * fminf(e->w, e->h) * 0.5f;
        e->wob_t = s.t + i * 7.3f;

    }

    s.face.color = lv_color_to_u16(lv_color_make((uint8_t)(fminf(fmaxf(s.tint[0].x, 0), 1) * 255),
                                                 (uint8_t)(fminf(fmaxf(s.tint[1].x, 0), 1) * 255),
                                                 (uint8_t)(fminf(fmaxf(s.tint[2].x, 0), 1) * 255)));
    s.face.blush = fmaxf(s.blush.x, 0);
    effects(m->fx, dt);

    /* text above the eyes: swearing shakes, a question mark bobs */
    if (m->text != s.text_shown) {
        s.text_shown = m->text;
        lv_label_set_text(s.text, m->text ? m->text : "");
        lv_obj_set_style_text_color(s.text, lv_color_hex(m->text_color), 0);
    }
    if (m->text) {
        float bob = mood == PIP_SWEARING ? 0 : sinf(s.t * 3) * 6;
        lv_obj_align(s.text, LV_ALIGN_TOP_MID, (int)(jx * 2), 48 + (int)(bob + jy * 2));
    }

    /* keep the eyes inside the round screen: they move as a pair, so the one that hits the rim stops both */
    for (int pass = 0; pass < 3; pass++) {
        float mx = 0, my = 0, worst = 0;
        for (int i = 0; i < 2; i++) {
            eye_t *e = &s.face.eye[i];
            float dx = e->x - SCR / 2, dy = e->y - SCR / 2, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
            float ea = e->w * 0.5f * (1 + fabsf(e->taper)), eb = e->h * 0.5f;
            float over = d + sqrtf(ea * dx * ea * dx + eb * dy * eb * dy) / d - (SCR / 2 - 8);
            if (over > worst) { worst = over; mx = -dx / d * over; my = -dy / d * over; }
        }
        if (worst <= 0) break;
        for (int i = 0; i < 2; i++) s.face.eye[i].x += mx, s.face.eye[i].y += my;
    }
}

/* LVGL redraws where an eye or a cheek was and is, a box each: boxes far apart stay apart, so the black between
   the eyes is not copied to the screen again */
static void redraw(const face_rect_t *was, const face_rect_t *now)
{
    for (int i = 0; i < 4; i++) {
        face_rect_t a = was[i].x2 >= was[i].x1 ? was[i] : now[i], b = now[i].x2 >= now[i].x1 ? now[i] : a;
        if (a.x2 < a.x1) continue;
        lv_area_t r = {LV_MIN(a.x1, b.x1), LV_MIN(a.y1, b.y1), LV_MAX(a.x2, b.x2), LV_MAX(a.y2, b.y2)};
        lv_obj_invalidate_area(s.canvas, &r);
    }
}

/* how long frames take: smoothed, and the worst of each second with what was on then */
#define WORST_S 8
typedef struct { uint32_t sec; float ms; int mood; const char *reaction; } worst_t;
static worst_t worst[WORST_S];

/* ---------- developer mode: the wireframe and numbers under the skin, to show how he is made ---------- */
static struct {
    bool on;
    lv_obj_t *top, *bot;
    int frames;
    uint32_t since;
    float fps;
    char note[96];
} dev;
#define DEV_GREEN 0x34C759
#define DEV_PINK 0xFF2D95

/* The wireframe is plotted straight into the face's buffer, clipped to the boxes the renderer paints anyway (they are
   wiped with the next frame): no extra LVGL drawing, no extra area to refresh. */
static uint16_t *dev_px;
static face_rect_t dev_clip[4];
static uint16_t dev_ink(uint32_t rgb)
{
    uint16_t c = (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
    return s.face.swapped ? (uint16_t)(c << 8 | c >> 8) : c;
}

static void dev_dot(int x, int y, uint16_t c)
{
    for (int i = 0; i < 4; i++) {
        const face_rect_t *r = &dev_clip[i];
        if (x >= r->x1 && x <= r->x2 && y >= r->y1 && y <= r->y2) { dev_px[y * SCR + x] = c; return; }
    }
}

static void dev_line(float x1, float y1, float x2, float y2, uint16_t c, bool thick)
{
    int n = (int)fmaxf(fabsf(x2 - x1), fabsf(y2 - y1)) + 1;
    for (int k = 0; k <= n; k++) {
        int x = (int)(x1 + (x2 - x1) * k / n), y = (int)(y1 + (y2 - y1) * k / n);
        dev_dot(x, y, c);
        if (thick) dev_dot(x + 1, y, c), dev_dot(x, y + 1, c);
    }
}

static void dev_paint(uint16_t *px, const face_rect_t *clip)
{
    if (!dev.on) return;
    dev_px = px;
    memcpy(dev_clip, clip, sizeof dev_clip);
    uint16_t green = dev_ink(DEV_GREEN), white = dev_ink(0xFFFFFF), pink = dev_ink(DEV_PINK);
    for (int i = 0; i < 2; i++) {
        const eye_t *eye = &s.face.eye[i];
        float a = eye->w * 0.5f, b = eye->h * 0.5f, c = cosf(eye->roll), sn = sinf(eye->roll), px0 = 0, py0 = 0;
        for (int k = 0; k <= 32; k++) { /* the outline the shape springs toward */
            float t = k * 6.2831853f / 32, x = cosf(t) * a, y = sinf(t) * b;
            float X = eye->x + x * c - y * sn, Y = eye->y + x * sn + y * c;
            if (k) dev_line(px0, py0, X, Y, green, true);
            px0 = X, py0 = Y;
        }
        float ly = eye->y - b + eye->lid * eye->h; /* the lid */
        dev_line(eye->x - a, ly - eye->tilt * a, eye->x + a, ly + eye->tilt * a, green, true);
        float gx = eye->x + sinf(eye->look_x) * a, gy = eye->y + sinf(eye->look_y) * b; /* the gaze */
        dev_line(eye->x, eye->y, gx, gy, white, false);
        dev_line(gx - 8, gy, gx + 8, gy, white, false), dev_line(gx, gy - 8, gx, gy + 8, white, false);
    }
    for (int i = 0; i < 4; i++) { /* what the renderer repainted */
        const face_rect_t *r = &clip[i];
        if (r->x2 < r->x1) continue;
        dev_line(r->x1, r->y1, r->x2, r->y1, pink, false), dev_line(r->x2, r->y1, r->x2, r->y2, pink, false);
        dev_line(r->x2, r->y2, r->x1, r->y2, pink, false), dev_line(r->x1, r->y2, r->x1, r->y1, pink, false);
    }
}

static void dev_text(void)
{
    static const char *const why[] = {"сон", "карточка дня", "тело", "послевкусие", "нужда", "ночь", "занят", "палитра"};
    pet_why_t w;
    pet_mood_why(&s.pet, s.tz, &w);
    char t[160], b[200];
    snprintf(t, sizeof t, "DEV MODE\n%.0f к/с  кадр %.0f мс\n%s%s%s  <- %s", dev.fps, s.frame_ms, moods[s.shown].name,
             s.react_mood >= 0 ? " / " : "", s.react_mood >= 0 ? s.react_name : "", w.kind >= 0 && w.kind < 8 ? why[w.kind] : "?");
    snprintf(b, sizeof b, "наклон %+.1f %+.1f\nтряска %.1f  мик %.0f дБ  бат %d%%%s%s", s.imu.tilt_x, s.imu.tilt_y, s.imu.energy,
             mic_on() ? mic_db() : 0.0f, s.bat_pct, dev.note[0] ? "\n" : "", dev.note);
    lv_label_set_text(dev.top, t);
    lv_label_set_text(dev.bot, b);
}

static void dev_frame(void) /* each shown frame: count it, and redraw the wireframe where it was and is */
{
    if (!dev.on) return;
    dev.frames++;
    if (lv_tick_elaps(dev.since) >= 500) {
        dev.fps = dev.frames * 1000.0f / lv_tick_elaps(dev.since), dev.frames = 0, dev.since = lv_tick_get();
        dev_text();
    }
}

void pip_dev(bool on)
{
    dev.on = on;
    lv_obj_set_flag(dev.top, LV_OBJ_FLAG_HIDDEN, !on);
    lv_obj_set_flag(dev.bot, LV_OBJ_FLAG_HIDDEN, !on);
    dev.since = lv_tick_get(), dev.frames = 0;
    if (on) dev_text();
}
bool pip_dev_on(void) { return dev.on; }
void pip_dev_note(const char *line) { snprintf(dev.note, sizeof dev.note, "%s", line); }

static void dev_create(lv_obj_t *parent)
{
    for (int i = 0; i < 2; i++) {
        lv_obj_t *t = lv_label_create(parent);
        lv_obj_set_style_text_font(t, th->tiny, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(DEV_GREEN), 0);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(t, 330);
        lv_obj_align(t, i ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_TOP_MID, 0, i ? -18 : 26);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
        if (i) dev.bot = t;
        else dev.top = t;
    }
    pip_dev(false);
}

static void frame_took(float ms)
{
    s.frame_ms += (ms - s.frame_ms) * 0.1f;
    dev_frame();
    uint32_t sec = lv_tick_get() / 1000;
    if (worst[sec % WORST_S].sec != sec || ms > worst[sec % WORST_S].ms)
        worst[sec % WORST_S] = (worst_t){sec, ms, s.shown, s.react_mood >= 0 ? s.react_name : ""};
}

float pip_frame_worst(int secs, const char **mood, const char **reaction)
{
    uint32_t now = lv_tick_get() / 1000;
    int k = -1;
    for (int i = 0; i < WORST_S; i++)
        if (now - worst[i].sec < (uint32_t)secs && worst[i].ms > 0 && (k < 0 || worst[i].ms > worst[k].ms)) k = i;
    if (mood) *mood = k < 0 ? "" : moods[worst[k].mood].name;
    if (reaction) *reaction = k < 0 ? "" : worst[k].reaction;
    return k < 0 ? 0 : worst[k].ms;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_elaps(s.last_tick);
    s.last_tick = lv_tick_get();
    update(fminf(el / 1000.0f, 0.1f));
    if (s.dark || lv_obj_get_screen(s.canvas) != lv_screen_active()) { /* dark, or an app is open: keep living, skip drawing */
        s.last_swap = lv_tick_get(); /* and the time away is no slow frame */
        return;
    }
    if (face_start) { /* the firmware draws the next frame in the background while LVGL shows this one */
        if (face_busy) return;
        if (s.pending) { /* it is ready: swap the canvases and redraw where the eyes were and are */
            uint8_t *d = s.buf->data;
            s.buf->data = s.back->data, s.back->data = d;
            dev_paint((uint16_t *)s.buf->data, s.rface.drawn);
            redraw(s.front_rects, s.rface.drawn);
            memcpy(s.back_rects, s.front_rects, sizeof s.back_rects);
            memcpy(s.front_rects, s.rface.drawn, sizeof s.front_rects);
            s.pending = false;
            frame_took((float)lv_tick_elaps(s.last_swap));
            s.last_swap = lv_tick_get();
        }
        s.rface = s.face; /* this moment's eyes, drawn over what the back canvas held two frames ago */
        memcpy(s.rface.drawn, s.back_rects, sizeof s.rface.drawn);
        s.pending = true;
        face_start(&s.rface, (uint16_t *)s.back->data, SCR, SCR);
        return;
    }
    face_rect_t was[4];
    memcpy(was, s.face.drawn, sizeof was);
    face_draw(&s.face, (uint16_t *)s.buf->data, SCR, SCR);
    dev_paint((uint16_t *)s.buf->data, s.face.drawn);
    redraw(was, s.face.drawn);
    frame_took((float)lv_tick_elaps(s.last_tick));
}

/* tap = poke, press and stroke = pet, press and hold = Pip looks at the finger */
static void touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        float d = code == LV_EVENT_PRESSED ? 0 : hypotf(p.x - s.fx, p.y - s.fy);
        s.fx = p.x;
        s.fy = p.y;
        s.path += d;
        if (d > 1.5f) s.pet_recent = 0.3f;
        if (lv_tick_elaps(s.press_tick) > 450 && s.path > 80) s.petting = true; /* quicker is a swipe */
    }
    if (code == LV_EVENT_PRESSED) {
        pip_seen();
        s.sx = s.fx, s.sy = s.fy;
        s.press_tick = lv_tick_get();
        s.touching = true;
        s.petting = false;
        s.path = s.pet_total = 0;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s.touching = false;
        s.next_look = frand(0.4f, 1.0f);
        float dx = s.fx - s.sx, dy = s.fy - s.sy;
        uint32_t ms = lv_tick_elaps(s.press_tick);
        /* a quick one-way flick is a swipe to the apps; slow back-and-forth strokes stay petting */
        bool swipe = ms < 450 && fabsf(dx) > 110 && fabsf(dy) < fabsf(dx) * 0.6f && s.path < fabsf(dx) * 1.4f;
        if (code == LV_EVENT_RELEASED && swipe && s.on_swipe) s.on_swipe(dx < 0 ? -1 : 1);
        else if (code == LV_EVENT_RELEASED && ms < TAP_MS && s.path < 25) poke();
    }
}

void pip_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

    /* the face is drawn in the screen's own byte order (the board's panel takes RGB565 swapped): LVGL then copies it
       to the frame as it is, without a pass swapping every pixel */
    s.face.swapped = lv_display_get_color_format(NULL) == LV_COLOR_FORMAT_RGB565_SWAPPED;
    lv_color_format_t cf = s.face.swapped ? LV_COLOR_FORMAT_RGB565_SWAPPED : LV_COLOR_FORMAT_RGB565;
    s.buf = lv_draw_buf_create(SCR, SCR, cf, 0);
    LV_ASSERT_MALLOC(s.buf);
    lv_draw_buf_clear(s.buf, NULL);
    if (face_start) {
        s.back = lv_draw_buf_create(SCR, SCR, cf, 0);
        LV_ASSERT_MALLOC(s.back);
        lv_draw_buf_clear(s.back, NULL);
        for (int i = 0; i < 4; i++) s.front_rects[i] = s.back_rects[i] = (face_rect_t){0, 0, -1, -1};
    }
    s.canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(s.canvas, s.buf);
    lv_obj_add_flag(s.canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s.canvas, touch_cb, LV_EVENT_PRESS_LOST, NULL);
    dev_create(parent); /* over the canvas */

    s.face.color = 0xFFFF;
    s.face.pupil_color = 0x18C3; /* #1C1C1C: a touch above black so a pupil at the rim still reads on AMOLED */
    s.face.heart_color = 0xFA2F; /* #FF4479 */
    s.face.shading = 0; /* flat whites: the soft shadow crescent cost a third of the ESP32 frame */
    s.face.blush_color = 0xFBD4; /* #FF7AA0 */
    shape_t start = mood_shape(&moods[0]);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < SHAPE_N; j++) s.shape[i][j].x = ((const float *)&start)[j];
    for (int c = 0; c < 3; c++) s.tint[c].x = 1;

    s.face.vein_color = 0xD9A7;  /* #D8343A */
    s.face.water_color = 0xAEDF; /* #A8D8FF */
    s.wink_t = -1;

    s.rain_cloud = sprite(parent, 170, 80, doodle_rain_cloud);
    for (int i = 0; i < N_DROPS; i++) {
        lv_obj_t *d = drops[i].o = lv_obj_create(parent);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 3, 16);
        lv_obj_set_style_radius(d, 2, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(0x9CC4FF), 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < N_SWEAR; i++) {
        lv_obj_t *l = swear[i].o = lv_label_create(parent);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
        lv_obj_set_size(l, 60, 58);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }

    s.text = lv_label_create(parent);
    lv_obj_set_style_text_font(s.text, &lv_font_montserrat_48, 0);
    lv_label_set_text(s.text, "");
    s.open.x = 1;
    s.react_mood = -1;
    s.auto_mood = true;
    s.blink_t = -1;
    s.next_blink = 2;
    s.last_tick = lv_tick_get();
    lv_timer_create(tick_cb, face_start ? 4 : 16, NULL); /* in the background mode a finished frame is picked up quickly */
}

void pip_glance(float x, float y, float secs)
{
    s.glance_x = x, s.glance_y = y, s.glance_left = secs;
}

void pip_pet_rebase(int64_t now)
{
    s.pet.last = now;
}
