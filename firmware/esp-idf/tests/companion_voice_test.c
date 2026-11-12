#include "companion_voice.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t render_phrase(int cue, int16_t *out, size_t capacity)
{
    voice_init(16000);
    voice_set_volume(0.24f);
    voice_say(cue);
    size_t offset = 0;
    while (voice_busy() && offset + 256 <= capacity) {
        voice_render(out + offset, 256);
        offset += 256;
    }
    return offset;
}

int main(void)
{
    int16_t happy[24000] = {0};
    int16_t sad[24000] = {0};
    int16_t muted[24000] = {0};

    size_t happy_n = render_phrase(VOICE_HAPPY, happy, 24000);
    size_t sad_n = render_phrase(VOICE_SAD, sad, 24000);
    assert(happy_n > 0 && sad_n > 0);
    assert(memcmp(happy, sad, (happy_n < sad_n ? happy_n : sad_n) * sizeof(int16_t)) != 0);

    voice_init(16000);
    voice_set_volume(0.0f);
    voice_say(VOICE_LOVE);
    for (size_t i = 0; i < 24000 && voice_busy(); i += 256) {
        size_t n = 24000 - i < 256 ? 24000 - i : 256;
        voice_render(muted + i, (int)n);
    }
    for (size_t i = 0; i < 24000; ++i) assert(muted[i] == 0);

    for (size_t i = 0; i < happy_n; ++i) {
        assert(happy[i] <= 32000);
        assert(happy[i] >= -32000);
    }

    puts("companion voice tests passed");
    return 0;
}
