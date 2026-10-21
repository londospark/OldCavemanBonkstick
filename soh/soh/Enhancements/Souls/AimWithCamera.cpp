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

// OldCavemanBonkstick: aim where the camera looks. First-person view (C-Up, or drawing the slingshot, bow,
// hookshot...) always starts in the direction Link faces. With the camera turned freely on the right
// stick, that can be somewhere the player wasn't looking, so on the frame Link enters first person he
// turns to face the camera's direction first. Not while Z-targeting (that aims at the target already) or
// riding Epona.
//
// unk_6AD: 0 = normal, 1 = first person (C-Up), 2 = first person aiming an item. The camera updates after
// all actors, so turning Link in OnPlayerUpdate on the frame it changes is what the first-person camera
// starts from.

static void RegisterAimWithCamera() {
    COND_HOOK(OnPlayerUpdate, CVAR_SOULS_AIM_CAMERA_VALUE, []() {
        static u8 sLastCameraMode = 0;
        Player* player = GET_PLAYER(gPlayState);
        const u8 mode = player->unk_6AD;
        const bool entered = (sLastCameraMode == 0) && ((mode == 1) || (mode == 2));
        sLastCameraMode = mode;

        if (!entered || (player->focusActor != NULL) || (player->stateFlags1 & PLAYER_STATE1_ON_HORSE)) {
            return;
        }

        const s16 cameraYaw = Camera_GetCamDirYaw(GET_ACTIVE_CAM(gPlayState));
        // First person looks along focus.rot.y and recomputes Link's facing from it (func_8084ABD8), so
        // the look direction has to turn too.
        player->actor.focus.rot.y = cameraYaw;
        player->actor.shape.rot.y = cameraYaw;
        player->actor.world.rot.y = cameraYaw;
        player->yaw = cameraYaw;
    });
}

static RegisterShipInitFunc initFunc(RegisterAimWithCamera, { CVAR_SOULS_AIM_CAMERA_NAME });
