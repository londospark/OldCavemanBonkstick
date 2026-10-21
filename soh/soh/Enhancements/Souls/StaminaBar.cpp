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
int16_t OTRGetRectDimensionFromLeftEdge(float v);
int16_t OTRGetRectDimensionFromRightEdge(float v);
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

// Top-left of the bar's frame. HUD columns and rows are 16-bit, matching the gfx rectangles the bar is
// drawn with.
struct StaminaBarFrame {
    s16 x;
    s16 y;
};

// The magic meter's HUD editor stores whole-pixel offsets from a screen edge, but the OTR anchor
// helpers apply the aspect ratio in float pixels. This is the only place the two meet: the rect
// helpers already floor the float result for us, which also keeps the bar on the same pixel grid as
// the magic meter's own "original location" slot. Everything downstream stays in whole pixels.
static s32 AnchoredColumn(bool fromRight, s32 offset) {
    const f32 offsetPx = (f32)offset; // CVar offsets are ints; the anchor helpers take float pixels
    return fromRight ? OTRGetRectDimensionFromRightEdge(offsetPx) : OTRGetRectDimensionFromLeftEdge(offsetPx);
}

// Mirrors Interface_DrawMagicBar's placement rules: same anchors, same margins, same slot under the
// hearts. The offsets come from int CVars and int magic-meter registers, so the arithmetic stays in
// ints and narrows once, on the way out.
static StaminaBarFrame GetStaminaBarFrame() {
    const s32 magicDrop = R_MAGIC_BAR_LARGE_Y - R_MAGIC_BAR_SMALL_Y + 2;
    const s32 lineLength = CVarGetInteger(CVAR_COSMETIC("HUD.Hearts.LineLength"), 10);
    const s32 heartRows = (lineLength == 0) ? 0 : (gSaveContext.healthCapacity - 1) / (0x10 * lineLength);
    const bool useMargins = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.UseMargins"), 0) != 0;
    const s32 posType = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosType"), ORIGINAL_LOCATION);
    const s32 posX = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosX"), 0);
    const s32 posY = CVarGetInteger(CVAR_COSMETIC("HUD.MagicBar.PosY"), 0);
    const s32 leftMargin = useMargins ? Left_HUD_Margin : 0;
    const s32 rightMargin = useMargins ? Right_HUD_Margin : 0;
    const s32 topMargin = useMargins ? -Top_HUD_Margin : 0;

    s32 x = 0;
    s32 y = 0;

    switch (posType) {
        case ANCHOR_LEFT:
            x = AnchoredColumn(false, posX + leftMargin);
            y = posY + topMargin;
            break;
        case ANCHOR_RIGHT:
            x = AnchoredColumn(true, posX + rightMargin);
            y = posY + topMargin;
            break;
        case ANCHOR_NONE:
            x = posX + leftMargin;
            y = posY + topMargin;
            break;
        case ANCHOR_TO_LIFE_METER:
            x = posX + getHealthMeterXOffset() + R_MAGIC_BAR_X - 1;
            y = R_MAGIC_BAR_SMALL_Y - 2 + magicDrop * heartRows + posY + getHealthMeterYOffset();
            break;
        default: // ORIGINAL_LOCATION, or the magic meter is hidden: use the original slot under the hearts.
            x = AnchoredColumn(false, R_MAGIC_BAR_X + leftMargin);
            y = (heartRows >= 1) ? R_MAGIC_BAR_LARGE_Y + topMargin + magicDrop * (heartRows - 1)
                                 : R_MAGIC_BAR_SMALL_Y + topMargin;
            break;
    }

    if ((gSaveContext.magicLevel != 0) && (posType != HIDDEN)) {
        y += STAMINA_BAR_MAGIC_GAP;
    }

    return { (s16)x, (s16)y };
}

static void DrawStaminaBar(bool fullUi) {
    PlayState* play = gPlayState;
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    s32 stamina = Stamina_Get();

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
    s32 fill = (STAMINA_BAR_WIDTH * stamina) / Stamina_GetMax();
    if (fill > STAMINA_BAR_WIDTH) {
        fill = STAMINA_BAR_WIDTH;
    }
    // healthAlpha is a u16 that never holds more than a byte, and the gfx macros take a u8.
    const u8 alpha = (u8)interfaceCtx->healthAlpha;

    const StaminaBarFrame frame = GetStaminaBarFrame();
    const s32 fillX = frame.x + (R_MAGIC_FILL_X - R_MAGIC_BAR_X);
    // The gfx rectangles are 16-bit as well, so the derived edges are computed in that space.
    const s16 midX = (s16)(frame.x + 8);
    const s16 capX = (s16)(frame.x + STAMINA_BAR_WIDTH + 8);
    const s16 capEndX = (s16)(frame.x + STAMINA_BAR_WIDTH + 16);

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_39Overlay(play->state.gfxCtx);

    // Frame: the magic meter's left cap, stretched middle, and mirrored right cap.
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, kStaminaBorder.r, kStaminaBorder.g, kStaminaBorder.b, alpha);
    gDPSetEnvColor(OVERLAY_DISP++, 100, 50, 50, 255);
    OVERLAY_DISP =
        Gfx_TextureIA8(OVERLAY_DISP, (void*)gMagicMeterEndTex, 8, 16, frame.x, frame.y, 8, 16, 1 << 10, 1 << 10);
    OVERLAY_DISP = Gfx_TextureIA8(OVERLAY_DISP, (void*)gMagicMeterMidTex, 24, 16, midX, frame.y, STAMINA_BAR_WIDTH, 16,
                                  1 << 10, 1 << 10);
    gDPLoadTextureBlock(OVERLAY_DISP++, gMagicMeterEndTex, G_IM_FMT_IA, G_IM_SIZ_8b, 8, 16, 0, G_TX_MIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, 3, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPWideTextureRectangle(OVERLAY_DISP++, capX << 2, frame.y << 2, capEndX << 2, (frame.y + 16) << 2, G_TX_RENDERTILE,
                            256, 0, 1 << 10, 1 << 10);

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
        gSPWideTextureRectangle(OVERLAY_DISP++, fillX << 2, (frame.y + 3) << 2, (fillX + fill) << 2,
                                (frame.y + 10) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
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
