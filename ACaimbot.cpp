#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <cfloat>
#include <cmath>
#include <iostream>

// Definition von M_PI, falls nicht vorhanden
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
constexpr float RAD2DEG = 180.0f / static_cast<float>(M_PI);

// Hilfsfunktion: Prozess-ID anhand des Namens ermitteln
DWORD GetProcessID(const wchar_t* processName) {
    PROCESSENTRY32 pe32{};
    pe32.dwSize = sizeof(pe32);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    if (Process32First(hSnap, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, processName) == 0) {
                CloseHandle(hSnap);
                return pe32.th32ProcessID;
            }
        } while (Process32Next(hSnap, &pe32));
    }
    CloseHandle(hSnap);
    return 0;
}

int main() {
    // 1) Prozess öffnen
    DWORD pid = GetProcessID(L"ac_client.exe");
    if (!pid) {
        std::cerr << "ac_client.exe nicht gefunden\n";
        return 1;
    }
    HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, pid);
    if (!hProc) {
        std::cerr << "OpenProcess fehlgeschlagen\n";
        return 1;
    }

    // 2) Statische Adressen/Offsets
    const uintptr_t LOCAL_PLAYER_PTR = 0x58AC00;
    const uintptr_t ENTITY_LIST_PTR = 0x58AC04;
    const uintptr_t ENTITY_COUNT_PTR = 0x58AC0C;
    const uintptr_t OFF_POS_X = 0x04;  // X-Achse (links/rechts)
    const uintptr_t OFF_POS_Z = 0x08;  // Z-Achse (vorwärts/rückwärts)
    const uintptr_t OFF_POS_Y = 0x0C;  // Y-Achse (Höhe)
    const uintptr_t OFF_YAW = 0x34;  // Yaw (Grad)
    const uintptr_t OFF_PITCH = 0x38;  // Pitch (Grad)
    const uintptr_t OFF_HEALTH = 0xEC;  // Gesundheit
    const uintptr_t OFF_TEAM = 0x30C; // Team-ID (int) :contentReference[oaicite:1]{index=1}

    // 3) Basisadressen einlesen
    uintptr_t localBase = 0;
    ReadProcessMemory(hProc, (LPCVOID)LOCAL_PLAYER_PTR, &localBase, sizeof(localBase), nullptr);
    uintptr_t listBase = 0;
    ReadProcessMemory(hProc, (LPCVOID)ENTITY_LIST_PTR, &listBase, sizeof(listBase), nullptr);
    int count = 0;
    ReadProcessMemory(hProc, (LPCVOID)ENTITY_COUNT_PTR, &count, sizeof(count), nullptr);

    if (!localBase || !listBase || count <= 0) {
        std::cerr << "Ungültige Basisadressen oder Spieleranzahl\n";
        CloseHandle(hProc);
        return 1;
    }

    // 4) Eigene Team-ID einlesen
    int myTeam = 0;
    ReadProcessMemory(hProc, (LPCVOID)(localBase + OFF_TEAM), &myTeam, sizeof(myTeam), nullptr);

    std::cout << "Aimbot (mit Team-Check) läuft – rechte Maustaste gedrückt halten zum Zielen.\n";

    // 5) Endlosschleife
    while (true) {
        // a) Eigene Position
        float myX, myZ, myY;
        ReadProcessMemory(hProc, (LPCVOID)(localBase + OFF_POS_X), &myX, sizeof(myX), nullptr);
        ReadProcessMemory(hProc, (LPCVOID)(localBase + OFF_POS_Z), &myZ, sizeof(myZ), nullptr);
        ReadProcessMemory(hProc, (LPCVOID)(localBase + OFF_POS_Y), &myY, sizeof(myY), nullptr);

        // b) Nächsten lebenden, gegnerischen Spieler finden
        float bestDist = FLT_MAX;
        uintptr_t tgtBase = 0;
        float tx = 0, tz = 0, ty = 0;
        for (int i = 0; i < count; ++i) {
            uintptr_t ent = 0;
            ReadProcessMemory(hProc, (LPCVOID)(listBase + i * sizeof(uintptr_t)), &ent, sizeof(ent), nullptr);
            if (!ent || ent == localBase) continue;

            // Team-Check: überspringe Mitspieler
            int entTeam = 0;
            ReadProcessMemory(hProc, (LPCVOID)(ent + OFF_TEAM), &entTeam, sizeof(entTeam), nullptr);
            if (entTeam == myTeam) continue;

            // nur lebende Gegner
            int hp = 0;
            ReadProcessMemory(hProc, (LPCVOID)(ent + OFF_HEALTH), &hp, sizeof(hp), nullptr);
            if (hp <= 0) continue;

            // Gegner-Position
            float ex, ez, ey;
            ReadProcessMemory(hProc, (LPCVOID)(ent + OFF_POS_X), &ex, sizeof(ex), nullptr);
            ReadProcessMemory(hProc, (LPCVOID)(ent + OFF_POS_Z), &ez, sizeof(ez), nullptr);
            ReadProcessMemory(hProc, (LPCVOID)(ent + OFF_POS_Y), &ey, sizeof(ey), nullptr);

            // Distanz berechnen
            float dx = ex - myX, dz = ez - myZ, dy = ey - myY;
            float d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d < bestDist) {
                bestDist = d;
                tgtBase = ent;
                tx = ex; tz = ez; ty = ey;
            }
        }

        // c) Winkel berechnen & ins Spiel schreiben
        if (tgtBase && (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
            float dx = tx - myX;
            float dz = tz - myZ;
            float dy = ty - myY;

            // Yaw (horizontal)
            float yaw = atan2f(dz, dx) * RAD2DEG + 90.0f;
            if (yaw < 0.0f) yaw += 360.0f;

            // Pitch (vertikal): oben positiv, unten negativ
            float horiz = sqrtf(dx * dx + dz * dz);
            float pitch = atan2f(dy, horiz) * RAD2DEG;

            WriteProcessMemory(hProc, (LPVOID)(localBase + OFF_YAW), &yaw, sizeof(yaw), nullptr);
            WriteProcessMemory(hProc, (LPVOID)(localBase + OFF_PITCH), &pitch, sizeof(pitch), nullptr);
        }

        Sleep(16); // ~60 FPS
    }

    // Wird nie erreicht
    CloseHandle(hProc);
    return 0;
}
