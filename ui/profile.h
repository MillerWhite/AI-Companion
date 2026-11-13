/* What is personal on the keychain: the name on the badge and the links in Quick QR. Everyone sets their own from the
   panel; the host keeps it (firmware: NVS). Pure C. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PROFILE_LINKS 4
typedef struct { char service[20], handle[48], url[200]; } profile_link_t;
typedef struct {
    char first[32], last[32], org[24]; /* the badge: two lines and a small one under them */
    profile_link_t link[PROFILE_LINKS]; /* empty url: not there */
} profile_t;

extern profile_t profile;
extern int profile_rev;             /* bumps on every change: Quick QR draws its codes again */
int profile_links(void);            /* how many links there are */
const profile_link_t *profile_link(int i); /* the i-th one that is there */
int profile_brand(const char *url); /* BRAND_* from the address: t.me, instagram.com */
void profile_changed(void);         /* call after editing profile: tells the host to keep it */
void profile_on_change(void (*cb)(void));
