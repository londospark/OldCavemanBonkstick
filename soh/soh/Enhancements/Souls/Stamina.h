#pragma once

#include "soh/Enhancements/Souls/Souls.h"

// OldCavemanBonkstick: Dark Souls-style stamina.
// Rolling and club attacks consume stamina. Blocking with the shield consumes stamina (scaled by
// the hit). Holding the shield up regenerates stamina much more slowly. If a hit can't be paid for
// with stamina, the guard breaks: Link is knocked back and takes part of the damage.
//
// Stamina debt: an action is allowed while stamina is above zero, and can drive it negative down
// to STAMINA_DEBT_FLOOR. While negative you can't roll, attack, block or raise the shield, and
// recovery back above zero runs at the normal rate. Debt isn't drawn on the HUD (the bar just
// sits empty), matching Dark Souls.

#define STAMINA_MAX_CHILD 100
#define STAMINA_MAX_ADULT 130
#define STAMINA_DEBT_FLOOR (-50)

#define STAMINA_COST_ROLL 28 // Dark Souls roll/jump cost
#define STAMINA_COST_ATTACK 25
#define STAMINA_COST_JUMP_ATTACK 65
#define STAMINA_COST_JUMP_ATTACK_CHAIN 45 // a jump attack made soon after another
#define STAMINA_JUMP_CHAIN_WINDOW 40      // frames (~2s @ 20fps) that count as "quick succession"
#define STAMINA_COST_BLOCK 12             // base cost, plus the incoming hit's damage

// whether the stamina system is enabled
bool Stamina_IsEnabled();

// per-frame regeneration tick. Shielding slows regeneration a lot; debt recovers at the full rate.
void Stamina_Update(bool shielding);

// refill to the current max and clear timers (file load, death)
void Stamina_Reset();

// current stamina (negative while in debt)
int Stamina_Get();

// max stamina for the current Link age
int Stamina_GetMax();

// whether any stamina-consuming action or raising the shield may start right now
bool Stamina_CanAct();

// Roll/attack: allowed whenever stamina is above zero (can go into debt).
// Returns false (action blocked) when stamina is zero or already in debt.
bool Stamina_TrySpend(int cost);

// Jump attack: costs STAMINA_COST_JUMP_ATTACK, or STAMINA_COST_JUMP_ATTACK_CHAIN if another
// jump attack went through within STAMINA_JUMP_CHAIN_WINDOW frames. Same rules as Stamina_TrySpend.
bool Stamina_TrySpendJumpAttack();

// Shield block: requires the full cost up-front. Returns true if paid (block succeeds),
// false if there isn't enough (guard breaks).
bool Stamina_TryBlock(int cost);
