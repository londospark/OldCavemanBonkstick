#include "soh/Enhancements/Souls/Stamina.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern SaveContext gSaveContext;
extern PlayState* gPlayState;
}

// Dark Souls-like tuning.
//   regen:  Dark Souls' base 45/s, i.e. 2.25/frame @ 20fps, after a short pause following spending
#define STAMINA_REGEN_PER_TICK 2.25f
#define STAMINA_REGEN_DELAY 18
#define STAMINA_REGEN_DELAY_FACTOR 0.35f               // regen multiplier during the post-spend pause
#define STAMINA_REGEN_SHIELD_FACTOR 0.2f               // regen multiplier while the shield is held up
#define STAMINA_REGEN_DEBT_RATE STAMINA_REGEN_PER_TICK // debt recovers at the normal rate, as in Dark Souls

static float sStamina = (float)STAMINA_MAX_CHILD;
static short sRegenDelay = 0;
static short sJumpChainTimer = 0;

bool Stamina_IsEnabled() {
    return CVAR_SOULS_STAMINA_VALUE;
}

int Stamina_GetMax() {
    // Adult Link is stronger: more stamina.
    return (gSaveContext.linkAge == LINK_AGE_ADULT) ? STAMINA_MAX_ADULT : STAMINA_MAX_CHILD;
}

int Stamina_Get() {
    return (int)(sStamina < 0.0f ? sStamina - 0.5f : sStamina + 0.5f);
}

void Stamina_Reset() {
    sStamina = (float)Stamina_GetMax();
    sRegenDelay = 0;
    sJumpChainTimer = 0;
}

bool Stamina_CanAct() {
    return !Stamina_IsEnabled() || sStamina > 0.0f;
}

void Stamina_Update(bool shielding) {
    const float max = (float)Stamina_GetMax();

    if (sJumpChainTimer > 0) {
        sJumpChainTimer--;
    }
    if (sRegenDelay > 0) {
        sRegenDelay--;
    }

    if (sStamina < 0.0f) {
        // Climbing out of debt runs at the full rate. It doesn't take the post-spend pause,
        // so a few rolls don't leave you stuck below zero.
        sStamina += STAMINA_REGEN_DEBT_RATE;
    } else if (sStamina < max) {
        float rate = STAMINA_REGEN_PER_TICK;
        if (sRegenDelay > 0) {
            rate *= STAMINA_REGEN_DELAY_FACTOR;
        }
        if (shielding) {
            // Holding the shield up makes stamina recover much more slowly.
            rate *= STAMINA_REGEN_SHIELD_FACTOR;
        }
        sStamina += rate;
    }

    // Also pulls stamina down when the max shrinks (adult -> child).
    if (sStamina > max) {
        sStamina = max;
    }
}

bool Stamina_TrySpend(int cost) {
    if (!Stamina_IsEnabled()) {
        return true;
    }

    // Actions need stamina above zero. Once above zero, the action goes through even if it
    // costs more than remains, which can drive stamina negative (down to the debt floor).
    if (!Stamina_CanAct()) {
        return false;
    }

    sStamina -= (float)cost;
    if (sStamina < (float)STAMINA_DEBT_FLOOR) {
        sStamina = (float)STAMINA_DEBT_FLOOR;
    }
    sRegenDelay = STAMINA_REGEN_DELAY;
    return true;
}

bool Stamina_TrySpendJumpAttack() {
    int cost = (sJumpChainTimer > 0) ? STAMINA_COST_JUMP_ATTACK_CHAIN : STAMINA_COST_JUMP_ATTACK;
    if (!Stamina_TrySpend(cost)) {
        return false;
    }
    // Only a jump attack that actually goes through starts the chain window.
    sJumpChainTimer = STAMINA_JUMP_CHAIN_WINDOW;
    return true;
}

bool Stamina_TryBlock(int cost) {
    if (!Stamina_IsEnabled()) {
        return true;
    }

    // A block must be fully paid for up-front. If you can't afford it, the guard breaks.
    if (sStamina < (float)cost) {
        return false;
    }

    sStamina -= (float)cost;
    sRegenDelay = STAMINA_REGEN_DELAY;
    return true;
}

static void RegisterStamina() {
    // Start every session, and every re-enable, with a full bar.
    Stamina_Reset();

    COND_HOOK(OnLoadGame, CVAR_SOULS_STAMINA_VALUE, [](int32_t) { Stamina_Reset(); });

    COND_HOOK(OnPlayerUpdate, CVAR_SOULS_STAMINA_VALUE, []() {
        Player* player = GET_PLAYER(gPlayState);

        // Dying (or a game over) refills the bar, so a respawn never starts in debt.
        if (player->stateFlags1 & PLAYER_STATE1_DEAD) {
            Stamina_Reset();
            return;
        }

        Stamina_Update((player->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0);
    });
}

static RegisterShipInitFunc initFunc(RegisterStamina, { CVAR_SOULS_STAMINA_NAME });
