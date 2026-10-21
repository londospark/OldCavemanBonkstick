#include <unordered_map>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/Souls/Souls.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// OldCavemanBonkstick: no contact damage. Some enemies keep their hitbox on the whole time they're active
// (Queen Gohma whenever she isn't stunned, her larvae, ...), so brushing against one after a missed swing
// hurts even though it isn't attacking. A hit to Link's body is ignored only when all of these hold:
//   - the part that hit him is also the enemy's hurtbox (its body). Pure hitboxes - claws, weapons,
//     projectiles - always count;
//   - the enemy is standing still: not walking/charging across the ground, not jumping or falling;
//   - the part that hit him barely moved since the previous frame, so it isn't a swing, bite or lunge.
// Anything else, including a hit from a part with no recorded history, hurts as normal.
//
// Part speeds come from the hitbox list at the end of each frame (OnGameFrameUpdate): it holds the
// positions the next frame's collision check uses, so the last two recordings are the part's movement at
// the moment it hit.

#define CONTACT_MAX_ACTOR_SPEED 3.0f // units/frame across the ground; faster is a charge
#define CONTACT_MAX_ACTOR_VY 1.0f    // jumping or falling onto Link is an attack
#define CONTACT_MAX_PART_SPEED 4.0f  // units/frame; a swing, bite or lunge moves faster

struct PartTrack {
    Vec3f pos;
    Vec3f prevPos;
    u32 frame;
};

static std::unordered_map<const ColliderInfo*, PartTrack> sParts;
static u32 sFrame = 0;

static void TrackPart(const ColliderInfo* info, Vec3f pos) {
    PartTrack& track = sParts[info];
    track.prevPos = (track.frame == sFrame - 1) ? track.pos : pos;
    track.pos = pos;
    track.frame = sFrame;
}

static Vec3f Vec3sToF(const Vec3s& v) {
    return { (f32)v.x, (f32)v.y, (f32)v.z };
}

static void TrackHitboxes() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    sFrame++;

    CollisionCheckContext* colChkCtx = &gPlayState->colChkCtx;
    for (s32 i = 0; i < colChkCtx->colATCount; i++) {
        Collider* collider = colChkCtx->colAT[i];
        if ((collider == NULL) || (collider->actor == NULL) ||
            ((collider->actor->category != ACTORCAT_ENEMY) && (collider->actor->category != ACTORCAT_BOSS))) {
            continue;
        }

        switch (collider->shape) {
            case COLSHAPE_JNTSPH: {
                ColliderJntSph* jntSph = (ColliderJntSph*)collider;
                for (s32 j = 0; j < jntSph->count; j++) {
                    TrackPart(&jntSph->elements[j].info, Vec3sToF(jntSph->elements[j].dim.worldSphere.center));
                }
                break;
            }
            case COLSHAPE_CYLINDER: {
                ColliderCylinder* cylinder = (ColliderCylinder*)collider;
                TrackPart(&cylinder->info, Vec3sToF(cylinder->dim.pos));
                break;
            }
            case COLSHAPE_TRIS: {
                ColliderTris* tris = (ColliderTris*)collider;
                for (s32 j = 0; j < tris->count; j++) {
                    const Vec3f* vtx = tris->elements[j].dim.vtx;
                    TrackPart(&tris->elements[j].info,
                              { (vtx[0].x + vtx[1].x + vtx[2].x) / 3.0f, (vtx[0].y + vtx[1].y + vtx[2].y) / 3.0f,
                                (vtx[0].z + vtx[1].z + vtx[2].z) / 3.0f });
                }
                break;
            }
            case COLSHAPE_QUAD: {
                ColliderQuad* quad = (ColliderQuad*)collider;
                const Vec3f* q = quad->dim.quad;
                TrackPart(&quad->info,
                          { (q[0].x + q[1].x + q[2].x + q[3].x) / 4.0f, (q[0].y + q[1].y + q[2].y + q[3].y) / 4.0f,
                            (q[0].z + q[1].z + q[2].z + q[3].z) / 4.0f });
                break;
            }
            default:
                break;
        }
    }

    // Forget parts that stopped registering a hitbox (and colliders that were freed with their actor).
    if ((sFrame % 60) == 0) {
        for (auto it = sParts.begin(); it != sParts.end();) {
            it = (sFrame - it->second.frame > 2) ? sParts.erase(it) : std::next(it);
        }
    }
}

static bool IsContactOnly(Player* player) {
    const Actor* attacker = player->cylinder.base.ac;
    const ColliderInfo* part = player->cylinder.info.acHitInfo;
    if ((attacker == NULL) || (part == NULL) || !(part->bumperFlags & BUMP_ON)) {
        return false;
    }
    if (((attacker->category != ACTORCAT_ENEMY) && (attacker->category != ACTORCAT_BOSS)) ||
        (fabsf(attacker->speedXZ) >= CONTACT_MAX_ACTOR_SPEED) ||
        (fabsf(attacker->velocity.y) >= CONTACT_MAX_ACTOR_VY)) {
        return false;
    }

    auto track = sParts.find(part);
    if ((track == sParts.end()) || (sFrame - track->second.frame > 1)) {
        return false;
    }
    return Math_Vec3f_DistXYZ(&track->second.pos, &track->second.prevPos) < CONTACT_MAX_PART_SPEED;
}

static void RegisterContactDamage() {
    bool enabled = CVAR_SOULS_CONTACT_VALUE;
    sParts.clear();

    COND_HOOK(OnGameFrameUpdate, enabled, TrackHitboxes);
    COND_VB_SHOULD(VB_PLAYER_TAKE_BODY_HIT, enabled, {
        Player* player = va_arg(args, Player*);
        if (IsContactOnly(player)) {
            *should = false;
            player->actor.colChkInfo.damage = 0;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterContactDamage, { CVAR_SOULS_CONTACT_NAME });
