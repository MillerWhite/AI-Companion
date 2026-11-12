#include "voice.h"
#include <math.h>
#include <string.h>

#define MAX_SYL 14
#define TWO_PI 6.2831853f

typedef struct {
    float f0, f1;
    float dur, gap;
    float vib_rate, vib_depth;
    float rough, bright, open, noise, amp;
    uint8_t wave;
} syl_t;

typedef struct { syl_t s[MAX_SYL]; int n; } phrase_t;

static struct {
    int rate;
    float volume;
    phrase_t cur;
    volatile int pending;
    int i;
    float t, phase, low, band;
    uint32_t rng, noise;
} v = {.rate = 16000, .volume = 0.8f, .pending = -1};

static uint32_t next(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static float rnd(float a, float b) { return a + (b - a) * (next(&v.rng) % 10000U) / 10000.0f; }
static float semi(float n) { return powf(2.0f, n / 12.0f); }

static void add(phrase_t *p, syl_t s)
{
    if (p->n < MAX_SYL) {
        if (!s.amp) s.amp = 1.0f;
        if (!s.bright) s.bright = 4.0f;
        if (!s.open) s.open = 1.6f;
        p->s[p->n++] = s;
    }
}

static void compose(phrase_t *p, int sound)
{
    memset(p, 0, sizeof(*p));
    float base = 520.0f * semi(rnd(-2.0f, 2.0f));
    switch (sound) {
    case VOICE_POKE:
        add(p, (syl_t){.f0=base*1.4f,.f1=base*2.1f,.dur=.09f,.bright=5});
        break;
    case VOICE_HAPPY:
        for (int i=0,n=2+(int)rnd(0,2); i<n; ++i)
            add(p,(syl_t){.f0=base*semi(i*3),.f1=base*semi(i*3+5),.dur=rnd(.08f,.12f),.gap=.04f,.vib_rate=9,.vib_depth=.02f});
        break;
    case VOICE_SAD:
        add(p,(syl_t){.f0=base*.8f,.f1=base*.55f,.dur=.45f,.gap=.08f,.vib_rate=5,.vib_depth=.03f,.bright=2.5f,.open=1.2f});
        add(p,(syl_t){.f0=base*.62f,.f1=base*.42f,.dur=.5f,.vib_rate=5,.vib_depth=.04f,.bright=2.2f,.open=1.1f,.noise=.05f});
        break;
    case VOICE_GROWL:
        for (int i=0,n=1+(int)rnd(0,2); i<n; ++i)
            add(p,(syl_t){.f0=base*.38f,.f1=base*.3f,.dur=rnd(.2f,.3f),.gap=.05f,.rough=.9f,.bright=6,.open=1.4f,.wave=1,.amp=.9f});
        break;
    case VOICE_OH:
        add(p,(syl_t){.f0=base*.9f,.f1=base*1.9f,.dur=.16f,.gap=.03f,.open=2.4f});
        add(p,(syl_t){.f0=base*1.7f,.f1=base*2.3f,.dur=.1f});
        break;
    case VOICE_LOVE:
        add(p,(syl_t){.f0=base*1.6f,.f1=base*1.1f,.dur=.7f,.vib_rate=11,.vib_depth=.06f,.bright=3.5f,.open=1.8f});
        break;
    case VOICE_PURR:
        add(p,(syl_t){.f0=95,.f1=90,.dur=.9f,.rough=1,.bright=5,.open=1.2f,.wave=1,.noise=.1f,.amp=.55f});
        break;
    case VOICE_NOM:
        for (int i=0;i<3;++i) add(p,(syl_t){.f0=base*.55f,.f1=base*.5f,.dur=.08f,.gap=.07f,.bright=1.8f,.open=1.1f,.amp=.8f});
        add(p,(syl_t){.f0=base,.f1=base*1.5f,.dur=.12f});
        break;
    case VOICE_BURP:
        add(p,(syl_t){.f0=130,.f1=85,.dur=.38f,.rough=1,.bright=7,.open=1.5f,.wave=1,.noise=.15f});
        break;
    case VOICE_YAWN:
        add(p,(syl_t){.f0=base*.6f,.f1=base*.95f,.dur=.45f,.bright=2,.open=2.5f,.noise=.12f,.amp=.7f});
        add(p,(syl_t){.f0=base*.9f,.f1=base*.45f,.dur=.6f,.bright=2,.open=1.4f,.noise=.1f,.amp=.6f});
        break;
    case VOICE_DIZZY:
        add(p,(syl_t){.f0=base*1.3f,.f1=base*.6f,.dur=.8f,.vib_rate=6,.vib_depth=.14f,.open=1.4f});
        break;
    case VOICE_DERP:
        add(p,(syl_t){.f0=base*.7f,.f1=base*.5f,.dur=.25f,.vib_rate=14,.vib_depth=.08f,.bright=2.5f,.wave=1});
        add(p,(syl_t){.f0=base*.5f,.f1=base*.75f,.dur=.2f,.vib_rate=14,.vib_depth=.08f,.bright=2.5f,.wave=1});
        break;
    case VOICE_HM:
        add(p,(syl_t){.f0=base*.7f,.f1=base*1.05f,.dur=.28f,.bright=1.8f,.open=1.6f});
        break;
    case VOICE_TICK:
        add(p,(syl_t){.f0=2600,.f1=1800,.dur=.014f,.bright=2,.open=1,.noise=.6f,.wave=2,.amp=.5f});
        break;
    case VOICE_HMPH:
        add(p,(syl_t){.f0=base*.5f,.f1=base*.35f,.dur=.22f,.rough=.5f,.bright=3,.open=1.2f,.noise=.25f,.wave=1});
        break;
    default:
        break;
    }
}

void voice_init(int sample_rate) { v.rate=sample_rate>0?sample_rate:16000; v.i=v.cur.n=0; v.rng=0x1234567u; v.noise=99; v.pending=-1; }
void voice_say(int sound) { if (sound>=0 && sound<VOICE_COUNT) v.pending=sound; }
void voice_set_volume(float volume) { v.volume=volume<0?0:volume>1?1:volume; }
float voice_volume(void) { return v.volume; }
bool voice_busy(void) { return v.pending>=0 || v.i<v.cur.n; }

const char *voice_name(int sound)
{
    static const char *const names[VOICE_COUNT]={"poke","happy","sad","growl","oh","love","purr","nom","burp","yawn","dizzy","derp","hm","censor","alarm","hmph","tick"};
    return sound>=0 && sound<VOICE_COUNT?names[sound]:"";
}

bool voice_render(int16_t *out, int count)
{
    if (!out || count<=0 || v.rate<=0) return false;
    int sound=v.pending;
    if (sound>=0) { v.pending=-1; compose(&v.cur,sound); v.i=0; v.t=0; v.phase=0; v.low=0; v.band=0; }
    bool audible=false;
    const float dt=1.0f/(float)v.rate;
    for (int k=0;k<count;++k) {
        float y=0;
        if (v.i<v.cur.n) {
            const syl_t *s=&v.cur.s[v.i];
            if (v.t<s->dur) {
                float x=v.t/s->dur;
                float f=s->f0*powf(s->f1/s->f0,x)*(1+s->vib_depth*sinf(TWO_PI*s->vib_rate*v.t));
                v.phase+=f*dt; v.phase-=floorf(v.phase);
                float src=s->wave==1?2*v.phase-1:s->wave==2?(v.phase<.5f?.7f:-.7f):sinf(TWO_PI*v.phase)+.3f*sinf(2*TWO_PI*v.phase);
                src+=s->noise*((int32_t)next(&v.noise)/2147483648.0f);
                float fc=fminf(f*s->bright*(1+(s->open-1)*sinf(3.14159f*x)),v.rate*.16f);
                float g=2*sinf(3.14159f*fc/v.rate);
                v.low+=g*v.band; v.band+=g*(src-v.low-.9f*v.band);
                float env=fminf(v.t/.008f,1)*(x>.7f?(1-x)/.3f:1);
                if(s->rough>0) env*=1-s->rough*.45f*(1+sinf(TWO_PI*38*v.t));
                y=v.low*env*s->amp;
            } else if(v.t>=s->dur+s->gap) { ++v.i; v.t=0; }
            v.t+=dt;
        }
        y*=v.volume*.6f; y=y/(1+fabsf(y));
        out[k]=(int16_t)(y*32000);
        if(fabsf(y)>.01f) audible=true;
    }
    return audible;
}
