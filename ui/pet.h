/* Pip's needs and day: the tamagotchi part. Pure C, no LVGL; the whole state is one POD struct to persist. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PET_VERSION 3

typedef struct {
    uint32_t version;
    int64_t last;     /* unix time this state is valid for */
    int64_t up_until; /* woken up: no dozing off again before this */
    float food;       /* 1 full .. 0 starving */
    float energy;     /* 1 rested .. 0 exhausted */
    float fun;        /* 1 entertained .. 0 bored */
    float love;       /* attention: 1 cuddled .. 0 lonely */
    float walk;       /* 1 just walked .. 0 cooped up */
    float day_mood;   /* -1..1, rolled every morning (day_base), wandering a little hour by hour */
    float annoy;      /* recent pokes, fades in a couple of minutes */
    int32_t day;      /* local day number day_mood belongs to */
    int32_t steps_today;
    uint32_t rng;
    bool asleep;
    bool bedded;              /* put to bed (face down): no waking up on his own until picked up */
    /* the mood's memory */
    bool charging;            /* the senses the tree reads, kept up to date by pip.c */
    int8_t bat;               /* %, -1 unknown */
    int8_t after_mood;        /* an aftertaste of something that happened, until after_until (-1: none) */
    int8_t after_why;         /* PET_AFTER_* */
    int8_t rare_mood;         /* today's rare card, from rare_at for pet_tune.rare_min minutes (-1: none today) */
    int8_t pool;              /* the palette shown last: a line must be crossed by pet_tune.hyst to leave it */
    int8_t hold;              /* what has held since hold_since: a battery state or the worst urge (pet_why_t.hold) */
    int8_t cur, prev;         /* the base mood in 5-minute slot `slot`, and in the slot before: no palette repeats */
    int32_t slot;
    float day_base;
    float stride;             /* recent steps, fading in half a minute: walking now */
    float unseen;             /* hours awake since anyone touched him */
    int64_t after_until, rare_at, hold_since, charge_since, woke_at, stuffed_at;
} pet_t;
_Static_assert(sizeof(pet_t) <= 256, "the firmware reads it back into a 256-byte buffer (main.c pet_load)");

enum { PET_FEED, PET_PLAY, PET_STROKE, PET_POKE, PET_STEP, PET_SHAKE, PET_WAKE, PET_SLEEP,
       PET_SEEN,     /* someone touched him or pressed a button: after hours alone he greets or sulks */
       PET_WIN,      /* he beat you at a game */
       PET_LOSE,     /* you beat him */
       PET_RECORD,   /* a new best score with him */
       PET_FRIGHT,   /* a bang or a drop: afraid for a while */
       PET_FLOOD,    /* a pile of notifications at once */
       PET_FED_UP }; /* poked once too often: the face for how much he has had */
/* What an aftertaste came from */
enum { PET_AFTER_WIN, PET_AFTER_LOSE, PET_AFTER_RECORD, PET_AFTER_FRIGHT, PET_AFTER_FLOOD, PET_AFTER_SULK, PET_AFTER_MISSED,
       PET_AFTER_FED_UP };

void pet_init(pet_t *p, int64_t now, int tz_min, uint32_t seed);
/* Advance to `now`, catching up any gap (minutes or days off) in small steps, sleep included. */
void pet_update(pet_t *p, int64_t now, int tz_min);
/* Returns a mood to react with (PIP_*), or -1. */
int pet_event(pet_t *p, int ev);
/* Who he thinks you are when handed over: roll is 0..99. */
int pet_meet(const pet_t *p, int roll);
/* The base mood for the current needs, day mood and hour. Changes every few minutes for variety. */
int pet_mood(const pet_t *p, int tz_min);
/* The same, saying why. The tree, first match wins:
     0 asleep
     1 a rare card (which: 0 stoned, 1 pinkeye; value: minutes left)
     2 the body (which: PET_BODY_*; value: battery % or how many needs are gone)
     3 an aftertaste (which: PET_AFTER_*; value: seconds left)
     4 a need under its line (which: the urge, +8 when its other face shows after a long spell; value: how far under, 0..1)
     5 a tired night (value: energy)
     6 a situation (which: PET_SITU_*)
     7 the day's palette (which: 0 great .. 3 low, +4 in the evening; value: the valence)
   A battery state or a need that has held for pet_tune.stale minutes stops hogging the face: the battery gives every
   other 5 minutes to the rest of the tree, a need turns face, other face, the rest. hold says which one is held
   (kind * 16 + which, -1 none), whether or not it shows right now. */
typedef struct { int kind, which; float value; int hold; } pet_why_t;
enum { PET_WHY_ASLEEP, PET_WHY_RARE, PET_WHY_BODY, PET_WHY_AFTER, PET_WHY_URGE, PET_WHY_NIGHT, PET_WHY_SITU, PET_WHY_POOL };
enum { PET_BODY_EMPTY, PET_BODY_SICK, PET_BODY_LOWBAT, PET_BODY_NERVOUS };
enum { PET_SITU_WAKING, PET_SITU_WALK, PET_SITU_CHARGE };
int pet_mood_why(const pet_t *p, int tz_min, pet_why_t *why);
/* pet_mood, remembering what it showed (the palette for hysteresis, how long an urge has held). Call it often. */
int pet_settle(pet_t *p, int tz_min);
/* A rare card now (mood < 0: one of the two at random), for the debug page. */
void pet_rare_now(pet_t *p, int mood);

/* The knobs: each urge is a need (0 food, 1 energy, 2 fun, 3 love, 4 walk) under a line giving a mood, and another
   mood it alternates with when it has held for `stale` minutes; the rest are named in pet_knobs[]. */
#define PET_URGES 8
typedef struct { int need; float line; int mood, alt; } pet_urge_t;
typedef struct {
    pet_urge_t urges[PET_URGES];
    float great, good, meh, hyst;       /* palettes by valence; how far past a line it must go to switch */
    float evening_h;                    /* from this hour the palettes calm down */
    float stale;                        /* min: an urge face this old alternates with its other face */
    float drift;                        /* how far the day's mood wanders each hour */
    float rare_chance, rare_min;        /* a rare card: the chance a day brings one, minutes it lasts */
    float sick_line;                    /* two needs under it at once: sick */
    float bat_low, bat_tired, bat_empty; /* %: nervous, tired, sick */
    float waking_min, walk_now;         /* sleepy after waking up; steps lately that make a walk */
    float away_h, sulk_love;            /* back after hours alone: sulks if love is under sulk_love, glad otherwise */
    float after_win, after_lose, after_record, after_fright, after_flood, after_sulk, after_missed, after_fed_up; /* s */
    float loud_db, loud_jump, shock_db; /* a bang: this loud, this much over the room; shocked over shock_db */
    float flood_n, flood_s;             /* notifications: this many within this many seconds */
    float furious, rage, swear;         /* annoyance (pokes) for each step up from angry when fed up */
    float meet_like, meet_shy, meet_wary, meet_rude; /* meeting: % chance to like at valence 0 (±35 at ±1); the share of
                                                        likes that come out shy, of dislikes that stay wary; annoyed
                                                        this much a dislike is belligerent */
    float full_line, disgust_s;         /* fed over full_line: nauseous, and again within disgust_s: disgusted */
    float play_tired, play_bored;       /* play: refuses under this energy, can't wait under this fun */
    float rewake_min;                   /* woken again this soon after the last time: indignant */
} pet_tune_t;
extern pet_tune_t pet_tune;
typedef struct { const char *name; float *v; float min, max; } pet_knob_t;
extern const pet_knob_t pet_knobs[];
extern const int pet_knob_count;
float *pet_knob(const char *name); /* NULL if unknown */
float pet_valence(const pet_t *p); /* -1 miserable .. 1 delighted */
int pet_hour(int64_t now, int tz_min);
