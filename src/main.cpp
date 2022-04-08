#include <iostream>
#include <filesystem>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <UVTD/UVTD.hpp>

#define NOMINMAX
#include <Windows.h>

using namespace RC;

// We're outside DllMain here
auto thread_dll_start([[maybe_unused]]LPVOID thread_param) -> unsigned long
{

    std::filesystem::path module_path{};

    if (thread_param)
    {
        auto module_handle = reinterpret_cast<HMODULE>(thread_param);
        wchar_t module_filename_buffer[1024]{'\0'};
        GetModuleFileNameW(module_handle, module_filename_buffer, sizeof(module_filename_buffer) / sizeof(wchar_t));
        module_path = module_filename_buffer;
        module_path = module_path.parent_path();
    }

    Output::set_default_devices<Output::NewFileDevice>();
    auto& file_device = Output::get_device<Output::NewFileDevice>();
    file_device.set_file_name_and_path(module_path / "UVTD.log");

    try
    {
        Output::send(STR("Unreal Virtual Table Dumper -> START\n"));
        UVTD::main();
    }
    catch (std::exception& e)
    {
        Output::send(STR("Exception caught: {}\n"), to_wstring(e.what()));
    }

    return 0;
}

// We're still inside DllMain so be careful what you do here
auto dll_process_attached(HMODULE moduleHandle) -> void
{
    if (HANDLE handle = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(thread_dll_start), moduleHandle, 0, nullptr); handle)
    {
        CloseHandle(handle);
    }

    std::cin.get();
}

auto main() -> int
{
    thread_dll_start(nullptr);
    std::cin.get();
    return 0;
}

auto DllMain(HMODULE hModule, DWORD ul_reason_for_call, [[maybe_unused]] LPVOID lpReserved) -> BOOL
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
            dll_process_attached(hModule);
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
