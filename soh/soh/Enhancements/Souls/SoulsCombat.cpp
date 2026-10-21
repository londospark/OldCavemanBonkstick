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
extern SaveContext gSaveContext;
void Player_Action_Roll(Player* player, PlayState* play);
void Player_Action_808502D0(Player* player, PlayState* play); // melee attack in progress
void func_80837C0C(PlayState* play, Player* player, s32 damageResponseType, f32 speed, f32 yVelocity, s16 yRot,
                   s32 invincibilityTimer);
}

// OldCavemanBonkstick: Souls combat. Each block below only registers while its feature is on.

// MARK: - Club aim assist

// The overhead smash sweeps a thin vertical slice: between hammer_hit frames 5 and 6 the club drops from
// overhead to the floor, then rests there until frame 10 (measured). Its tip lands 45 units ahead for
// child Link and 64 for adult. Anything not in that slice is missed, even when locked on, so during the
// wind-up Link turns toward his target (the Z-target, or the nearest enemy in front) and, if it's just
// out of reach, takes a small step in. The leaping slam is aimed at the target the same way.
#define AIM_CONE 0x3000            // +-67.5 degrees: enemies in front count without a lock-on
#define AIM_EXTRA_RANGE 30.0f      // how far past the club's reach a target is still picked up
#define AIM_MAX_HEIGHT 80.0f       // ignore enemies far above or below
#define AIM_TURN_STEP 0x1000       // 22.5 degrees per frame of wind-up
#define AIM_WINDUP_END 6.0f        // hammer_hit frame where the club comes down
#define AIM_STEP_IN_PER_FRAME 3.0f // step-in speed
#define AIM_STEP_IN_MAX 12.0f      // most a single swing will step in
#define AIM_LEDGE_TOLERANCE 10.0f  // only step onto floor this close to Link's height

static Actor* sAimTarget = NULL;
static bool sAimSwingStarted = false;
static f32 sAimSteppedIn = 0.0f;

// Where the club tip lands at impact, minus a little so the head of the club connects, not the very tip.
static f32 ClubReach() {
    return LINK_IS_ADULT ? 55.0f : 38.0f;
}

static bool IsAimable(Player* player, Actor* actor) {
    return (actor != NULL) && (actor != &player->actor) && (actor->update != NULL) &&
           (fabsf(actor->world.pos.y - player->actor.world.pos.y) <= AIM_MAX_HEIGHT);
}

// Z-target first; otherwise the closest, most centred targetable enemy in front of Link.
static Actor* FindAimTarget(Player* player) {
    if (IsAimable(player, player->focusActor)) {
        return player->focusActor;
    }

    Actor* best = NULL;
    f32 bestScore = 0.0f;
    for (s32 category : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[category].head; actor != NULL; actor = actor->next) {
            if (!IsAimable(player, actor) || !(actor->flags & ACTOR_FLAG_ATTENTION_ENABLED)) {
                continue;
            }
            const s16 offAngle =
                ABS((s16)(Actor_WorldYawTowardActor(&player->actor, actor) - player->actor.shape.rot.y));
            const f32 gap = Actor_WorldDistXZToActor(&player->actor, actor) - actor->colChkInfo.cylRadius;
            if ((offAngle > AIM_CONE) || (gap > ClubReach() + AIM_EXTRA_RANGE)) {
                continue;
            }
            const f32 score = gap * (1.0f + (f32)offAngle / AIM_CONE);
            if ((best == NULL) || (score < bestScore)) {
                best = actor;
                bestScore = score;
            }
        }
    }
    return best;
}

// The target can die or despawn mid-swing; only trust the pointer while it's still in an actor list.
static bool IsStillSpawned(Actor* target) {
    for (s32 category = 0; category < ACTORCAT_MAX; category++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[category].head; actor != NULL; actor = actor->next) {
            if (actor == target) {
                return true;
            }
        }
    }
    return false;
}

static void FaceActor(Player* player, s16 yaw) {
    player->actor.shape.rot.y = yaw;
    player->actor.world.rot.y = yaw;
    player->yaw = yaw;
}

// Step toward the target, but never off a ledge: the floor at the new spot must be about Link's height.
static void StepIn(Player* player, Actor* target) {
    const f32 gap =
        Actor_WorldDistXZToActor(&player->actor, target) - (target->colChkInfo.cylRadius * 0.5f) - ClubReach();
    const f32 step = MIN(MIN(gap, AIM_STEP_IN_PER_FRAME), AIM_STEP_IN_MAX - sAimSteppedIn);
    if (step <= 0.0f) {
        return;
    }

    Vec3f probe = player->actor.world.pos;
    probe.x += Math_SinS(player->actor.shape.rot.y) * step;
    probe.z += Math_CosS(player->actor.shape.rot.y) * step;
    probe.y += 30.0f;
    CollisionPoly* poly;
    s32 bgId;
    const f32 floorY = BgCheck_EntityRaycastFloor3(&gPlayState->colCtx, &poly, &bgId, &probe);
    if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - player->actor.world.pos.y) > AIM_LEDGE_TOLERANCE)) {
        return;
    }

    player->actor.world.pos.x = probe.x;
    player->actor.world.pos.z = probe.z;
    sAimSteppedIn += step;
}

static bool IsClubSmashWindup(Player* player) {
    return SOULS_IS_SWORD_SLOT_HELD(player) && (player->actionFunc == Player_Action_808502D0) &&
           ((player->meleeWeaponAnimation == PLAYER_MWA_HAMMER_FORWARD) ||
            (player->meleeWeaponAnimation == PLAYER_MWA_HAMMER_SIDE)) &&
           (player->skelAnime.curFrame < AIM_WINDUP_END);
}

static void AimLeapAtTarget(Player* player) {
    if (!CVAR_SOULS_AIM_VALUE) {
        return;
    }
    Actor* target = FindAimTarget(player);
    if (target != NULL) {
        FaceActor(player, Actor_WorldYawTowardActor(&player->actor, target));
    }
}

static void RegisterSoulsAim() {
    sAimTarget = NULL;
    sAimSwingStarted = false;

    COND_HOOK(OnPlayerUpdate, CVAR_SOULS_CLUB_VALUE && CVAR_SOULS_AIM_VALUE, []() {
        Player* player = GET_PLAYER(gPlayState);
        if (!IsClubSmashWindup(player)) {
            sAimSwingStarted = false;
            sAimTarget = NULL;
            return;
        }
        if (!sAimSwingStarted) {
            sAimSwingStarted = true;
            sAimTarget = FindAimTarget(player);
            sAimSteppedIn = 0.0f;
        }
        if ((sAimTarget == NULL) || !IsStillSpawned(sAimTarget)) {
            sAimTarget = NULL;
            return;
        }

        s16 yaw = player->actor.shape.rot.y;
        Math_ScaledStepToS(&yaw, Actor_WorldYawTowardActor(&player->actor, sAimTarget), AIM_TURN_STEP);
        FaceActor(player, yaw);
        StepIn(player, sAimTarget);
    });
}

static RegisterShipInitFunc aimInitFunc(RegisterSoulsAim, { CVAR_SOULS_CLUB_NAME, CVAR_SOULS_AIM_NAME });

// MARK: - Reinforced Club

// A club is slower than a sword, so it hits one weapon tier harder: the Kokiri-slot club deals Master
// Sword damage and the Master-slot club deals Biggoron damage (2x, e.g. 1 -> 2 per smash on Gohma). Only
// the sword damage flags move up a tier, never the Megaton Hammer's, so hammer-only objects (rusted
// switches etc.) still need the real hammer.
static const u32 kClubSlashFlags[] = { 0x00000200, 0x00000400 }; // Master slash, Giant slash
static const u32 kClubJumpFlags[] = { 0x08000000, 0x04000000 };  // Master jump, Giant jump

// After the impact (hammer_hit frame 6) the rest of the smash can't be cancelled, and the follow-through
// locks Link's facing; both play faster so Link recovers sooner. The wind-up and impact are unchanged.
#define CLUB_IMPACT_FRAME 6.0f
#define CLUB_POST_IMPACT_SPEED 2.0f    // hammer_hit after the impact (normally 1.0)
#define CLUB_FOLLOWTHROUGH_SPEED 2.25f // hammer_hit_end (normally 1.5)

static void BoostClubDamage(Player* player) {
    const bool strongClub = (player->heldItemAction != PLAYER_IA_SWORD_KOKIRI) && !Player_HoldsBrokenKnife(player);
    const bool jump = (player->meleeWeaponAnimation >= PLAYER_MWA_FLIPSLASH_START) &&
                      (player->meleeWeaponAnimation <= PLAYER_MWA_JUMPSLASH_FINISH);
    const u32 flags = jump ? kClubJumpFlags[strongClub] : kClubSlashFlags[strongClub];
    player->meleeWeaponQuads[0].info.toucher.dmgFlags = flags;
    player->meleeWeaponQuads[1].info.toucher.dmgFlags = flags;
}

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
            if (*should) {
                AimLeapAtTarget(player); // the leap goes where Link faces (func_8083BA90)
            }
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

    // Plant Link's feet for the whole smash and its follow-through. The swing borrows the Megaton Hammer animation,
    // whose root motion is authored from the hammer's own leaned-back stance: started from the sword stance it yanks
    // Link ~7 units backwards (z -741 vs 0 in animation space) before lunging forward, which can walk him off a ledge.
    // ANIM_FLAG_NOMOVE is one-shot (SkelAnime_UpdateTranslation clears it), so it's set every frame; OnPlayerUpdate
    // runs before the queued move is applied.
    COND_HOOK(OnPlayerUpdate, club, []() {
        Player* player = GET_PLAYER(gPlayState);
        // The follow-through runs in a different action that keeps the root motion. It's the animation
        // playing on the first frame after the smash ends (func_8083A098), and lasts until anything else
        // plays; PLAYER_STATE3_FINISHED_ATTACKING can outlive it, so it isn't used.
        static const void* sFollowThroughAnim = NULL;
        static bool sWasSmashing = false;
        const bool smashing = (player->actionFunc == Player_Action_808502D0);
        if (smashing) {
            sFollowThroughAnim = NULL;
        } else if (sWasSmashing && (player->stateFlags3 & PLAYER_STATE3_FINISHED_ATTACKING)) {
            // set together with the follow-through animation; anything else (a hit, a roll) cut it short
            sFollowThroughAnim = player->skelAnime.animation;
        } else if (player->skelAnime.animation != sFollowThroughAnim) {
            sFollowThroughAnim = NULL;
        }
        sWasSmashing = smashing;
        const bool followThrough = !smashing && (sFollowThroughAnim != NULL);
        const bool swinging = smashing || followThrough;
        if (!SOULS_IS_SWORD_SLOT_HELD(player)) {
            return;
        }
        // Damage flags are stamped when an attack starts (func_80837948); raise them for every club attack,
        // smash or leaping slam, before this frame's collision checks run.
        if (player->meleeWeaponState != 0) {
            BoostClubDamage(player);
        }
        if (swinging && ((player->meleeWeaponAnimation == PLAYER_MWA_HAMMER_FORWARD) ||
                         (player->meleeWeaponAnimation == PLAYER_MWA_HAMMER_SIDE))) {
            player->skelAnime.movementFlags |= ANIM_FLAG_NOMOVE;
            if (smashing) {
                if (player->skelAnime.curFrame >= CLUB_IMPACT_FRAME) {
                    player->skelAnime.playSpeed = CLUB_POST_IMPACT_SPEED;
                }
            } else {
                player->skelAnime.playSpeed = CLUB_FOLLOWTHROUGH_SPEED;
            }
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
        // colChkInfo.damage is a byte; a quarter of the hit, but never zero.
        player->actor.colChkInfo.damage = (u8)MAX(1, damage / 4);
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
