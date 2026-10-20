#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/Souls/Stamina.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "textures/parameter_static/parameter_static.h"
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
int16_t OTRGetRectDimensionFromLeftEdge(float v);
Gfx* Gfx_TextureIA8(Gfx* displayListHead, void* texture, s16 textureWidth, s16 textureHeight, s16 rectLeft, s16 rectTop,
                    s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy);
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
extern PlayState* gPlayState;
extern SaveContext gSaveContext;
extern s16 Top_HUD_Margin;
extern s16 Left_HUD_Margin;
extern s16 Right_HUD_Margin;
}

// OldCavemanBonkstick: Dark Souls-style stamina bar, framed exactly like the magic meter (same end caps
// and middle, same fill height). It sits where the magic meter would, or just under it when Link has
// magic, and follows the magic meter's HUD editor placement and margins. Debt isn't drawn: below zero
// the bar simply stays empty, as in Dark Souls.
//
// While stamina is on, the magic meter is blue so the two green bars can't be mistaken for each other.
// A magic colour picked in the Cosmetics Editor still wins.

#define STAMINA_BAR_WIDTH 96     // the width of a double magic meter, so the two line up
#define STAMINA_BAR_MAGIC_GAP 13 // drop below a visible magic meter (its frame is 16 tall with a soft edge)

static const Color_RGB8 kStaminaFill = { 60, 200, 60 };
static const Color_RGB8 kStaminaBorder = { 255, 255, 255 }; // the magic meter's default border
static const Color_RGB8 kMagicBlue = { 30, 100, 255 };
static const Color_RGB8 kMagicGreen = { 0, 200, 0 }; // vanilla R_MAGIC_FILL_COLOR (z_construct.c)

// Top-left of the bar's frame, mirroring Interface_DrawMagicBar's position rules.
static void GetStaminaBarPos(s16* x, s16* y) {
    const s16 magicDrop = R_MAGIC_BAR_LARGE_Y - R_MAGIC_BAR_SMALL_Y + 2;
    const s32 lineLength = CVarGetInteger(CVAR_COSMETIC("HUD.Hearts.LineLength"), 10);
    const s32 heartRows = (lineLength == 0) ? 0 : (gSaveContext.healthCapacity - 1) / (0x10 * lineLength);
    const bool useMargins = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.UseMargins"), 0) != 0;
    const s32 posType = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosType"), ORIGINAL_LOCATION);
    const s32 posX = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosX"), 0);
    const s16 yMargin = useMargins ? -Top_HUD_Margin : 0;

    switch (posType) {
        case ANCHOR_LEFT:
            *x = OTRGetDimensionFromLeftEdge(posX + (useMargins ? Left_HUD_Margin : 0));
            *y = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosY"), 0) + yMargin;
            break;
        case ANCHOR_RIGHT:
            *x = OTRGetDimensionFromRightEdge(posX + (useMargins ? Right_HUD_Margin : 0));
            *y = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosY"), 0) + yMargin;
            break;
        case ANCHOR_NONE:
            *x = posX + (useMargins ? Left_HUD_Margin : 0);
            *y = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosY"), 0) + yMargin;
            break;
        case ANCHOR_TO_LIFE_METER:
            *x = posX + getHealthMeterXOffset() + R_MAGIC_BAR_X - 1;
            *y = R_MAGIC_BAR_SMALL_Y - 2 + magicDrop * heartRows +
                 CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosY"), 0) + getHealthMeterYOffset();
            break;
        default: // ORIGINAL_LOCATION, or the magic meter is hidden: use the original slot under the hearts.
            *x = OTRGetRectDimensionFromLeftEdge(R_MAGIC_BAR_X + (useMargins ? Left_HUD_Margin : 0));
            *y = (heartRows >= 1) ? R_MAGIC_BAR_LARGE_Y + yMargin + magicDrop * (heartRows - 1)
                                  : R_MAGIC_BAR_SMALL_Y + yMargin;
            break;
    }

    if ((gSaveContext.magicLevel != 0) && (posType != HIDDEN)) {
        *y += STAMINA_BAR_MAGIC_GAP;
    }
}

static void DrawStaminaBar(bool fullUi) {
    PlayState* play = gPlayState;
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    s32 stamina = Stamina_Get();
    s16 x;
    s16 y;

    // Same visibility as the hearts, plus whenever the bar isn't full so Minimal UI still shows it in a fight.
    if (!fullUi && (gSaveContext.health == gSaveContext.healthCapacity) && (stamina >= Stamina_GetMax())) {
        return;
    }
    // Fade in and out with the hearts (cutscenes, text boxes, minigames).
    if (interfaceCtx->healthAlpha == 0) {
        return;
    }

    if (stamina < 0) {
        stamina = 0;
    }
    s16 fill = (s16)((STAMINA_BAR_WIDTH * stamina) / Stamina_GetMax());
    if (fill > STAMINA_BAR_WIDTH) {
        fill = STAMINA_BAR_WIDTH;
    }
    const u8 alpha = interfaceCtx->healthAlpha;

    GetStaminaBarPos(&x, &y);
    const s16 fillX = x + (R_MAGIC_FILL_X - R_MAGIC_BAR_X);

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_39Overlay(play->state.gfxCtx);

    // Frame: the magic meter's left cap, stretched middle, and mirrored right cap.
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, kStaminaBorder.r, kStaminaBorder.g, kStaminaBorder.b, alpha);
    gDPSetEnvColor(OVERLAY_DISP++, 100, 50, 50, 255);
    OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (void*)gMagicMeterEndTex, 8, 16, x, y, 8, 16, 1 << 10, 1 << 10);
    OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (void*)gMagicMeterMidTex, 24, 16, x + 8, y, STAMINA_BAR_WIDTH, 16,
                                  1 << 10, 1 << 10);
    gDPLoadTextureBlock(OVERLAY_DISP++, gMagicMeterEndTex, G_IM_FMT_IA, G_IM_SIZ_8b, 8, 16, 0, G_TX_MIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, 3, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPWideTextureRectangle(OVERLAY_DISP++, (x + STAMINA_BAR_WIDTH + 8) << 2, y << 2, (x + STAMINA_BAR_WIDTH + 16) << 2,
                            (y + 16) << 2, G_TX_RENDERTILE, 256, 0, 1 << 10, 1 << 10);

    // Fill, drawn the same way as the magic fill.
    if (fill > 0) {
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE, PRIMITIVE,
                          ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE);
        gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, kStaminaFill.r, kStaminaFill.g, kStaminaFill.b, alpha);
        gDPLoadMultiBlock_4b(OVERLAY_DISP++, gMagicMeterFillTex, 0, G_TX_RENDERTILE, G_IM_FMT_I, 16, 16, 0,
                             G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                             G_TX_NOLOD);
        gSPWideTextureRectangle(OVERLAY_DISP++, fillX << 2, (y + 3) << 2, (fillX + fill) << 2, (y + 10) << 2,
                                G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

static void SetMagicFillColor(Color_RGB8 color) {
    if (gGameInfo == NULL) { // not allocated yet at startup; OnInterfaceUpdate sets it once the game runs
        return;
    }
    R_MAGIC_FILL_COLOR(0) = color.r;
    R_MAGIC_FILL_COLOR(1) = color.g;
    R_MAGIC_FILL_COLOR(2) = color.b;
}

static void RegisterStaminaBar() {
    bool stamina = CVAR_SOULS_STAMINA_VALUE;

    COND_HOOK(AfterInterfaceDrawMagicBar, stamina, DrawStaminaBar);

    // The magic fill colour lives in a debug register that z_construct.c resets on every scene load, so
    // set it every frame while stamina is on (as cheap as it gets), and put vanilla green back when off.
    COND_HOOK(OnInterfaceUpdate, stamina, []() { SetMagicFillColor(kMagicBlue); });
    SetMagicFillColor(stamina ? kMagicBlue : kMagicGreen);
}

static RegisterShipInitFunc initFunc(RegisterStaminaBar, { CVAR_SOULS_STAMINA_NAME });
