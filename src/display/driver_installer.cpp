#include "driver_installer.h"
#include <windows.h>
#include <newdev.h>
#include <setupapi.h>
#include <string>
#include <vector>

namespace display {
namespace {
const wchar_t HardwareId[] = L"Root\\MttVDD"; // from MttVDD.inf
const wchar_t SettingsFolder[] = L"C:\\VirtualDisplayDriver";
const GUID DisplayClass = {
    0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

bool hasHardwareId(HDEVINFO set, SP_DEVINFO_DATA& device)
{
    wchar_t ids[512] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_HARDWAREID, nullptr,
                                           reinterpret_cast<BYTE*>(ids),
                                           sizeof(ids) - 2 * sizeof(wchar_t), nullptr))
        return false;
    for (const wchar_t* id = ids; *id; id += wcslen(id) + 1)
        if (_wcsicmp(id, HardwareId) == 0)
            return true;
    return false;
}

int ensureSingleDevice()
{
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return int(GetLastError());
    std::vector<SP_DEVINFO_DATA> found;
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &device); ++i)
        if (hasHardwareId(set, device))
            found.push_back(device);
    int error = 0;
    for (size_t i = 1; i < found.size(); ++i)
        if (!SetupDiCallClassInstaller(DIF_REMOVE, set, &found[i]))
            error = int(GetLastError());
    SetupDiDestroyDeviceInfoList(set);
    if (error || !found.empty())
        return error;

    set = SetupDiCreateDeviceInfoList(&DisplayClass, nullptr);
    if (set == INVALID_HANDLE_VALUE)
        return int(GetLastError());
    SP_DEVINFO_DATA created{};
    created.cbSize = sizeof(created);
    // The hardware ID is a list of strings, so it ends with two terminators.
    wchar_t ids[sizeof(HardwareId) / sizeof(wchar_t) + 1] = {};
    wcscpy(ids, HardwareId);
    const bool ok =
        SetupDiCreateDeviceInfoW(set, L"Display", &DisplayClass, nullptr, nullptr, DICD_GENERATE_ID,
                                 &created) &&
        SetupDiSetDeviceRegistryPropertyW(set, &created, SPDRP_HARDWAREID,
                                          reinterpret_cast<const BYTE*>(ids), sizeof(ids)) &&
        SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &created);
    error = ok ? 0 : int(GetLastError());
    SetupDiDestroyDeviceInfoList(set);
    return error;
}
} // namespace

int installDriver(const wchar_t* folder)
{
    const std::wstring dir(folder);
    CreateDirectoryW(SettingsFolder, nullptr);
    if (!CopyFileW((dir + L"\\vdd_settings.xml").c_str(),
                   (std::wstring(SettingsFolder) + L"\\vdd_settings.xml").c_str(), FALSE))
        return int(GetLastError());

    if (const int error = ensureSingleDevice())
        return error;

    BOOL reboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, HardwareId, (dir + L"\\MttVDD.inf").c_str(),
                                            INSTALLFLAG_FORCE, &reboot))
        return int(GetLastError());
    return 0;
}
} // namespace display
