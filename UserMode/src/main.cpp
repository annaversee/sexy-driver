#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
// Make sure these are located in the same folder as main.cpp
#include "client.dll.hpp" // Include your client.
#include "offsets.hpp" // Include your offsets.

static DWORD get_process_id(const wchar_t* process_name) {
    DWORD process_id = 0;

    HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snap_shot == INVALID_HANDLE_VALUE)
        return process_id;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snap_shot, &entry) == TRUE) {
        if (_wcsicmp(process_name, entry.szExeFile) == 0) {
            process_id = entry.th32ProcessID;
        }
        else {
            while (Process32NextW(snap_shot, &entry) == TRUE) {
                if (_wcsicmp(process_name, entry.szExeFile) == 0) {
                    process_id = entry.th32ProcessID;
                    break;
                }
            }
        }
    }

    CloseHandle(snap_shot);
    return process_id;
}

static std::uintptr_t get_module_base(const DWORD pid, const wchar_t* module_name) {
    std::uintptr_t module_base = 0;

    HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap_shot == INVALID_HANDLE_VALUE)
        return module_base;

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);

    if (Module32FirstW(snap_shot, &entry) == TRUE) {
        if (wcsstr(module_name, entry.szModule) != nullptr)
            module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        else {
            while (Module32NextW(snap_shot, &entry) == TRUE) {
                if (wcsstr(module_name, entry.szModule) != nullptr) {
                    module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                    break;
                }
            }
        }
    }

    CloseHandle(snap_shot);
    return module_base;
}

namespace driver {
    namespace codes {
        constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    }

    struct Request {
        HANDLE process_id;
        PVOID target;
        PVOID buffer;
        SIZE_T size;
        SIZE_T return_size;
    };

    bool attach_to_process(HANDLE driver_handle, const DWORD pid) {
        Request r;
        r.process_id = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));
        return DeviceIoControl(driver_handle, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
    }

    template <class T>
    T read_memory(HANDLE driver_handle, const std::uintptr_t addr) {
        T temp = {};
        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = &temp;
        r.size = sizeof(T);

        if (!DeviceIoControl(driver_handle, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr)) {
            std::cerr << "Failed to read memory.\n";
        }

        return temp;
    }

    template <class T>
    void write_memory(HANDLE driver_handle, const std::uintptr_t addr, const T& value) {
        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = (PVOID)&value;
        r.size = sizeof(T);

        if (!DeviceIoControl(driver_handle, codes::write, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr)) {
            std::cerr << "Failed to write memory.\n";
        }
    }
}

int main() {
    const DWORD pid = get_process_id(L"cs2.exe"); // Enter your process id you want to utilize

    if (pid == 0) {
        std::cerr << "Failed to find cs2\n"; // Change this to your process's name
        std::cin.get();
        return 1;
    }

    HANDLE driver = CreateFile(L"\\\\.\\SexyDriver", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (driver == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create driver handle.\n";
        std::cin.get();
        return 1;
    }

    if (driver::attach_to_process(driver, pid)) {
        std::cout << "Attachment successful.\n";
        // client_dll is not included, you need to get your offsets and client.
        if (const std::uintptr_t client = get_module_base(pid, L"client_dll"); client != 0) {
            std::cout << "Client found.\n";
            //Modify this to your heart's content, this is outdated so use this as an example.
            while (true) {
                if (GetAsyncKeyState(VK_END))
                    break;

                const auto local_player_pawn = driver::read_memory<std::uintptr_t>(driver,
                    client + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);

                if (local_player_pawn == 0)
                    continue;

                const auto flags = driver::read_memory<std::uint32_t>(driver, local_player_pawn + C_BaseEntiry::m_fFlags);

                const bool in_air = flags & (1 << 0);
                const bool space_pressed = GetAsyncKeyState(VK_SPACE);
                const auto force_jump = driver::read_memory<std::<DWORD>(driver,
                    client + client_dll::dwForceJump);

                if (sapce_pressed && in_air) {
                    Sleep(5);
                    driver::write_memory(driver, client + client_dll::dwForceJump, 65537);
                }
                else if (space_pressed && !in_air) {
                    driver::write_memory(driver, client + client_dll::dwForceJump, 256);
                }
                else if (!space_pressed && force_jump == 65537) {
                    driver::write_memory(driver, client + client_dll::dwForceJump, 256);
                }


            }
        }
    }
    // Do not touch below.
    else {
        std::cerr << "Failed to attach to process.\n";
    }

    CloseHandle(driver);
    std::cin.get();
    return 0;
}
