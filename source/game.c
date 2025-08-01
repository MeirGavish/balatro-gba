#include "game.h"

#include <maxmod.h>
#include <tonc.h>
#include <string.h>
#include <stdlib.h>

#include "util.h"
#include "sprite.h"
#include "card.h"
#include "hand_analysis.h"
#include "blind.h"
#include "joker.h"
#include "graphic_utils.h"
#include "tonc_video.h"

#include "background_gfx.h"
#include "background_shop_gfx.h"
#include "background_blind_select_gfx.h"

#include "soundbank.h"

// This seed system is temporary.
static uint rng_seed = 2048;
static bool seeded = false;

static uint timer = 0; // This might already exist in libtonc but idk so i'm just making my own
static int game_speed = 1;
static int background = 0;

static enum GameState game_state = GAME_BLIND_SELECT; // The current game state, this is used to determine what the game is doing at any given time
static enum HandState hand_state = HAND_DRAW;
static enum PlayState play_state = PLAY_PLAYING;
static int state = 0; // General state variable, used for switch statements in each game state related function

static enum HandType hand_type = NONE;

static Sprite *playing_blind_token = NULL; // The sprite that displays the blind when in "GAME_PLAYING/GAME_ROUND_END" state
static Sprite *round_end_blind_token = NULL; // The sprite that displays the blind when in "GAME_ROUND_END" state

static Sprite *blind_select_tokens[MAX_BLINDS] = {NULL}; // The sprites that display the blinds when in "GAME_BLIND_SELECT" state

static int current_blind = SMALL_BLIND;
static enum BlindState blinds[MAX_BLINDS] = {BLIND_CURRENT, BLIND_UPCOMING, BLIND_UPCOMING}; // The current state of the blinds, this is used to determine what the game is doing at any given time

// Red deck default (can later be moved to a deck.h file or something)
static int max_hands = 4;
static int max_discards = 4;
// Set in game_init and game_round_init
static int hands = 0;
static int discards = 0;

static int round = 0;
static int ante = 1;
static int money = 4;
static int score = 0;
static int temp_score = 0; // This is the score that shows in the same spot as the hand type.
static FIXED lerped_score = 0;
static FIXED lerped_temp_score = 0;

static int chips = 0;
static int mult = 0;

static int hand_size = 8; // Default hand size is 8
static int cards_drawn = 0;
static int hand_selections = 0;

static int selection_x = 0;
static int selection_y = 0;

static bool sort_by_suit = false;

// Stacks
static JokerObject *jokers[MAX_JOKERS_HELD_SIZE] = {NULL};
static int jokers_top = -1;

static CardObject *played[MAX_SELECTION_SIZE] = {NULL};
static int played_top = -1;

static CardObject *hand[MAX_HAND_SIZE] = {NULL};
static int hand_top = -1;

static Card *deck[MAX_DECK_SIZE] = {NULL};
static int deck_top = -1;

static Card *discard_pile[MAX_DECK_SIZE] = {NULL};
static int discard_top = -1;

// Joker stack
static inline void joker_push(JokerObject *joker)
{
    if (jokers_top >= MAX_JOKERS_HELD_SIZE - 1) return;
    jokers[++jokers_top] = joker;
}

static inline JokerObject *joker_pop()
{
    if (jokers_top < 0) return NULL;
    return jokers[jokers_top--];
}

// Played stack
static inline void played_push(CardObject *card_object)
{
    if (played_top >= MAX_SELECTION_SIZE - 1) return;
    played[++played_top] = card_object;
}

static inline CardObject *played_pop()
{
    if (played_top < 0) return NULL;
    return played[played_top--];
}

// Deck stack
static inline void deck_push(Card *card)
{
    if (deck_top >= MAX_DECK_SIZE - 1) return;
    deck[++deck_top] = card;
}

static inline Card *deck_pop()
{
    if (deck_top < 0) return NULL;
    return deck[deck_top--];
}

// Discard stack
static inline void discard_push(Card *card)
{
    if (discard_top >= MAX_DECK_SIZE - 1) return;
    discard_pile[++discard_top] = card;
}

static inline Card *discard_pop()
{
    if (discard_top < 0) return NULL;
    return discard_pile[discard_top--];
}

// Consts

// Rects                                       left     top     right   bottom
// Screenblock rects
static const Rect ROUND_END_MENU_RECT       = {9,       7,      24,     20 }; 

static const Rect POP_MENU_ANIM_RECT        = {9,       7,      24,     31 };
// The rect for popping menu animations (round end, shop, blinds) 
// - extends beyond the visible screen to the end of the screenblock
// It includes both the target and source position rects. 
// This is because when popping, the target position is blank so we just animate 
// the whole rect so we don't have to track its position

static const Rect SINGLE_BLIND_SELECT_RECT = { 9,       7,      13,     32 };

static const Rect HAND_BG_RECT_SELECTING    = {9,       11,     24,     17 };
// TODO: Currently unused, remove?
//static const Rect HAND_BG_RECT_PLAYING      = {9,       14,     24,     18 };

static const Rect TOP_LEFT_ITEM_SRC_RECT    = {0,       20,     8,      25 };
static const BG_POINT TOP_LEFT_PANEL_POINT  = {0,       0, };
static const Rect TOP_LEFT_PANEL_ANIM_RECT  = {0,       0,      8,      4  };
/* Contains the shop icon/current blind etc. 
 * The difference between TOP_LEFT_PANEL_ANIM_RECT and TOP_LEFT_PANEL_RECT 
 * is due to an overlap between the bottom of the top left panel
 * and the top of the score panel in the tiles connecting them.
 * TOP_LEFT_PANEL_ANIM_RECT should be used for animations, 
 * TOP_LEFT_PANEL_RECT for copies etc. but mind the overlap
 */
static const BG_POINT TOP_LEFT_BLIND_TITLE_POINT = {0,  21, };
static const Rect BIG_BLIND_TITLE_SRC_RECT  = {0,       26,     8,      26 };
static const Rect BOSS_BLIND_TITLE_SRC_RECT = {0,       27,     8,      27 };

// Rects for TTE (in pixels)
static const Rect HAND_SIZE_RECT            = {128,     128,    152,    160 }; // Seems to include both SELECT and PLAYING
static const Rect HAND_SIZE_RECT_SELECT     = {128,     128,    152,    136 };
static const Rect HAND_SIZE_RECT_PLAYING    = {128,     152,    152,    160 };
static const Rect HAND_TYPE_RECT            = {8,       64,     64,     72  };
// Score displayed in the same place as the hand type
static const Rect TEMP_SCORE_RECT           = {8,       64,     64,     72  }; 
static const Rect SCORE_RECT                = {32,      48,     64,     56  };

static const Rect PLAYED_CARDS_SCORES_RECT  = {72,      48,     240,    56  };
static const Rect BLIND_TOKEN_TEXT_RECT     = {80,      72,     200,    160 };
static const Rect MONEY_TEXT_RECT           = {8,       120,    64,     128 };
static const Rect CHIPS_TEXT_RECT           = {8,       80,     32,     88  };
static const Rect MULT_TEXT_RECT            = {40,      80,     64,     88  };
static const Rect BLIND_REWARD_RECT         = {40,      32,     64,     40  };
static const Rect BLIND_REQ_TEXT_RECT       = {32,      24,     64,     32  };
static const Rect SHOP_PRICES_TEXT_RECT     = {72,      56,     192,    160 };

// Rects with UNDEFINED are only used in tte_printf, they need to be fully defined
// to be used with tte_erase_rect_wrapper()
static const Rect HANDS_TEXT_RECT           = {16,      104,    UNDEFINED, UNDEFINED };
static const Rect DISCARDS_TEXT_RECT        = {48,      104,    UNDEFINED, UNDEFINED };
static const Rect DECK_SIZE_RECT            = {200,     152,    UNDEFINED, UNDEFINED };
static const Rect ROUND_TEXT_RECT           = {48,      144,    UNDEFINED, UNDEFINED };
static const Rect ANTE_TEXT_RECT            = {8,       144,    UNDEFINED, UNDEFINED };
static const Rect ROUND_END_BLIND_REQ_RECT  = {104,     96,     136,       UNDEFINED };
static const Rect ROUND_END_BLIND_REWARD_RECT = { 168,  96,     UNDEFINED, UNDEFINED };
static const Rect ROUND_END_NUM_HANDS_RECT  = {88,      116,    UNDEFINED, UNDEFINED };
static const Rect HAND_REWARD_RECT          = {168,     UNDEFINED, UNDEFINED, UNDEFINED };
static const Rect CASHOUT_RECT              = {88,      72,     UNDEFINED, UNDEFINED };
static const Rect SHOP_REROLL_RECT          = {88,      96,    UNDEFINED, UNDEFINED };

//TODO: Properly define and use
#define MENU_POP_OUT_ANIM_FRAMES 20
#define SCORED_CARD_TEXT_Y 48

// General functions
void set_seed(int seed)
{
    if (!seeded)
    {
        seeded = true;
        rng_seed = seed;
        srand(rng_seed);
    }
}

void sort_hand_by_suit()
{
    for (int a = 0; a < hand_top; a++)
    {
        for (int b = a + 1; b <= hand_top; b++)
        {
            if (hand[a] == NULL || (hand[b] != NULL && (hand[a]->card->suit > hand[b]->card->suit || (hand[a]->card->suit == hand[b]->card->suit && hand[a]->card->rank > hand[b]->card->rank))))
            {
                CardObject* temp = hand[a];
                hand[a] = hand[b];
                hand[b] = temp;
            }
        }
    }
}

void sort_hand_by_rank()
{
    for (int a = 0; a < hand_top; a++)
    {
        for (int b = a + 1; b <= hand_top; b++)
        {
            if (hand[a] == NULL || (hand[b] != NULL && hand[a]->card->rank > hand[b]->card->rank))
            {
                CardObject* temp = hand[a];
                hand[a] = hand[b];
                hand[b] = temp;
            }
        }
    }
}

void sort_cards()
{
    if (sort_by_suit)
    {
        sort_hand_by_suit();
    }
    else
    {
        sort_hand_by_rank();
    }

    // Update the sprites in the hand by destroying them and creating new ones in the correct order
    // (This is feels like a diabolical solution but like literally how else would you do this)
    for (int i = 0; i <= hand_top; i++)
    {
        if (hand[i] != NULL)
        {
            // card_object_get_sprite() will not work here since we need the address
            sprite_destroy(&(hand[i]->sprite_object->sprite));
        }
    }

    for (int i = 0; i <= hand_top; i++)
    {
        if (hand[i] != NULL)
        {
            //hand[i]->sprite = sprite_new(ATTR0_SQUARE | ATTR0_4BPP | ATTR0_AFF, ATTR1_SIZE_32, card_sprite_lut[hand[i]->card->suit][hand[i]->card->rank], 0, i);
            card_object_set_sprite(hand[i], i); // Set the sprite for the card object
            sprite_position(card_object_get_sprite(hand[i]), fx2int(hand[i]->sprite_object->x), fx2int(hand[i]->sprite_object->y));
        }
    }
}

CardObject **get_hand_array(void) {
    return hand;
}

int get_hand_top(void) {
    return hand_top;
}

CardObject **get_played_array(void) {
    return played;
}

int get_played_top(void) {
    return played_top;
}

enum HandType hand_get_type()
{
    enum HandType res_hand_type = NONE;

    // Idk if this is how Balatro does it but this is how I'm doing it
    if (hand_selections == 0 || hand_state == HAND_DISCARD)
    {
        res_hand_type = NONE;
        return res_hand_type;
    }

    res_hand_type = HIGH_CARD;

    u8 suits[NUM_SUITS];
    u8 ranks[NUM_RANKS];
    get_hand_distribution(ranks, suits);

    // Check for flush
    if (hand_contains_flush(suits))
        res_hand_type = FLUSH;

    // Check for straight
    if (hand_contains_straight(ranks)) {
        if (res_hand_type == FLUSH)
            res_hand_type = STRAIGHT_FLUSH;
        else
            res_hand_type = STRAIGHT;
    }

    // Check for royal flush vs regular straight flush
    if (res_hand_type == STRAIGHT_FLUSH) {
        if (ranks[TEN] && ranks[JACK] && ranks[QUEEN] && ranks[KING] && ranks[ACE])
            return ROYAL_FLUSH;
        return STRAIGHT_FLUSH;
    }

    // The following can be optimized better but not sure how much it matters
    u8 n_of_a_kind = hand_contains_n_of_a_kind(ranks);

    if (n_of_a_kind >= 5) {
        if (res_hand_type == FLUSH) {
            return FLUSH_FIVE;
        }
        return FIVE_OF_A_KIND;
    }

    if (n_of_a_kind == 4) {
        return FOUR_OF_A_KIND;
    }

    if (n_of_a_kind == 3 && hand_contains_full_house(ranks)) {
        return FULL_HOUSE;
    }

    // Flush is more valuable than the remaining hand types, so return now
    if (res_hand_type == FLUSH) {
        return FLUSH;
    }

    if (n_of_a_kind == 3) {
        return THREE_OF_A_KIND;
    }

    if (n_of_a_kind == 2) {
        if (hand_contains_two_pair(ranks)) {
            return TWO_PAIR;
        }
        return PAIR;
    }

    return res_hand_type; // should be HIGH_CARD
}

/* Copies the appropriate item into the top left panel (blind/shop icon)
 * from where it was put outside the screenview
 */
void bg_copy_current_item_to_top_left_panel()
{
    main_bg_se_copy_rect(TOP_LEFT_ITEM_SRC_RECT, TOP_LEFT_PANEL_POINT);
}

void change_background(int id)
{
    if (background == id)
    {
        return;
    }
    else if (id == BG_ID_CARD_SELECTING)
    {
        tte_erase_rect_wrapper(HAND_SIZE_RECT_PLAYING);
        REG_WIN0V = (REG_WIN0V << 8) | 0x80; // Set window 0 top to 128

        if (background == BG_ID_CARD_PLAYING)
        {
            int offset = 11;
            memcpy16(&se_mem[MAIN_BG_SBB][SE_ROW_LEN * offset], &background_gfxMap[SE_ROW_LEN * offset], SE_ROW_LEN * 8);
        }
        else
        {
            REG_DISPCNT |= DCNT_WIN0; // Enable window 0 to make hand background transparent
            // Load the tiles and palette
            // Background
            memcpy(pal_bg_mem, background_gfxPal, 64); // This '64" isn't a specific number, I'm just using it to prevent the text colors from being overridden
            GRIT_CPY(&tile8_mem[MAIN_BG_CBB], background_gfxTiles); // Deadass i have no clue how any of these memory things work but I just messed with them until stuff worked
            GRIT_CPY(&se_mem[MAIN_BG_SBB], background_gfxMap);

            if (current_blind == BIG_BLIND) // Change text and palette depending on blind type
            {
                main_bg_se_copy_rect(BIG_BLIND_TITLE_SRC_RECT, TOP_LEFT_BLIND_TITLE_POINT);
            }
            else if (current_blind == BOSS_BLIND)
            {
                main_bg_se_copy_rect(BOSS_BLIND_TITLE_SRC_RECT, TOP_LEFT_BLIND_TITLE_POINT);
            }

            bg_copy_current_item_to_top_left_panel();

            // This would change the palette of the background to match the blind, but the backgroun doesn't use the blind token's exact colors so a different approach is required
            memset16(&pal_bg_mem[19], blind_get_color(current_blind, BLIND_BACKGROUND_MAIN_COLOR_INDEX), 1);
            memset16(&pal_bg_mem[5], blind_get_color(current_blind, BLIND_BACKGROUND_SECONDARY_COLOR_INDEX), 1);
            memset16(&pal_bg_mem[2], blind_get_color(current_blind, BLIND_BACKGROUND_SHADOW_COLOR_INDEX), 1);

            // Copy the Play Hand and Discard button colors to their selection highlights
            memcpy16(&pal_bg_mem[1], &pal_bg_mem[7], 1);
            memcpy16(&pal_bg_mem[9], &pal_bg_mem[12], 1);
        }
    }
    else if (id == BG_ID_CARD_PLAYING)
    {
        if (background != BG_ID_CARD_SELECTING)
        {
            change_background(BG_ID_CARD_SELECTING);
            background = BG_ID_CARD_PLAYING;
        }

        REG_WIN0V = (REG_WIN0V << 8) | 0xA0; // Set window 0 bottom to 160

        for (int i = 0; i <= 2; i++)
        {
            main_bg_se_move_rect_1_tile_vert(HAND_BG_RECT_SELECTING, SE_DOWN);
        }

        tte_erase_rect_wrapper(HAND_SIZE_RECT_SELECT);
    }
    else if (id == BG_ID_ROUND_END)
    {
        if (background != BG_ID_CARD_SELECTING && background != BG_ID_CARD_PLAYING)
        {
            change_background(BG_ID_CARD_SELECTING);
            background = BG_ID_ROUND_END;
        }

        REG_DISPCNT &= ~DCNT_WIN0; // Disable window 0 so it doesn't make the cashout menu transparent

        main_bg_se_clear_rect(ROUND_END_MENU_RECT);
        tte_erase_rect_wrapper(HAND_SIZE_RECT);
    }
    else if (id == BG_ID_SHOP)
    {
        REG_DISPCNT &= ~DCNT_WIN0;

        memcpy(pal_bg_mem, background_shop_gfxPal, 64);
        GRIT_CPY(&tile_mem[MAIN_BG_CBB], background_shop_gfxTiles);
        GRIT_CPY(&se_mem[MAIN_BG_SBB], background_shop_gfxMap);

        // Set the outline colors for the shop background. This is used for the alternate shop palettes when opening packs
        memset16(&pal_bg_mem[26], 0x213D, 1);
        memset16(&pal_bg_mem[6], 0x10B4, 1);
        
        memset16(&pal_bg_mem[14], 0x32BE, 1); // Reset the shop lights to correct colors
        memset16(&pal_bg_mem[17], 0x4B5F, 1);
        memset16(&pal_bg_mem[22], 0x5F9F, 1);
        memset16(&pal_bg_mem[8], 0xFFFF, 1);

        memcpy16(&pal_bg_mem[7], &pal_bg_mem[3], 1); // Disable the button highlight colors
        memcpy16(&pal_bg_mem[5], &pal_bg_mem[16], 1);
    }
    else if (id == BG_ID_BLIND_SELECT)
    {
        obj_unhide(blind_select_tokens[SMALL_BLIND]->obj, 0);
        obj_unhide(blind_select_tokens[BIG_BLIND]->obj, 0);
        obj_unhide(blind_select_tokens[BOSS_BLIND]->obj, 0);

        const int default_y = 89 + (TILE_SIZE * 12); // Default y position for the blind select tokens. 8 is the size of a tile and 12 is the amound of tiles the background is shifted down by
        sprite_position(blind_select_tokens[SMALL_BLIND], 80, default_y);
        sprite_position(blind_select_tokens[BIG_BLIND], 120, default_y);
        sprite_position(blind_select_tokens[BOSS_BLIND], 160, default_y);

        REG_DISPCNT &= ~DCNT_WIN0;

        memcpy16(pal_bg_mem, background_blind_select_gfxPal, 64);
        GRIT_CPY(&tile_mem[MAIN_BG_CBB], background_blind_select_gfxTiles);
        GRIT_CPY(&se_mem[MAIN_BG_SBB], background_blind_select_gfxMap);

        // Copy boss blind colors to blind select palette
        memset16(&pal_bg_mem[1], blind_get_color(BOSS_BLIND, BLIND_BACKGROUND_MAIN_COLOR_INDEX), 1);
        memset16(&pal_bg_mem[7], blind_get_color(BOSS_BLIND, BLIND_BACKGROUND_SHADOW_COLOR_INDEX), 1);

        // Disable the button highlight colors
        // Select button PID is 15 and the outline is 18
        memcpy16(&pal_bg_mem[18], &pal_bg_mem[15], 1);
        // Skip button PID is 10 and the outline is 5
        memcpy16(&pal_bg_mem[10 ], &pal_bg_mem[5], 1);

        for (int i = 0; i < MAX_BLINDS; i++)
        {
            if (blinds[i] != BLIND_CURRENT && (i == SMALL_BLIND || i == BIG_BLIND)) // Make the skip button gray
            {
                // TODO: Switch all the copies here to use main_bg_se_copy_rect()
                int x_from = 0;
                int y_from = 24 + (i * 4);

                int x_to = 9 + (i * 5);
                int y_to = 29;

                for (int j = 0; j < 3; j++)
                {
                    memcpy16(&se_mem[MAIN_BG_SBB][x_to + 32 * y_to], &se_mem[MAIN_BG_SBB][x_from + 32 * y_from], 5);
                    y_from++;
                    y_to++;
                }
            }

            if (blinds[i] == BLIND_CURRENT) // Raise the blind panel up a bit
            {
                int x_from = 0;
                int y_from = 27;

                Rect blind_rect = SINGLE_BLIND_SELECT_RECT;

                // There's no gap between them
                blind_rect.left += i * rect_width(&SINGLE_BLIND_SELECT_RECT);
                blind_rect.right += i * rect_width(&SINGLE_BLIND_SELECT_RECT);
                main_bg_se_copy_rect_1_tile_vert(blind_rect, SE_UP);

                int x_to = blind_rect.left;
                int y_to = 31;

                if (i == BIG_BLIND)
                {
                    y_from = 31;
                }
                else if (i == BOSS_BLIND)
                {
                    x_from = x_to;
                    y_from = 30;
                }

                // Copy plain tiles onto the bottom of the raised blind panel to fill the gap created by the raise
                Rect gap_fill_rect = {x_from, y_from, x_from + rect_width(&SINGLE_BLIND_SELECT_RECT) - 1, y_from}; // - 1 to stay within rect boundaries
                BG_POINT gap_fill_point = {x_to, y_to};
                main_bg_se_copy_rect(gap_fill_rect, gap_fill_point);

                sprite_position(blind_select_tokens[i], blind_select_tokens[i]->pos.x, blind_select_tokens[i]->pos.y - TILE_SIZE); // Move token up by a tile
            }
            else if (blinds[i] == BLIND_UPCOMING) // Change the select icon to "NEXT" 
            {
                int x_from = 0;
                int y_from = 20;

                int x_to = 10 + (i * 5);
                int y_to = 20;

                memcpy16(&se_mem[MAIN_BG_SBB][x_to + 32 * y_to], &se_mem[MAIN_BG_SBB][x_from + 32 * y_from], 3);
            }
            else if (blinds[i] == BLIND_SKIPPED) // Change the select icon to "SKIP"
            {
                int x_from = 3;
                int y_from = 20;

                int x_to = 10 + (i * 5);
                int y_to = 20;

                memcpy16(&se_mem[MAIN_BG_SBB][x_to + 32 * y_to], &se_mem[MAIN_BG_SBB][x_from + 32 * y_from], 3);
            }
            else if (blinds[i] == BLIND_DEFEATED) // Change the select icon to "DEFEATED"
            {
                int x_from = 6;
                int y_from = 20;

                int x_to = 10 + (i * 5);
                int y_to = 20;

                memcpy16(&se_mem[MAIN_BG_SBB][x_to + 32 * y_to], &se_mem[MAIN_BG_SBB][x_from + 32 * y_from], 3);
            }
        }
    }
    else
    {
        return; // Invalid background ID
    }

    background = id;
}

void display_temp_score(int value)
{
    int x_offset = 40 - get_digits_even(value) * TILE_SIZE;
    tte_erase_rect_wrapper(TEMP_SCORE_RECT);
    tte_printf("#{P:%d,%d; cx:0xF000}%d", x_offset, TEMP_SCORE_RECT.top, value);
}

void display_score(int value)
{
    // Clear the existing text before redrawing
    tte_erase_rect_wrapper(SCORE_RECT);
    
    char score_suffix = ' ';
    int display_value = value;
    
    if(value >= 10000)
    {
        score_suffix = 'k';
        display_value = value / 1000; // 12,986 = 12k
    }
    
    // Calculate text width: digits + suffix character (if 'k')
    int num_digits = get_digits(display_value);
    int text_width = num_digits * TILE_SIZE;
    if(score_suffix == 'k')
    {
        text_width += TILE_SIZE; // Add width for 'k' suffix
    }
    
    // Calculate center position within SCORE_RECT
    int rect_width = SCORE_RECT.right - SCORE_RECT.left;
    int x_offset = SCORE_RECT.left + (rect_width - text_width) / 2;
    
    tte_printf("#{P:%d,48; cx:0xF000}%d%c", x_offset, display_value, score_suffix);
}

void display_money(int value)
{
    int x_offset = 32 - get_digits_odd(value) * TILE_SIZE;
    tte_erase_rect_wrapper(MONEY_TEXT_RECT);
    tte_printf("#{P:%d,%d; cx:0xC000}$%d", x_offset, MONEY_TEXT_RECT.top, value);
}

void display_chips(int value)
{
    Rect chips_text_rect = CHIPS_TEXT_RECT;
    tte_erase_rect_wrapper(CHIPS_TEXT_RECT);
    update_text_rect_to_right_align_num(&chips_text_rect, value, OVERFLOW_LEFT);
    tte_printf("#{P:%d,%d; cx:0xF000;}%d", chips_text_rect.left, chips_text_rect.top, value);
}

void display_mult(int value)
{
    tte_erase_rect_wrapper(MULT_TEXT_RECT);
    tte_printf("#{P:%d,%d; cx:0xF000;}%d", MULT_TEXT_RECT.left, MULT_TEXT_RECT.top, value); // Mult
}

void display_round(int value)
{
    //tte_erase_rect_wrapper(ROUND_TEXT_RECT);
    tte_printf("#{P:%d,%d; cx:0xC000}%d", ROUND_TEXT_RECT.left, ROUND_TEXT_RECT.top, round);
}

void display_ante(int value)
{
    tte_printf("#{P:%d,%d; cx:0xC000}%d#{cx:0xF000}/%d", ANTE_TEXT_RECT.left, ANTE_TEXT_RECT.top, value, MAX_ANTE);
}

void display_hands(int value)
{
    //tte_erase_rect_wrapper(HANDS_TEXT_RECT);
    tte_printf("#{P:%d,%d; cx:0xD000}%d", HANDS_TEXT_RECT.left, HANDS_TEXT_RECT.top, hands); // Hand
}

void display_discards(int value)
{
    //tte_erase_rect_wrapper(DISCARDS_TEXT_RECT);
    tte_printf("#{P:%d,%d; cx:0xE000}%d", DISCARDS_TEXT_RECT.left, DISCARDS_TEXT_RECT.top, discards); // Discard
}

static void print_hand_type(const char* hand_type_str)
{
    if (hand_type_str == NULL)
        return; // NULL-checking paranoia
    tte_printf("#{P:%d,%d; cx:0xF000}%s", HAND_TYPE_RECT.left, HAND_TYPE_RECT.top, hand_type_str);
}

void set_hand()
{
    tte_erase_rect_wrapper(HAND_TYPE_RECT);
    hand_type = hand_get_type();
    switch (hand_type)
    {
    case HIGH_CARD:
        print_hand_type("HIGH C");
        chips = 5;
        mult = 1;
        break;
    case PAIR:
        print_hand_type("PAIR");
        chips = 10;
        mult = 2;
        break;
    case TWO_PAIR:
        print_hand_type("2 PAIR");
        chips = 20;
        mult = 2;
        break;
    case THREE_OF_A_KIND:
        print_hand_type("3 OAK");
        chips = 30;
        mult = 3;
        break;
    case STRAIGHT:
        print_hand_type("STRT");
        chips = 30;
        mult = 4;
        break;
    case FLUSH:
        print_hand_type("FLUSH");
        chips = 35;
        mult = 4;
        break;
    case FULL_HOUSE:
        print_hand_type("FULL H");
        chips = 40;
        mult = 4;
        break;
    case FOUR_OF_A_KIND:
        print_hand_type("4 OAK");
        chips = 60;
        mult = 7;
        break;
    case STRAIGHT_FLUSH:
        print_hand_type("STRT F");
        chips = 100;
        mult = 8;
        break;
    case ROYAL_FLUSH:
        print_hand_type("ROYAL F");
        chips = 100;
        mult = 8;
        break;
    case FIVE_OF_A_KIND:
        print_hand_type("5 OAK");
        chips = 120;
        mult = 12;
        break;
    case FLUSH_HOUSE:
        print_hand_type("FLUSH H");
        chips = 140;
        mult = 14;
        break;
    case FLUSH_FIVE:
        print_hand_type("FLUSH 5");
        chips = 160;
        mult = 16;
        break;
    case NONE:
        chips = 0;
        mult = 0;
        break;
    }

    display_chips(chips);
    display_mult(mult);
}

void card_draw()
{
    if (deck_top < 0 || hand_top >= hand_size - 1 || hand_top >= MAX_HAND_SIZE - 1) return;

    CardObject *card_object = card_object_new(deck_pop());

    const FIXED deck_x = int2fx(208);
    const FIXED deck_y = int2fx(110);

    card_object->sprite_object->x = deck_x;
    card_object->sprite_object->y = deck_y;

    hand[++hand_top] = card_object;

    // Sort the hand after drawing a card
    sort_cards();

    const int pitch_lut[MAX_HAND_SIZE] = {1024, 1048, 1072, 1096, 1120, 1144, 1168, 1192, 1216, 1240, 1264, 1288, 1312, 1336, 1360, 1384};
    mm_sound_effect sfx_draw = {{SFX_CARD_DRAW}, pitch_lut[cards_drawn], 0, 255, 128,};
    mmEffectEx(&sfx_draw);
}

void hand_set_focus(int index)
{
    if (index < 0 || index > hand_top || hand_state != HAND_SELECT) return;
    selection_x = index;

    mm_sound_effect sfx_focus = {{SFX_CARD_FOCUS}, 1024 + rand() % 512, 0, 255, 128,};
    mmEffectEx(&sfx_focus);
}

void hand_select()
{
    if (hand_state != HAND_SELECT || hand[selection_x] == NULL) return;

    if (card_object_is_selected(hand[selection_x]))
    {
        card_object_set_selected(hand[selection_x], false);
        hand_selections--;

        mm_sound_effect sfx_select = {{SFX_CARD_SELECT}, 1024, 0, 255, 128,};
        mmEffectEx(&sfx_select);
    }
    else if (hand_selections < MAX_SELECTION_SIZE)
    {
        card_object_set_selected(hand[selection_x], true);
        hand_selections++;

        mm_sound_effect sfx_deselect = {{SFX_CARD_DESELECT}, 1024, 0, 255, 128,};
        mmEffectEx(&sfx_deselect);
    }
}

void hand_change_sort()
{
    sort_by_suit = !sort_by_suit;
    sort_cards();
}

int hand_get_size()
{
    return hand_top + 1;
}

int hand_get_max_size()
{
    return hand_size;
}

bool hand_discard()
{
    if (hand_state != HAND_SELECT || hand_selections == 0) return false;
    return true;
}

bool hand_play()
{
    if (hand_state != HAND_SELECT || hand_selections == 0) return false;
    return true;
}

int deck_get_size()
{
    return deck_top + 1;
}

int deck_get_max_size()
{
    return hand_top + played_top + deck_top + discard_top + 4; // This is the max amount of cards that the player currently has in their possession
}

void deck_shuffle()
{
    for (int i = deck_top; i > 0; i--) 
    {
        int j = rand() % (i + 1);
        Card *temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

void increment_blind(enum BlindState increment_reason)
{
    current_blind++;
    if (current_blind >= MAX_BLINDS)
    {
        current_blind = 0;
        blinds[0] = BLIND_CURRENT; // Reset the blinds to the first one
        blinds[1] = BLIND_UPCOMING; // Set the next blind to upcoming
        blinds[2] = BLIND_UPCOMING; // Set the next blind to upcoming
    }
    else
    {
        blinds[current_blind] = BLIND_CURRENT;
        blinds[current_blind - 1] = increment_reason; 
    }
}

void game_round_init()
{
    // once we have a main menu, move the seed set there. this is only here now because there isn't a way to set the seed to a psudorandom value unless there is something to wait on
    set_seed(rng_seed);

    hand_state = HAND_DRAW;
    cards_drawn = 0;
    hand_selections = 0;

    playing_blind_token = blind_token_new(current_blind, 8, 18, MAX_SELECTION_SIZE + MAX_HAND_SIZE + 1); // Create the blind token sprite at the top left corner
    // TODO: Hide blind token and display it after sliding blind rect animation
    //if (playing_blind_token != NULL)
    //{
    //    obj_hide(playing_blind_token->obj); // Hide the blind token sprite for now
    //}
    round_end_blind_token = blind_token_new(current_blind, 81, 86, MAX_SELECTION_SIZE + MAX_HAND_SIZE + 2); // Create the blind token sprite for round end

    if (round_end_blind_token != NULL)
    {
        obj_hide(round_end_blind_token->obj); // Hide the blind token sprite for now
    }

    Rect blind_req_text_rect = BLIND_REQ_TEXT_RECT;
    int blind_requirement = blind_get_requirement(current_blind, ante);
    
    char score_suffix = ' ';
    if(blind_requirement >= 10000)
    {
        // clear existing text
        tte_erase_rect_wrapper(blind_req_text_rect);
        
        score_suffix = 'k';
        blind_requirement /= 1000; // 11,000 = 11k
    }
    
    // Update text rect for right alignment AFTER shortening the number
    update_text_rect_to_right_align_num(&blind_req_text_rect, blind_requirement, OVERFLOW_RIGHT);
    
    // If we added a suffix, adjust position to account for the extra character
    if(score_suffix == 'k')
    {
        blind_req_text_rect.left -= TILE_SIZE; // Move left by one character width to make room for 'k'
    }

    tte_printf("#{P:%d,%d; cx:0xE000}%d%c", blind_req_text_rect.left, blind_req_text_rect.top, blind_requirement, score_suffix); // Blind requirement
    tte_printf("#{P:%d,%d; cx:0xC000}$%d", BLIND_REWARD_RECT.left, BLIND_REWARD_RECT.top, blind_get_reward(current_blind)); // Blind reward

    deck_shuffle(); // Shuffle the deck at the start of the round
}

void init_game_state(enum GameState game_state_to_init)
{
    // Switch written out, add init for states as needed
    switch (game_state_to_init)
    {
    case GAME_PLAYING:
        game_round_init();
        break;
    case GAME_ROUND_END:
        break;
    case GAME_SHOP:
        break;
    case GAME_BLIND_SELECT:
        break;
    case GAME_LOSE:
        break;
    default:
        break;
    }
}

// Game functions
void game_set_state(enum GameState new_game_state)
{
    timer = 0; // Reset the timer
    init_game_state(new_game_state);
    game_state = new_game_state;
}

void game_init()
{
    hands = max_hands;
    discards = max_discards;

    blind_select_tokens[SMALL_BLIND] = blind_token_new(SMALL_BLIND, 8, 18, MAX_SELECTION_SIZE + MAX_HAND_SIZE + 3);
    blind_select_tokens[BIG_BLIND] = blind_token_new(BIG_BLIND, 8, 18, MAX_SELECTION_SIZE + MAX_HAND_SIZE + 4);
    blind_select_tokens[BOSS_BLIND] = blind_token_new(BOSS_BLIND, 8, 18, MAX_SELECTION_SIZE + MAX_HAND_SIZE + 5);

    obj_hide(blind_select_tokens[SMALL_BLIND]->obj);
    obj_hide(blind_select_tokens[BIG_BLIND]->obj);
    obj_hide(blind_select_tokens[BOSS_BLIND]->obj);

    // Fill the deck with all the cards. Later on this can be replaced with a more dynamic system that allows for different decks and card types.
    for (int suit = 0; suit < NUM_SUITS; suit++)
    {
        for (int rank = 0; rank < NUM_RANKS; rank++)
        {
            Card *card = card_new(suit, rank);
            deck_push(card);
        }
    }

    change_background(BG_ID_BLIND_SELECT);

    tte_printf("#{P:%d,%d; cx:0xF000}%d/%d", DECK_SIZE_RECT.left, DECK_SIZE_RECT.top, deck_get_size(), deck_get_max_size()); // Deck size/max size
    
    display_round(round); // Set the round display
    display_score(score); // Set the score display

    display_chips(chips); // Set the chips display
    display_mult(mult); // Set the multiplier display

    display_hands(hands); // Hand
    display_discards(discards); // Discard

    //tte_printf("#{P:24,120; cx:0xC000}$%d", money); // Money
    display_money(money); // Set the money display

    tte_printf("#{P:%d,%d; cx:0xC000}%d#{cx:0xF000}/%d", ANTE_TEXT_RECT.left, ANTE_TEXT_RECT.top, ante, MAX_ANTE); // Ante
}

static void game_playing_process_hand_select_input()
{
    static bool discard_button_highlighted = false; // true = play button highlighted, false = discard button highlighted

    if (key_hit(KEY_LEFT))
    {
        if (selection_y == 0)
        {
            hand_set_focus(selection_x + 1); // The reason why this adds 1 is because the hand is drawn from right to left. There is no particular reason for this, it's just how I did it.
        }
        else
        {
            discard_button_highlighted = false; // Play button
        }
    }
    else if (key_hit(KEY_RIGHT))
    {
        if (selection_y == 0)
        {
            hand_set_focus(selection_x - 1);
        }
        else
        {
            discard_button_highlighted = true; // Discard button
        }
    }
    else if (key_hit(KEY_UP) && selection_y != 0)
    {
        selection_y = 0;
    }
    else if (key_hit(KEY_DOWN) && selection_y != 1)
    {
        selection_y = 1;

        if (selection_x > hand_top / 2)
        {
            discard_button_highlighted = false; // Play button
        }
        else
        {
            discard_button_highlighted = true; // Discard button
        }
    }

    if (selection_y == 1) // On row of play/discard buttons
    {
        if (discard_button_highlighted == false) // Play button logic
        {
            memset16(&pal_bg_mem[1], 0xFFFF, 1);
            memcpy16(&pal_bg_mem[9], &pal_bg_mem[12], 1);

            if (key_hit(SELECT_CARD) && hands > 0 && hand_play())
            {
                hand_state = HAND_PLAY;
                selection_x = 0;
                selection_y = 0;
                display_hands(--hands);
            }
        }
        else // Discard button logic
        {
            memcpy16(&pal_bg_mem[1], &pal_bg_mem[7], 1);
            memset16(&pal_bg_mem[9], 0xFFFF, 1);

            if (key_hit(SELECT_CARD) && discards > 0 && hand_discard())
            {
                hand_state = HAND_DISCARD;
                selection_x = 0;
                selection_y = 0;
                display_hands(--discards);
                set_hand();
                tte_printf("#{P:%d,%d; cx:0xE000}%d", DISCARDS_TEXT_RECT.left, DISCARDS_TEXT_RECT.top, discards);
            }
        }
    }

    if (selection_y == 0) // On row of cards
    {
        memcpy16(&pal_bg_mem[1], &pal_bg_mem[7], 1); // Play button highlight color
        memcpy16(&pal_bg_mem[9], &pal_bg_mem[12], 1); // Discard button highlight color
        
        if (key_hit(SELECT_CARD))
        {
            hand_select();
            set_hand();
        }
    }

    if (key_hit(SORT_HAND))
    {
        hand_change_sort();
    }
}

static void game_playing_process_input_and_state()
{
    if (hand_state == HAND_SELECT)
    {
        game_playing_process_hand_select_input();
    }
    else if (play_state == PLAY_ENDING)
    {
        if (mult > 0)
        {
            temp_score = chips * mult;
            lerped_temp_score = int2fx(temp_score);
            lerped_score = int2fx(score);

            display_temp_score(temp_score);

            chips = 0;
            mult = 0;
            display_mult(mult);
            display_chips(chips);
        }
    }
    else if (play_state == PLAY_ENDED)
    {
        lerped_temp_score -= int2fx(temp_score) / 40;
        lerped_score += int2fx(temp_score) / 40;

        if (lerped_temp_score > 0)
        {
            display_temp_score(fx2int(lerped_temp_score));

            // We actually don't need to erase this because the score only increases
            display_score(fx2int(lerped_score)); // Set the score display

            if (temp_score <= 0)
            {
                tte_erase_rect_wrapper(TEMP_SCORE_RECT);
            }
        }
        else
        {
            score += temp_score;
            temp_score = 0;
            lerped_temp_score = 0;
            lerped_score = 0;

            tte_erase_rect_wrapper(TEMP_SCORE_RECT); // Just erase the temp score

            display_score(score);
        }
    }
}

static void game_playing_process_card_draw()
{
    if (hand_state == HAND_DRAW && cards_drawn < hand_size)
    {
        if (timer % FRAMES(10) == 0) // Draw a card every 10 frames
        {
            cards_drawn++;
            card_draw();
        }
    }
    else if (hand_state == HAND_DRAW)
    {
        hand_state = HAND_SELECT; // Change the hand state to select after drawing all the cards
        cards_drawn = 0;
        timer = 0;
    }
}

static void game_playing_discarded_cards_loop()
{
    // Discarded cards loop (mainly for shuffling)
    if (hand_get_size() == 0 && hand_state == HAND_SHUFFLING && discard_top >= -1 && timer > FRAMES(10))
    {
        change_background(BG_ID_ROUND_END); // Change the background to the round end background. This is how it works in Balatro, so I'm doing it this way too.

        // We take each discarded card and put it back into the deck with a short animation
        static CardObject* discarded_card_object = NULL;
        if (discarded_card_object == NULL)
        {
            discarded_card_object = card_object_new(discard_pop());
            //discarded_card_object->sprite = sprite_new(ATTR0_SQUARE | ATTR0_4BPP | ATTR0_AFF, ATTR1_SIZE_32, card_sprite_lut[discarded_card_object->card->suit][discarded_card_object->card->rank], 0, 0);
            card_object_set_sprite(discarded_card_object, 0); // Set the sprite for the discarded card object
            sprite_object_reset_transform(discarded_card_object->sprite_object);

            discarded_card_object->sprite_object->tx = int2fx(204);
            discarded_card_object->sprite_object->ty = int2fx(112);
            discarded_card_object->sprite_object->x = int2fx(240);
            discarded_card_object->sprite_object->y = int2fx(80);

            card_object_update(discarded_card_object);
        }
        else
        {
            card_object_update(discarded_card_object);

            if (discarded_card_object->sprite_object->y >= discarded_card_object->sprite_object->ty)
            {
                deck_push(discarded_card_object->card); // Put the card back into the deck
                card_object_destroy(&discarded_card_object);

                // play draw sound
                const int pitch_lut[MAX_HAND_SIZE] = { 1024, 1048, 1072, 1096, 1120, 1144, 1168, 1192, 1216, 1240, 1264, 1288, 1312, 1336, 1360, 1384 };
                mm_sound_effect sfx_draw = { {SFX_CARD_DRAW}, pitch_lut[2], 0, 255, 128, };
                mmEffectEx(&sfx_draw);
            }
        }

        if (discard_top == -1 && discarded_card_object == NULL) // If there are no more discarded cards, stop shuffling
        {
            game_set_state(GAME_ROUND_END); // Set the game state back to playing
        }
    }
}

static void cards_in_hand_update_loop(bool* discarded_card, int* played_selections, bool* sound_played)
{
    for (int i = hand_top + 1; i >= 0; i--) // Start from the end of the hand and work backwards because that's how Balatro does it
    {
        if (hand[i] != NULL)
        {
            const int spacing_lut[MAX_HAND_SIZE] = { 28, 28, 28, 28, 27, 21, 18, 15, 13, 12, 10, 9, 9, 8, 8, 7 }; // This is a stupid way to do this but I don't care

            FIXED hand_x = int2fx(120);
            FIXED hand_y = int2fx(90);

            switch (hand_state)
            {
            case HAND_DRAW:
                hand_x = hand_x + (int2fx(i) - int2fx(hand_top) / 2) * -spacing_lut[hand_top];
                break;
            case HAND_SELECT:
                bool is_focused = (i == selection_x && selection_y == 0);

                if (is_focused && !card_object_is_selected(hand[i]))
                {
                    hand_y -= int2fx(10);
                }
                else if (!is_focused && card_object_is_selected(hand[i]))
                {
                    hand_y -= int2fx(15);
                }
                else if (is_focused && card_object_is_selected(hand[i]))
                {
                    hand_y -= int2fx(20);
                }

                if (i != selection_x && hand[i]->sprite_object->y > hand_y)
                {
                    hand[i]->sprite_object->y = hand_y;
                    hand[i]->sprite_object->vy = 0;
                }

                hand_x = hand_x + (int2fx(i) - int2fx(hand_top) / 2) * -spacing_lut[hand_top]; // TODO: Change this later to reference a 2D LUT of positions
                break;
            case HAND_SHUFFLING:
                /* FALL THROUGH */
            case HAND_DISCARD: // TODO: Add sound
                if (card_object_is_selected(hand[i]) || hand_state == HAND_SHUFFLING)
                {
                    if (!*discarded_card)
                    {
                        hand_x = int2fx(240);
                        hand_y = int2fx(70);

                        if (!*sound_played)
                        {
                            const int pitch_lut[MAX_SELECTION_SIZE] = { 1024, 960, 896, 832, 768 };
                            mm_sound_effect sfx_draw = { {SFX_CARD_DRAW}, pitch_lut[cards_drawn], 0, 255, 128, };
                            mmEffectEx(&sfx_draw);
                            *sound_played = true;
                        }

                        if (hand[i]->sprite_object->x >= hand_x)
                        {
                            discard_push(hand[i]->card);
                            card_object_destroy(&hand[i]);
                            sort_cards();

                            hand_top--;
                            cards_drawn++; // This technically isn't drawing cards, I'm just reusing the variable
                            *sound_played = false;
                            timer = 0;

                            hand_y = hand[i]->sprite_object->y;
                            hand_x = hand[i]->sprite_object->x;
                        }

                        *discarded_card = true;
                    }
                    else
                    {
                        if (hand_state == HAND_DISCARD)
                        {
                            hand_y -= int2fx(15); // Don't raise the card if we're mass discarding, it looks stupid.
                        }
                        else
                        {
                            hand_y += int2fx(24);
                        }
                        hand_x = hand_x + (int2fx(i) - int2fx(hand_top) / 2) * -spacing_lut[hand_top];
                    }
                }
                else
                {
                    hand_x = hand_x + (int2fx(i) - int2fx(hand_top) / 2) * -spacing_lut[hand_top];
                }

                if (i == 0 && *discarded_card == false && timer % FRAMES(10) == 0)
                {
                    hand_state = HAND_DRAW;
                    *sound_played = false;
                    cards_drawn = 0;
                    hand_selections = 0;
                    timer = 0;
                    break;
                }

                break;
            case HAND_PLAY:
                hand_x = hand_x + (int2fx(i) - int2fx(hand_top) / 2) * -spacing_lut[hand_top];
                hand_y += int2fx(24);

                if (card_object_is_selected(hand[i]) && *discarded_card == false && timer % FRAMES(10) == 0)
                {
                    card_object_set_selected(hand[i], false);
                    played_push(hand[i]);
                    sprite_destroy(&hand[i]->sprite_object->sprite);
                    hand[i] = NULL;
                    sort_cards();

                    const int pitch_lut[MAX_SELECTION_SIZE] = { 1024, 960, 896, 832, 768 };
                    mm_sound_effect sfx_draw = { {SFX_CARD_DRAW}, pitch_lut[cards_drawn], 0, 255, 128, };
                    mmEffectEx(&sfx_draw);

                    hand_top--;
                    hand_selections--;
                    cards_drawn++;

                    *discarded_card = true;
                }

                if (i == 0 && *discarded_card == false && timer % FRAMES(10) == 0)
                {
                    hand_state = HAND_PLAYING;
                    cards_drawn = 0;
                    hand_selections = 0;
                    timer = 0;
                    *played_selections = played_top + 1;

                    switch (hand_type) // select the cards that apply to the hand type
                    {
                    case NONE:
                        break;
                    case HIGH_CARD: // find the card with the highest rank in the hand
                        int highest_rank_index = 0;

                        for (int i = 0; i <= played_top; i++)
                        {
                            if (played[i]->card->rank > played[highest_rank_index]->card->rank)
                            {
                                highest_rank_index = i;
                            }
                        }

                        card_object_set_selected(played[highest_rank_index], true);
                        break;
                    case PAIR: // find two cards with the same rank (Requires recursion)
                        for (int i = 0; i <= played_top - 1; i++)
                        {
                            for (int j = i + 1; j <= played_top; j++)
                            {
                                if (played[i]->card->rank == played[j]->card->rank)
                                {
                                    card_object_set_selected(played[i], true);
                                    card_object_set_selected(played[j], true);
                                    break;
                                }
                            }

                            if (card_object_is_selected(played[i])) break;
                        }
                        break;
                    case TWO_PAIR: // find two pairs of cards with the same rank (Requires recursion)
                        int i;

                        for (i = 0; i <= played_top - 1; i++)
                        {
                            for (int j = i + 1; j <= played_top; j++)
                            {
                                if (played[i]->card->rank == played[j]->card->rank)
                                {
                                    card_object_set_selected(played[i], true);
                                    card_object_set_selected(played[j], true);

                                    break;
                                }
                            }

                            if (card_object_is_selected(played[i])) break;
                        }

                        for (; i <= played_top - 1; i++) // Find second pair
                        {
                            for (int j = i + 1; j <= played_top; j++)
                            {
                                if (played[i]->card->rank == played[j]->card->rank && !card_object_is_selected(played[i]) && !card_object_is_selected(played[j]))
                                {
                                    card_object_set_selected(played[i], true);
                                    card_object_set_selected(played[j], true);
                                    break;
                                }
                            }
                        }
                        break;
                    case THREE_OF_A_KIND: // find three cards with the same rank (requires recursion)
                        for (int i = 0; i <= played_top - 1; i++)
                        {
                            for (int j = i + 1; j <= played_top; j++)
                            {
                                if (played[i]->card->rank == played[j]->card->rank)
                                {
                                    card_object_set_selected(played[i], true);
                                    card_object_set_selected(played[j], true);

                                    for (int k = j + 1; k <= played_top; k++)
                                    {
                                        if (played[i]->card->rank == played[k]->card->rank && !card_object_is_selected(played[k]))
                                        {
                                            card_object_set_selected(played[k], true);
                                            break;
                                        }
                                    }

                                    break;
                                }
                            }

                            if (card_object_is_selected(played[i])) break;
                        }
                        break;
                    case FOUR_OF_A_KIND: // find four cards with the same rank (requires recursion)
                        if (played_top >= 3) // If there are 5 cards selected we just need to find the one card that doesn't match, and select the others
                        {
                            int unmatched_index = -1;

                            for (int i = 0; i <= played_top; i++)
                            {
                                if (played[i]->card->rank != played[(i + 1) % played_top]->card->rank && played[i]->card->rank != played[(i + 2) % played_top]->card->rank)
                                {
                                    unmatched_index = i;
                                    break;
                                }
                            }

                            for (int i = 0; i <= played_top; i++)
                            {
                                if (i != unmatched_index)
                                {
                                    card_object_set_selected(played[i], true);
                                }
                            }
                        }
                        else // If there are only 4 cards selected we know they match
                        {
                            for (int i = 0; i <= played_top; i++)
                            {
                                card_object_set_selected(played[i], true);
                            }
                        }
                        break;
                    case STRAIGHT:
                        /* FALL THROUGH */
                    case FLUSH:
                        /* FALL THROUGH */
                    case FULL_HOUSE:
                        /* FALL THROUGH */
                    case STRAIGHT_FLUSH:
                        /* FALL THROUGH */
                    case ROYAL_FLUSH:
                        /* FALL THROUGH */
                    case FIVE_OF_A_KIND:
                        /* FALL THROUGH */
                    case FLUSH_HOUSE:
                        /* FALL THROUGH */
                    case FLUSH_FIVE: // Select all played cards in the hand (This is functionally identical as the above hand types)
                        for (int i = 0; i <= played_top; i++)
                        {
                            card_object_set_selected(played[i], true);
                        }
                        break;
                    }
                }

                break;
            case HAND_PLAYING: // Don't need to do anything here, just wait for the player to select cards
                hand_x = hand_x + (int2fx(i) - int2fx(hand_top) / 2) * -spacing_lut[hand_top];
                hand_y += int2fx(24);
                break;
            }

            hand[i]->sprite_object->tx = hand_x;
            hand[i]->sprite_object->ty = hand_y;
            card_object_update(hand[i]);
        }
    }
}

static void played_cards_update_loop(bool* discarded_card, int* played_selections, bool* sound_played)
{
    // So this one is a bit fucking weird because I have to work kinda backwards for everything because of the order of the pushed cards from the hand to the play stack
    // (also crazy that the company that published Balatro is called "Playstack" and this is a play stack, but I digress)
    for (int i = 0; i <= played_top; i++)
    {
        if (played[i] != NULL)
        {
            if (card_object_get_sprite(played[i]) == NULL)
            {
                //played[i]->sprite = sprite_new(ATTR0_SQUARE | ATTR0_4BPP | ATTR0_AFF, ATTR1_SIZE_32, card_sprite_lut[played[i]->card->suit][played[i]->card->rank], 0, i + MAX_HAND_SIZE);
                card_object_set_sprite(played[i], i + MAX_HAND_SIZE); // Set the sprite for the played card object
            }

            FIXED played_x = int2fx(120);
            FIXED played_y = int2fx(70);
            FIXED played_scale = float2fx(1.0f);

            played_x = played_x + (int2fx(played_top - i) - int2fx(played_top) / 2) * -27;

            switch (play_state)
            {
                case PLAY_PLAYING:
                    if (i == 0 && (timer % FRAMES(10) == 0 || !card_object_is_selected(played[played_top - *played_selections])) && timer > FRAMES(40))
                    {
                        (*played_selections)--;

                        if (*played_selections == 0)
                        {
                            play_state = PLAY_SCORING;
                            timer = 0;
                        }
                    }

                    if (card_object_is_selected(played[i]) && played_top - i >= *played_selections)
                    {
                        played_y -= int2fx(10);
                    }
                    break;
                case PLAY_SCORING:
                    if (i == 0 && (timer % FRAMES(30) == 0) && timer > FRAMES(40))
                    {
                        // So pretend "played_selections" is now called "scored_cards" and it counts the number of cards that have been scored
                        int scored_cards = 0;
                        for (int j = 0; j <= played_top; j++)
                        {
                            tte_erase_rect_wrapper(PLAYED_CARDS_SCORES_RECT);

                            if (*played_selections > 0)
                            {
                                for (int k = 0; k <= jokers_top; k++)
                                {
                                    if (joker_object_score(jokers[k], played[*played_selections - 1]->card, &chips, &mult, NULL, &money, NULL)) // NULLs aren't implemented yet
                                    {
                                        display_chips(chips);
                                        display_mult(mult);
                                        display_money(money);

                                        return; 
                                    }
                                }
                            }

                            if (card_object_is_selected(played[j]))
                            {
                                scored_cards = j + 1; // Count the number of cards that have been scored
                                if (scored_cards > *played_selections)
                                {
                                    for (int k = 0; k <= jokers_top; k++)
                                    {
                                        if (jokers[k] != NULL)
                                        {
                                            jokers[k]->joker->processed = false; // Reset the joker's processed state for the next score
                                        }
                                    }

                                    tte_set_pos(fx2int(played[j]->sprite_object->x) + 8, SCORED_CARD_TEXT_Y); // Offset of 16 pixels to center the text on the card
                                    tte_set_special(0xD000); // Set text color to blue from background memory

                                    // Write the score to a character buffer variable
                                    char score_buffer[5]; // Assuming the maximum score is 99, we need 4 characters (2 digits + null terminator)
                                    snprintf(score_buffer, sizeof(score_buffer), "+%d", card_get_value(played[j]->card));
                                    tte_write(score_buffer);

                                    *played_selections = scored_cards;
                                    card_object_shake(played[j], SFX_CARD_SELECT);

                                    // Relocated card scoring logic here
                                    chips += card_get_value(played[j]->card);
                                    display_chips(chips);

                                    break;
                                }
                            }

                            if (j == played_top && scored_cards == *played_selections) // Check if it's the last card 
                            {
                                tte_erase_rect_wrapper(PLAYED_CARDS_SCORES_RECT);

                                for (int k = 0; k <= jokers_top; k++) // Independent joker scoring loop
                                {
                                    if (joker_object_score(jokers[k], NULL, &chips, &mult, NULL, &money, NULL)) // NULLs aren't implemented yet
                                    {
                                        display_chips(chips);
                                        display_mult(mult);
                                        display_money(money);

                                        return; // Returning was just the easiest way to break out of the loop
                                    }
                                }

                                for (int k = 0; k <= jokers_top; k++)
                                {
                                    if (jokers[k] != NULL)
                                    {
                                        jokers[k]->joker->processed = false; // Reset the joker's processed state for the next round
                                    }
                                }

                                play_state = PLAY_ENDING;
                                timer = 0;
                                *played_selections = played_top + 1; // Reset the played selections to the top of the played stack
                                break;
                            }
                        }
                    }

                    if (card_object_is_selected(played[i]))
                    {
                        played_y -= int2fx(10);
                    }
                    break;
                case PLAY_ENDING: // This is the reverse of PLAY_PLAYING. The cards get reset back to their neutral position sequentially
                    if (i == 0 && (timer % FRAMES(10) == 0 || !card_object_is_selected(played[played_top - *played_selections])) && timer > FRAMES(40))
                    {
                        (*played_selections)--;

                        if (*played_selections == 0)
                        {
                            play_state = PLAY_ENDED;
                            timer = 0;
                        }
                    }

                    if (card_object_is_selected(played[i]) && played_top - i <= *played_selections - 1)
                    {
                        played_y -= int2fx(10);
                    }
                    break;
                case PLAY_ENDED: // Basically a copy of HAND_DISCARD
                    if (!*discarded_card && played[i] != NULL && timer > FRAMES(40))
                    {
                        played_x = int2fx(240);
                        played_y = int2fx(70);

                        if (!*sound_played)
                        {
                            const int pitch_lut[MAX_SELECTION_SIZE] = {1024, 960, 896, 832, 768};
                            mm_sound_effect sfx_draw = {{SFX_CARD_DRAW}, pitch_lut[cards_drawn], 0, 255, 128,};
                            mmEffectEx(&sfx_draw);
                            *sound_played = true;
                        }

                        if (played[i]->sprite_object->x >= played_x)
                        {
                            discard_push(played[i]->card); // Push the card to the discard pile
                            card_object_destroy(&played[i]);

                            //played_top--; 
                            cards_drawn++; // This technically isn't drawing cards, I'm just reusing the variable
                            *sound_played = false;

                            if (i == played_top)
                            {
                                if (score >= blind_get_requirement(current_blind, ante))
                                {
                                    hand_state = HAND_SHUFFLING;

                                    if (current_blind == BOSS_BLIND)
                                    {
                                        display_ante(++ante);
                                    }
                                }
                                else
                                {
                                    hand_state = HAND_DRAW;
                                }

                                play_state = PLAY_PLAYING;
                                cards_drawn = 0;
                                hand_selections = 0;
                                *played_selections = 0;
                                played_top = -1; // Reset the played stack
                                timer = 0;
                                break; // Break out of the loop to avoid accessing an invalid index
                            }
                        }

                        *discarded_card = true;
                    }

                    break;
            }

            played[i]->sprite_object->tx = played_x;
            played[i]->sprite_object->ty = played_y;
            played[i]->sprite_object->tscale = played_scale;
            card_object_update(played[i]);
        }
    }
}

static void held_jokers_update_loop()
{
    const int spacing_lut[MAX_JOKERS_HELD_SIZE][MAX_JOKERS_HELD_SIZE] = 
    {
        {0, 0, 0, 0, 0},
        {13, -13, 0, 0, 0},
        {26, 0, -26, 0, 0},
        {39, 13, -13, -39, 0},
        {40, 20, 0, -20, -40}
    };

    FIXED hand_x = int2fx(108);
    FIXED hand_y = int2fx(10);

    for (int i = jokers_top; i >= 0; i--)
    {
        jokers[i]->sprite_object->tx = hand_x - int2fx(spacing_lut[jokers_top][i]);
        jokers[i]->sprite_object->ty = hand_y;

        if (joker_object_is_selected(jokers[i]))
        {
            jokers[i]->sprite_object->ty -= int2fx(10);
        }

        joker_object_update(jokers[i]);
    }
}

static void game_playing_ui_text_update()
{
    static int last_hand_size = 0;
    static int last_deck_size = 0;

    if (last_hand_size != hand_get_size() || last_deck_size != deck_get_size())
    {
        if (background == BG_ID_CARD_SELECTING)
        {
            tte_printf("#{P:%d,%d; cx:0xF000}%d/%d", HAND_SIZE_RECT_SELECT.left, HAND_SIZE_RECT_SELECT.top, hand_get_size(), hand_get_max_size()); // Hand size/max size
        }
        else if (background == BG_ID_CARD_PLAYING)
        {
            tte_printf("#{P:%d,%d; cx:0xF000}%d/%d", HAND_SIZE_RECT_PLAYING.left, HAND_SIZE_RECT_PLAYING.top, hand_get_size(), hand_get_max_size()); // Hand size/max size
        }

        tte_printf("#{P:%d,%d; cx:0xF000}%d/%d", DECK_SIZE_RECT.left, DECK_SIZE_RECT.top, deck_get_size(), deck_get_max_size()); // Deck size/max size

        last_hand_size = hand_get_size();
        last_deck_size = deck_get_size();
    }
}

static void game_round_end_cashout()
{
    money += hands + blind_get_reward(current_blind); // Reward the player
    display_money(money);

    hands = max_hands; // Reset the hands to the maximum
    discards = max_discards; // Reset the discards to the maximum
    display_hands(hands); // Set the hands display
    display_discards(discards); // Set the discards display

    score = 0;
    display_score(score); // Set the score display
}

static void game_shop_create_items(JokerObject *shop_jokers[], bool first_time)
{
    tte_erase_rect_wrapper(SHOP_PRICES_TEXT_RECT);

    for (int i = 0; i < MAX_SHOP_JOKERS; i++)
    {
        if (shop_jokers[i] != NULL)
        {
            joker_object_destroy(&shop_jokers[i]); // Destroy the joker object if it exists
        }
        
        u8 joker_id = random() % get_joker_registry_size();

        shop_jokers[i] = joker_object_new(joker_new(joker_id));
        shop_jokers[i]->sprite_object->x = int2fx(120 + i * 32);
        shop_jokers[i]->sprite_object->y = int2fx(160);
        shop_jokers[i]->sprite_object->tx = shop_jokers[i]->sprite_object->x;
        shop_jokers[i]->sprite_object->ty = int2fx(71);

        int x = fx2int(shop_jokers[i]->sprite_object->tx) + TILE_SIZE - (get_digits_even(shop_jokers[i]->joker->value) - 1) * TILE_SIZE;
        int y = fx2int(shop_jokers[i]->sprite_object->ty);
        tte_printf("#{P:%d,%d; cx:0xC000}$%d", x, y, shop_jokers[i]->joker->value);

        if (first_time == false)
        {
            shop_jokers[i]->sprite_object->y = shop_jokers[i]->sprite_object->ty; // If it's not the first time, set the y position to the target position
            joker_object_shake(shop_jokers[i], UNDEFINED); // Give the joker a little wiggle animation
        }

        sprite_position(joker_object_get_sprite(shop_jokers[i]), fx2int(shop_jokers[i]->sprite_object->x), fx2int(shop_jokers[i]->sprite_object->y));
    }
}

void game_playing()
{
    // TODO: Blind rect sliding into view animation...
    if (hand_state == HAND_SELECT && hands == 0) 
    {
        // TODO: Check if it's safe to change to game_set_state() without side effects
        game_state = GAME_LOSE;
    }

    // Background logic (thissss might be moved to the card'ssss logic later. I'm a sssssnake)
    if (hand_state == HAND_DRAW || hand_state == HAND_DISCARD || hand_state == HAND_SELECT)
    {
        change_background(BG_ID_CARD_SELECTING);
    }
    else if (hand_state != HAND_SHUFFLING)
    {
        change_background(BG_ID_CARD_PLAYING);
    }

    game_playing_process_input_and_state();

    // Card logic

    game_playing_process_card_draw();

    game_playing_discarded_cards_loop();

    static int played_selections = 0;
    static bool sound_played = false;
    bool discarded_card = false;

    cards_in_hand_update_loop(&discarded_card, &played_selections, &sound_played);
	played_cards_update_loop(&discarded_card, &played_selections, &sound_played);
    
    game_playing_ui_text_update();
}

void game_round_end_cleanup()
{
    // Cleanup blind tokens from this round to avoid accumulating 
    // allocated blind sprites each round
    sprite_destroy(&playing_blind_token);
    sprite_destroy(&round_end_blind_token);
    // TODO: Reuse sprites for blind selection?
}

void game_round_end()
{
    static int blind_reward = 0;
    static int hand_reward = 0;
    static int interest_reward = 0; 

    switch (state)
    {
        case 0:
        {
            if (timer == 30) // Reset static variables to default values upon re-entering the round end state
            {   
                change_background(BG_ID_ROUND_END); // Change the background to the round end background
                state = 1; // Change the state to the next one
                timer = 0; // Reset the timer
                blind_reward = blind_get_reward(current_blind);
                hand_reward = hands;
            }
            break;
        }
        case 1: // This creates the top 16 by 7 tiles of the pop up. It places it in vram, moving it up one tile each frame, not clearing the previous row of tiles so they fill the blank space as it moves up.
        {
            main_bg_se_copy_rect_1_tile_vert(POP_MENU_ANIM_RECT, SE_UP);

            if (timer == 13)
            {
                state = 2;
                timer = 0;
            }
            break;
        }
        case 2: // Display the beaten blind, expand the panel border down a tile and wait until a bit until going to the next state
        {
            obj_unhide(round_end_blind_token->obj, 0);
            
            int current_ante = ante;
            if (current_blind == BOSS_BLIND) current_ante--; // Beating the boss blind increases the ante, so we need to display the previous ante value

            Rect blind_req_rect = ROUND_END_BLIND_REQ_RECT;
            int blind_req = blind_get_requirement(current_blind, current_ante);
            update_text_rect_to_right_align_num(&blind_req_rect, blind_req, OVERFLOW_RIGHT);

            tte_printf("#{P:%d,%d; cx:0xE000}%d", blind_req_rect.left, blind_req_rect.top, blind_req);

            if (timer == 1)
            {
                Rect single_line_rect = ROUND_END_MENU_RECT;
                single_line_rect.top = 11;
                single_line_rect.bottom = single_line_rect.top + 1;
                main_bg_se_copy_rect_1_tile_vert(single_line_rect, SE_DOWN);
            }

            if (timer >= 30)
            {
                state = 3;
                timer = 0;
            }
            break;
        }
        case 3: // Sequentially display the "score min" text over the next 4 frames
        {
            int timer_offset = timer - 1;

            int x_from = 0;
            int y_from = 29;

            int x_to = 13;
            int y_to = 11;

            memcpy16(&se_mem[MAIN_BG_SBB][x_to + timer_offset + 32 * y_to], &se_mem[MAIN_BG_SBB][x_from + timer_offset + 32 * y_from], 1);

            if (timer >= 4)
            {
                state = 4;
                timer = 0;
            }
            break;
        }
        case 4: // Every 20 frames, display the blind reward and update the text until it reaches 0
        {
            if (timer % FRAMES(20) != 0) break;

            // TODO: Add sound effect here

            if (blind_reward > 0)
            {
                blind_reward--;
                tte_printf("#{P:%d,%d; cx:0xC000}$%d", BLIND_REWARD_RECT.left , BLIND_REWARD_RECT.top, blind_reward);
                tte_printf("#{P:%d,%d; cx:0xC000}$%d", ROUND_END_BLIND_REWARD_RECT.left, ROUND_END_BLIND_REWARD_RECT.top, blind_get_reward(current_blind) - blind_reward);
            }
            else if (timer > FRAMES(20))
            {
                tte_erase_rect_wrapper(BLIND_REWARD_RECT);
                tte_erase_rect_wrapper(BLIND_REQ_TEXT_RECT);
                obj_hide(playing_blind_token->obj);
                state = 5;
                timer = 0;
            }
            break;
        }
        case 5: // Slide the "small blind" panel out of view
        {
            if (timer < 8)
            {
                main_bg_se_copy_rect_1_tile_vert(TOP_LEFT_PANEL_ANIM_RECT, SE_UP);

                if (timer == 1) // Copied from shop. Feels slightly too niche of a function for me personally to make one.
                {
                    int y = 6;
                    memset16(&se_mem[MAIN_BG_SBB][32 * (y - 1)], 0x0006, 1);
                    memset16(&se_mem[MAIN_BG_SBB][1 + 32 * (y - 1)], 0x0007, 2);
                    memset16(&se_mem[MAIN_BG_SBB][3 + 32 * (y - 1)], 0x0008, 1);
                    memset16(&se_mem[MAIN_BG_SBB][4 + 32 * (y - 1)], 0x0009, 4);
                    memset16(&se_mem[MAIN_BG_SBB][7 + 32 * (y - 1)], 0x000A, 1);
                    memset16(&se_mem[MAIN_BG_SBB][8 + 32 * (y - 1)], 0x0406, 1);
                }
                else if (timer == 2)
                {
                    int y = 5;
                    memset16(&se_mem[MAIN_BG_SBB][32 * (y - 1)], 0x0001, 1);
                    memset16(&se_mem[MAIN_BG_SBB][1 + 32 * (y - 1)], 0x0002, 7);
                    memset16(&se_mem[MAIN_BG_SBB][8 + 32 * (y - 1)], 0x0401, 1); 
                }
            }   
            else if (timer > FRAMES(20))
            {
                memset16(&pal_bg_mem[19], 0x1483, 1);
                state = 6;
                timer = 0;
            }
            break;
        }
        case 6: // This state handles displaying the rewards earned from the completed round
        {
            int hand_y = 0;

            // TODO: Implement interest
            //int interest_y = 0;

            if (hands > 0)
            {
                hand_y = 1;
            }

            // TODO: implement interest
            // if (interest > 0)
            // {
            //     interest_y = 1 + hand_y;
            // }

            if (hand_reward <= 0 && interest_reward <= 0) // Once all rewards are accounted for go to the next state
            {
                timer = 0; // Reset the timer
                state = 7; // Go to the next state
            }
            else if (timer == 1) // Expand the black part of the panel down by one tile
            {
                Rect single_line_rect = ROUND_END_MENU_RECT;
                single_line_rect.top = 12;
                single_line_rect.bottom = single_line_rect.top + 1;
                main_bg_se_copy_rect_1_tile_vert(single_line_rect, SE_DOWN);
            }
            else if (timer < 16) // Use TTE to print '.' until the end of the panel width
            {
                // Print the separator dots
                int x = (8 + timer) * TILE_SIZE;
                int y = (13) * TILE_SIZE;

                tte_printf("#{P:%d,%d; cx:0xF000}.", x, y); 
            }
            else if (timer >= 30 && hand_reward > 0) // Wait an additional 15 frames since the last sequenced action
            {
                if (timer == 30) // Expand the black part of the panel down by one tile again
                {
                    Rect single_line_rect = ROUND_END_MENU_RECT;
                    single_line_rect.top = 12 + hand_y;
                    single_line_rect.bottom = single_line_rect.top + 1;
                    main_bg_se_copy_rect_1_tile_vert(single_line_rect, SE_DOWN);

                    tte_printf("#{P:%d,%d; cx:0xD000}%d #{cx:0xF000}Hands", ROUND_END_NUM_HANDS_RECT.left, ROUND_END_NUM_HANDS_RECT.top, hand_reward); // Print the hand reward
                }
                else if (timer > 45 && timer % FRAMES(20) == 0) // After 15 frames, every 20 frames, increment the hand reward text until the hand reward variable is depleted
                {
                    int y = (13 + hand_y) * TILE_SIZE;
                    hand_reward--;
                    tte_printf("#{P:%d, %d; cx:0xC000}$%d", HAND_REWARD_RECT.left, y, hands - hand_reward); // Print the hand reward
                }
            }

            break;
        }
        case 7:
        {
            if (timer == FRAMES(40)) // Put the "cash out" button onto the round end panel
            {
                Rect left_rect = {4, 29, 4, 31};
                BG_POINT left_point = {10, 8};
                main_bg_se_copy_rect(left_rect, left_point);

                Rect right_rect = {7, 29, 7, 31};
                BG_POINT right_point = {23, 8};
                main_bg_se_copy_rect(right_rect, right_point);

                Rect top_rect = {11, 8, 22, 8};
                BG_POINT top_point = {6, 29};
                main_bg_se_copy_tile_to_rect(main_bg_se_get_tile(top_point), top_rect);

                Rect middle_rect = {11, 9, 22, 9};
                BG_POINT middle_point = {6, 30};
                main_bg_se_copy_tile_to_rect(main_bg_se_get_tile(middle_point), middle_rect);

                Rect bottom_rect = {11, 10, 22, 10};
                BG_POINT bottom_point = {6, 31};
                main_bg_se_copy_tile_to_rect(main_bg_se_get_tile(bottom_point), bottom_rect);

                tte_printf("#{P:%d, %d; cx:0xF000}Cash Out: $%d", CASHOUT_RECT.left, CASHOUT_RECT.top, hands + blind_get_reward(current_blind)); // Print the cash out amount
            }
            else if (timer > FRAMES(40) && key_hit(SELECT_CARD)) // Wait until the player presses A to cash out
            {
                game_round_end_cashout();

                state = 8; // Go to the next state
                timer = 0; // Reset the timer
            
                obj_hide(round_end_blind_token->obj); // Hide the blind token object
                tte_erase_rect_wrapper(BLIND_TOKEN_TEXT_RECT); // Erase the blind token text
            }

            break;
        }
        case 8: // Shift the round end panel back out of view and go to the next state
        {   
            Rect round_end_down = ROUND_END_MENU_RECT;
            round_end_down.top--;
            main_bg_se_copy_rect_1_tile_vert(round_end_down, SE_DOWN);

            if (timer >= 20)
            {
                timer = 0; 
                state = 9;
            }
            break;
        }   
        default:
            timer = 0;
            state = 0;
            blind_reward = 0;
            hand_reward = 0;
            interest_reward = 0;
            game_round_end_cleanup();
            game_set_state(GAME_SHOP);
            break;
    }
}

void game_shop()
{
    change_background(BG_ID_SHOP);

    static JokerObject *shop_jokers[MAX_SHOP_JOKERS] = {NULL};

    for (int i = 0; i < MAX_SHOP_JOKERS; i++)
    {
        if (shop_jokers[i] != NULL)
        {
            joker_object_update(shop_jokers[i]);
        }
    }

    const int reroll_base_cost = 5; // Base cost for rerolling the shop items
    static int reroll_cost = reroll_base_cost;

    // temp variables for future implementation
    const ushort max_items_top = MAX_SHOP_JOKERS; 
    const ushort max_items_bottom = 0;

    if (timer % 20 == 0) // Shift palette around the border of the shop icon
    {
        COLOR shifted_palette[4];
        memcpy16(&shifted_palette[0], &pal_bg_mem[14], 1);
        memcpy16(&shifted_palette[1], &pal_bg_mem[17], 1);
        memcpy16(&shifted_palette[2], &pal_bg_mem[22], 1);
        memcpy16(&shifted_palette[3], &pal_bg_mem[8], 1);

        // Circularly shift the palette
        int last = shifted_palette[3];

        for (int i = 3; i > 0; --i) {
            shifted_palette[i] = shifted_palette[i - 1];
        }

        shifted_palette[0] = last;

        memcpy16(&pal_bg_mem[14], &shifted_palette[0], 1); // Copy the shifted palette to the next 4 slots
        memcpy16(&pal_bg_mem[17], &shifted_palette[1], 1);
        memcpy16(&pal_bg_mem[22], &shifted_palette[2], 1);
        memcpy16(&pal_bg_mem[8], &shifted_palette[3], 1);
    }

    switch (state) // I'm only using magic numbers here for the sake of simplicity since it's just sequential, but you can replace them with named constants or enums if it makes it clearer
    {
        case 0: // Intro sequence (menu and shop icon coming into frame)
        {           
            main_bg_se_copy_rect_1_tile_vert(POP_MENU_ANIM_RECT, SE_UP);

            if (timer == 1)
            {
                game_shop_create_items(shop_jokers, true);
            }

            if (timer >= 7) // Shift the shop icon
            {
                int timer_offset = timer - 6;

                // TODO: Extract to generic function?
                for (int y = 0; y < timer_offset; y++)
                {
                    int y_from = 26 + y - timer_offset;
                    int y_to = 0 + y;

                    Rect from = {0, y_from, 8, y_from};
                    BG_POINT to = {0, y_to};

                    main_bg_se_copy_rect(from, to);
                }
            }

            if (timer == 12)
            {
                state = 1;
                timer = 0; // Reset the timer
            }

            break;
        }    
        case 1: // Shop menu input and selection
        {
            if (timer == 1)
            {
                tte_printf("#{P:%d,%d; cx:0xF000}$%d", SHOP_REROLL_RECT.left, SHOP_REROLL_RECT.top, reroll_cost);
            }

            // Shop input logic
            if (key_hit(KEY_UP))
            {
                selection_y = 0;

                if (selection_x > max_items_top)
                {
                    selection_x = max_items_top;
                }
            }
            else if (key_hit(KEY_DOWN))
            {
                selection_y = 1;

                if (selection_x > max_items_bottom)
                {
                    selection_x = max_items_bottom;
                }
            }
            else if (key_hit(KEY_LEFT))
            {
                if (selection_x > 0)
                {
                    selection_x--;
                }
            }
            else if (key_hit(KEY_RIGHT))
            {
                if (selection_y == 0 && selection_x < max_items_top)
                {
                    selection_x++;
                }
                else if (selection_y == 1 && selection_x < max_items_bottom)
                {
                    selection_x++;
                }
            }

            memcpy16(&pal_bg_mem[7], &pal_bg_mem[3], 1);
            memcpy16(&pal_bg_mem[5], &pal_bg_mem[16], 1);

            // Shop selection logic
            if (selection_x == 0 && selection_y == 0)
            {
                memset16(&pal_bg_mem[5], 0xFFFF, 1);

                if (key_hit(SELECT_CARD))
                {
                    // Go to next blind selection game state
                    state = 2; // Go to the outro sequence state
                    timer = 0; // Reset the timer

                    memcpy16(&pal_bg_mem[5], &pal_bg_mem[6], 1);

                    // memcpy16(&pal_bg_mem[16], &pal_bg_mem[6], 1); 
                    // This changes the color of the button to a dark red.
                    // However, it shares a palette with the shop icon, so it will change the color of the shop icon as well.
                    // And I don't care enough to fix it right now.
                }
            }
            else if (selection_x == 0 && selection_y == 1)
            {
                memset16(&pal_bg_mem[7], 0xFFFF, 1);

                if (key_hit(SELECT_CARD) && money >= reroll_cost)
                {
                    money -= reroll_cost;
                    display_money(money); // Update the money display
                
                    game_shop_create_items(shop_jokers, false);

                    reroll_cost++;
                    tte_printf("#{P:%d,%d; cx:0xF000}$%d", SHOP_REROLL_RECT.left, SHOP_REROLL_RECT.top, reroll_cost);
                }
            }

            for (int i = 0; i < MAX_SHOP_JOKERS; i++)
            {
                if (shop_jokers[i] != NULL)
                {
                    if (i == selection_x - 1 && selection_y == 0)
                    {
                        shop_jokers[i]->sprite_object->ty = int2fx(61);

                        if (key_hit(SELECT_CARD) && jokers_top < MAX_JOKERS_HELD_SIZE - 1 && money >= shop_jokers[i]->joker->value)
                        {
                            joker_push(shop_jokers[i]);
                            money -= shop_jokers[i]->joker->value; // Deduct the money spent on the joker
                            display_money(money); // Update the money display

                            Rect joker_price_rect;
                            joker_price_rect.left = fx2int(shop_jokers[i]->sprite_object->tx);
                            joker_price_rect.top = fx2int(shop_jokers[i]->sprite_object->ty) + TILE_SIZE;
                            joker_price_rect.right = joker_price_rect.left + TILE_SIZE * 3;
                            joker_price_rect.bottom = joker_price_rect.top + TILE_SIZE;

                            tte_erase_rect_wrapper(joker_price_rect);

                            shop_jokers[i] = NULL; // Remove the joker from the shop
                        }
                    }
                    else
                    {
                        shop_jokers[i]->sprite_object->ty = int2fx(71);
                    }
                }
            }

            break;
        }
        case 2: // Outro sequence (menu and shop icon going out of frame)
        {
            // Shift the shop panel
            main_bg_se_move_rect_1_tile_vert(POP_MENU_ANIM_RECT, SE_DOWN);

            main_bg_se_copy_rect_1_tile_vert(TOP_LEFT_PANEL_ANIM_RECT, SE_UP);
            
            if (timer == 1)
            {
                tte_erase_rect_wrapper(SHOP_PRICES_TEXT_RECT); // Erase the shop prices text

                for (int i = 0; i < MAX_SHOP_JOKERS; i++)
                {
                    if (shop_jokers[i] != NULL)
                    {
                        shop_jokers[i]->sprite_object->ty = int2fx(160);
                    }
                }

                int y = 6;
                memset16(&se_mem[MAIN_BG_SBB][32 * (y - 1)], 0x0006, 1);
                memset16(&se_mem[MAIN_BG_SBB][1 + 32 * (y - 1)], 0x0007, 2);
                memset16(&se_mem[MAIN_BG_SBB][3 + 32 * (y - 1)], 0x0008, 1);
                memset16(&se_mem[MAIN_BG_SBB][4 + 32 * (y - 1)], 0x0009, 4);
                memset16(&se_mem[MAIN_BG_SBB][7 + 32 * (y - 1)], 0x000A, 1);
                memset16(&se_mem[MAIN_BG_SBB][8 + 32 * (y - 1)], 0x0406, 1);
            }
            else if (timer == 2)
            {
                int y = 5;
                memset16(&se_mem[MAIN_BG_SBB][32 * (y - 1)], 0x0001, 1);
                memset16(&se_mem[MAIN_BG_SBB][1 + 32 * (y - 1)], 0x0002, 7);
                memset16(&se_mem[MAIN_BG_SBB][8 + 32 * (y - 1)], 0x0401, 1);
            }

            if (timer >= MENU_POP_OUT_ANIM_FRAMES)
            {
                state = 3; // Go to the next state
                timer = 0; // Reset the timer
            }

            break;
        }
        default:
            state = 0; // Reset the state
            reroll_cost = reroll_base_cost;

            selection_x = 0; // Reset the selection
            selection_y = 0; // Reset the selection

            for (int i = 0; i < MAX_SHOP_JOKERS; i++)
            {
                joker_object_destroy(&shop_jokers[i]); // Destroy the joker objects
            }

            increment_blind(BLIND_DEFEATED);
            game_set_state(GAME_BLIND_SELECT); // If we reach here, we should go to the blind select state

            break;
    }
}

void game_blind_select()
{
    switch (state) // I'm only using magic numbers here for the sake of simplicity since it's just sequential, but you can replace them with named constants or enums if it makes it clearer
    {
        case 0: // Intro sequence (menu coming into frame)
        {           
            change_background(BG_ID_BLIND_SELECT);
            main_bg_se_copy_rect_1_tile_vert(POP_MENU_ANIM_RECT, SE_UP);

            for (int i = 0; i < MAX_BLINDS; i++)
            {
                sprite_position(blind_select_tokens[i], blind_select_tokens[i]->pos.x, blind_select_tokens[i]->pos.y - 8);
            }

            if (timer == 12)
            {
                state++;
                timer = 0; // Reset the timer
            }

            break;
        }
        case 1: // Blind select input and selection
        {
            if (timer == 1 && current_blind == BOSS_BLIND)
            {
                selection_y = 0;
            }

            // Blind select input logic
            if (key_hit(KEY_UP))
            {
                selection_y = 0;
            }
            else if (key_hit(KEY_DOWN) && current_blind != BOSS_BLIND)
            {
                selection_y = 1;
            }
            else if (key_hit(SELECT_CARD))
            {
                if (selection_y == 0) // Blind selected
                {
                    state++;
                    timer = 0;
                    display_round(++round);
                }
                else if (current_blind != BOSS_BLIND)
                {
                    increment_blind(BLIND_SKIPPED);
                    
                    background = UNDEFINED; // Force refresh of the background
                    change_background(BG_ID_BLIND_SELECT);

                    // TODO: Create a generic vertical move by any number of tiles to avoid for loops?
                    for (int i = 0; i < 12; i++)
                    {
                        main_bg_se_copy_rect_1_tile_vert(POP_MENU_ANIM_RECT, SE_UP);
                    }

                    for (int i = 0; i < MAX_BLINDS; i++)
                    {
                        sprite_position(blind_select_tokens[i], blind_select_tokens[i]->pos.x, blind_select_tokens[i]->pos.y - (TILE_SIZE * 12));
                    }

                    timer = 0;
                }
            }

            if (selection_y == 0)
            {
                memset16(&pal_bg_mem[18], 0xFFFF, 1);
                memcpy16(&pal_bg_mem[10], &pal_bg_mem[5], 1);
            }
            else
            {
                memcpy16(&pal_bg_mem[18], &pal_bg_mem[15], 1);
                memset16(&pal_bg_mem[10], 0xFFFF, 1);
            }

            break;
        }
        case 2: // Blind selected, perform menu popout animation
        {
            if (timer < 15)
            {
                Rect blinds_rect = POP_MENU_ANIM_RECT;
                blinds_rect.top -= 1; // Because of the raised blind
                main_bg_se_move_rect_1_tile_vert(blinds_rect, SE_DOWN);

                for (int i = 0; i < MAX_BLINDS; i++)
                {
                    sprite_position(blind_select_tokens[i], blind_select_tokens[i]->pos.x, blind_select_tokens[i]->pos.y + TILE_SIZE);
                }
            }
            else if (timer >= MENU_POP_OUT_ANIM_FRAMES)
            {
                for (int i = 0; i < MAX_BLINDS; i++)
                {
                    obj_hide(blind_select_tokens[i]->obj);
                }

                state++; // Reset the state
                timer = 0; // Reset the timer
            }
            break;
        }
        case 3: // Move the blind panel into view
        {
            if (timer < 7)
            {
                if (timer == 1) // Switches to the selecting background and clears the blind panel area
                {
                    change_background(BG_ID_CARD_SELECTING);

                    main_bg_se_clear_rect(ROUND_END_MENU_RECT);

                    for (int y = 0; y < 5; y++)
                    {
                        int y_from = 28;
                        int y_to = 0 + y;

                        Rect from = {0, y_from, 8, y_from + 1};
                        BG_POINT to = {0, y_to};

                        main_bg_se_copy_rect(from, to);
                    }

                    int y = 6;
                    memset16(&se_mem[MAIN_BG_SBB][32 * (y - 1)], 0x0006, 1);
                    memset16(&se_mem[MAIN_BG_SBB][1 + 32 * (y - 1)], 0x0007, 2);
                    memset16(&se_mem[MAIN_BG_SBB][3 + 32 * (y - 1)], 0x0008, 1);
                    memset16(&se_mem[MAIN_BG_SBB][4 + 32 * (y - 1)], 0x0009, 4);
                    memset16(&se_mem[MAIN_BG_SBB][7 + 32 * (y - 1)], 0x000A, 1);
                    memset16(&se_mem[MAIN_BG_SBB][8 + 32 * (y - 1)], 0x0406, 1); 
                }

                for (int y = 0; y < timer; y++) // Shift the blind panel down onto screen
                {
                    int y_from = 26 + y - timer;
                    int y_to = 0 + y;

                    Rect from = {0, y_from, 8, y_from};
                    BG_POINT to = {0, y_to};

                    main_bg_se_copy_rect(from, to);
                }
            }
            else
            {
                state++;
            }

            break;
        }
        default:
            state = 0;
            timer = 0;
            selection_y = 0;
            background = UNDEFINED;
            game_set_state(GAME_PLAYING);
            break;
    }
}

void game_update()
{
    timer++;

    if (seeded == false)
    {
        rng_seed++;
        if (key_curr_state() != key_prev_state()) // If the keys have changed, make it more pseudo-random
        {
            rng_seed *= 2;
        }
    }

    held_jokers_update_loop();

    switch (game_state)
    {
        case GAME_PLAYING:
            game_playing();
            break;
        case GAME_ROUND_END:
            game_round_end();
            break;
        case GAME_SHOP:
            game_shop();
            break;
        case GAME_BLIND_SELECT:
            game_blind_select();
            break;
        case GAME_LOSE:
            // Handle lose logic here
            break;
    }
}