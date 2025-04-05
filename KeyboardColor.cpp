#include <iostream>
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

void setColor(const std::array<byte, 3>& color) {
    std::array<byte, 4> buffer = { color[1], color[0], color[2], 240 };
    SetDCHU_Data(103, buffer.data(), 4);
}

void ColorUpdateThread() {
    const std::array<std::array<byte, 3>, 7> colorList{ {
        { 255, 0, 0 },
        { 255, 127, 0 },
        { 255, 255, 0 },
        { 0, 255, 0 },
        { 0, 255, 255 },
        { 0, 0, 255 },
        { 255, 0, 255 },
    } };
    int colorIndex = 0;

    while (running) {
        setColor(colorList[colorIndex]);
        colorIndex = (colorIndex + 1) % colorList.size();

        // Fade in
        for (int brightness = 0; brightness < 256; brightness += 8) {
            setBrightness(static_cast<byte>(brightness));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Fade out
        for (int brightness = 255; brightness >= 0; brightness -= 8) {
            setBrightness(static_cast<byte>(brightness));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Adjust sleep duration based on efficiency mode
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
    SetThreadPriority(colorThread.native_handle(), THREAD_PRIORITY_IDLE);
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