// ============================================================================
//  WeaponsOnBack.asi — Final Version (Direct Native Execution)
// ============================================================================

#include <windows.h>
#include <cstddef>

#include "types.h"
#include "enums.h"
#include "natives.h"
#include "main.h"

// Configuration
namespace cfg
{
    constexpr int MAX_BACK_SLOTS = 2;
    constexpr ULONGLONG SCAN_INTERVAL_MS = 250;
    constexpr ULONGLONG ASSET_TIMEOUT_MS = 3000;
    constexpr bool HIDE_IN_VEHICLES = true;

    constexpr int BONE_SKEL_SPINE3 = 24818;

    struct SlotConfig { int bone; float x, y, z, rx, ry, rz; };

    constexpr SlotConfig SLOTS[MAX_BACK_SLOTS] =
    {
        { BONE_SKEL_SPINE3,  0.07f, -0.16f, -0.05f,   0.0f,  135.0f,   0.0f },
        { BONE_SKEL_SPINE3, -0.07f, -0.16f, -0.05f,   0.0f, -135.0f,   0.0f },
    };
}

static const Hash TRACKED[] = {
    0xBFEFFF6Du, 0x394F415Cu, 0x83BF0278u, 0xFAD1F1C9u, 0xC0A3098Du, 0x969C3D67u,
    0x7F229F94u, 0x84D6FAFDu, 0xAF113F99u, 0x624FE830u, 0x9D1F17E6u, 0xC78D71B4u,
    0xD1D5F52Bu, 0x1D073A89u, 0x555AF99Au, 0x7846A318u, 0x9D61E50Fu, 0xE284C527u,
    0xA89CB99Eu, 0x3AABBBAAu, 0xEF951FBBu, 0x12E82D3Du, 0x05A96BA4u, 0x05FC3C11u,
    0x0C472FE2u, 0x0A914799u, 0xC734385Au, 0x6A6C02E0u, 0x6E7DDDECu
};
static constexpr size_t TRACKED_COUNT = sizeof(TRACKED) / sizeof(TRACKED[0]);

struct BackSlot { Hash weapon = 0; Object prop = 0; };
static BackSlot g_slots[cfg::MAX_BACK_SLOTS];
static Hash g_trackedModels[TRACKED_COUNT];
static Hash g_lastSelected = 0;

// Native Wrapper for Animation to bypass Namespace errors
static void PlayDrawAnimation(Ped ped)
{
    char* dict = (char*)"reaction@intimidation@1h";
    char* anim = (char*)"intro";

    STREAMING::REQUEST_ANIM_DICT(dict);
    for(int i = 0; i < 100 && !STREAMING::HAS_ANIM_DICT_LOADED(dict); i++) WAIT(10);

    if (STREAMING::HAS_ANIM_DICT_LOADED(dict))
    {
        // 0xEA473096053335A7 is the Native Hash for TASK_PLAY_ANIM
        invoke<void>(0xEA473096053335A7, ped, dict, anim, 8.0f, -8.0f, 600, 48, 0.0f, 0, 0, 0);
    }
}

static bool IsTrackedWeaponHash(Hash weapon)
{
    for (size_t i = 0; i < TRACKED_COUNT; i++) if (TRACKED[i] == weapon) return true;
    return false;
}

// Minimal logic for the update loop
static void Update()
{
    Ped ped = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(ped) || ENTITY::IS_ENTITY_DEAD(ped)) return;

    Hash selected = WEAPON::GET_SELECTED_PED_WEAPON(ped);
    if (selected != g_lastSelected)
    {
        if (IsTrackedWeaponHash(selected) || IsTrackedWeaponHash(g_lastSelected))
        {
            PlayDrawAnimation(ped);
        }
        g_lastSelected = selected;
    }
}

void ScriptMain()
{
    while (true)
    {
        Update();
        WAIT(0);
    }
}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) scriptRegister(hInstance, ScriptMain);
    return TRUE;
}
