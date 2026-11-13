#include "profile.h"
#include "doodle.h"
#include <string.h>

/* until someone sets their own: an obvious placeholder, and a code that leads to the project */
profile_t profile = {
    .first = "Твоё",
    .last = "Имя",
    .org = "KeyPet",
    .link = {{"GitHub", "Pip-KeyPet", "https://github.com/NovikovVisuals/Pip-KeyPet"}},
};
int profile_rev = 1;
static void (*on_change)(void);

int profile_links(void)
{
    int n = 0;
    for (int i = 0; i < PROFILE_LINKS; i++) n += profile.link[i].url[0] != 0;
    return n;
}

const profile_link_t *profile_link(int i)
{
    for (int k = 0; k < PROFILE_LINKS; k++)
        if (profile.link[k].url[0] && i-- == 0) return &profile.link[k];
    return &profile.link[0];
}

int profile_brand(const char *url)
{
    if (strstr(url, "t.me/") || strstr(url, "telegram.me/")) return BRAND_TELEGRAM;
    if (strstr(url, "instagram.com/")) return BRAND_INSTAGRAM;
    return BRAND_NONE;
}

void profile_changed(void)
{
    profile_rev++;
    if (on_change) on_change();
}

void profile_on_change(void (*cb)(void)) { on_change = cb; }
