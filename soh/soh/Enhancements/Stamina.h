#pragma once

#ifndef STAMINA_H
#define STAMINA_H

#ifdef __cplusplus
extern "C" {
#endif

// OldCavemanBonkstick: Dark Souls-style stamina.
// Rolling and (club) attacks consume stamina; it regenerates after a short delay.
// When stamina is too low for an action, that action cannot be performed.

#define CVAR_STAMINA_NAME "gEnhancements.StaminaSystem"

#define STAMINA_MAX 100
#define STAMINA_COST_ROLL 25
#define STAMINA_COST_ATTACK 20
#define STAMINA_COST_JUMP_ATTACK 25

// per-frame regeneration tick (call once per player update)
void Stamina_Update(void);

// current stamina, 0..STAMINA_MAX
int Stamina_Get(void);

// whether the stamina system is enabled
int Stamina_IsEnabled(void);

// Try to spend `cost`. Returns 1 if the action may proceed (stamina was spent),
// or 0 if there wasn't enough stamina. Always returns 1 when the system is disabled.
int Stamina_TrySpend(int cost);

// Forced drain (e.g. sprint), clamped to 0.
void Stamina_Drain(int amount);

#ifdef __cplusplus
}
#endif

#endif // STAMINA_H
