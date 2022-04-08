#include <stdexcept>
#include <format>
#include <thread>
#include <unordered_set>

#include <UVTD/UVTD.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/Handler.hpp>
#include <Helpers/String.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>

#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#include <dbghelp.h>

namespace RC::UVTD
{
    bool processing_events{false};
    Input::Handler input_handler{L"ConsoleWindowClass", L"UnrealWindow"};
    static std::filesystem::path exe_path{};
    static std::filesystem::path exe_name{};
    static std::filesystem::path dll_path{};
    static std::filesystem::path dll_name{};

    struct ProcessData
    {
        HMODULE module_handle{};
    };
    static ProcessData process_data{};

    auto static try_create_debug_console() -> void
    {
        if (!AllocConsole())
        {
            throw std::runtime_error{std::format("Was unable to create console: {}", GetLastError())};
        }
        else
        {
            FILE* stdin_filename;
            FILE* stdout_filename;
            FILE* stderr_filename;
            freopen_s(&stdin_filename, "CONIN$", "r", stdin);
            freopen_s(&stdout_filename, "CONOUT$", "w", stdout);
            freopen_s(&stderr_filename, "CONOUT$", "w", stderr);

            Output::set_default_devices<Output::DebugConsoleDevice>();
            Output::send(STR("Debug console created\n"));
        }
    }

    auto static setup_unreal() -> void
    {
        Unreal::UnrealInitializer::Config config;
        config.process_handle = GetCurrentProcess();
        config.module_handle = GetModuleHandleW(nullptr);
        config.game_exe = exe_path / exe_name;

        // Temporary until the cache system can be bypassed
        config.cache_path = exe_path / "cache";
        config.self_file = dll_path / dll_name;

        Unreal::UnrealInitializer::setup_unreal_modules({GetCurrentProcess(), process_data.module_handle});
        Unreal::UnrealInitializer::initialize(config);
    }

    auto static event_loop_update() -> void
    {
        for (processing_events = true; processing_events;)
        {
            input_handler.process_event();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    struct EnumModuleSymbolCallbackParams
    {
        const DWORD64 in_vtable_entry;
        std::vector<File::StringType> symbol_names;
    };
    auto static CALLBACK enum_module_symbols_callback(PSYMBOL_INFO sym_info, ULONG sym_size, PVOID user_context) -> BOOL
    {
        auto* params = static_cast<EnumModuleSymbolCallbackParams*>(user_context);

        if (sym_info->Address == params->in_vtable_entry)
        {
            params->symbol_names.emplace_back(to_wstring(sym_info->Name));
            return TRUE;
        }
        else
        {
            return TRUE;
        }
    }

    auto static is_symbol_virtual(File::StringViewType symbol_name) -> bool
    {
        return symbol_name.starts_with(STR("virtual "));
    }

    // This function works for reflected types
    // It does not work for non-reflected inheritance, such as 'UObject' inheriting from 'UObjectBaseUtility' and 'UObjectBase'
    // Unfortunately these must be hard-coded
    auto static does_symbol_belong_to_object(Unreal::UObject* object, File::StringViewType symbol_name) -> bool
    {
        auto starts_with = [](Unreal::UObject* obj, File::StringViewType with) {
            if (with.find(obj->get_name() + STR("::"), 1) == 1) { return true; }

            if (obj == Unreal::UObject::static_class())
            {
                // For now, hard-coding UObjectBaseUtility & UObjectBase
                if (with.find(STR("UObjectBaseUtility::")) != with.npos) { return true; }
                if (with.find(STR("UObjectBase::")) != with.npos) { return true; }
            }
            return false;
        };

        if (symbol_name.empty()) { return false; }

        auto* obj_as_class = Unreal::cast_uobject<Unreal::UClass>(object);
        if (obj_as_class)
        {
            if (starts_with(obj_as_class, symbol_name)) { return true; }

            if (obj_as_class->for_each_super_struct([&](Unreal::UStruct* super) {
                if (starts_with(super, symbol_name)) { return LoopAction::Break; }
                return LoopAction::Continue;
            }) == LoopAction::Break) { return true; }
        }
        else
        {
            auto* obj_class = object->get_uclass();
            if (starts_with(obj_class, symbol_name)) { return true; }

            if (obj_class->for_each_super_struct([&](Unreal::UStruct* super) {
                if (starts_with(super, symbol_name)) { return LoopAction::Break; }



                return LoopAction::Continue;
            }) == LoopAction::Break) { return true; }
        }

        return false;
    }

    auto undecorate_name(File::StringViewType decorated_name, DWORD flags = 0) -> File::StringType
    {
        static constexpr DWORD undecorated_symbol_name_buffer_length = 1000;
        wchar_t undecorated_symbol_name_buffer[undecorated_symbol_name_buffer_length];
        if (UnDecorateSymbolNameW(decorated_name.data(), undecorated_symbol_name_buffer, undecorated_symbol_name_buffer_length, flags) == 0)
        {
            throw std::runtime_error{std::format("UnDecorateSymbolName failed, error: {}", GetLastError())};
        }

        return File::StringType{undecorated_symbol_name_buffer};
    }

    auto main(std::filesystem::path& param_dll_path, std::filesystem::path& param_dll_name) -> void
    {
        dll_path = param_dll_path;
        dll_name = param_dll_name;
        process_data.module_handle = GetModuleHandleW(nullptr);

        wchar_t process_path[1024] {'\0'};
        K32GetModuleFileNameExW(GetCurrentProcess(), process_data.module_handle, process_path, sizeof(process_path) / sizeof(wchar_t));
        exe_path = process_path;
        exe_name = exe_path.filename();
        exe_path = exe_path.parent_path();

        try_create_debug_console();
        setup_unreal();
        Output::send(STR("Unreal module successfully setup\n"));

        input_handler.register_keydown_event(Input::Key::K, {Input::ModifierKey::CONTROL}, []() {
            Output::send(STR("CTRL + K hit\n"));

            SymSetOptions(/*SYMOPT_UNDNAME | */SYMOPT_DEFERRED_LOADS);
            if (!SymInitialize(GetCurrentProcess(), nullptr, TRUE))
            {
                throw std::runtime_error{std::format("SymInitialize error: {}", GetLastError())};
            }

            auto add_vtable_contents_to_vector = [](std::vector<DWORD64>& vector, void* uobject) {
                auto validate_ptr = [](void* vtable_entry) -> bool {
                    size_t bytes_read;
                    uintptr_t is_valid_ptr_buffer;

                    if (!ReadProcessMemory(GetCurrentProcess(), vtable_entry, &is_valid_ptr_buffer, 0x8, &bytes_read))
                    {
                        return false;
                    }
                    else
                    {
                        return true;
                    }

                };

                void* vtable = *static_cast<void**>(uobject);
                void* vtable_entry = *static_cast<void**>(vtable);
                Output::send(STR("vtable_entry: {}\n"), vtable_entry);

                size_t index{};
                bool is_valid_pointer = validate_ptr(vtable_entry);

                while (is_valid_pointer)
                {
                    vector.emplace_back(reinterpret_cast<DWORD64>(vtable_entry));

                    ++index;
                    vtable_entry = *reinterpret_cast<void**>(static_cast<char*>(vtable) + (index * 8));
                    is_valid_pointer = validate_ptr(vtable_entry);
                }
            };

            Output::Targets<Output::NewFileDevice> scoped_dumper;
            auto& file_device = scoped_dumper.get_device<Output::NewFileDevice>();
            file_device.set_file_name_and_path(dll_path / "vtable_dump.txt");
            file_device.set_formatter([](File::StringViewType string) -> File::StringType {
                return File::StringType{string};
            });

            Unreal::UObject* object = Unreal::UObjectGlobals::static_find_object(nullptr, nullptr, STR("/Script/CoreUObject.Default__Object"));
            if (!object)
            {
                throw std::runtime_error{"'object' was nullptr"};
            }
            Output::send(STR("Finding virtual function symbols for {:X} '{}'\n"), reinterpret_cast<uintptr_t>(object), object->get_full_name());

            std::vector<DWORD64> obj_ptrs_for_symbol_lookup{};
            add_vtable_contents_to_vector(obj_ptrs_for_symbol_lookup, object);

            std::unordered_set<DWORD64> resolved_addresses{};
            for (size_t i = 0; i < obj_ptrs_for_symbol_lookup.size(); ++i)
            {
                DWORD64 vtable_entry = obj_ptrs_for_symbol_lookup[i];
                // Uncomment to get rid of duplicates
                // This will leave some blank spaces in the vtable
                // But those are all duplicates and can safely be mapped to the first address that was found for the symbol
                //if (resolved_addresses.contains(vtable_entry)) { continue; }

                EnumModuleSymbolCallbackParams callback_params{.in_vtable_entry = vtable_entry};
                if (!SymEnumSymbols(GetCurrentProcess(), reinterpret_cast<ULONG64>(process_data.module_handle), "*", enum_module_symbols_callback, &callback_params))
                {
                    Output::send(STR("Symbolication for {:X} failed with error {}\n"), vtable_entry, GetLastError());
                }
                else
                {
                    Output::send(STR("Symbolication for {:X} was successful\n"), vtable_entry);
                }

                if (callback_params.symbol_names.empty())
                {
                    Output::send(STR("Symbol name for '{:X}' was empty\n"), vtable_entry);
                }
                else
                {
                    for (const auto& decorated_symbol_name : callback_params.symbol_names)
                    {
                        File::StringType undecorated_symbol_name_only{undecorate_name(decorated_symbol_name, UNDNAME_NAME_ONLY)};
                        if (does_symbol_belong_to_object(object, undecorated_symbol_name_only))
                        {
                            auto undecorated_symbol_full_name{undecorate_name(decorated_symbol_name, UNDNAME_NO_ACCESS_SPECIFIERS |
                                                                                                     UNDNAME_NO_ALLOCATION_LANGUAGE |
                                                                                                     UNDNAME_NO_ALLOCATION_MODEL |
                                                                                                     UNDNAME_NO_CV_THISTYPE |
                                                                                                     UNDNAME_NO_MS_KEYWORDS |
                                                                                                     UNDNAME_NO_MS_THISTYPE)
                            };

                            if (!is_symbol_virtual(undecorated_symbol_full_name)) { continue; }

                            scoped_dumper.send(STR("[{:X}] {} [o: {:X}]\n"), vtable_entry, undecorated_symbol_full_name, i * 8);

                            if (undecorated_symbol_name_only == STR("UObject::CallRemoteFunction"))
                            {
                                auto func_dec = undecorated_symbol_full_name;
                                auto func_def = undecorated_symbol_full_name;

                                // Function Declaration -> START
                                auto class_name = object->get_uclass()->get_name();
                                auto class_name_pos = func_dec.find(class_name);
                                if (class_name_pos == func_dec.npos)
                                {
                                    throw std::runtime_error{to_string(std::format(STR("Did not class name '{}' in function signature '{}'"), class_name, undecorated_symbol_full_name))};
                                }

                                // Remove the class name (e.g: Object), the prefix (e.g: U), and the ::
                                func_dec.erase(class_name_pos - 1, class_name.size() + 3);
                                // Function Declaration -> END

                                // Function Definition (param names) -> START
                                // Give all parameters a name
                                if (func_def.find(STR("(void)")) == func_def.npos)
                                {
                                    int32_t param_count{1};
                                    size_t param_pos = func_def.find(L',');
                                    while (param_pos != func_def.npos)
                                    {
                                        auto param_count_str = std::format(STR("p{}"), param_count++);
                                        func_def.insert(param_pos, param_count_str);
                                        param_pos = func_def.find(L',', param_pos + param_count_str.size() + 1);
                                    }

                                    size_t closing_parenthesis_pos = func_def.find(L')');
                                    if (closing_parenthesis_pos == func_def.npos)
                                    {
                                        throw std::runtime_error{"Closing parenthesis for function not found"};
                                    }
                                    func_def.insert(closing_parenthesis_pos, std::format(STR("p{}"), param_count));
                                    // Function Definition (param names) -> END
                                }

                                Output::send(STR("Function: UObject::CallRemoteFunction\n"));
                                Output::send(STR("Declaration: {}\n"), func_dec);
                                Output::send(STR("Definition: {}\n"), func_def);
                                Output::send(STR("RC::Function: Function<TBD>\n"));
                            }
                        }
                    }
                    resolved_addresses.emplace(vtable_entry);
                }
            }

            Output::send(STR("Virtual function symbolication completed."));
        });

        auto event_loop_thread = std::thread{&event_loop_update};
        event_loop_thread.join();
    }
}
