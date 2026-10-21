#include <unordered_map>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/Souls/Souls.h"
#include "soh/ShipUtils.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "textures/parameter_static/parameter_static.h"
Gfx* Gfx_TextureIA8(Gfx* displayListHead, void* texture, s16 textureWidth, s16 textureHeight, s16 rectLeft, s16 rectTop,
                    s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy);
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
extern PlayState* gPlayState;
}

// OldCavemanBonkstick: Dark Souls-style boss health bar. A long bar centred near the bottom of the screen,
// in the magic meter's frame, with the boss's name above it. Damage shows as a lighter chunk that holds
// for a moment and then drains. Hidden during cutscenes and once the boss is dead; fades with the hearts.
//
// Bosses keep their HP in colChkInfo.health. The maximum is the highest value seen since the boss spawned
// (Gohma, for one, raises hers from 8 to 10 just after spawning). When a boss is made of several actors
// with the same id, the bar follows the one with the most HP.

#define BOSS_BAR_WIDTH 192         // fill width, in N64 screen units (screen is 320 wide)
#define BOSS_BAR_Y 196             // top of the frame
#define BOSS_BAR_NAME_GAP 13       // name baseline above the frame
#define BOSS_BAR_NAME_SCALE 0.75f  // the message font is 16px; draw it at 12px
#define BOSS_BAR_TRAIL_HOLD 10     // frames the damage chunk holds before draining (~0.5s @ 20fps)
#define BOSS_BAR_TRAIL_DRAIN 0.08f // HP per frame the chunk drains, as a fraction of max HP

static const Color_RGB8 kBossFill = { 190, 20, 20 };
static const Color_RGB8 kBossTrail = { 230, 200, 120 };
static const Color_RGB8 kBossName = { 255, 255, 255 };

static const char* BossName(s16 actorId) {
    switch (actorId) {
        case ACTOR_BOSS_GOMA:
            return "Queen Gohma";
        case ACTOR_BOSS_DODONGO:
            return "King Dodongo";
        case ACTOR_BOSS_VA:
            return "Barinade";
        case ACTOR_BOSS_GANONDROF:
            return "Phantom Ganon";
        case ACTOR_BOSS_FD:
        case ACTOR_BOSS_FD2:
            return "Volvagia";
        case ACTOR_BOSS_MO:
            return "Morpha";
        case ACTOR_BOSS_SST:
            return "Bongo Bongo";
        case ACTOR_BOSS_TW:
            return "Twinrova";
        case ACTOR_BOSS_GANON:
            return "Ganondorf";
        case ACTOR_BOSS_GANON2:
            return "Ganon";
        default:
            return NULL;
    }
}

static std::unordered_map<const Actor*, s32> sMaxHealth;
static const Actor* sTrailActor = NULL;
static f32 sTrailHealth = 0.0f;
static s16 sTrailHold = 0;

// Health as a signed value: several bosses subtract past zero, which wraps the u8 (see BossGoma_UpdateHit).
static s32 BossHealth(const Actor* actor) {
    return MAX(0, (s8)actor->colChkInfo.health);
}

static Actor* FindBoss() {
    Actor* best = NULL;
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head; actor != NULL; actor = actor->next) {
        if (BossName(actor->id) == NULL) {
            continue;
        }
        s32& max = sMaxHealth[actor];
        max = MAX(max, BossHealth(actor));
        if ((max > 0) && ((best == NULL) || (max > sMaxHealth[best]))) {
            best = actor;
        }
    }
    return best;
}

static s16 DrawNameText(Gfx** gfxp, const char* text, s16 x, s16 y, u8 alpha) {
    Gfx* gfx = *gfxp;
    const u16 scaleStep = (u16)((1 << 10) / BOSS_BAR_NAME_SCALE);
    const s16 size = (s16)(FONT_CHAR_TEX_WIDTH * BOSS_BAR_NAME_SCALE);

    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
    for (const char* c = text; *c != '\0'; c++) {
        if (*c != ' ') {
            void* texture = Ship_GetCharFontTexture((u8)*c);
            gDPLoadTextureBlock_4b(gfx++, texture, G_IM_FMT_I, FONT_CHAR_TEX_WIDTH, FONT_CHAR_TEX_HEIGHT, 0,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);
            // shadow, then the letter
            gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, alpha);
            gSPWideTextureRectangle(gfx++, (x + 1) << 2, (y + 1) << 2, (x + 1 + size) << 2, (y + 1 + size) << 2,
                                    G_TX_RENDERTILE, 0, 0, scaleStep, scaleStep);
            gDPSetPrimColor(gfx++, 0, 0, kBossName.r, kBossName.g, kBossName.b, alpha);
            gSPWideTextureRectangle(gfx++, x << 2, y << 2, (x + size) << 2, (y + size) << 2, G_TX_RENDERTILE, 0, 0,
                                    scaleStep, scaleStep);
        }
        x += (s16)(Ship_GetCharFontWidth((u8)*c) * BOSS_BAR_NAME_SCALE);
    }
    *gfxp = gfx;
    return x;
}

static void DrawBossHealthBar(bool fullUi) {
    PlayState* play = gPlayState;
    Player* player = GET_PLAYER(play);
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    if ((play->csCtx.state != CS_STATE_IDLE) || (player->csAction != 0) || (interfaceCtx->healthAlpha == 0)) {
        return;
    }

    Actor* boss = FindBoss();
    if ((boss == NULL) || (BossHealth(boss) <= 0)) {
        sTrailActor = NULL;
        return;
    }

    const s32 max = sMaxHealth[boss];
    const s32 health = MIN(BossHealth(boss), max);
    if (sTrailActor != boss) {
        sTrailActor = boss;
        sTrailHealth = (f32)health;
        sTrailHold = 0;
    }
    if ((f32)health < sTrailHealth) {
        if (sTrailHold < BOSS_BAR_TRAIL_HOLD) {
            sTrailHold++;
        } else {
            sTrailHealth = MAX((f32)health, sTrailHealth - ((f32)max * BOSS_BAR_TRAIL_DRAIN));
        }
    } else {
        sTrailHealth = (f32)health; // healed, or caught up
        sTrailHold = 0;
    }

    const u8 alpha = (u8)CLAMP(interfaceCtx->healthAlpha, 0, 255);
    const s16 frameX = (SCREEN_WIDTH / 2) - ((BOSS_BAR_WIDTH + 16) / 2);
    const s16 fillX = frameX + 8;
    const s16 y = BOSS_BAR_Y;
    const s16 fill = (s16)((BOSS_BAR_WIDTH * health) / max);
    const s16 trail = (s16)(((f32)BOSS_BAR_WIDTH * sTrailHealth) / (f32)max);

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_39Overlay(play->state.gfxCtx);

    // Frame: the magic meter's left cap, stretched middle, and mirrored right cap.
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, alpha);
    gDPSetEnvColor(OVERLAY_DISP++, 100, 50, 50, 255);
    OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (void*)gMagicMeterEndTex, 8, 16, frameX, y, 8, 16, 1 << 10, 1 << 10);
    OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (void*)gMagicMeterMidTex, 24, 16, frameX + 8, y, BOSS_BAR_WIDTH, 16,
                                  1 << 10, 1 << 10);
    gDPLoadTextureBlock(OVERLAY_DISP++, gMagicMeterEndTex, G_IM_FMT_IA, G_IM_SIZ_8b, 8, 16, 0, G_TX_MIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, 3, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPWideTextureRectangle(OVERLAY_DISP++, (frameX + BOSS_BAR_WIDTH + 8) << 2, y << 2,
                            (frameX + BOSS_BAR_WIDTH + 16) << 2, (y + 16) << 2, G_TX_RENDERTILE, 256, 0, 1 << 10,
                            1 << 10);

    // Fill and damage trail, drawn like the magic fill.
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE);
    gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
    gDPLoadMultiBlock_4b(OVERLAY_DISP++, gMagicMeterFillTex, 0, G_TX_RENDERTILE, G_IM_FMT_I, 16, 16, 0,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                         G_TX_NOLOD);
    if (trail > fill) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, kBossTrail.r, kBossTrail.g, kBossTrail.b, alpha);
        gSPWideTextureRectangle(OVERLAY_DISP++, (fillX + fill) << 2, (y + 3) << 2, (fillX + trail) << 2, (y + 10) << 2,
                                G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }
    if (fill > 0) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, kBossFill.r, kBossFill.g, kBossFill.b, alpha);
        gSPWideTextureRectangle(OVERLAY_DISP++, fillX << 2, (y + 3) << 2, (fillX + fill) << 2, (y + 10) << 2,
                                G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }

    // Name, left-aligned over the bar.
    DrawNameText(&OVERLAY_DISP, BossName(boss->id), fillX, y - BOSS_BAR_NAME_GAP, alpha);

    CLOSE_DISPS(play->state.gfxCtx);
}

static void RegisterBossHealthBar() {
    sMaxHealth.clear();
    sTrailActor = NULL;

    COND_HOOK(AfterInterfaceDrawMagicBar, CVAR_SOULS_BOSS_BAR_VALUE, DrawBossHealthBar);
    // Forget bosses from the last room so a reused actor address doesn't inherit an old maximum.
    COND_HOOK(OnSceneInit, CVAR_SOULS_BOSS_BAR_VALUE, [](int16_t) {
        sMaxHealth.clear();
        sTrailActor = NULL;
    });
}

static RegisterShipInitFunc initFunc(RegisterBossHealthBar, { CVAR_SOULS_BOSS_BAR_NAME });
