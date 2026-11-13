/* Pip's moods. Plain C so the pet logic can pick them without LVGL. */
#pragma once

enum {
    /* the first twelve have debug keys 1..9 0 - = */
    PIP_NEUTRAL, PIP_HAPPY, PIP_SAD, PIP_ANGRY, PIP_SURPRISED, PIP_SLEEPY, PIP_ASLEEP, PIP_SKEPTICAL,
    PIP_CURIOUS, PIP_LOVE, PIP_DIZZY, PIP_OUCH,
    /* body */
    PIP_STONED, PIP_PINKEYE, PIP_NAUSEOUS, PIP_SICK, PIP_TIRED, PIP_EXHAUSTED,
    /* anger, from mild to red */
    PIP_ANNOYED, PIP_FURIOUS, PIP_RAGE, PIP_SWEARING, PIP_INDIGNANT, PIP_BELLIGERENT, PIP_HURT,
    /* dark side */
    PIP_DISGUSTED, PIP_SUSPICIOUS, PIP_CYNICAL, PIP_AFRAID, PIP_NERVOUS, PIP_DESPONDENT, PIP_LONELY, PIP_GLOOMY, PIP_DULL,
    PIP_SHY, PIP_SMUG, PIP_BORED, PIP_SHOCKED, PIP_DISMAYED, PIP_PUZZLED,
    /* bright side */
    PIP_AMAZED, PIP_CHEEKY, PIP_CONTENT, PIP_COMFY,
    /* answers: a nod, a head shake */
    PIP_YES, PIP_NO,
    PIP_MOOD_COUNT
};
