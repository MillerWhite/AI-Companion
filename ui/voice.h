/* Pip's voice: babbling chirps synthesized on the fly, no samples. Pure C.
   The UI calls voice_say(); the audio side pulls PCM with voice_render() from its own task/callback. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
    VOICE_POKE,    /* "eep!" */
    VOICE_HAPPY,   /* rising chirps */
    VOICE_SAD,     /* slow falling "uuu" */
    VOICE_GROWL,   /* annoyed / angry */
    VOICE_OH,      /* surprised "oh!?" */
    VOICE_LOVE,    /* warbly trill */
    VOICE_PURR,    /* while petted */
    VOICE_NOM,     /* eating */
    VOICE_BURP,    /* too full */
    VOICE_YAWN,    /* going to sleep */
    VOICE_DIZZY,   /* wobbly glide */
    VOICE_DERP,    /* shaken silly */
    VOICE_HM,      /* curious */
    VOICE_CENSOR,  /* swearing: garble and a beep */
    VOICE_ALARM,   /* the timer's little song */
    VOICE_HMPH,    /* doesn't like you */
    VOICE_TICK,    /* a detent click: the menu wheel passing a row */
    VOICE_COUNT
};

void voice_init(int sample_rate);
void voice_say(int sound);       /* replaces whatever is playing */
void voice_set_volume(float v);  /* 0 = mute .. 1 */
float voice_volume(void);
bool voice_busy(void);
/* Fill n mono 16-bit samples (silence when idle); returns true if anything was audible. */
bool voice_render(int16_t *out, int n);
const char *voice_name(int sound);
