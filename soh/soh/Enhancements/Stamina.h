#pragma once

#ifndef STAMINA_H
#define STAMINA_H

#include "soh/Enhancements/Souls.h"

#ifdef __cplusplus
extern "C" {
#endif

// OldCavemanBonkstick: Dark Souls-style stamina.
// Rolling and (club) attacks consume stamina. Blocking with the shield consumes stamina
// (scaled by the hit). Holding the shield up regenerates stamina much more slowly. If a
// hit can't be paid for with stamina, the guard breaks and you take partial damage.
//
// Stamina debt: an action is allowed while stamina is above zero, and can drive it
// negative down to STAMINA_DEBT_FLOOR. While negative you can't roll, attack, block or
// raise the shield, and recovery back above zero is much slower.

#define CVAR_STAMINA_NAME CVAR_SOULS_STAMINA_NAME

#define STAMINA_MAX_CHILD 100
#define STAMINA_MAX_ADULT 130
#define STAMINA_DEBT_FLOOR (-50)

#define STAMINA_COST_ROLL 25
#define STAMINA_COST_ATTACK 25
#define STAMINA_COST_JUMP_ATTACK 65
#define STAMINA_COST_JUMP_ATTACK_CHAIN 45 // a jump attack made soon after another
#define STAMINA_JUMP_CHAIN_WINDOW 40 // frames (~2s @ 20fps) that count as "quick succession"
#define STAMINA_COST_BLOCK 12 // base cost, plus the incoming damage

// per-frame regeneration tick. Shielding slows regeneration a lot; debt slows it further.
void Stamina_Update(int shielding);

// current stamina (negative while in debt)
int Stamina_Get(void);

// max stamina for the current Link age
int Stamina_GetMax(void);

// whether the stamina system is enabled
int Stamina_IsEnabled(void);

// whether stamina is in debt (below zero)
int Stamina_IsInDebt(void);

// whether any stamina-consuming action or raising the shield may start right now
int Stamina_CanAct(void);

// Jump attack: costs STAMINA_COST_JUMP_ATTACK, or STAMINA_COST_JUMP_ATTACK_CHAIN if another
// jump attack landed within STAMINA_JUMP_CHAIN_WINDOW frames. Same rules as Stamina_TrySpend.
int Stamina_TrySpendJumpAttack(void);

// Roll/attack: allowed whenever stamina is above zero (can go into debt).
// Returns 0 (action blocked) when stamina is zero or already in debt.
int Stamina_TrySpend(int cost);

// Shield block: requires the full cost up-front. Returns 1 if paid (block succeeds),
// 0 if there isn't enough (guard breaks).
int Stamina_TryBlock(int cost);

// Forced drain (e.g. sprint), clamped to the debt floor.
void Stamina_Drain(int amount);

#ifdef __cplusplus
}
#endif

#endif // STAMINA_H
