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

typedef enum
{ 
  DEFAULT = 0,
  TRUCK = 2,
  CONVERTIBLE_PINK = 3,
  SEDAN_AGAIN = 4,
  PUNCH_BUGGY = 5,
  CONVERTIBLE_YELLOW = 6,
  STATION_WAGON = 7,
  COMPACT_CAR = 8,
  MINI_VAN = 9, 
  JEEP = 10,
  CHARGER = 11,
  SEDAN = 12,
  MOTORCYCLE = 13,
  CHOPPER = 14,
  EIGHTEEN_WHEELER = 15,
  TRUCK_CAB = 16,
  BOX_TRUCK = 17,
  SCHOOL_BUS = 18,
  ATV = 19,
  ARMY_MOTORCYCLE = 20,
  ARMY_JEEP = 21,
  ARMY_TRUCK = 22,
  MISSILE_CARRIER = 23,
  // CRASHES = 24,
  // CRASHES = 25,
  DEER = 26,
  TUMBLE_WEED = 27,
  // CRASHES = 28,
  // CRASHES = 29,
  AMBULANCE = 30,
  GASOLINE_TRUCK = 31,
} CarID;