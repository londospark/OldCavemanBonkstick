#include "soh/Enhancements/Stamina.h"

#include <libultraship/bridge/consolevariablebridge.h>

// Dark Souls-like tuning.
//   regen:  ~1.05/frame @ 20fps -> ~21/s, full refill in ~4.8s (DS1 base is ~this)
//   delay:  short pause after spending before regen resumes
#define STAMINA_REGEN_PER_TICK 1.05f
#define STAMINA_REGEN_DELAY 12

static float sStamina = (float)STAMINA_MAX;
static short sRegenDelay = 0;

int Stamina_IsEnabled(void) {
    return CVarGetInteger(CVAR_STAMINA_NAME, 1);
}

int Stamina_Get(void) {
    return (int)(sStamina + 0.5f);
}

void Stamina_Update(void) {
    if (!Stamina_IsEnabled()) {
        // keep it topped up while disabled so it doesn't look odd if re-enabled
        sStamina = (float)STAMINA_MAX;
        sRegenDelay = 0;
        return;
    }

    if (sRegenDelay > 0) {
        sRegenDelay--;
        // regen slowly even during the delay, like DS
    }

    if (sStamina < (float)STAMINA_MAX) {
        sStamina += (sRegenDelay > 0) ? (STAMINA_REGEN_PER_TICK * 0.35f) : STAMINA_REGEN_PER_TICK;
        if (sStamina > (float)STAMINA_MAX) {
            sStamina = (float)STAMINA_MAX;
        }
    }
}

int Stamina_TrySpend(int cost) {
    if (!Stamina_IsEnabled()) {
        return 1;
    }
    if (sStamina < (float)cost) {
        return 0;
    }
    sStamina -= (float)cost;
    sRegenDelay = STAMINA_REGEN_DELAY;
    if (sStamina < 0.0f) {
        sStamina = 0.0f;
    }
    return 1;
}

void Stamina_Drain(int amount) {
    if (!Stamina_IsEnabled()) {
        return;
    }
    sStamina -= (float)amount;
    if (sStamina < 0.0f) {
        sStamina = 0.0f;
    }
    sRegenDelay = STAMINA_REGEN_DELAY;
}
