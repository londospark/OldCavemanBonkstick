#pragma once

#ifndef SOULS_H
#define SOULS_H

// OldCavemanBonkstick: feature flags for the Dark Souls-inspired features, grouped under
// Enhancements -> Souls.

#define CVAR_SOULS_CLUB_NAME "gEnhancements.Souls.Club"
#define CVAR_SOULS_STAMINA_NAME "gEnhancements.Souls.Stamina"

// True when the Reinforced Club is enabled and `player` is holding a sword-slot item
// (Kokiri / Master / Biggoron). When disabled, swords behave as vanilla.
#define SOULS_IS_CLUB_HELD(player)                                                                        \
    (CVarGetInteger(CVAR_SOULS_CLUB_NAME, 1) &&                                                           \
     (((player)->heldItemAction == PLAYER_IA_SWORD_KOKIRI) || ((player)->heldItemAction == PLAYER_IA_SWORD_MASTER) || \
      ((player)->heldItemAction == PLAYER_IA_SWORD_BIGGORON)))

#endif // SOULS_H
