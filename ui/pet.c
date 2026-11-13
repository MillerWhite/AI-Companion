#include "pet.h"
#include "moods.h"
#include <math.h>
#include <string.h>

/* Per hour. Awake: hungry in ~15 h, tired by midnight, bored or lonely in ~12 h without play or cuddles. */
#define FOOD_AWAKE 0.06f
#define FOOD_ASLEEP 0.03f
#define ENERGY_AWAKE 0.06f
#define ENERGY_ASLEEP 0.18f
#define FUN_AWAKE 0.08f
#define LOVE_AWAKE 0.07f
#define LOVE_ASLEEP 0.02f
#define WALK_AWAKE 0.06f
#define WALK_ASLEEP 0.02f
#define WALK_PER_STEP 0.0005f /* 1000 steps = half a walk */
#define SLOT 300              /* s: a palette mood holds this long */

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float clamp1(float v) { return v < -1 ? -1 : v > 1 ? 1 : v; }
static int64_t local(int64_t now, int tz_min) { return now + (int64_t)tz_min * 60; }
int pet_hour(int64_t now, int tz_min) { return (int)((local(now, tz_min) / 3600) % 24); }
static int32_t pet_day(int64_t now, int tz_min) { return (int32_t)(local(now, tz_min) / 86400); }

static uint32_t next(uint32_t *s) /* xorshift32: deterministic, and saved with the pet */
{
    *s ^= *s << 13, *s ^= *s >> 17, *s ^= *s << 5;
    return *s;
}
static float rnd(uint32_t *s, float a, float b) { return a + (b - a) * (next(s) % 10000) / 10000.0f; }

pet_tune_t pet_tune = {
    .urges = {{0, .10f, PIP_ANGRY, PIP_FURIOUS},      {0, .22f, PIP_ANNOYED, PIP_BELLIGERENT}, /* hangry */
              {1, .12f, PIP_EXHAUSTED, PIP_DULL},     {1, .25f, PIP_TIRED, PIP_SLEEPY},
              {3, .10f, PIP_LONELY, PIP_DESPONDENT},  {3, .22f, PIP_HURT, PIP_SAD},
              {2, .12f, PIP_BORED, PIP_CYNICAL},      {4, .10f, PIP_GLOOMY, PIP_NERVOUS}}, /* cooped up, restless */
    .great = 0.55f, .good = 0.25f, .meh = -0.1f, .hyst = 0.05f, .evening_h = 20,
    .stale = 30, .drift = 0.08f,
    .rare_chance = 1 / 30.0f, .rare_min = 20,
    .sick_line = 0.08f,
    .bat_low = 20, .bat_tired = 10, .bat_empty = 4,
    .waking_min = 15, .walk_now = 20,
    .away_h = 4, .sulk_love = 0.35f,
    .after_win = 180, .after_lose = 120, .after_record = 120, .after_fright = 30, .after_flood = 60, .after_sulk = 180,
    .after_missed = 60, .after_fed_up = 60,
    .loud_db = 75, .loud_jump = 20, .shock_db = 88,
    .flood_n = 3, .flood_s = 10,
    .furious = 5, .rage = 8, .swear = 11,
    .meet_like = 50, .meet_shy = 0.3f, .meet_wary = 0.5f, .meet_rude = 2,
    .full_line = 0.85f, .disgust_s = 120, .play_tired = 0.15f, .play_bored = 0.3f,
    .rewake_min = 5,
};

#define K(n, lo, hi) {#n, &pet_tune.n, lo, hi}
const pet_knob_t pet_knobs[] = {
    K(great, -1, 1), K(good, -1, 1), K(meh, -1, 1), K(hyst, 0, 0.3f), K(evening_h, 15, 23), K(stale, 5, 180), K(drift, 0, 0.5f),
    K(rare_chance, 0, 1), K(rare_min, 1, 120), K(sick_line, 0, 0.3f),
    K(bat_low, 0, 50), K(bat_tired, 0, 30), K(bat_empty, 0, 15),
    K(waking_min, 0, 60), K(walk_now, 3, 60), K(away_h, 0.5f, 12), K(sulk_love, 0, 1),
    K(after_win, 0, 900), K(after_lose, 0, 900), K(after_record, 0, 900), K(after_fright, 0, 300), K(after_flood, 0, 300),
    K(after_sulk, 0, 900), K(after_missed, 0, 300), K(after_fed_up, 0, 300),
    K(loud_db, 50, 110), K(loud_jump, 5, 50), K(shock_db, 60, 120), K(flood_n, 2, 8), K(flood_s, 5, 120),
    K(furious, 2, 20), K(rage, 3, 25), K(swear, 4, 30), K(meet_like, -50, 150), K(meet_shy, 0, 1), K(meet_wary, 0, 1), K(meet_rude, 0, 6),
    K(full_line, 0.5f, 1), K(disgust_s, 10, 600), K(play_tired, 0, 0.5f), K(play_bored, 0, 1), K(rewake_min, 0, 30),
};
#undef K
const int pet_knob_count = sizeof pet_knobs / sizeof pet_knobs[0];

float *pet_knob(const char *name)
{
    for (int i = 0; i < pet_knob_count; i++)
        if (!strcmp(pet_knobs[i].name, name)) return pet_knobs[i].v;
    return NULL;
}

static void new_day(pet_t *p, int32_t day, int tz_min)
{
    /* yesterday's walks set the tone; the rest is just how the day goes */
    float walked = p->steps_today >= 4000 ? 0.2f : p->steps_today < 500 ? -0.15f : 0;
    p->day_base = p->day_mood = clamp1(rnd(&p->rng, -0.35f, 0.35f) + walked);
    p->steps_today = 0;
    p->day = day;
    /* now and then a day deals a rare card: twenty odd minutes some time in the afternoon */
    p->rare_mood = -1, p->rare_at = 0;
    if (rnd(&p->rng, 0, 1) < pet_tune.rare_chance) {
        p->rare_mood = next(&p->rng) % 2 ? PIP_PINKEYE : PIP_STONED;
        p->rare_at = (int64_t)day * 86400 - (int64_t)tz_min * 60 + (int64_t)(rnd(&p->rng, 11, 20) * 3600);
    }
}

void pet_init(pet_t *p, int64_t now, int tz_min, uint32_t seed)
{
    memset(p, 0, sizeof *p);
    p->version = PET_VERSION;
    p->last = now;
    p->food = p->energy = p->fun = p->love = p->walk = 0.9f;
    p->rng = seed ? seed : 0x9E3779B9u;
    p->steps_today = 1000; /* no grudge on the first day */
    p->bat = p->after_mood = p->pool = p->hold = p->cur = p->prev = -1;
    new_day(p, pet_day(now, tz_min), tz_min);
}

static void advance(pet_t *p, int64_t t, int dt, int tz_min)
{
    float h = dt / 3600.0f;
    int hour = pet_hour(t, tz_min);
    int32_t day = pet_day(t, tz_min);
    if (day != p->day) new_day(p, day, tz_min);
    else if (local(t, tz_min) / 3600 != local(t + dt, tz_min) / 3600) /* on the hour the day's mood wanders, tethered to the morning's */
        p->day_mood = clamp1(p->day_mood + rnd(&p->rng, -pet_tune.drift, pet_tune.drift) + (p->day_base - p->day_mood) * 0.3f);
    if (p->asleep) {
        p->food -= FOOD_ASLEEP * h, p->energy += ENERGY_ASLEEP * h, p->love -= LOVE_ASLEEP * h, p->walk -= WALK_ASLEEP * h;
        bool day_time = hour >= 7 && hour < 23;
        if (!p->bedded && ((p->energy >= 0.95f && day_time) || (hour >= 9 && hour < 21 && p->energy >= 0.6f))) p->asleep = false, p->woke_at = t + dt;
    } else {
        p->food -= FOOD_AWAKE * h, p->energy -= ENERGY_AWAKE * h, p->fun -= FUN_AWAKE * h;
        p->love -= LOVE_AWAKE * h, p->walk -= WALK_AWAKE * h;
        p->unseen += h;
        bool night = hour >= 23 || hour < 7;
        if (((night && p->energy < 0.6f) || p->energy < 0.06f) && t >= p->up_until) p->asleep = true, p->after_mood = -1;
    }
    p->annoy = fmaxf(0, p->annoy - dt / 90.0f);
    p->stride *= expf(-dt / 30.0f);
    if (p->after_mood >= 0 && t + dt >= p->after_until) p->after_mood = -1;
    p->food = clamp01(p->food), p->energy = clamp01(p->energy), p->fun = clamp01(p->fun);
    p->love = clamp01(p->love), p->walk = clamp01(p->walk);
}

void pet_update(pet_t *p, int64_t now, int tz_min)
{
    if (now - p->last > 30LL * 86400) p->last = now - 30LL * 86400; /* away a month: that's enough misery */
    while (p->last < now) {
        int step = (int)fminf((float)(now - p->last), now - p->last > 3600 ? 600 : 60);
        advance(p, p->last, step, tz_min);
        p->last += step;
    }
}

static int after(pet_t *p, int mood, float secs, int why)
{
    if (secs > 0) p->after_mood = (int8_t)mood, p->after_why = (int8_t)why, p->after_until = p->last + (int64_t)secs;
    return mood;
}

static void played(pet_t *p)
{
    p->fun = clamp01(p->fun + 0.35f), p->love = clamp01(p->love + 0.05f), p->energy = clamp01(p->energy - 0.04f);
}

int pet_event(pet_t *p, int ev)
{
    const pet_tune_t *k = &pet_tune;
    switch (ev) {
    case PET_FEED: {
        bool stuffed = p->food > k->full_line, again = stuffed && p->stuffed_at && p->last - p->stuffed_at < (int64_t)k->disgust_s;
        p->food = clamp01(p->food + 0.4f), p->fun = clamp01(p->fun + 0.05f);
        if (stuffed) p->stuffed_at = p->last;
        return again ? PIP_DISGUSTED : stuffed ? PIP_NAUSEOUS : PIP_CONTENT;
    }
    case PET_PLAY: {
        if (p->energy < k->play_tired) return PIP_NO; /* too tired to play */
        bool bored = p->fun < k->play_bored;
        played(p);
        return bored ? PIP_YES : PIP_HAPPY;
    }
    case PET_STROKE:
        p->love = clamp01(p->love + 0.05f), p->fun = clamp01(p->fun + 0.01f);
        if (p->after_mood >= 0 && p->after_why == PET_AFTER_SULK) p->after_mood = -1; /* made up */
        return -1;
    case PET_POKE: p->annoy += 1, p->love = clamp01(p->love - 0.01f); return -1;
    case PET_STEP:
        p->steps_today++, p->walk = clamp01(p->walk + WALK_PER_STEP), p->fun = clamp01(p->fun + 0.00005f);
        p->stride = fminf(p->stride + 1, 100);
        return -1;
    case PET_SHAKE: p->annoy += 0.5f, p->fun = clamp01(p->fun + 0.03f); return -1;
    case PET_WAKE: {
        if (!p->asleep) return -1;
        bool again = p->woke_at && p->last - p->woke_at < (int64_t)(k->rewake_min * 60);
        p->asleep = p->bedded = false, p->annoy += 1, p->up_until = p->last + 10 * 60, p->woke_at = p->last;
        return again ? PIP_INDIGNANT : p->energy < 0.5f ? PIP_ANNOYED : PIP_SURPRISED;
    }
    case PET_SLEEP: p->asleep = p->bedded = true, p->after_mood = -1; return -1;
    case PET_SEEN: {
        float away = p->unseen;
        p->unseen = 0;
        if (p->asleep || away < k->away_h) return -1;
        return p->love < k->sulk_love ? after(p, PIP_HURT, k->after_sulk, PET_AFTER_SULK) /* left alone: sulks */
                                      : (after(p, PIP_HAPPY, k->after_missed, PET_AFTER_MISSED), PIP_LOVE); /* missed you */
    }
    case PET_WIN: played(p); return after(p, PIP_SMUG, k->after_win, PET_AFTER_WIN);
    case PET_LOSE: played(p); after(p, PIP_INDIGNANT, k->after_lose, PET_AFTER_LOSE); return PIP_NO; /* "no way!", then sulks */
    case PET_RECORD: return after(p, PIP_AMAZED, k->after_record, PET_AFTER_RECORD);
    case PET_FRIGHT: after(p, PIP_AFRAID, k->after_fright, PET_AFTER_FRIGHT); return -1;
    case PET_FLOOD: return after(p, PIP_DISMAYED, k->after_flood, PET_AFTER_FLOOD);
    case PET_FED_UP: {
        int m = p->annoy < k->furious ? PIP_ANGRY : p->annoy < k->rage ? PIP_FURIOUS : p->annoy < k->swear ? PIP_RAGE : PIP_SWEARING;
        after(p, PIP_ANNOYED, k->after_fed_up, PET_AFTER_FED_UP); /* it takes a minute to calm down */
        return m;
    }
    }
    return -1;
}

int pet_meet(const pet_t *p, int roll)
{
    /* a happy Pip likes almost everyone, a miserable one almost no one */
    const pet_tune_t *k = &pet_tune;
    int like = (int)(k->meet_like + 35 * pet_valence(p));
    if (roll < like) return roll < like * (1 - k->meet_shy) ? PIP_LOVE : PIP_SHY;
    if (p->annoy >= k->meet_rude) return PIP_BELLIGERENT;
    return roll < like + (100 - like) * k->meet_wary ? PIP_SUSPICIOUS : PIP_ANGRY;
}

void pet_rare_now(pet_t *p, int mood)
{
    p->rare_mood = (int8_t)(mood == PIP_STONED || mood == PIP_PINKEYE ? mood : next(&p->rng) % 2 ? PIP_PINKEYE : PIP_STONED);
    p->rare_at = p->last;
}

float pet_valence(const pet_t *p)
{
    float avg = (p->food + p->energy + p->fun + p->love + p->walk) / 5;
    return fmaxf(-1, fminf(1, (avg - 0.5f) * 2 + p->day_mood * 0.5f - p->annoy * 0.1f));
}

int pet_mood(const pet_t *p, int tz_min) { return pet_mood_why(p, tz_min, NULL); }

/* A palette: moods with weights, the rarer ones light */
typedef struct { uint8_t mood, weight; } pick_t;
static int pick(const pick_t *pool, int n, uint32_t h)
{
    int total = 0, i = 0;
    for (int j = 0; j < n; j++) total += pool[j].weight;
    for (int r = (int)(h % (uint32_t)total); r >= pool[i].weight; i++) r -= pool[i].weight;
    return i;
}
static uint32_t slot_hash(const pet_t *p, int64_t slot) { return ((uint32_t)slot * 2654435761u ^ p->rng) >> 7; }

static int palette(float v)
{
    return v > pet_tune.great ? 0 : v > pet_tune.good ? 1 : v > pet_tune.meh ? 2 : 3;
}

static int out(pet_why_t *why, int kind, int which, float value, int hold, int mood)
{
    *why = (pet_why_t){kind, which, value, hold};
    return mood;
}

int pet_mood_why(const pet_t *p, int tz_min, pet_why_t *why)
{
    pet_why_t w;
    if (!why) why = &w;
    if (p->asleep) return out(why, PET_WHY_ASLEEP, 0, 0, -1, PIP_ASLEEP);
    const pet_tune_t *k = &pet_tune;
    int64_t now = p->last, slot = now / SLOT;
    int hour = pet_hour(now, tz_min), hold = -1;
#define HELD (p->hold == hold && now - p->hold_since > (int64_t)(k->stale * 60))

    /* a rare card: twenty minutes of something odd, about once a month */
    int64_t rare_end = p->rare_at + (int64_t)(k->rare_min * 60);
    if (p->rare_mood >= 0 && now >= p->rare_at && now < rare_end)
        return out(why, PET_WHY_RARE, p->rare_mood == PIP_PINKEYE, (rare_end - now) / 60.0f, -1, p->rare_mood);

    /* the body: the battery is his blood sugar. Low for long, it leaves every other 5 minutes to the rest */
    bool bat = p->bat >= 0 && !p->charging;
    if (bat && p->bat < k->bat_empty) return out(why, PET_WHY_BODY, PET_BODY_EMPTY, p->bat, -1, PIP_SICK);
    if (bat && p->bat < k->bat_low) {
        int b = p->bat < k->bat_tired ? PET_BODY_LOWBAT : PET_BODY_NERVOUS; /* running on empty, or getting there */
        hold = PET_WHY_BODY * 16 + b;
        if (!HELD || slot % 2 == 0) return out(why, PET_WHY_BODY, b, p->bat, hold, b == PET_BODY_LOWBAT ? PIP_TIRED : PIP_NERVOUS);
    }

    /* something that happened lately still shows */
    if (p->after_mood >= 0 && now < p->after_until)
        return out(why, PET_WHY_AFTER, p->after_why, (float)(p->after_until - now), hold, p->after_mood);

    /* the most pressing need wins, measured by how far it is under its line. Needs about as bad take turns every
       5 minutes and two gone at once make him ill now and then; held long, the need shows its other face, then
       leaves a turn to the rest of the tree */
    const pet_urge_t *urges = k->urges;
    const float needs[5] = {p->food, p->energy, p->fun, p->love, p->walk};
    float sev[PET_URGES], worst = 0;
    int u = -1, gone = 0, turns[PET_URGES], n = 0;
    for (int i = 0; i < 5; i++) gone += needs[i] < k->sick_line;
    for (int i = 0; i < PET_URGES; i++) {
        sev[i] = (urges[i].line - needs[urges[i].need]) / urges[i].line;
        for (int j = 0; j < PET_URGES; j++) /* of two lines on one need, the deeper one speaks */
            if (urges[j].need == urges[i].need && urges[j].line < urges[i].line && needs[urges[j].need] < urges[j].line) sev[i] = 0;
        if (sev[i] > worst) worst = sev[i], u = i;
    }
    for (int i = 0; i < PET_URGES && u >= 0; i++)
        if (sev[i] > 0 && sev[i] >= worst - 0.1f) turns[n++] = i;
    int m = n + (gone >= 2), turn = m ? (int)(slot % m) : 0;
    if (m) {
        if (hold < 0) hold = PET_WHY_URGE * 16 + (u >= 0 ? u : 15);
        int phase = HELD ? (int)(slot / m % 3) : 0; /* the face, the other face, the rest */
        if (phase < 2 && turn == n) return out(why, PET_WHY_BODY, PET_BODY_SICK, (float)gone, hold, PIP_SICK);
        if (phase < 2) {
            int i = turns[turn];
            return out(why, PET_WHY_URGE, i + 8 * phase, sev[i], hold, phase ? urges[i].alt : urges[i].mood);
        }
    }
#undef HELD
    if ((hour >= 22 || hour < 6) && p->energy < 0.5f) return out(why, PET_WHY_NIGHT, 0, p->energy, hold, PIP_SLEEPY);

    /* what he is doing: still waking up, out on a walk, on the charger */
    if (p->woke_at && now - p->woke_at < (int64_t)(k->waking_min * 60)) return out(why, PET_WHY_SITU, PET_SITU_WAKING, 0, hold, PIP_SLEEPY);
    if (p->stride > k->walk_now) return out(why, PET_WHY_SITU, PET_SITU_WALK, p->stride, hold, slot % 2 ? PIP_AMAZED : PIP_CURIOUS);
    if (p->charging && now - p->charge_since > 60) return out(why, PET_WHY_SITU, PET_SITU_CHARGE, 0, hold, PIP_COMFY);

    /* otherwise, how good life is picks the palette (it sticks a little: no flicking across a line), the evening calms
       it down, and the clock picks a mood from it every 5 minutes, never the same twice running */
    static const pick_t pools[8][6] = {
        {{PIP_HAPPY, 3}, {PIP_CHEEKY, 2}, {PIP_CURIOUS, 2}, {PIP_AMAZED, 2}, {PIP_CONTENT, 2}, {PIP_SMUG, 1}},
        {{PIP_CONTENT, 3}, {PIP_COMFY, 2}, {PIP_CURIOUS, 2}, {PIP_AMAZED, 1}, {PIP_NEUTRAL, 2}, {PIP_CHEEKY, 1}},
        {{PIP_NEUTRAL, 3}, {PIP_SKEPTICAL, 2}, {PIP_CURIOUS, 2}, {PIP_SUSPICIOUS, 2}, {PIP_BORED, 1}, {PIP_CYNICAL, 1}},
        {{PIP_SAD, 3}, {PIP_DESPONDENT, 2}, {PIP_GLOOMY, 2}, {PIP_CYNICAL, 1}, {PIP_DISMAYED, 1}, {PIP_HURT, 1}},
        /* the evening */
        {{PIP_HAPPY, 2}, {PIP_CONTENT, 3}, {PIP_COMFY, 2}, {PIP_CHEEKY, 1}, {PIP_SMUG, 1}, {PIP_LOVE, 1}},
        {{PIP_COMFY, 3}, {PIP_CONTENT, 3}, {PIP_NEUTRAL, 2}, {PIP_SLEEPY, 1}, {PIP_CURIOUS, 1}, {PIP_SKEPTICAL, 1}},
        {{PIP_NEUTRAL, 3}, {PIP_BORED, 2}, {PIP_SKEPTICAL, 2}, {PIP_SLEEPY, 1}, {PIP_CYNICAL, 1}, {PIP_SUSPICIOUS, 1}},
        {{PIP_SAD, 3}, {PIP_GLOOMY, 3}, {PIP_DESPONDENT, 2}, {PIP_LONELY, 1}, {PIP_TIRED, 1}, {PIP_CYNICAL, 1}},
    };
    float v = pet_valence(p);
    int pal = palette(v);
    if (p->pool >= 0 && p->pool < 4 && pal != p->pool && palette(v + k->hyst) <= p->pool && palette(v - k->hyst) >= p->pool) pal = p->pool;
    int which = pal + (hour >= (int)k->evening_h ? 4 : 0);
    const pick_t *pool = pools[which];
    int i = pick(pool, 6, slot_hash(p, slot)), before = slot == p->slot ? p->prev : p->cur;
    if (pool[i].mood == before) i = (i + 1) % 6;
    return out(why, PET_WHY_POOL, which, v, hold, pool[i].mood);
}

int pet_settle(pet_t *p, int tz_min)
{
    pet_why_t w;
    int m = pet_mood_why(p, tz_min, &w);
    int32_t slot = (int32_t)(p->last / SLOT);
    if (slot != p->slot) p->prev = p->cur, p->slot = slot;
    p->cur = (int8_t)m;
    if (w.kind == PET_WHY_POOL) p->pool = (int8_t)(w.which & 3);
    if (w.hold != p->hold) p->hold = (int8_t)w.hold, p->hold_since = p->last;
    return m;
}
