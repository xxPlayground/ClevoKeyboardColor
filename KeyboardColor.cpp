#include <iostream>
#include <future>
#include <array>
#include <windows.h>
#include <processthreadsapi.h>
#include <thread>
#include <chrono>
#include <atomic>

extern "C" typedef int (*SetDCHU_DataPtr)(int command, byte* buffer, int length);

SetDCHU_DataPtr SetDCHU_Data;
std::atomic<bool> running(true);

void setBrightness(byte v) {
    std::array<byte, 4> buf{ v, 0, 0, 244 };
    SetDCHU_Data(103, buf.data(), 4);
}

void ColorUpdateThread() {
    static const std::array<std::array<byte, 4>, 100> colorTable = [] {
        std::array<std::array<byte, 4>, 100> table;
        for (int i = 0; i < 100; ++i) {
            float hue = i / 100.0f;
            float h = hue * 6.0f;
            int sector = static_cast<int>(h) % 6;
            float f = h - static_cast<int>(h);

            float r, g, b;
            switch (sector) {
            case 0: r = 1.0f; g = f;     b = 0.0f; break;
            case 1: r = 1 - f;  g = 1.0f;  b = 0.0f; break;
            case 2: r = 0.0f; g = 1.0f;  b = f;    break;
            case 3: r = 0.0f; g = 1 - f;   b = 1.0f; break;
            case 4: r = f;    g = 0.0f;  b = 1.0f; break;
            case 5: r = 1.0f; g = 0.0f;  b = 1 - f;  break;
            default: r = g = b = 0.0f; break;
            }

            // Swap red and green channels and create the buffer
            table[i] = {
                static_cast<byte>(g * 255),
                static_cast<byte>(r * 255),
                static_cast<byte>(b * 255),
                240
            };
        }
        return table;
        }();

    int idx = 0;
    while (running) {
        SetDCHU_Data(103, const_cast<byte*>(colorTable[idx].data()), 4);
        idx = (idx + 1) % 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    PROCESS_POWER_THROTTLING_STATE powerThrottling = {
        .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
        .StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    };

    SetProcessInformation(
        GetCurrentProcess(),
        ProcessPowerThrottling,
        &powerThrottling,
        sizeof(powerThrottling)
    );

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (hKernel32) {
        typedef BOOL(WINAPI* PFN_SET_PROCESS_INFORMATION)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
        auto pfnSetProcessInformation = (PFN_SET_PROCESS_INFORMATION)GetProcAddress(hKernel32, "SetProcessInformation");

        if (pfnSetProcessInformation) {
            PROCESS_POWER_THROTTLING_STATE state = {
                .Version = 1,
                .ControlMask = 0x1,
                .StateMask = 0x1
            };
            pfnSetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
        }
    }

    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentProcess(), THREAD_PRIORITY_IDLE);

    LPCWSTR mutexName = L"ClevoKeyboardColor";
    HANDLE hMutex = CreateMutexW(NULL, TRUE, mutexName);

    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return -1;
    }

    HMODULE hDll = LoadLibraryA("InsydeDCHU.dll");
    if (!hDll) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    SetDCHU_Data = reinterpret_cast<SetDCHU_DataPtr>(GetProcAddress(hDll, "SetDCHU_Data"));
    if (!SetDCHU_Data) {
        FreeLibrary(hDll);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 2;
    }

    setBrightness(255);

    std::thread colorThread(ColorUpdateThread);
    SetThreadPriority(ColorUpdateThread, THREAD_PRIORITY_IDLE);
    colorThread.detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    FreeLibrary(hDll);

    return static_cast<int>(msg.wParam);
}