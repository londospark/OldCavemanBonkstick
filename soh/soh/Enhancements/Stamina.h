#pragma once

#ifndef STAMINA_H
#define STAMINA_H

#ifdef __cplusplus
extern "C" {
#endif

// OldCavemanBonkstick: Dark Souls-style stamina.
// Rolling and (club) attacks consume stamina. Blocking with the shield consumes stamina
// (scaled by the hit). Holding the shield up regenerates stamina much more slowly. If a
// hit can't be paid for with stamina, the guard breaks and you take partial damage.

#include "soh/Enhancements/Souls.h"

#define CVAR_STAMINA_NAME CVAR_SOULS_STAMINA_NAME

#define STAMINA_MAX_CHILD 100
#define STAMINA_MAX_ADULT 130

#define STAMINA_COST_ROLL 25
#define STAMINA_COST_ATTACK 20
#define STAMINA_COST_JUMP_ATTACK 35
#define STAMINA_COST_BLOCK 12 // base cost, plus the incoming damage

// per-frame regeneration tick. Shielding slows regeneration a lot.
void Stamina_Update(int shielding);

// current stamina (can be negative when in debt)
int Stamina_Get(void);

// max stamina for the current Link age
int Stamina_GetMax(void);

// whether the stamina system is enabled
int Stamina_IsEnabled(void);

// Roll/attack: allowed whenever any stamina remains (can go into debt).
// Returns 0 (action blocked) only when stamina is already exhausted (<= 0).
int Stamina_TrySpend(int cost);

// Shield block: requires the full cost up-front. Returns 1 if paid (block succeeds),
// 0 if there isn't enough (guard breaks).
int Stamina_TryBlock(int cost);

// Forced drain (e.g. sprint).
void Stamina_Drain(int amount);

#ifdef __cplusplus
}
#endif

#endif // STAMINA_H
