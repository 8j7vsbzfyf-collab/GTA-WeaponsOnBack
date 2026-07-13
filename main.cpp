// ============================================================================
//  WeaponsOnBack.asi — ScriptHookV mod for Grand Theft Auto V (single-player)
// ----------------------------------------------------------------------------
//  Large weapons (assault rifles, shotguns, sniper rifles) that the player
//  owns but is NOT currently holding are rendered as props attached to the
//  player's back. Equipping a weapon removes its back prop instantly;
//  holstering or switching away restores it.
//
//  BUILD
//    - Visual Studio, x64, Release, C++17 or newer.
//    - Add the ScriptHookV SDK "inc" folder to the include path and link
//      against ScriptHookV.lib (adjust the #include paths below to match
//      your project layout).
//    - Rename the built DLL to WeaponsOnBack.asi and place it in the GTA V
//      root folder (requires ScriptHookV + dinput8.dll installed).
//
//  NOTE ON natives.h VERSIONS
//    - Older SDK headers declare CREATE_WEAPON_OBJECT with 8 parameters;
//      newer NativeDB-generated headers use 10 (..., Any p7, Any p8, Any p9).
//      If your header wants more arguments, append 0s. The 7th float is
//      documented as "heading" in old headers and "scale" in new ones —
//      1.0f is safe for both interpretations.
// ============================================================================

#include <windows.h>
#include <cstddef>

#include "inc\types.h"
#include "inc\enums.h"
#include "inc\natives.h"
#include "inc\main.h"

// ============================================================================
// Configuration
// ============================================================================
namespace cfg
{
    constexpr int       MAX_BACK_SLOTS   = 3;       // props shown at once
    constexpr ULONGLONG SCAN_INTERVAL_MS = 250;     // inventory re-scan period
    constexpr ULONGLONG ASSET_TIMEOUT_MS = 3000;    // weapon asset load timeout
    constexpr bool      HIDE_IN_VEHICLES = false;   // true = no props in cars

    // Ped skeleton bone tags (input to GET_PED_BONE_INDEX)
    constexpr int BONE_SKEL_SPINE3 = 24818;         // upper back (main mount)
    constexpr int BONE_SKEL_PELVIS = 11816;         // hips (alternative mount)

    struct SlotConfig
    {
        int   bone;                 // bone tag to attach to
        float x,  y,  z;            // offset in the bone's local space
        float rx, ry, rz;           // rotation: pitch (X), roll (Y), yaw (Z)
    };

    // Hand-tuned so standard rifles rest flat against the torso:
    //   x : left/right across the back      y : in/out of the body (negative = behind)
    //   z : up/down along the spine         ry: roll around the weapon's barrel axis
    // If one specific model clips (extra-long snipers, drum-mag shotguns),
    // nudge y outward by ~0.02 and/or adjust ry by +-10. Swap a slot's bone to
    // BONE_SKEL_PELVIS for a hip-holster style mount (retune offsets after).
    constexpr SlotConfig SLOTS[MAX_BACK_SLOTS] =
    {
        { BONE_SKEL_SPINE3,  0.13f, -0.17f, -0.12f,   0.0f,  155.0f,   0.0f }, // slot 0: diagonal, stock over right shoulder
        { BONE_SKEL_SPINE3, -0.13f, -0.17f, -0.09f,   0.0f, -155.0f,   0.0f }, // slot 1: mirrored diagonal, left shoulder
        { BONE_SKEL_SPINE3,  0.00f, -0.22f, -0.32f,   0.0f,   90.0f,  87.0f }, // slot 2: horizontal across the lower back
    };
}

// ============================================================================
// Weapons rendered on the back while holstered
// ============================================================================
static const Hash TRACKED[] =
{
    // ----- Assault rifles -----
    0xBFEFFF6Du, // WEAPON_ASSAULTRIFLE
    0x394F415Cu, // WEAPON_ASSAULTRIFLE_MK2
    0x83BF0278u, // WEAPON_CARBINERIFLE
    0xFAD1F1C9u, // WEAPON_CARBINERIFLE_MK2
    0xC0A3098Du, // WEAPON_SPECIALCARBINE
    0x969C3D67u, // WEAPON_SPECIALCARBINE_MK2
    0x7F229F94u, // WEAPON_BULLPUPRIFLE
    0x84D6FAFDu, // WEAPON_BULLPUPRIFLE_MK2
    0xAF113F99u, // WEAPON_ADVANCEDRIFLE
    0x624FE830u, // WEAPON_COMPACTRIFLE
    0x9D1F17E6u, // WEAPON_MILITARYRIFLE
    0xC78D71B4u, // WEAPON_HEAVYRIFLE
    0xD1D5F52Bu, // WEAPON_TACTICALRIFLE

    // ----- Shotguns -----
    0x1D073A89u, // WEAPON_PUMPSHOTGUN
    0x555AF99Au, // WEAPON_PUMPSHOTGUN_MK2
    0x7846A318u, // WEAPON_SAWNOFFSHOTGUN
    0x9D61E50Fu, // WEAPON_BULLPUPSHOTGUN
    0xE284C527u, // WEAPON_ASSAULTSHOTGUN
    0xA89CB99Eu, // WEAPON_MUSKET
    0x3AABBBAAu, // WEAPON_HEAVYSHOTGUN
    0xEF951FBBu, // WEAPON_DBSHOTGUN
    0x12E82D3Du, // WEAPON_AUTOSHOTGUN
    0x05A96BA4u, // WEAPON_COMBATSHOTGUN

    // ----- Sniper rifles -----
    0x05FC3C11u, // WEAPON_SNIPERRIFLE
    0x0C472FE2u, // WEAPON_HEAVYSNIPER
    0x0A914799u, // WEAPON_HEAVYSNIPER_MK2
    0xC734385Au, // WEAPON_MARKSMANRIFLE
    0x6A6C02E0u, // WEAPON_MARKSMANRIFLE_MK2
    0x6E7DDDECu, // WEAPON_PRECISIONRIFLE

    // ----- Machine guns (uncomment to enable) -----
    // 0x9D07F764u, // WEAPON_MG
    // 0x7FD62962u, // WEAPON_COMBATMG
    // 0xDBBD7280u, // WEAPON_COMBATMG_MK2
    // 0x61012683u, // WEAPON_GUSENBERG
};
static constexpr size_t TRACKED_COUNT = sizeof(TRACKED) / sizeof(TRACKED[0]);
static_assert(TRACKED_COUNT > 0, "TRACKED weapon list must not be empty");

// ============================================================================
// Runtime state
// ============================================================================
struct BackSlot
{
    Hash   weapon = 0;   // weapon hash occupying this slot (0 = free)
    Object prop   = 0;   // world object attached to the player's back
};

static BackSlot  g_slots[cfg::MAX_BACK_SLOTS];
static Hash      g_trackedModels[TRACKED_COUNT];  // cached prop model hashes
static Ped       g_lastPed      = 0;
static Hash      g_lastCurrent  = 0;
static Hash      g_lastSelected = 0;
static ULONGLONG g_nextScanAt   = 0;
static bool      g_forceScan    = true;

// ============================================================================
// Prop lifecycle
// ============================================================================

// Safely destroy a prop we own. Zeroes the handle so it can never dangle.
static void DeleteProp(Object& obj)
{
    if (obj != 0 && ENTITY::DOES_ENTITY_EXIST(obj))
    {
        ENTITY::DETACH_ENTITY(obj, TRUE, TRUE);
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(obj, TRUE, TRUE);
        OBJECT::DELETE_OBJECT(&obj);
    }
    obj = 0;
}

static void ClearSlot(int s)
{
    DeleteProp(g_slots[s].prop);
    g_slots[s].weapon = 0;
}

static void DeleteAllProps()
{
    for (int s = 0; s < cfg::MAX_BACK_SLOTS; ++s)
        ClearSlot(s);
    g_forceScan = true;
}

// Create a weapon prop and attach it to the player's back at the given slot.
// Returns 0 on any failure — the caller simply retries on a later scan, so a
// single failed stream request can never wedge or crash the script.
static Object CreateBackProp(Ped ped, Hash weapon, int slotIdx)
{
    WEAPON::REQUEST_WEAPON_ASSET(weapon, 31, 0);

    const ULONGLONG deadline = GetTickCount64() + cfg::ASSET_TIMEOUT_MS;
    while (!WEAPON::HAS_WEAPON_ASSET_LOADED(weapon))
    {
        WAIT(0);
        if (GetTickCount64() > deadline)     return 0; // streaming timeout
        if (!ENTITY::DOES_ENTITY_EXIST(ped)) return 0;
        if (PLAYER::PLAYER_PED_ID() != ped)  return 0; // ped changed mid-wait
    }

    const Vector3 c = ENTITY::GET_ENTITY_COORDS(ped, TRUE);

    // Created at the ped's position and attached in the same script tick, so
    // it is never visible free-standing. See header note about param count.
    Object obj = WEAPON::CREATE_WEAPON_OBJECT(weapon, 1, c.x, c.y, c.z, TRUE, 1.0f, 0);

    WEAPON::REMOVE_WEAPON_ASSET(weapon); // release our streaming ref either way

    if (obj == 0 || !ENTITY::DOES_ENTITY_EXIST(obj))
        return 0;

    // Own the entity so world streaming can't cull it, and make it inert.
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(obj, TRUE, TRUE);
    ENTITY::SET_ENTITY_COLLISION(obj, FALSE, FALSE);
    ENTITY::SET_ENTITY_INVINCIBLE(obj, TRUE);

    // Mirror the tint the player applied to the real weapon.
    const int tint = WEAPON::GET_PED_WEAPON_TINT_INDEX(ped, weapon);
    if (tint > 0)
        WEAPON::SET_WEAPON_OBJECT_TINT_INDEX(obj, tint);

    const cfg::SlotConfig& sc = cfg::SLOTS[slotIdx];
    const int boneIndex = PED::GET_PED_BONE_INDEX(ped, sc.bone);

    ENTITY::ATTACH_ENTITY_TO_ENTITY(
        obj, ped, boneIndex,
        sc.x,  sc.y,  sc.z,
        sc.rx, sc.ry, sc.rz,
        FALSE,   // p9
        FALSE,   // useSoftPinning — rigid mount, no wobble
        FALSE,   // collision — never collide while on the back
        FALSE,   // attached entity is a ped
        2,       // rotation order
        TRUE);   // fixedRot — lock rotation to the bone

    return obj;
}

// ============================================================================
// Helpers
// ============================================================================

// A weapon is shown on the back only if the player owns it AND it is not in
// (or about to be drawn into) the hands. Checking the *selected* weapon in
// addition to the *current* one removes the back prop the moment the swap
// animation starts, so the gun never renders in both places at once.
static bool Qualifies(Ped ped, Hash weapon, Hash current, Hash selected)
{
    if (weapon == current || weapon == selected)
        return false;
    return WEAPON::HAS_PED_GOT_WEAPON(ped, weapon, FALSE) != FALSE;
}

static bool IsSlotted(Hash weapon)
{
    for (int s = 0; s < cfg::MAX_BACK_SLOTS; ++s)
        if (g_slots[s].weapon == weapon)
            return true;
    return false;
}

static int FirstFreeSlot()
{
    for (int s = 0; s < cfg::MAX_BACK_SLOTS; ++s)
        if (g_slots[s].weapon == 0)
            return s;
    return -1;
}

// Recover from the game deleting/detaching a prop behind our back (area
// streaming, mission scripts, etc.). The slot is freed and its weapon gets
// recreated on the next scan — no ghost handles, no crashes.
static void ValidateProps(Ped ped)
{
    for (int s = 0; s < cfg::MAX_BACK_SLOTS; ++s)
    {
        if (g_slots[s].weapon == 0)
            continue;

        const Object o = g_slots[s].prop;
        if (o == 0 ||
            !ENTITY::DOES_ENTITY_EXIST(o) ||
            !ENTITY::IS_ENTITY_ATTACHED_TO_ENTITY(o, ped))
        {
            ClearSlot(s);
            g_forceScan = true;
        }
    }
}

static bool IsTrackedModel(Hash model)
{
    for (size_t i = 0; i < TRACKED_COUNT; ++i)
        if (g_trackedModels[i] == model)
            return true;
    return false;
}

// If ScriptHookV hot-reloads scripts (Insert key), a previous instance's
// props survive as orphans attached to the player. Sweep them once at start
// so a reload never leaves "ghost weapons" floating on the character.
static void SweepOrphans()
{
    const Ped ped = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(ped))
        return;

    Hash current = 0;
    WEAPON::GET_CURRENT_PED_WEAPON(ped, &current, TRUE);
    const Hash heldModel = (current != 0) ? WEAPON::GET_WEAPONTYPE_MODEL(current) : 0;

    static int handles[2048];
    const int n = worldGetAllObjects(handles, 2048);

    for (int i = 0; i < n; ++i)
    {
        Object o = handles[i];
        if (!ENTITY::DOES_ENTITY_EXIST(o))                 continue;
        if (!ENTITY::IS_ENTITY_ATTACHED_TO_ENTITY(o, ped)) continue;

        const Hash model = ENTITY::GET_ENTITY_MODEL(o);
        if (model == heldModel)   continue; // never touch the weapon in hand
        if (!IsTrackedModel(model)) continue;

        DeleteProp(o);
    }
}

// ============================================================================
// Per-frame update
// ============================================================================
static void Update()
{
    const Ped ped = PLAYER::PLAYER_PED_ID();

    // Character switch / respawn: old handles are stale, rebuild from scratch.
    if (ped != g_lastPed)
    {
        DeleteAllProps();
        g_lastPed = ped;
    }

    if (!ENTITY::DOES_ENTITY_EXIST(ped) ||
        !PLAYER::IS_PLAYER_PLAYING(PLAYER::PLAYER_ID()))
    {
        DeleteAllProps();
        return;
    }

    // Cutscenes use their own ped rigs, and a dead player shouldn't sprout
    // rifles during the death cam. Inventory persists, so everything is
    // rebuilt automatically the moment normal gameplay resumes.
    if (CUTSCENE::IS_CUTSCENE_PLAYING() || ENTITY::IS_ENTITY_DEAD(ped))
    {
        DeleteAllProps();
        return;
    }

    if (cfg::HIDE_IN_VEHICLES && PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE))
    {
        DeleteAllProps();
        return;
    }

    ValidateProps(ped);

    Hash current = 0;
    WEAPON::GET_CURRENT_PED_WEAPON(ped, &current, TRUE);
    const Hash selected = WEAPON::GET_SELECTED_PED_WEAPON(ped);

    // React instantly to equip/holster/switch; otherwise the full inventory
    // diff runs on a slow 250 ms timer to keep per-frame native calls minimal.
    if (current  != g_lastCurrent)  { g_lastCurrent  = current;  g_forceScan = true; }
    if (selected != g_lastSelected) { g_lastSelected = selected; g_forceScan = true; }

    const ULONGLONG now = GetTickCount64();
    if (!g_forceScan && now < g_nextScanAt)
        return;

    g_forceScan  = false;
    g_nextScanAt = now + cfg::SCAN_INTERVAL_MS;

    // 1) Free any slot whose weapon was equipped, selected, or removed from
    //    the inventory (mission strips, wanted-bust, trainer, etc.).
    for (int s = 0; s < cfg::MAX_BACK_SLOTS; ++s)
    {
        if (g_slots[s].weapon != 0 &&
            !Qualifies(ped, g_slots[s].weapon, current, selected))
        {
            ClearSlot(s);
        }
    }

    // 2) Mount qualifying weapons that aren't displayed yet. Slot assignment
    //    is stable: a weapon keeps its position on the back across scans.
    for (size_t i = 0; i < TRACKED_COUNT; ++i)
    {
        const Hash w = TRACKED[i];
        if (IsSlotted(w) || !Qualifies(ped, w, current, selected))
            continue;

        const int s = FirstFreeSlot();
        if (s < 0)
            break; // all visual slots occupied; extra weapons stay hidden

        const Object prop = CreateBackProp(ped, w, s);
        if (prop != 0)
        {
            g_slots[s].weapon = w;
            g_slots[s].prop   = prop;
        }
    }
}

// ============================================================================
// Entry points
// ============================================================================
void ScriptMain()
{
    // Reset all state explicitly — ScriptHookV re-enters ScriptMain on script
    // reload without reloading the DLL, so never trust static initialisers.
    for (int s = 0; s < cfg::MAX_BACK_SLOTS; ++s)
        g_slots[s] = BackSlot{};
    g_lastPed      = 0;
    g_lastCurrent  = 0;
    g_lastSelected = 0;
    g_nextScanAt   = 0;
    g_forceScan    = true;

    // Cache the prop model hash of every tracked weapon (used by the sweep).
    for (size_t i = 0; i < TRACKED_COUNT; ++i)
        g_trackedModels[i] = WEAPON::GET_WEAPONTYPE_MODEL(TRACKED[i]);

    SweepOrphans();

    while (true)
    {
        Update();
        WAIT(0);
    }
}

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID /*lpReserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hInstance);
        scriptRegister(hInstance, ScriptMain);
        break;

    case DLL_PROCESS_DETACH:
        scriptUnregister(hInstance);
        break;
    }
    return TRUE;
}
