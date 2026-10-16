#include "soh/Enhancements/Stamina.h"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
extern SaveContext gSaveContext;
}

// Dark Souls-like tuning.
//   regen:  ~1.05/frame @ 20fps -> ~21/s (DS1 base is about this)
//   delay:  short pause after spending before full regen resumes
#define STAMINA_REGEN_PER_TICK 0.7f
#define STAMINA_REGEN_DELAY 18
#define STAMINA_REGEN_SHIELD_FACTOR 0.2f // regen multiplier while the shield is held up

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
    return (int)(sStamina + 0.5f);
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

    // As long as there is any stamina left, the action is allowed -- even if it costs
    // more than remains (you go into debt). Only a fully exhausted bar blocks you.
    if (sStamina <= 0.0f) {
        return 0;
    }

    sStamina -= (float)cost;
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
    sRegenDelay = STAMINA_REGEN_DELAY;
}
