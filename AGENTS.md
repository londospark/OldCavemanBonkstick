# OldCavemanBonkstick

A Ship of Harkinian (SoH) fork that "Dark Souls-ifies" Ocarina of Time for the streamer OldManBroski:
the hero is Blonk, swords are replaced by the Reinforced Club, and there is stamina, a Souls-style roll
and guard breaks. Everything else is upstream SoH (`upstream/develop`), which we merge in regularly.

## The one rule: game changes go through GameInteractor hooks

Do not write mod logic inside vanilla code (`soh/src/**`: `z_player.c`, `z_parameter.c`, actors...).
Vanilla files only get a **one-line hook call** at the point where behaviour should change; the logic
lives in an enhancement file under `soh/soh/Enhancements/Souls/`. This keeps our diff against upstream
tiny, so merging upstream doesn't turn into conflict resolution inside 15,000-line files, and every
feature can be switched off from the menu.

There are two kinds of hook:

- **Vanilla behaviour ("VB") hooks** ask "should vanilla do X?" and can veto or replace it.
  The vanilla call site passes what vanilla would have done as the default, plus any arguments:

  ```c
  // soh/src/overlays/actors/ovl_player_actor/z_player.c
  if ((...) && GameInteractor_Should(VB_PLAYER_ROLL, true, this)) {
      Player_SetupRoll(this, play);
  ```

  The enhancement registers a handler that may change `*should`. To let a hook change a value, pass a
  pointer (see `VB_PLAYER_ROLL_IFRAMES`, `VB_PLAYER_MODIFY_ROLL_SPEED`):

  ```cpp
  // soh/soh/Enhancements/Souls/SoulsCombat.cpp
  COND_VB_SHOULD(VB_PLAYER_ROLL, CVAR_SOULS_STAMINA_VALUE, {
      if (!Stamina_TrySpend(STAMINA_COST_ROLL)) {
          *should = false;
      }
  });
  ```

  Arguments are read with `va_arg(args, Type)` **in the same order the call site passes them**. Small
  integer types are promoted: read `s16`/`u8` arguments as `int`/`s32`.

- **Event hooks** (`DEFINE_HOOK`) say "this happened" and can carry pointers to modify, e.g.
  `OnPlayerUpdate`, `OnLoadGame`, `OnSceneInit`, `OnPlayerRollStart(Player*, float* animSpeed)`.

  ```cpp
  COND_HOOK(OnLoadGame, CVAR_SOULS_STAMINA_VALUE, [](int32_t) { Stamina_Reset(); });
  ```

### Registering

Each feature has a register function that `COND_`-registers its hooks from its CVar, wired up with
`RegisterShipInitFunc`. It runs at startup and again whenever a listed CVar changes, so toggling a
menu checkbox registers or unregisters the hooks live:

```cpp
static void RegisterSoulsRoll() {
    bool roll = CVAR_SOULS_ROLL_VALUE;
    COND_VB_SHOULD(VB_PLAYER_SET_BODY_AC, roll, { ... });
    COND_HOOK(OnPlayerUpdate, roll, []() { ... });
}
static RegisterShipInitFunc rollInitFunc(RegisterSoulsRoll, { CVAR_SOULS_ROLL_NAME });
```

Handlers are capture-less lambdas: keep state in file-level `static` variables. Use the existing
upstream enhancements (`Enhancements/TimeSavers/ImprovedRoll.cpp`, `Enhancements/Cheats/EasyQPA.cpp`,
`Enhancements/Fixes/HammerHandFix.cpp`) as further examples.

### Adding a new hook

1. Look for an existing one first: `game-interactor/vanilla-behavior/GIVanillaBehavior.h` (VB) and
   `game-interactor/GameInteractor_HookTable.h` (events).
2. **VB:** add an entry to the `// MARK: - OldCavemanBonkstick` section at the end of
   `GIVanillaBehavior.h`, documenting the vanilla `result` and the `args`, in the same style as the
   upstream entries. Then put the `GameInteractor_Should(...)` call in vanilla code.
3. **Event:** add a `DEFINE_HOOK` under `// OldCavemanBonkstick` in `GameInteractor_HookTable.h`, and
   a `GameInteractor_Execute...` wrapper in the `// MARK: - OldCavemanBonkstick` sections of
   `GameInteractor_Hooks.h` / `.cpp` (vanilla C code calls the wrapper).
4. Keep our additions in those marked sections so upstream merges stay clean.
5. If you need a function from `z_player.c` in C++, it must not be `static`; declare it in an
   `extern "C"` block (see `SoulsCombat.cpp`).
6. Anything else that calls the vanilla function you hooked around has to respect the hook too. Example:
   Improved Roll chains rolls by calling `Player_SetupRoll` directly, so it also asks `VB_PLAYER_ROLL`.

### Hooks this fork added

| Hook | Called from | Used by |
|---|---|---|
| `VB_PLAYER_ALLOW_SHIELD` | `Player_ActionHandler_11` (raise), `func_80834B5C` (keep up) | stamina debt drops the shield |
| `VB_PLAYER_BLOCK_WITH_SHIELD` | `func_808382DC` | block cost and guard break |
| `VB_PLAYER_ROLL` | `Player_TryRoll`, Improved Roll | roll stamina cost |
| `OnPlayerRollStart` | `Player_SetupRoll` | roll animation speed, anti-spam |
| `VB_PLAYER_ROLL_IFRAMES` | `Player_Action_Roll` (every frame) | roll i-frames |
| `VB_PLAYER_MODIFY_ROLL_SPEED` | `Player_Action_Roll` | roll distance |
| `VB_PLAYER_SET_BODY_AC` | `Player_UpdateCommon` | attacks pass through a rolling Link |
| `VB_PLAYER_B_BUTTON_JUMPSLASH` | `Player_ActionHandler_7` | club leaping slam |
| `VB_PLAYER_START_MELEE_ATTACK` | `Player_ActionHandler_7` | club attack stamina cost |
| `VB_PLAYER_USE_HAMMER_ATTACKS` | `func_80837818` | club swings like the hammer |
| `VB_PLAYER_DRAW_SWORD_TRAIL` | `func_800906D4` (`z_player_lib.c`) | no sword trail for the club |
| `AfterInterfaceDrawMagicBar` | `Interface_Draw` | stamina bar |

## Where things live

- `soh/soh/Enhancements/Souls/`
  - `Souls.h`: CVar names/defaults for the Souls features (`gEnhancements.Souls.*`).
  - `Stamina.{h,cpp}`: stamina state, costs and regen; resets on file load and on death.
  - `SoulsCombat.cpp`: club, stamina-gated shield/roll/block, Dark Souls roll, club aim assist. Club
    swings play the Megaton Hammer animation with Link's feet planted (`ANIM_FLAG_NOMOVE` every frame),
    because its root motion assumes the hammer stance and otherwise yanks Link backwards. The smash
    lands between hammer_hit frames 5 and 6, tip 45 units ahead (child) / 64 (adult); aim assist turns
    and steps Link toward his target during frames 0-5 and never steps onto floor at a different height.
    The club hits one sword tier up (Kokiri slot -> Master Sword damage flags, Master slot -> Biggoron),
    never the hammer's flags; the flags are re-stamped every active frame in `OnPlayerUpdate`. Post-impact
    (frame 6+) plays at 2x and the follow-through at 2.25x. The follow-through is identified as the
    animation that starts on the frame the smash ends with `PLAYER_STATE3_FINISHED_ATTACKING` set; that
    flag alone can outlive it.
  - `StaminaBar.cpp`: the HUD bar, framed with the magic meter's own textures (follows the magic meter's
    HUD editor placement, fades with the hearts, never draws debt). While stamina is on it also turns the
    magic meter blue (via `R_MAGIC_FILL_COLOR`, reset every scene load so it's set each frame) so the
    two bars can't be confused; a Cosmetics Editor magic colour still wins.
  - `ClubBackModel.cpp`: removes the sword and sheath from Link's back models at runtime (below).
- Menu: Enhancements -> **Souls** (last entry in the sidebar), in `soh/soh/SohGui/SohMenuEnhancements.cpp`.
- Small direct edits that aren't behaviour (kept minimal): the "Blonk" default file name in
  `z_file_choose.c` and its menu entry.

## Assets

- `bonkstick/*.o2r` are the Reinforced Club model (in hand), swing animations and item icons. CMake
  installs them to `bonkstick/` next to `soh.o2r` (and copies them into `build-cmake/soh/bonkstick` for dev
  builds); `LoadBonkstickArchives()` in `OTRGlobals.cpp` loads them, called from `UpdateModFiles` in
  `mod_menu.cpp` right **after** the user's mods. They are not in `mods/`: on Linux SoH reads
  `~/.local/share/soh/mods` instead of the install folder, so a bundled `mods/` folder would be ignored.
  Loading last means the club wins over texture packs that replace the same files, while everything else
  in the pack still applies.
- Texture packs ship their replacements under `alt/` (used when Alt Assets is on), and SoH prefers any
  `alt/` file over a plain one. So every vanilla-named texture we replace must exist in both places:
  `reinforced_club_icons.o2r` has `textures/icon_item_static/gItemIconSword*Tex` **and**
  `alt/textures/icon_item_static/gItemIconSword*Tex`. Without the `alt/` copies, OoT Reloaded's HD sword
  icons replace the club icons.
- Link's back models are **not** replaced with an `.o2r`. `ClubBackModel.cpp` turns the sword/sheath
  triangles in vanilla's own display lists into no-ops (`ResourceMgr_PatchGfxByName`), classifying each
  triangle by the texture it is drawn with. This keeps vanilla textures (so HD packs such as OoT
  Reloaded apply to the shield on Link's back), keeps the low-detail Far models, and restores the vanilla
  back when the club is turned off. Prefer this approach for "hide part of a vanilla model" changes; ship
  an `.o2r` only for genuinely new geometry.
- If an old `reinforced_club_back.o2r` is still in a mods folder it overrides the back models (drawing the
  shield torn into shards) and the patch skips them. Delete it. The patch only touches back models that
  load vertices from Link's own `object_link_*Vtx_*` buffers, because tool-exported models don't always
  set `IsCustom`; it logs `[Souls] N of Link's back models are replaced by a mod` once when it skips any.
- Which `mods/` folder is used: SoH runs "portable" from the launch folder when that folder has its own
  `oot.o2r` / `shipofharkinian.json` (e.g. `build-cmake/soh/` after a dev run), and then reads the
  `mods/` folder next to `soh.elf`, not `~/.local/share/soh/mods`. Check both for stale archives.

## Building and testing (Linux)

```bash
cmake --build build-cmake -j$(nproc)
```

Local builds treat warnings as errors, because CI does. Windows is compiled with MSVC `/W3 /WX`
(`soh/CMakeLists.txt`), so anything MSVC grumbles about is a red build even when GCC was happy. The
Linux/Clang branch therefore runs with `-Werror` and `-Wfloat-conversion`, and our own sources
(`Enhancements/Souls/*.cpp`) additionally with `-Wconversion` — the only way GCC reports the implicit
`int`→`float` conversions that MSVC fails on.

That is how the Windows build broke while Linux stayed green: `StaminaBar.cpp` fed `s32` CVar offsets
into the `float`-pixel HUD anchor helpers and stored the `float` results in `s16`, which is `C4244` on
MSVC. When arithmetic changes type:

- Compute in the natural type (`s32`, `f32`) and convert **once**, at the boundary where the other
  type is unavoidable, inside a named helper — see `AnchoredColumn()` and `StaminaBarFrame` in
  `StaminaBar.cpp`. Do not sprinkle casts through every branch; that is how the bug got in.
- Vanilla's HUD/draw APIs are 16-bit (`s16` rects, `u8` alphas). Passing `int` expressions to them is
  fine; a `float`→`int` or `u16`→`u8` narrowing needs an explicit conversion with a comment.
- `-Wconversion` is deliberately not project-wide: upstream code and vanilla's `s16` HUD APIs fail it
  in hundreds of files. Only our sources have to be clean, so keep new type-mixing logic out of
  `soh/src/**` (see the hook rule) and it stays enforced.
- Upstream `kaleido.cpp` and `nametag.cpp` swap `calloc`'s arguments and GCC 14 warns about it; that
  single warning is carved out in CMake. Don't widen the carve-out — fix new code properly.

### Editor / LSP

`clangd` (and OpenCode's LSP) needs the compile database: there is a gitignored `compile_commands.json`
symlink at the repo root pointing at `build-cmake/compile_commands.json`, so it is found from any
source file. Generate it after a fresh configure:

```bash
cmake -S . -B build-cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

The binary is `build-cmake/soh/soh.elf`. To test without touching your own saves and mods, point
`SHIP_HOME` at a scratch folder containing (symlinks to) `oot.o2r` and a `mods/` folder with whatever
you want to test, e.g. with and without a texture pack:

```bash
mkdir -p /tmp/soh-test/mods && ln -sf ~/.local/share/soh/oot.o2r /tmp/soh-test/
SHIP_HOME=/tmp/soh-test build-cmake/soh/soh.elf
```

Logs go to `$SHIP_HOME/logs/`. The Souls features log what they set up there (e.g.
`[Souls] Removed N sword/sheath triangle commands from Link's back models`).

Format C/C++ before committing. `./run-clang-format.sh` wants `clang-format-14`; if only a newer
binary is installed, pass it explicitly: `CLANG_FORMAT=clang-format ./run-clang-format.sh`.
