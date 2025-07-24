#ifndef BLIND_H
#define BLIND_H

#include "sprite.h"

#define MAX_ANTE 8 // The GBA's max uint value is around 4 billion, so we're going to not add endless mode for simplicity's sake

// +1 is added because we'll actually be indexing at 1, but if something causes you to go to ante 0, there will still be a value there.
static const int ante_lut[MAX_ANTE + 1] = {100, 300, 800, 2000, 5000, 11000, 20000, 35000, 50000};

#define MAX_BLINDS 3

#define SMALL_BLIND 0
#define BIG_BLIND 1
#define BOSS_BLIND 2

#define SMALL_BLIND_PB 1
#define BIG_BLIND_PB 2
#define BOSS_BLIND_PB 3

#define BLIND_SPRITE_OFFSET 16
#define SMALL_BLIND_TID 960
#define BIG_BLIND_TID (BLIND_SPRITE_OFFSET + SMALL_BLIND_TID)
#define BOSS_BLIND_TID (BLIND_SPRITE_OFFSET + BIG_BLIND_TID)

#define BLIND_TEXT_COLOR_INDEX 1
#define BLIND_SHADOW_COLOR_INDEX 2
#define BLIND_HIGHLIGHT_COLOR_INDEX 3
#define BLIND_MAIN_COLOR_INDEX 4
#define BLIND_BACKGROUND_MAIN_COLOR_INDEX 5
#define BLIND_BACKGROUND_SECONDARY_COLOR_INDEX 6
#define BLIND_BACKGROUND_SHADOW_COLOR_INDEX 7

enum BlindState
{
    BLIND_CURRENT,
    BLIND_UPCOMING,
    BLIND_DEFEATED,
    BLIND_SKIPPED
};

void blind_init();

int blind_get_requirement(int type, int ante);
int blind_get_reward(int type);
u16 blind_get_color(int type, int index);

Sprite *blind_token_new(int type, int x, int y, int sprite_index); 

#endif // BLIND_H