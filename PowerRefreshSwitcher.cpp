#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600

#include <windows.h>

#include <cstring>
#include <cwchar>

namespace {

constexpr DWORD kBatteryRefreshHz = 60;
constexpr DWORD kAcRefreshHz = 144;
constexpr bool kPrimaryDisplayOnly = true;

constexpr wchar_t kMutexName[] = L"Local\\PowerRefreshSwitcher";
constexpr wchar_t kWindowClassName[] = L"PowerRefreshSwitcherWindow";

const GUID kGuidAcdcPowerSource = {
    0x5d3e9a59,
    0xe9d5,
    0x4b00,
    {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};

const GUID kGuidPowerModeBestEfficiency = {
    0x961cc777,
    0x2547,
    0x4f9d,
    {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};

const GUID kGuidPowerModeBestPerformance = {
    0xded574b5,
    0x45a0,
    0x4f42,
    {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};

const GUID kGuidPowerSaverScheme = {
    0xa1841308,
    0x3541,
    0x4fab,
    {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};

const GUID kGuidHighPerformanceScheme = {
    0x8c5e7fda,
    0xe8bf,
    0x4a96,
    {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};

const GUID kGuidEnergySaverSubgroup = {
    0xde830923,
    0xa562,
    0x41af,
    {0xa0, 0x86, 0xe3, 0xa2, 0xc6, 0xba, 0xd2, 0xda}};

const GUID kGuidEnergySaverBatteryThreshold = {
    0xe69653ca,
    0xcf7f,
    0x4f05,
    {0xaa, 0x73, 0xcb, 0x83, 0x3f, 0xa9, 0x0a, 0xd4}};

const GUID kGuidEnergySaverPolicy = {
    0x5c5bb349,
    0xad29,
    0x4ee2,
    {0x9d, 0x0b, 0x2b, 0x25, 0x27, 0x0f, 0x7a, 0x81}};

enum class PowerSource {
    Unknown,
    Battery,
    Ac
};

HPOWERNOTIFY gPowerNotify = nullptr;
PowerSource gLastAppliedSource = PowerSource::Unknown;

bool SameGuid(const GUID& left, const GUID& right) {
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

bool HasArgument(PWSTR commandLine, const wchar_t* argument) {
    return commandLine != nullptr && std::wcsstr(commandLine, argument) != nullptr;
}

PowerSource DetectPowerSource() {
    SYSTEM_POWER_STATUS status = {};
    if (!GetSystemPowerStatus(&status)) {
        return PowerSource::Unknown;
    }

    if (status.ACLineStatus == 0) {
        return PowerSource::Battery;
    }

    if (status.ACLineStatus == 1) {
        return PowerSource::Ac;
    }

    return PowerSource::Unknown;
}

bool IsTargetDisplay(const DISPLAY_DEVICEW& displayDevice) {
    if ((displayDevice.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
        return false;
    }

    return !kPrimaryDisplayOnly || (displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
}

bool FindDisplayMode(const wchar_t* displayName, const DEVMODEW& currentMode, DWORD targetHz, DEVMODEW* selectedMode) {
    DEVMODEW fallbackMode = {};
    bool hasFallback = false;

    for (DWORD modeIndex = 0;; ++modeIndex) {
        DEVMODEW candidate = {};
        candidate.dmSize = sizeof(candidate);

        if (!EnumDisplaySettingsW(displayName, modeIndex, &candidate)) {
            break;
        }

        const bool sameSize = candidate.dmPelsWidth == currentMode.dmPelsWidth &&
                              candidate.dmPelsHeight == currentMode.dmPelsHeight;
        const bool sameColorDepth = candidate.dmBitsPerPel == currentMode.dmBitsPerPel;
        const bool exactHz = candidate.dmDisplayFrequency == targetHz;
        const bool closeHz = candidate.dmDisplayFrequency + 1 == targetHz ||
                             candidate.dmDisplayFrequency == targetHz + 1;

        if (!sameSize || !sameColorDepth) {
            continue;
        }

        if (exactHz) {
            *selectedMode = candidate;
            return true;
        }

        if (closeHz && !hasFallback) {
            fallbackMode = candidate;
            hasFallback = true;
        }
    }

    if (hasFallback) {
        *selectedMode = fallbackMode;
        return true;
    }

    return false;
}

bool SetDisplayRefreshRate(DWORD targetHz) {
    bool changedOrAlreadyCorrect = true;
    bool foundDisplay = false;

    for (DWORD displayIndex = 0;; ++displayIndex) {
        DISPLAY_DEVICEW displayDevice = {};
        displayDevice.cb = sizeof(displayDevice);

        if (!EnumDisplayDevicesW(nullptr, displayIndex, &displayDevice, 0)) {
            break;
        }

        if (!IsTargetDisplay(displayDevice)) {
            continue;
        }

        foundDisplay = true;

        DEVMODEW currentMode = {};
        currentMode.dmSize = sizeof(currentMode);
        if (!EnumDisplaySettingsW(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &currentMode)) {
            changedOrAlreadyCorrect = false;
            continue;
        }

        if (currentMode.dmDisplayFrequency == targetHz) {
            continue;
        }

        DEVMODEW targetMode = {};
        if (!FindDisplayMode(displayDevice.DeviceName, currentMode, targetHz, &targetMode)) {
            changedOrAlreadyCorrect = false;
            continue;
        }

        targetMode.dmFields |= DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;

        const LONG testResult = ChangeDisplaySettingsExW(displayDevice.DeviceName, &targetMode, nullptr, CDS_TEST, nullptr);
        if (testResult != DISP_CHANGE_SUCCESSFUL) {
            changedOrAlreadyCorrect = false;
            continue;
        }

        const LONG changeResult = ChangeDisplaySettingsExW(
            displayDevice.DeviceName,
            &targetMode,
            nullptr,
            CDS_UPDATEREGISTRY,
            nullptr);

        if (changeResult != DISP_CHANGE_SUCCESSFUL) {
            changedOrAlreadyCorrect = false;
        }
    }

    return foundDisplay && changedOrAlreadyCorrect;
}

HMODULE PowrprofModule() {
    static HMODULE module = LoadLibraryW(L"powrprof.dll");
    return module;
}

bool SetModernPowerMode(PowerSource source) {
    HMODULE module = PowrprofModule();
    if (module == nullptr) {
        return false;
    }

    using PowerSetModeFn = DWORD(WINAPI*)(const GUID*);

    const char* functionName = source == PowerSource::Battery
                                   ? "PowerSetUserConfiguredDCPowerMode"
                                   : "PowerSetUserConfiguredACPowerMode";
    const GUID& targetMode = source == PowerSource::Battery
                                 ? kGuidPowerModeBestEfficiency
                                 : kGuidPowerModeBestPerformance;

    auto setPowerMode = reinterpret_cast<PowerSetModeFn>(GetProcAddress(module, functionName));
    if (setPowerMode == nullptr) {
        return false;
    }

    return setPowerMode(&targetMode) == ERROR_SUCCESS;
}

bool SetFallbackPowerScheme(PowerSource source) {
    HMODULE module = PowrprofModule();
    if (module == nullptr) {
        return false;
    }

    using PowerSetActiveSchemeFn = DWORD(WINAPI*)(HKEY, const GUID*);

    auto setActiveScheme = reinterpret_cast<PowerSetActiveSchemeFn>(GetProcAddress(module, "PowerSetActiveScheme"));
    if (setActiveScheme == nullptr) {
        return false;
    }

    const GUID& targetScheme = source == PowerSource::Battery
                                   ? kGuidPowerSaverScheme
                                   : kGuidHighPerformanceScheme;

    return setActiveScheme(nullptr, &targetScheme) == ERROR_SUCCESS;
}

bool WriteEnergySaverValue(const GUID* scheme, bool acValue, const GUID& setting, DWORD value) {
    HMODULE module = PowrprofModule();
    if (module == nullptr) {
        return false;
    }

    using PowerWriteValueIndexFn = DWORD(WINAPI*)(HKEY, const GUID*, const GUID*, const GUID*, DWORD);

    const char* functionName = acValue ? "PowerWriteACValueIndex" : "PowerWriteDCValueIndex";
    auto writeValue = reinterpret_cast<PowerWriteValueIndexFn>(GetProcAddress(module, functionName));
    if (writeValue == nullptr) {
        return false;
    }

    return writeValue(nullptr, scheme, &kGuidEnergySaverSubgroup, &setting, value) == ERROR_SUCCESS;
}

bool SetEnergySaver(PowerSource source) {
    HMODULE module = PowrprofModule();
    if (module == nullptr || source == PowerSource::Unknown) {
        return false;
    }

    using PowerGetActiveSchemeFn = DWORD(WINAPI*)(HKEY, GUID**);
    using PowerSetActiveSchemeFn = DWORD(WINAPI*)(HKEY, const GUID*);

    auto getActiveScheme = reinterpret_cast<PowerGetActiveSchemeFn>(GetProcAddress(module, "PowerGetActiveScheme"));
    auto setActiveScheme = reinterpret_cast<PowerSetActiveSchemeFn>(GetProcAddress(module, "PowerSetActiveScheme"));
    if (getActiveScheme == nullptr || setActiveScheme == nullptr) {
        return false;
    }

    GUID* activeScheme = nullptr;
    if (getActiveScheme(nullptr, &activeScheme) != ERROR_SUCCESS || activeScheme == nullptr) {
        return false;
    }

    const bool useAcValues = source == PowerSource::Ac;
    const DWORD threshold = source == PowerSource::Battery ? 100 : 0;
    const DWORD policy = source == PowerSource::Battery ? 1 : 0;

    const bool thresholdWritten = WriteEnergySaverValue(
        activeScheme,
        useAcValues,
        kGuidEnergySaverBatteryThreshold,
        threshold);
    const bool policyWritten = WriteEnergySaverValue(
        activeScheme,
        useAcValues,
        kGuidEnergySaverPolicy,
        policy);

    if (thresholdWritten || policyWritten) {
        setActiveScheme(nullptr, activeScheme);
    }

    LocalFree(activeScheme);
    return thresholdWritten && policyWritten;
}

void SetPowerPreference(PowerSource source) {
    if (source == PowerSource::Unknown) {
        return;
    }

    if (!SetModernPowerMode(source)) {
        SetFallbackPowerScheme(source);
    }
}

void ApplyProfile(PowerSource source, bool force) {
    if (source == PowerSource::Unknown || (!force && source == gLastAppliedSource)) {
        return;
    }

    const DWORD targetHz = source == PowerSource::Battery ? kBatteryRefreshHz : kAcRefreshHz;

    SetDisplayRefreshRate(targetHz);
    SetPowerPreference(source);
    SetEnergySaver(source);

    gLastAppliedSource = source;
}

void ApplyCurrentProfile(bool force) {
    ApplyProfile(DetectPowerSource(), force);
}

PowerSource PowerSourceFromNotification(const POWERBROADCAST_SETTING* setting) {
    if (setting == nullptr || !SameGuid(setting->PowerSetting, kGuidAcdcPowerSource) ||
        setting->DataLength < sizeof(DWORD)) {
        return PowerSource::Unknown;
    }

    DWORD value = 0;
    std::memcpy(&value, setting->Data, sizeof(value));

    if (value == 0) {
        return PowerSource::Ac;
    }

    if (value == 1) {
        return PowerSource::Battery;
    }

    return PowerSource::Unknown;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            gPowerNotify = RegisterPowerSettingNotification(
                window,
                &kGuidAcdcPowerSource,
                DEVICE_NOTIFY_WINDOW_HANDLE);
            ApplyCurrentProfile(true);
            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_POWERSETTINGCHANGE) {
                const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                const PowerSource notifiedSource = PowerSourceFromNotification(setting);
                ApplyProfile(notifiedSource == PowerSource::Unknown ? DetectPowerSource() : notifiedSource, false);
                return TRUE;
            }

            if (wParam == PBT_APMPOWERSTATUSCHANGE || wParam == PBT_APMRESUMEAUTOMATIC ||
                wParam == PBT_APMRESUMESUSPEND) {
                ApplyCurrentProfile(true);
                return TRUE;
            }

            break;

        case WM_DESTROY:
            if (gPowerNotify != nullptr) {
                UnregisterPowerSettingNotification(gPowerNotify);
                gPowerNotify = nullptr;
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int RunMessageLoop(HINSTANCE instance) {
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClassName;

    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        L"Power Refresh Switcher",
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
        return 1;
    }

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    if (HasArgument(commandLine, L"--once")) {
        ApplyCurrentProfile(true);
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex == nullptr) {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    const int result = RunMessageLoop(instance);

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return result;
}
