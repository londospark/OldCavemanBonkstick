#pragma once

// OldCavemanBonkstick: feature flags for the Dark Souls-inspired features, grouped under
// Enhancements -> Souls. Each feature registers its GameInteractor hooks only while its CVar is on,
// so turning a feature off returns that part of the game to vanilla.

#define CVAR_SOULS_CLUB_NAME "gEnhancements.Souls.Club"
#define CVAR_SOULS_CLUB_DEFAULT 1
#define CVAR_SOULS_CLUB_VALUE CVarGetInteger(CVAR_SOULS_CLUB_NAME, CVAR_SOULS_CLUB_DEFAULT)

#define CVAR_SOULS_STAMINA_NAME "gEnhancements.Souls.Stamina"
#define CVAR_SOULS_STAMINA_DEFAULT 1
#define CVAR_SOULS_STAMINA_VALUE CVarGetInteger(CVAR_SOULS_STAMINA_NAME, CVAR_SOULS_STAMINA_DEFAULT)

#define CVAR_SOULS_ROLL_NAME "gEnhancements.Souls.Roll"
#define CVAR_SOULS_ROLL_DEFAULT 1
#define CVAR_SOULS_ROLL_VALUE CVarGetInteger(CVAR_SOULS_ROLL_NAME, CVAR_SOULS_ROLL_DEFAULT)

// True when `player` is holding a sword-slot item (Kokiri / Master / Biggoron), which the
// Reinforced Club replaces. Callers check CVAR_SOULS_CLUB_VALUE themselves (usually by only
// registering their hook while it is on).
#define SOULS_IS_SWORD_SLOT_HELD(player)                                                                             \
    (((player)->heldItemAction == PLAYER_IA_SWORD_KOKIRI) || ((player)->heldItemAction == PLAYER_IA_SWORD_MASTER) || \
     ((player)->heldItemAction == PLAYER_IA_SWORD_BIGGORON))
