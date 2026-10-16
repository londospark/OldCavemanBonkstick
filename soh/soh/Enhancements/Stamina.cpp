#include "soh/Enhancements/Stamina.h"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern SaveContext gSaveContext;
}

// Dark Souls-like tuning.
//   regen:  ~0.7/frame @ 20fps (~14/s) after a short pause following spending
#define STAMINA_REGEN_PER_TICK 0.7f
#define STAMINA_REGEN_DELAY 18
#define STAMINA_REGEN_SHIELD_FACTOR 0.2f // regen multiplier while the shield is held up
#define STAMINA_REGEN_DEBT_FACTOR 0.35f  // recovering from negative is much slower

static float sStamina = (float)STAMINA_MAX_CHILD;
static short sRegenDelay = 0;

int Stamina_IsEnabled(void) {
    return CVarGetInteger(CVAR_STAMINA_NAME, 1);
}

int Stamina_GetMax(void) {
    // Adult Link is stronger: more stamina.
    return (gSaveContext.linkAge == LINK_AGE_ADULT) ? STAMINA_MAX_ADULT : STAMINA_MAX_CHILD;
}

int Stamina_Get(void) {
    return (int)(sStamina < 0.0f ? sStamina - 0.5f : sStamina + 0.5f);
}

int Stamina_IsInDebt(void) {
    return Stamina_IsEnabled() && sStamina < 0.0f;
}

int Stamina_CanAct(void) {
    return !Stamina_IsEnabled() || sStamina > 0.0f;
}

void Stamina_Update(int shielding) {
    if (!Stamina_IsEnabled()) {
        sStamina = (float)STAMINA_MAX_ADULT;
        sRegenDelay = 0;
        return;
    }

    if (sRegenDelay > 0) {
        sRegenDelay--;
    }

    if (sStamina < (float)Stamina_GetMax()) {
        float rate = (sRegenDelay > 0) ? (STAMINA_REGEN_PER_TICK * 0.35f) : STAMINA_REGEN_PER_TICK;
        if (shielding) {
            // Holding the shield up makes stamina recover much more slowly.
            rate *= STAMINA_REGEN_SHIELD_FACTOR;
        }
        if (sStamina < 0.0f) {
            // Climbing out of debt takes much longer than topping up an empty bar.
            rate *= STAMINA_REGEN_DEBT_FACTOR;
        }
        sStamina += rate;
        if (sStamina > (float)Stamina_GetMax()) {
            sStamina = (float)Stamina_GetMax();
        }
    }
}

int Stamina_TrySpend(int cost) {
    if (!Stamina_IsEnabled()) {
        return 1;
    }

    // Actions need stamina above zero. Once above zero, the action goes through even if it
    // costs more than remains, which can drive stamina negative (down to the debt floor).
    if (!Stamina_CanAct()) {
        return 0;
    }

    sStamina -= (float)cost;
    if (sStamina < (float)STAMINA_DEBT_FLOOR) {
        sStamina = (float)STAMINA_DEBT_FLOOR;
    }
    sRegenDelay = STAMINA_REGEN_DELAY;
    return 1;
}

int Stamina_TryBlock(int cost) {
    if (!Stamina_IsEnabled()) {
        return 1;
    }

    // A block must be fully paid for up-front. If you can't afford it, the guard breaks.
    if (sStamina < (float)cost) {
        return 0;
    }

    sStamina -= (float)cost;
    sRegenDelay = STAMINA_REGEN_DELAY;
    return 1;
}

void Stamina_Drain(int amount) {
    if (!Stamina_IsEnabled()) {
        return;
    }
    sStamina -= (float)amount;
    if (sStamina < (float)STAMINA_DEBT_FLOOR) {
        sStamina = (float)STAMINA_DEBT_FLOOR;
    }
    sRegenDelay = STAMINA_REGEN_DELAY;
}
