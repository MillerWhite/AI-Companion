/* Pet logic checks over simulated days: every branch of the mood tree. Run by sim/check.py. */
#include "moods.h"
#include "pet.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); } } while (0)
#define H 3600LL
static const int64_t NOON = 1791633600; /* 2026-10-10 12:00 UTC */

static pet_t fresh(void)
{
    pet_t p;
    pet_init(&p, NOON, 0, 42);
    p.day_mood = p.day_base = 0.2f;
    return p;
}
static int why_kind(const pet_t *p)
{
    pet_why_t w;
    pet_mood_why(p, 0, &w);
    return w.kind;
}

int main(void)
{
    pet_t p = fresh();
    CHECK(!p.asleep && pet_mood(&p, 0) != PIP_ASLEEP, "awake at noon");

    /* left alone until 01:00: hungry-ish, tired, and asleep by then */
    pet_update(&p, NOON + 13 * H, 0);
    CHECK(p.asleep, "asleep after midnight");
    CHECK(pet_mood(&p, 0) == PIP_ASLEEP, "sleeping face");
    /* woken at night he grumbles and stays up a while before dozing off again */
    pet_t woken = p;
    pet_event(&woken, PET_WAKE);
    pet_update(&woken, NOON + 13 * H + 300, 0);
    CHECK(!woken.asleep, "stays up 5 min after being woken");
    pet_update(&woken, NOON + 13 * H + 1800, 0);
    CHECK(woken.asleep, "back asleep after a while");
    CHECK(pet_event(&woken, PET_WAKE) != PIP_INDIGNANT, "woken once more after a while: not indignant yet");
    pet_event(&woken, PET_SLEEP);
    CHECK(pet_event(&woken, PET_WAKE) == PIP_INDIGNANT, "woken again within minutes -> indignant");

    /* the night restores energy, and he gets up in the morning, sleepy for a bit */
    pet_t morning = p;
    for (int64_t t = NOON + 13 * H; morning.asleep && t < NOON + 22 * H; t += 60) pet_update(&morning, t, 0);
    CHECK(!morning.asleep && morning.energy > 0.8f, "up in the morning");
    morning.food = morning.fun = morning.love = morning.walk = 0.8f; /* fed and cuddled: no need speaks over it */
    pet_settle(&morning, 0);
    CHECK(pet_mood(&morning, 0) == PIP_SLEEPY && why_kind(&morning) == PET_WHY_SITU, "just woke up -> sleepy (situation)");
    pet_update(&p, NOON + 21 * H, 0); /* 09:00 */
    CHECK(!p.asleep && p.energy > 0.8f, "rested and awake at 9 (up at 7)");

    /* skip meals and cuddles for a day: hangry, lonely, ill now and then; the faces take turns */
    pet_update(&p, NOON + 30 * H, 0);
    int m = pet_mood(&p, 0), seen[PIP_MOOD_COUNT] = {0}, kinds = 0;
    CHECK(pet_valence(&p) < 0, "neglect hurts");
    for (int i = 0; i < 24; i++) { /* two hours, minute by minute */
        pet_update(&p, NOON + 30 * H + i * 300, 0);
        m = pet_settle(&p, 0);
        kinds += !seen[m]++;
        CHECK(m == PIP_ANGRY || m == PIP_FURIOUS || m == PIP_ANNOYED || m == PIP_LONELY || m == PIP_DESPONDENT || m == PIP_HURT ||
                  m == PIP_SAD || m == PIP_BORED || m == PIP_CYNICAL || m == PIP_GLOOMY || m == PIP_NERVOUS || m == PIP_SICK ||
                  m == PIP_DISMAYED || m == PIP_TIRED, /* the low palettes get a turn too */
              "neglect shows");
    }
    CHECK(seen[PIP_SICK] && kinds >= 4, "neglect: several faces take turns, sick among them");

    /* care fixes it */
    for (int i = 0; i < 3; i++) pet_event(&p, PET_FEED);
    for (int i = 0; i < 40; i++) pet_event(&p, PET_STROKE);
    for (int i = 0; i < 3; i++) pet_event(&p, PET_PLAY);
    for (int i = 0; i < 3000; i++) pet_event(&p, PET_STEP);
    CHECK(p.food > 0.9f && p.love > 0.9f && p.fun > 0.9f && p.walk > 0.9f, "needs refilled");
    CHECK(pet_event(&p, PET_FEED) == PIP_NAUSEOUS, "overfed");
    CHECK(pet_event(&p, PET_FEED) == PIP_DISGUSTED, "overfed twice in a row -> disgusted");
    p.energy = 0.9f; /* a nap: tired would speak over the walk */
    CHECK(pet_mood(&p, 0) == PIP_AMAZED || pet_mood(&p, 0) == PIP_CURIOUS, "just walked 3000 steps -> out on a walk");
    CHECK(why_kind(&p) == PET_WHY_SITU, "the walk is a situation");

    /* play: too tired to, or can't wait to */
    pet_t q = fresh();
    q.energy = 0.1f;
    CHECK(pet_event(&q, PET_PLAY) == PIP_NO, "too tired to play -> no");
    q = fresh(), q.fun = 0.2f;
    CHECK(pet_event(&q, PET_PLAY) == PIP_YES, "bored, asked to play -> yes");

    /* the palette: never the same mood twice running, and it stays put near a line */
    q = fresh();
    int last = -1, repeats = 0;
    for (int i = 0; i < 60; i++) {
        q.last = NOON + i * 300;
        int b = pet_settle(&q, 0);
        repeats += b == last, last = b;
    }
    CHECK(repeats == 0, "palette moods change every 5 minutes");
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.75f, q.day_mood = 0;
    pet_settle(&q, 0);
    pet_why_t w;
    pet_mood_why(&q, 0, &w);
    CHECK(w.kind == PET_WHY_POOL && w.which == 1, "valence 0.5: the good palette");
    q.food = q.energy = q.fun = q.love = q.walk = 0.79f; /* 0.58: just over great's line, within hyst */
    pet_mood_why(&q, 0, &w);
    CHECK(w.which == 1, "a hair over the line: still good (hysteresis)");
    q.food = q.energy = q.fun = q.love = q.walk = 0.85f;
    pet_mood_why(&q, 0, &w);
    CHECK(w.which == 0, "well over: great");
    q.last = NOON + 9 * H; /* 21:00 */
    pet_mood_why(&q, 0, &w);
    CHECK(w.which == 4, "in the evening the palettes calm down");

    /* a long spell of one need shows its other face too */
    q = fresh();
    q.love = 0.15f;
    int faces = 0;
    for (int i = 0; i < 24; i++) {
        q.last = NOON + i * 300;
        int b = pet_settle(&q, 0);
        faces |= (b == PIP_HURT) | (b == PIP_SAD) << 1 | (b != PIP_HURT && b != PIP_SAD) << 2;
        if (i < 6) CHECK(b == PIP_HURT, "hurt for the first half hour");
    }
    CHECK(faces == 7, "then hurt, sad and the rest of the tree by turns");
    q = fresh();
    q.bat = 15;
    int nervous = 0, rest = 0;
    for (int i = 0; i < 24; i++) {
        q.last = NOON + i * 300;
        int b = pet_settle(&q, 0);
        if (i < 6) CHECK(b == PIP_NERVOUS, "battery low: nervous for the first half hour");
        else nervous += b == PIP_NERVOUS, rest += b != PIP_NERVOUS;
    }
    CHECK(nervous >= 8 && rest >= 8, "then nervous every other 5 minutes");

    /* aftertastes: a win shows for minutes, then it is gone */
    q = fresh();
    CHECK(pet_event(&q, PET_WIN) == PIP_SMUG && pet_mood(&q, 0) == PIP_SMUG, "won -> smug");
    CHECK(why_kind(&q) == PET_WHY_AFTER, "smug is an aftertaste");
    pet_update(&q, q.last + (int64_t)pet_tune.after_win + 60, 0);
    CHECK(pet_mood(&q, 0) != PIP_SMUG || why_kind(&q) != PET_WHY_AFTER, "the win wears off");
    CHECK(pet_event(&q, PET_LOSE) == PIP_NO && pet_mood(&q, 0) == PIP_INDIGNANT, "lost -> no, then indignant");
    CHECK(pet_event(&q, PET_RECORD) == PIP_AMAZED && pet_mood(&q, 0) == PIP_AMAZED, "record -> amazed");
    pet_event(&q, PET_FRIGHT);
    CHECK(pet_mood(&q, 0) == PIP_AFRAID, "a fright -> afraid for a while");
    CHECK(pet_event(&q, PET_FLOOD) == PIP_DISMAYED && pet_mood(&q, 0) == PIP_DISMAYED, "notification flood -> dismayed");
    pet_event(&q, PET_SLEEP);
    CHECK(q.after_mood < 0, "sleep wipes an aftertaste");

    /* poked again and again: angry, furious, red, swearing; then annoyed for a minute */
    q = fresh();
    int ladder[] = {PIP_ANGRY, PIP_FURIOUS, PIP_RAGE, PIP_SWEARING};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) pet_event(&q, PET_POKE);
        CHECK(pet_event(&q, PET_FED_UP) == ladder[i], "the poke ladder");
    }
    CHECK(pet_mood(&q, 0) == PIP_ANNOYED, "fed up -> annoyed for a while");

    /* back after hours alone: glad, or sulking until stroked */
    q = fresh();
    pet_update(&q, NOON + 2 * H, 0);
    CHECK(pet_event(&q, PET_SEEN) == -1, "two hours is nothing");
    pet_update(&q, NOON + 7 * H, 0);
    CHECK(pet_event(&q, PET_SEEN) == PIP_LOVE && pet_mood(&q, 0) == PIP_HAPPY, "back after 5 h, loved -> glad");
    q = fresh();
    pet_update(&q, NOON + 7 * H, 0);
    q.love = 0.3f;
    CHECK(pet_event(&q, PET_SEEN) == PIP_HURT && pet_mood(&q, 0) == PIP_HURT, "back after 7 h, neglected -> sulks");
    pet_event(&q, PET_STROKE);
    CHECK(q.after_mood < 0, "a stroke makes up");

    /* meeting someone: love, shy, wary, angry, belligerent */
    q = fresh();
    q.food = q.energy = q.fun = q.love = q.walk = 0.5f, q.day_mood = 0; /* valence 0: half like */
    CHECK(pet_meet(&q, 10) == PIP_LOVE && pet_meet(&q, 45) == PIP_SHY, "liked: love, or shy");
    CHECK(pet_meet(&q, 60) == PIP_SUSPICIOUS && pet_meet(&q, 95) == PIP_ANGRY, "disliked: wary, or angry");
    q.annoy = 3;
    CHECK(pet_meet(&q, 99) == PIP_BELLIGERENT, "disliked while annoyed: belligerent");

    /* the battery: nervous, tired, ill; on the charger he gets comfy */
    q = fresh();
    q.bat = 15;
    CHECK(pet_mood(&q, 0) == PIP_NERVOUS, "battery 15% -> nervous");
    q.bat = 7;
    CHECK(pet_mood(&q, 0) == PIP_TIRED, "battery 7% -> tired");
    q.bat = 2;
    CHECK(pet_mood(&q, 0) == PIP_SICK && why_kind(&q) == PET_WHY_BODY, "battery 2% -> sick");
    q.charging = true, q.charge_since = q.last - 120;
    CHECK(pet_mood(&q, 0) == PIP_COMFY, "charging -> comfy");

    /* rare cards: about one a month, twenty minutes in the afternoon */
    q = fresh();
    int cards = 0;
    for (int d = 1; d <= 365; d++) {
        pet_update(&q, NOON + d * 24 * H, 0);
        cards += q.rare_mood >= 0;
        q.food = q.energy = q.fun = q.love = q.walk = 0.9f;
    }
    CHECK(cards >= 4 && cards <= 30, "about a rare card a month");
    float chance = pet_tune.rare_chance;
    pet_tune.rare_chance = 1;
    q = fresh();
    pet_update(&q, NOON + 12 * H, 0); /* midnight: a new day, a card dealt */
    pet_tune.rare_chance = chance;
    CHECK(q.rare_mood == PIP_STONED || q.rare_mood == PIP_PINKEYE, "a card is dealt");
    int64_t at = q.rare_at;
    q.asleep = false, q.last = at + 60;
    q.food = q.energy = q.fun = q.love = q.walk = 0.9f;
    CHECK(pet_mood(&q, 0) == q.rare_mood && why_kind(&q) == PET_WHY_RARE, "the card shows");
    q.last = at + (int64_t)pet_tune.rare_min * 60 + 60;
    CHECK(pet_mood(&q, 0) != q.rare_mood, "and is gone after its minutes");

    /* a month away is caught up without hanging and leaves a starving pet */
    pet_update(&p, NOON + 400 * 24 * H, 0);
    CHECK(p.food == 0, "a month away");
    puts("pet_test ok");
    return 0;
}
