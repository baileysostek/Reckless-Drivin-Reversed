#pragma once

/* All Ppic full-screen images used in the game */
typedef enum
{
  PPIC_MENU = 1000,
  PPIC_MENU_HIGHLIGHT = 1001,
  PPIC_MENU_SELECTED = 1002,
  PPIC_LOADING = 1003,
  PPIC_HIGH_SCORES = 1004,
  PPIC_SEEN_EVERYTHING_TEXT = 1005,
  PPIC_PAUSED = 1006,
  PPIC_HELP_SCREEN_1 = 1007,
  PPIC_HELP_SCREEN_2 = 1008,
} PPicID;

/* All SoundIDs for the various sound effects in the game */
typedef enum
{
  SOUND_TEST = 100,
} SoundID;

/* All Object IDs */
typedef enum 
{
  OBJECT_TIRE = 1012,
  OBJECT_BUMPER = 1014,
  OBJECT_DOOR_LEFT = 1015,
  OBJECT_DOOR_RIGHT = 1016,

} ObjectID;