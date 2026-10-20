#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <fast/resource/type/DisplayList.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/Souls/Souls.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
}

// OldCavemanBonkstick: the Reinforced Club is never carried on Link's back, so the sword and sheath
// are taken off his back models while the shield stays.
//
// This edits vanilla's own display lists in memory instead of shipping replacement models: every
// triangle drawn with a sword or sheath texture becomes a no-op, and everything else (the shield,
// its textures, all render state) is left exactly as vanilla has it. HD texture packs therefore keep
// applying to the shield on Link's back, the low-detail "Far" models still work, and turning the club
// off restores the vanilla back. Display lists replaced by a model mod are left alone
// (ResourceMgr_PatchGfxByName skips custom assets).

static const char* sBackDLs[] = {
    gLinkChildDekuShieldSwordAndSheathNearDL,
    gLinkChildDekuShieldSwordAndSheathFarDL,
    gLinkChildDekuShieldAndSheathNearDL,
    gLinkChildDekuShieldAndSheathFarDL,
    gLinkChildHylianShieldSwordAndSheathNearDL,
    gLinkChildHylianShieldSwordAndSheathFarDL,
    gLinkChildHylianShieldAndSheathNearDL,
    gLinkChildHylianShieldAndSheathFarDL,
    gLinkChildSwordAndSheathNearDL,
    gLinkChildSwordAndSheathFarDL,
    gLinkChildSheathNearDL,
    gLinkChildSheathFarDL,
    gLinkAdultHylianShieldSwordAndSheathNearDL,
    gLinkAdultHylianShieldSwordAndSheathFarDL,
    gLinkAdultHylianShieldAndSheathNearDL,
    gLinkAdultHylianShieldAndSheathFarDL,
    gLinkAdultMirrorShieldSwordAndSheathNearDL,
    gLinkAdultMirrorShieldSwordAndSheathFarDL,
    gLinkAdultMirrorShieldAndSheathNearDL,
    gLinkAdultMirrorShieldAndSheathFarDL,
    gLinkAdultMasterSwordAndSheathNearDL,
    gLinkAdultMasterSwordAndSheathFarDL,
    gLinkAdultSheathNearDL,
    gLinkAdultSheathFarDL,
};

// F3DEX2 triangle opcodes, and the libultraship commands that take a second word for a CRC64 hash.
static constexpr uint8_t kOpTri1 = 0x05;
static constexpr uint8_t kOpTri2 = 0x06;
static constexpr uint8_t kOpQuad = 0x07;
static constexpr uint8_t kOpSetTImgOtrHash = 0x20;
static constexpr uint8_t kOpDlOtrHash = 0x31;
static constexpr uint8_t kOpVtxOtrHash = 0x32;
static constexpr uint8_t kOpMarker = 0x33;

// (display list, patch name) for every triangle currently patched out
static std::vector<std::pair<std::string, std::string>> sPatches;

// Whether triangles drawn after this texture belong to the sword or sheath. Palettes (TLUTs) are
// loaded alongside a texture and don't change which part is being drawn.
static bool UpdateStripFromTexture(uint64_t hash, bool strip) {
    const char* name = ResourceGetNameByCrc(hash);
    if (name == nullptr || strstr(name, "TLUT") != nullptr) {
        return strip;
    }
    return (strstr(name, "Sheath") != nullptr) || (strstr(name, "Sword") != nullptr);
}

static void RemoveSheathFromBack() {
    if (!sPatches.empty()) {
        return;
    }

    for (const char* dlName : sBackDLs) {
        auto dl = std::static_pointer_cast<Fast::DisplayList>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(dlName));
        if (dl == nullptr || dl->GetInitData()->IsCustom) {
            continue;
        }

        std::vector<int> triangles;
        bool strip = false;
        for (size_t i = 0; i < dl->Instructions.size(); i++) {
            const Gfx* gfx = (const Gfx*)&dl->Instructions[i];
            const uint8_t opcode = (gfx->words.w0 >> 24) & 0xFF;

            if (opcode == kOpSetTImgOtrHash || opcode == kOpDlOtrHash || opcode == kOpVtxOtrHash ||
                opcode == kOpMarker) {
                if (i + 1 >= dl->Instructions.size()) {
                    break;
                }
                if (opcode == kOpSetTImgOtrHash) {
                    const Gfx* hashWords = gfx + 1;
                    strip = UpdateStripFromTexture(
                        ((uint64_t)hashWords->words.w0 << 32) | (uint32_t)hashWords->words.w1, strip);
                }
                i++;
            } else if (strip && (opcode == kOpTri1 || opcode == kOpTri2 || opcode == kOpQuad)) {
                triangles.push_back((int)i);
            }
        }

        for (int index : triangles) {
            std::string patchName = "SoulsClubBack" + std::to_string(index);
            ResourceMgr_PatchGfxByName(dlName, patchName.c_str(), index, gsDPNoOp());
            sPatches.emplace_back(dlName, patchName);
        }
    }

    SPDLOG_INFO("[Souls] Removed {} sword/sheath triangle commands from Link's back models", sPatches.size());
}

static void RestoreSheathOnBack() {
    for (const auto& [dlName, patchName] : sPatches) {
        ResourceMgr_UnpatchGfxByName(dlName.c_str(), patchName.c_str());
    }
    sPatches.clear();
}

static void RegisterClubBackModel() {
    if (CVAR_SOULS_CLUB_VALUE) {
        RemoveSheathFromBack();
    } else {
        RestoreSheathOnBack();
    }

    // Resources can be reloaded (scene changes, toggling alt assets); patch them again when they are.
    COND_HOOK(OnSceneInit, CVAR_SOULS_CLUB_VALUE, [](int16_t) {
        RestoreSheathOnBack();
        RemoveSheathFromBack();
    });
    COND_HOOK(OnAssetAltChange, CVAR_SOULS_CLUB_VALUE, []() {
        RestoreSheathOnBack();
        RemoveSheathFromBack();
    });
}

static RegisterShipInitFunc initFunc(RegisterClubBackModel, { CVAR_SOULS_CLUB_NAME });
