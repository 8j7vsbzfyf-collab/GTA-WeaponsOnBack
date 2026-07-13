#include <windows.h>
#include <main.h>
#include <natives.h>

void ScriptMain() {
    while (true) {
        Ped playerPed = PLAYER::PLAYER_PED_ID();
        
        if (ENTITY::DOES_ENTITY_EXIST(playerPed) && !ENTITY::IS_ENTITY_DEAD(playerPed)) {
            Hash currentWeapon;
            WEAPON::GET_CURRENT_PED_WEAPON(playerPed, &currentWeapon, true);
            
            Hash weapons[] = {
                0x1D073A89, // Shotgun
                0x95EF8AD2, // Assault Rifle
                0x05FC3C11  // Sniper Rifle
            };

            for (int i = 0; i < 3; i++) {
                if (WEAPON::HAS_PED_GOT_WEAPON(playerPed, weapons[i], false)) {
                    if (currentWeapon != weapons[i]) {
                        // تفريغ الكود هنا للاستدعاء
                    }
                }
            }
        }
        scriptWait(100);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        scriptRegister(hModule, ScriptMain);
        break;
    case DLL_PROCESS_DETACH:
        scriptUnregister(hModule);
        break;
    }
    return TRUE;
}
