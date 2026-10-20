#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/Souls/Stamina.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
void Player_Action_Roll(Player* player, PlayState* play);
void func_80837C0C(PlayState* play, Player* player, s32 damageResponseType, f32 speed, f32 yVelocity, s16 yRot,
                   s32 invincibilityTimer);
}

// OldCavemanBonkstick: Souls combat. Each block below only registers while its feature is on.

// MARK: - Reinforced Club

static bool IsMovingAttack(Player* player, s32 floorType) {
    return (player->controlStickDirections[player->controlStickDataIndex] > PLAYER_STICK_DIR_NONE) && (floorType != 7);
}

static void RegisterSoulsClub() {
    bool club = CVAR_SOULS_CLUB_VALUE;

    // Swords are replaced by a hammer-class club: every standing swing is an overhead smash.
    COND_VB_SHOULD(VB_PLAYER_USE_HAMMER_ATTACKS, club, {
        Player* player = va_arg(args, Player*);
        s32* controlStickDirection = va_arg(args, s32*);
        if (SOULS_IS_SWORD_SLOT_HELD(player)) {
            *controlStickDirection = PLAYER_STICK_DIR_FORWARD;
            *should = true;
        }
    });

    // A B press while moving is a leaping slam, a heavier commitment than a standing smash.
    COND_VB_SHOULD(VB_PLAYER_B_BUTTON_JUMPSLASH, club, {
        Player* player = va_arg(args, Player*);
        s32 floorType = va_arg(args, s32);
        if (SOULS_IS_SWORD_SLOT_HELD(player) && IsMovingAttack(player, floorType)) {
            *should = Stamina_TrySpendJumpAttack();
        }
    });

    COND_VB_SHOULD(VB_PLAYER_START_MELEE_ATTACK, club, {
        Player* player = va_arg(args, Player*);
        s32 floorType = va_arg(args, s32);
        if (!SOULS_IS_SWORD_SLOT_HELD(player)) {
            return;
        }
        if (IsMovingAttack(player, floorType)) {
            // VB_PLAYER_B_BUTTON_JUMPSLASH already refused this one: no stamina for the slam.
            *should = false;
        } else if (!Stamina_TrySpend(STAMINA_COST_ATTACK)) {
            *should = false;
        }
    });

    // No thin blade streak: swings read as a heavy club rather than a sword.
    COND_VB_SHOULD(VB_PLAYER_DRAW_SWORD_TRAIL, club, {
        Player* player = va_arg(args, Player*);
        if (SOULS_IS_SWORD_SLOT_HELD(player)) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc clubInitFunc(RegisterSoulsClub, { CVAR_SOULS_CLUB_NAME });

// MARK: - Stamina-gated shield

static void RegisterSoulsShield() {
    bool stamina = CVAR_SOULS_STAMINA_VALUE;

    // The shield can't be raised, and drops if already up, while stamina is in debt.
    COND_VB_SHOULD(VB_PLAYER_ALLOW_SHIELD, stamina, {
        if (!Stamina_CanAct()) {
            *should = false;
        }
    });

    // Blocking costs stamina scaled by the hit. If it can't be paid the guard breaks: Link is
    // knocked back and takes a quarter of the damage.
    COND_VB_SHOULD(VB_PLAYER_BLOCK_WITH_SHIELD, stamina, {
        Player* player = va_arg(args, Player*);
        PlayState* play = va_arg(args, PlayState*);
        if (!*should) {
            return;
        }

        // The shield collider is AC_HARD, so the hit's damage never reaches colChkInfo.damage;
        // read it from the attacker instead.
        ColliderInfo* attack = player->shieldQuad.info.acHitInfo;
        s32 damage = (attack != NULL) ? attack->toucher.damage : 0;
        if (Stamina_TryBlock(STAMINA_COST_BLOCK + damage)) {
            return;
        }

        *should = false;
        Actor* attacker = player->shieldQuad.base.ac;
        s16 knockbackYaw = (attacker != NULL) ? Actor_WorldYawTowardActor(attacker, &player->actor)
                                              : (s16)(player->actor.shape.rot.y + 0x8000);
        player->actor.colChkInfo.damage = MAX(1, damage / 4);
        func_80837C0C(play, player, PLAYER_HIT_RESPONSE_KNOCKBACK_SMALL, 4.0f, 5.0f, knockbackYaw, 20);
    });

    // Rolls cost stamina. Landing rolls after a fall don't go through here and stay free.
    COND_VB_SHOULD(VB_PLAYER_ROLL, stamina, {
        if (!Stamina_TrySpend(STAMINA_COST_ROLL)) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc shieldInitFunc(RegisterSoulsShield, { CVAR_SOULS_STAMINA_NAME });

// MARK: - Dark Souls roll

// The roll gets a tight, reliable window of invulnerability like a Dark Souls "medium load" roll,
// but it can't be spammed for permanent protection: chaining rolls strips the i-frames until the
// player has recovered.
#define ROLL_ANIM_SPEED 1.6f   // roll animation speed (vanilla 1.25)
#define ROLL_IFRAME_START 2.0f // roll animation frame where the i-frames begin (vanilla 8)
#define ROLL_IFRAMES 12        // i-frame duration, ~0.6s @ 20fps (vanilla 10)
#define ROLL_RECOVERY 20       // frames after a roll before the next roll gets i-frames
#define ROLL_SPEED_SCALE 2.2f  // roll speed vs walking speed (vanilla 1.5)
#define ROLL_SPEED_MIN 6.0f    // even a standing-still roll covers ground (vanilla 3)

static s16 sRollRecoveryTimer = 0;  // counts down after each roll
static bool sRollHasIFrames = true; // whether the current roll grants i-frames
static s16 sRollPhaseTimer = 0;     // frames left in the roll's i-frame window (enemy hits pass through)

static void RegisterSoulsRoll() {
    bool roll = CVAR_SOULS_ROLL_VALUE;

    sRollRecoveryTimer = 0;
    sRollPhaseTimer = 0;
    sRollHasIFrames = true;

    COND_HOOK(OnPlayerRollStart, roll, [](Player* player, float* animSpeed) {
        *animSpeed = ROLL_ANIM_SPEED;
        sRollHasIFrames = (sRollRecoveryTimer == 0);
        sRollRecoveryTimer = ROLL_RECOVERY;
    });

    COND_VB_SHOULD(VB_PLAYER_ROLL_IFRAMES, roll, {
        Player* player = va_arg(args, Player*);
        s32* rollIFrames = va_arg(args, s32*);
        *should = sRollHasIFrames && LinkAnimation_OnFrame(&player->skelAnime, ROLL_IFRAME_START);
        if (*should) {
            *rollIFrames = ROLL_IFRAMES;
            sRollPhaseTimer = ROLL_IFRAMES;
        }
    });

    COND_VB_SHOULD(VB_PLAYER_MODIFY_ROLL_SPEED, roll, {
        Player* player = va_arg(args, Player*);
        f32* speedTarget = va_arg(args, f32*);
        *speedTarget *= ROLL_SPEED_SCALE;
        if ((*speedTarget < ROLL_SPEED_MIN) || (player->controlStickDirections[player->controlStickDataIndex] != 0)) {
            *speedTarget = ROLL_SPEED_MIN;
        }
        *should = false;
    });

    // During the i-frames the body isn't registered as hittable at all, so enemy weapons pass
    // through instead of triggering a block/bounce. Link's own attacks still register.
    COND_VB_SHOULD(VB_PLAYER_SET_BODY_AC, roll, {
        if (sRollPhaseTimer > 0) {
            *should = false;
        }
    });

    COND_HOOK(OnPlayerUpdate, roll, []() {
        Player* player = GET_PLAYER(gPlayState);

        if (sRollRecoveryTimer > 0) {
            sRollRecoveryTimer--;
        }
        // A roll cut short (a bonk, a hit, a cutscene) ends its i-frame window with it.
        if ((sRollPhaseTimer > 0) && (player->actionFunc != Player_Action_Roll)) {
            sRollPhaseTimer = 0;
        } else if (sRollPhaseTimer > 0) {
            sRollPhaseTimer--;
        }
    });
}

static RegisterShipInitFunc rollInitFunc(RegisterSoulsRoll, { CVAR_SOULS_ROLL_NAME });
