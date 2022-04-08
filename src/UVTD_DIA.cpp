#define NOMINMAX

#include <stdexcept>
#include <format>
#include <thread>
#include <unordered_set>
#include <numeric>

#include <UVTD/UVTD.hpp>
#include <UVTD/ExceptionHandling.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/Handler.hpp>
#include <Helpers/String.hpp>

#include <Windows.h>
#include <Psapi.h>
#include <dbghelp.h>

#include <atlbase.h>
#include <dia2.h>

namespace RC::UVTD
{
    bool processing_events{false};
    Input::Handler input_handler{L"ConsoleWindowClass", L"UnrealWindow"};
    std::unordered_map<File::StringType, EnumEntries> g_enum_entries{};
    std::unordered_map<File::StringType, Classes> g_class_entries;

    auto static event_loop_update() -> void
    {
        for (processing_events = true; processing_events;)
        {
            input_handler.process_event();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    auto static HRESULTToString(HRESULT result) -> std::string
    {
        switch (result)
        {
            case S_OK:
                return "S_OK";
            case S_FALSE:
                return "S_FALSE";
            case E_PDB_NOT_FOUND:
                return "E_PDB_NOT_FOUND: Failed to open the file, or the file has an invalid format.";
            case E_PDB_FORMAT:
                return "E_PDB_FORMAT: Attempted to access a file with an obsolete format.";
            case E_PDB_INVALID_SIG:
                return "E_PDB_INVALID_SIG: Signature does not match.";
            case E_PDB_INVALID_AGE:
                return "E_PDB_INVALID_AGE: Age does not match.";
            case E_INVALIDARG:
                return "E_INVALIDARG: Invalid parameter.";
            case E_UNEXPECTED:
                return "E_UNEXPECTED: Data source has already been prepared.";
            case E_NOINTERFACE:
                return "E_NOINTERFACE";
            case E_POINTER:
                return "E_POINTER";
            case REGDB_E_CLASSNOTREG:
                return "REGDB_E_CLASSNOTREG";
            case CLASS_E_NOAGGREGATION:
                return "CLASS_E_NOAGGREGATION";
            case CO_E_NOTINITIALIZED:
                return "CO_E_NOTINITIALIZED";
            default:
                return std::format("Unknown error. (error: {:X})", result);
        }
    }

    auto static sym_tag_to_string(DWORD sym_tag) -> File::StringViewType
    {
        switch (sym_tag)
        {
            case SymTagNull:
                return STR("SymTagNull");
            case SymTagExe:
                return STR("SymTagExe");
            case SymTagCompiland:
                return STR("SymTagCompiland");
            case SymTagCompilandDetails:
                return STR("SymTagCompilandDetails");
            case SymTagCompilandEnv:
                return STR("SymTagCompilandEnv");
            case SymTagFunction:
                return STR("SymTagFunction");
            case SymTagBlock:
                return STR("SymTagBlock");
            case SymTagData:
                return STR("SymTagData");
            case SymTagAnnotation:
                return STR("SymTagAnnotation");
            case SymTagLabel:
                return STR("SymTagLabel");
            case SymTagPublicSymbol:
                return STR("SymTagPublicSymbol");
            case SymTagUDT:
                return STR("SymTagUDT");
            case SymTagEnum:
                return STR("SymTagEnum");
            case SymTagFunctionType:
                return STR("SymTagFunctionType");
            case SymTagPointerType:
                return STR("SymTagPointerType");
            case SymTagArrayType:
                return STR("SymTagArrayType");
            case SymTagBaseType:
                return STR("SymTagBaseType");
            case SymTagTypedef:
                return STR("SymTagTypedef");
            case SymTagBaseClass:
                return STR("SymTagBaseClass");
            case SymTagFriend:
                return STR("SymTagFriend");
            case SymTagFunctionArgType:
                return STR("SymTagFunctionArgType");
            case SymTagFuncDebugStart:
                return STR("SymTagFuncDebugStart");
            case SymTagFuncDebugEnd:
                return STR("SymTagFuncDebugEnd");
            case SymTagUsingNamespace:
                return STR("SymTagUsingNamespace");
            case SymTagVTableShape:
                return STR("SymTagVTableShape");
            case SymTagVTable:
                return STR("SymTagVTable");
            case SymTagCustom:
                return STR("SymTagCustom");
            case SymTagThunk:
                return STR("SymTagThunk");
            case SymTagCustomType:
                return STR("SymTagCustomType");
            case SymTagManagedType:
                return STR("SymTagManagedType");
            case SymTagDimension:
                return STR("SymTagDimension");
            case SymTagCallSite:
                return STR("SymTagCallSite");
            case SymTagInlineSite:
                return STR("SymTagInlineSite");
            case SymTagBaseInterface:
                return STR("SymTagBaseInterface");
            case SymTagVectorType:
                return STR("SymTagVectorType");
            case SymTagMatrixType:
                return STR("SymTagMatrixType");
            case SymTagHLSLType:
                return STR("SymTagHLSLType");
            case SymTagCaller:
                return STR("SymTagCaller");
            case SymTagCallee:
                return STR("SymTagCallee");
            case SymTagExport:
                return STR("SymTagExport");
            case SymTagHeapAllocationSite:
                return STR("SymTagHeapAllocationSite");
            case SymTagCoffGroup:
                return STR("SymTagCoffGroup");
            case SymTagInlinee:
                return STR("SymTagInlinee");
            case SymTagMax:
                return STR("SymTagMax");
            default:
                return STR("Unknown");
        }
    }

    auto constexpr static is_symbol_valid(HRESULT result) -> bool
    {
        if (result != S_OK && result != S_FALSE)
        {
            Output::send(STR("Ran into an error with a symbol, error: {}\n"), to_wstring(HRESULTToString(result)));
            return false;
        }
        else if (result == S_FALSE)
        {
            //Output::send(STR("Symbol not valid but no error\n"));
            return false;
        }
        else
        {
            return true;
        }
    }

    static auto get_symbol_name(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        BSTR name_buffer;
        File::StringType name{STR("NoName")};
        HRESULT hr;
        if (hr = symbol->get_name(&name_buffer); hr == S_OK)
        {
            name = name_buffer;
        }

        BSTR undecorated_name_buffer2;
        if (hr = symbol->get_undecoratedName(&undecorated_name_buffer2); hr == S_OK)
        {
            name = undecorated_name_buffer2;
        }

        return name;
    }

    auto VTableDumper::setup_symbol_loader() -> void
    {
        CoInitialize(nullptr);

        //if (!std::filesystem::exists(pdb_file))
        //{
        //    throw std::runtime_error{std::format("PDB '{}' not found", pdb_file.string())};
        //}

        auto hr = CoCreateInstance(CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IDiaDataSource), reinterpret_cast<void**>(&dia_source));
        if (FAILED(hr))
        {
            throw std::runtime_error{std::format("CoCreateInstance failed. Register msdia80.dll. Error: {}", HRESULTToString(hr))};
        }

        //if (hr = dia_source->loadDataFromPdb(pdb_file.c_str()); FAILED(hr))
        auto exe_file = pdb_file;
        exe_file.replace_extension(".exe");
        Output::send(STR("exe_file: {}\n"), exe_file.wstring());
        Output::send(STR("exe_file.parent_path(): {}\n"), exe_file.parent_path().wstring());
        if (hr = dia_source->loadDataForExe(STR("PDBs\\UE4SS_Main412-Win64-Shipping.exe"), nullptr, nullptr); FAILED(hr))
        {
            throw std::runtime_error{std::format("Failed to load symbol data with error: {}", HRESULTToString(hr))};
        }

        if (hr = dia_source->openSession(&dia_session); FAILED(hr))
        {
            throw std::runtime_error{std::format("Call to 'openSession' failed with error: {}", HRESULTToString(hr))};
        }

        if (hr = dia_session->get_globalScope(&dia_global_symbol); FAILED(hr))
        {
            throw std::runtime_error{std::format("Call to 'get_globalScope' failed with error: {}", HRESULTToString(hr))};
        }
    }

    //auto VTableDumper::dump_vtable_for_symbol(File::StringViewType symbol_name_to_look_for) -> void
    auto VTableDumper::dump_vtable_for_symbol(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void
    {
        //Output::send(STR("Dumping {}@{}\n"), pdb_file.filename().stem().wstring(), symbol_name_to_look_for);
        Output::send(STR("Dumping {} struct symbols for {}\n"), names.size(), pdb_file.filename().stem().wstring());
        HRESULT hr{};

        if (hr = dia_session->findChildren(nullptr, SymTagNull, nullptr, nsNone, &dia_global_symbols_enum); hr != S_OK)
            //if (hr = dia_global_symbol->findChildren(SymTagNull, symbol_name_to_look_for.data(), NULL, &dia_global_symbols_enum); hr != S_OK)
            //if (hr = dia_global_symbol->findChildren(SymTagNull, nullptr, NULL, &dia_global_symbols_enum); hr != S_OK)
        {
            throw std::runtime_error{std::format("Call to 'findChildren' failed with error '{}'", HRESULTToString(hr))};
        }

        // Symbol: UE4SS_Main422-Win64-Shipping
        CComPtr<IDiaSymbol> symbol;
        ULONG celt_fetched;
        hr = dia_global_symbols_enum->Next(1, &symbol, &celt_fetched);
        if (hr != S_OK)
        {
            throw std::runtime_error{std::format("Ran into an error with a symbol while calling 'Next', error: {}\n", HRESULTToString(hr))};
        }

        CComPtr<IDiaEnumSymbols> sub_symbols;
        hr = symbol->findChildren(SymTagUDT, nullptr, NULL, &sub_symbols);
        if (hr != S_OK)
        {
            throw std::runtime_error{std::format("Ran into a problem while calling 'findChildren', error: {}\n", HRESULTToString(hr))};
        }

        CComPtr<IDiaSymbol> sub_symbol;
        ULONG num_symbols_fetched;
        hr = sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched);

        std::unordered_set<File::StringType> symbols_found{};

        for (; hr == S_OK; hr = sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched))
        {
            DWORD sub_sym_tag;
            sub_symbol->get_symTag(&sub_sym_tag);
            //if (sub_sym_tag != SymTagBaseClass && sub_sym_tag != SymTagUDT)
            //{
            //    sub_symbol = nullptr;
            //    continue;
            //}

            BSTR symbol_name_buffer;
            if (hr = sub_symbol->get_name(&symbol_name_buffer); hr != S_OK)
            {
                SysFreeString(symbol_name_buffer);
                sub_symbol = nullptr;
                continue;
            }
            File::StringType symbol_name{symbol_name_buffer};

            //Output::send(STR("{}\n"), symbol_name);
            /**/
            //if (symbol_name != symbol_name_to_look_for)

            /**/
            auto name_it = names.find(symbol_name);
            if (name_it == names.end())
            {
                SysFreeString(symbol_name_buffer);
                sub_symbol = nullptr;
                continue;
            }
            symbols_found.emplace(symbol_name);
            //*/

            BSTR symbol_undecorated_name_buffer;
            if (hr = sub_symbol->get_undecoratedName(&symbol_undecorated_name_buffer); hr == S_OK)
            {
                symbol_name = symbol_undecorated_name_buffer;
            }

            Output::send(STR("Found symbol: '{}', tag: '{}'\n"), !symbol_name.empty() ? symbol_name : STR("NoName"), sym_tag_to_string(sub_sym_tag));

            CComPtr<IDiaEnumSymbols> sub_sub_symbols;
            if (hr = sub_symbol->findChildren(SymTagNull, nullptr, NULL, &sub_sub_symbols); hr != S_OK)
            {
                sub_symbol = nullptr;
                sub_sub_symbols = nullptr;
                continue;
            }

            CComPtr<IDiaSymbol> sub_sub_symbol;
            hr = sub_sub_symbols->Next(1, &sub_sub_symbol, &num_symbols_fetched);

            // Replace 'U' with 'F' for everything except UField & FField
            if (name_it->second.replace_u_prefix_with_f_prefix == ReplaceUPrefixWithFPrefix::Yes)
                //if (symbol_name.starts_with('U') && symbol_name != STR("UField") && symbol_name != STR("FField"))
            {
                symbol_name.replace(0, 1, STR("F"));
            }

            File::StringType symbol_name_clean{symbol_name};
            std::replace(symbol_name_clean.begin(), symbol_name_clean.end(), ':', '_');

            auto& enum_entries = g_enum_entries.emplace(symbol_name_clean, EnumEntries{
                    .class_name = File::StringType{symbol_name},
                    .class_name_clean = symbol_name_clean,
                    .pdb_file = pdb_file
            }).first->second;

            auto& class_entry = [&]() -> Class& {
                auto pdb_file_name = pdb_file.filename().stem();
                if (g_class_entries.contains(pdb_file_name))
                {
                    auto& classes = g_class_entries[pdb_file_name];
                    if (classes.entries.contains(symbol_name_clean))
                    {
                        return classes.entries[symbol_name_clean];
                    }
                    else
                    {
                        return classes.entries.emplace(symbol_name_clean, Class{
                                .class_name = File::StringType{symbol_name},
                                .class_name_clean = symbol_name_clean
                        }).first->second;
                    }
                }
                else
                {
                    return g_class_entries[pdb_file_name].entries[symbol_name_clean] = Class{
                            .class_name = File::StringType{symbol_name},
                            .class_name_clean = symbol_name_clean
                    };
                }
            }();

            SysFreeString(symbol_name_buffer);
            SysFreeString(symbol_undecorated_name_buffer);

            for (; hr == S_OK; hr = sub_sub_symbols->Next(1, &sub_sub_symbol, &num_symbols_fetched))
            {
                DWORD sub_sub_sym_tag;
                sub_sub_symbol->get_symTag(&sub_sub_sym_tag);

                BOOL is_virtual;
                hr = sub_sub_symbol->get_virtual(&is_virtual);
                if (hr != S_OK) { is_virtual = FALSE; }

                if (is_virtual == FALSE)
                {
                    sub_sub_symbol = nullptr;
                    continue;
                }

                BSTR sub_sub_symbol_name_buffer;
                if (hr = sub_sub_symbol->get_name(&sub_sub_symbol_name_buffer); hr != S_OK)
                {
                    SysFreeString(sub_sub_symbol_name_buffer);
                    sub_sub_symbol = nullptr;
                    continue;
                }
                File::StringViewType sub_sub_symbol_name{sub_sub_symbol_name_buffer};

                BSTR sub_sub_symbol_undecorated_name_buffer;
                if (hr = sub_sub_symbol->get_undecoratedName(&sub_sub_symbol_undecorated_name_buffer); hr == S_OK)
                {
                    sub_sub_symbol_name = sub_sub_symbol_undecorated_name_buffer;
                }

                DWORD offset_in_vtable;
                if (hr = sub_sub_symbol->get_virtualBaseOffset(&offset_in_vtable); hr != S_OK)
                {
                    throw std::runtime_error{std::format("Call to 'get_virtualBaseOffset' failed with error '{}'", HRESULTToString(hr))};
                }

                Output::send(STR("    {}, 0x{:X}, tag: {}\n"), !sub_sub_symbol_name.empty() ? sub_sub_symbol_name : STR("NoName"), offset_in_vtable, sym_tag_to_string(sub_sub_sym_tag));

                CComPtr<IDiaSymbol> parent;
                if (hr = sub_sub_symbol->get_classParent(&parent); hr == S_OK)
                {
                    DWORD tag;
                    parent->get_symTag(&tag);

                    BSTR name_buffer;
                    if (hr = parent->get_name(&name_buffer); hr != S_OK)
                    {
                        SysFreeString(name_buffer);
                        parent = nullptr;
                        continue;
                    }
                    File::StringViewType name{name_buffer};

                    BSTR undecorated_name_buffer;
                    if (hr = parent->get_undecoratedName(&undecorated_name_buffer); hr == S_OK)
                    {
                        name = undecorated_name_buffer;
                    }

                    Output::send(STR("    parent: {}, tag: {}\n"), name, sym_tag_to_string(tag));

                    CComPtr<IDiaEnumSymbols> parent_sub_symbols;
                    if (hr = parent->findChildren(SymTagVTableShape, nullptr, NULL, &parent_sub_symbols); hr == S_OK)
                    {
                        ULONG num_parent_sub_symbols_fetched;
                        CComPtr<IDiaSymbol> parent_sub_symbol;
                        hr = parent_sub_symbols->Next(1, &parent_sub_symbol, &num_parent_sub_symbols_fetched);

                        for (; hr == S_OK; hr = parent_sub_symbols->Next(1, &parent_sub_symbol, &num_parent_sub_symbols_fetched))
                        {
                            DWORD tag2;
                            parent->get_symTag(&tag2);

                            /*
                            BSTR name_buffer2;
                            File::StringType name2{STR("NoName")};
                            if (hr = parent->get_name(&name_buffer2); hr != S_OK)
                            {
                                SysFreeString(name_buffer2);
                            }
                            else
                            {
                                name2 = name_buffer2;
                            }

                            BSTR undecorated_name_buffer2;
                            if (hr = parent->get_undecoratedName(&undecorated_name_buffer2); hr != S_OK)
                            {
                                SysFreeString(name_buffer2);
                            }
                            else
                            {
                                name2 = undecorated_name_buffer2;
                            }
                            //*/

                            Output::send(STR("        parent_sub_symbol: {}, tag: {}\n"), get_symbol_name(parent_sub_symbol), sym_tag_to_string(tag2));

                            parent_sub_symbol = nullptr;
                        }
                    }
                }

                File::StringType sub_sub_symbol_name_clean{sub_sub_symbol_name};
                if (sub_sub_symbol_name_clean.empty())
                {
                    sub_sub_symbol_name_clean = STR("InvalidSymbolName");
                }
                else if (sub_sub_symbol_name_clean.find('~') != sub_sub_symbol_name.npos)
                {
                    sub_sub_symbol_name_clean.replace(0, 1, STR(""));
                    sub_sub_symbol_name_clean.append(STR("_Destructor"));
                }

                enum_entries.entries.emplace(sub_sub_symbol_name_clean, EnumEntry{.name = sub_sub_symbol_name_clean, .offset = offset_in_vtable});
                //test.entries.emplace(sub_sub_symbol_name_clean, EnumEntry{.name = sub_sub_symbol_name_clean, .offset = offset_in_vtable});
                auto& function = class_entry.functions[sub_sub_symbol_name_clean];
                function.name = sub_sub_symbol_name_clean;
                function.offset = offset_in_vtable;

                SysFreeString(sub_sub_symbol_name_buffer);
                SysFreeString(sub_sub_symbol_undecorated_name_buffer);
                sub_sub_symbol = nullptr;
            }

            sub_symbol = nullptr;
            sub_sub_symbols = nullptr;

            if (symbols_found.size() >= names.size())
            {
                break;
            }
        }

        Output::send(STR("Found {} of {} symbols\n"), symbols_found.size(), names.size());

        sub_symbols = nullptr;
        symbol = nullptr;
        dia_global_symbols_enum = nullptr;
    }

    auto static generate_files(const std::filesystem::path& output_dir) -> void
    {
        if (!std::filesystem::exists(output_dir))
        {
            std::filesystem::create_directory(output_dir);
        }

        if (std::filesystem::exists(output_dir / "generated_include"))
        {
            for (const auto& item : std::filesystem::directory_iterator(output_dir / "generated_include"))
            {
                if (item.is_directory()) { continue; }
                if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp")) { continue; }

                File::delete_file(item.path());
            }
        }

        if (std::filesystem::exists(output_dir / "generated_include/FunctionBodies"))
        {
            for (const auto& item : std::filesystem::directory_iterator(output_dir / "generated_include/FunctionBodies"))
            {
                if (item.is_directory()) { continue; }
                if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp")) { continue; }

                File::delete_file(item.path());
            }
        }

        if (std::filesystem::exists(output_dir / "generated_src"))
        {
            for (const auto& item : std::filesystem::directory_iterator(output_dir / "generated_src"))
            {
                if (item.is_directory()) { continue; }
                if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp")) { continue; }

                File::delete_file(item.path());
            }
        }

        for (const auto&[class_name, enum_entries] : g_enum_entries)
        {
            Output::send(STR("Generating file 'VTableOffsets_{}.hpp'\n"), enum_entries.class_name_clean);
            Output::Targets<Output::NewFileDevice> hpp_dumper;
            auto& hpp_file_device = hpp_dumper.get_device<Output::NewFileDevice>();
            hpp_file_device.set_file_name_and_path(output_dir / std::format(STR("generated_include/VTableOffsets_{}.hpp"), enum_entries.class_name_clean));
            hpp_file_device.set_formatter([](File::StringViewType string) {
                return File::StringType{string};
            });
            hpp_dumper.send(STR("struct VTableOffsets\n"));
            hpp_dumper.send(STR("{\n"));

            Output::send(STR("Generating file 'VTableOffsets_{}.cpp'\n"), enum_entries.class_name_clean);
            Output::Targets<Output::NewFileDevice> cpp_dumper;
            auto& cpp_file_device = cpp_dumper.get_device<Output::NewFileDevice>();
            cpp_file_device.set_file_name_and_path(output_dir / std::format(STR("generated_src/VTableOffsets_{}.cpp"), enum_entries.class_name_clean));
            cpp_file_device.set_formatter([](File::StringViewType string) {
                return File::StringType{string};
            });
            cpp_dumper.send(STR("#include <cstdint>\n\n"));
            // For now just include every struct that's had offsets generated
            // It would be better if only necessary files are included but I can look into that later
            cpp_dumper.send(STR("#include <Unreal/UObject.hpp>\n"));
            cpp_dumper.send(STR("#include <Unreal/UScriptStruct.hpp>\n"));
            cpp_dumper.send(STR("#include <Unreal/FMemory.hpp>\n"));
            cpp_dumper.send(STR("#include <Unreal/Property/FMulticastDelegateProperty.hpp>\n"));
            cpp_dumper.send(STR("#include <Unreal/Property/FNumericProperty.hpp>\n"));
            cpp_dumper.send(STR("#include <Unreal/Property/FObjectProperty.hpp>\n"));
            cpp_dumper.send(STR("#include <Unreal/FProperty.hpp>\n\n"));
            cpp_dumper.send(STR("namespace RC::Unreal\n{\n"));

            auto local_class_name = enum_entries.class_name;
            if (auto pos = local_class_name.find(STR("Property")); pos != local_class_name.npos)
            {
                local_class_name.replace(0, 1, STR("F"));
            }

            for (const auto&[enum_entry_name, enum_entry] : enum_entries.entries)
            {
                hpp_dumper.send(STR("    static uint32_t {};\n"), enum_entry_name);
                cpp_dumper.send(STR("    uint32_t {}::VTableOffsets::{} = 0;\n"), local_class_name, enum_entry_name);
            }

            hpp_dumper.send(STR("};\n"));

            cpp_dumper.send(STR("}\n"));
        }

        for (const auto&[pdb_name, classes] : g_class_entries)
        {
            for (const auto&[class_name, class_entry] : classes.entries)
            {
                Output::send(STR("Generating file '{}_VTableOffsets_{}_FunctionBody.cpp'\n"), pdb_name, class_entry.class_name_clean);
                Output::Targets<Output::NewFileDevice> function_body_dumper;
                auto& function_body_file_device = function_body_dumper.get_device<Output::NewFileDevice>();
                function_body_file_device.set_file_name_and_path(output_dir / std::format(STR("generated_include/FunctionBodies/{}_VTableOffsets_{}_FunctionBody.cpp"), pdb_name, class_name));
                function_body_file_device.set_formatter([](File::StringViewType string) {
                    return File::StringType{string};
                });

                for (const auto&[function_name, function_entry] : class_entry.functions)
                {
                    auto local_class_name = class_entry.class_name;
                    if (auto pos = local_class_name.find(STR("Property")); pos != local_class_name.npos)
                    {
                        local_class_name.replace(0, 1, STR("F"));
                    }
                    function_body_dumper.send(STR("{}::VTableOffsets::{} = 0x{:X};\n"), local_class_name, function_name, function_entry.offset);
                }
            }
        }
    }

    auto VTableDumper::generate_code() -> void
    {
        setup_symbol_loader();

        // TODO: Figure out what to do if a legacy struct still exists alongside the new struct
        //       An example of this is UField and FField. Both exist in 4.25+
        //       FField & UField are special and will not be combined
        //       All other types, that only differ by the prefix, must have their generated non-function-body files merged into one
        //       This should be done, confirm it before removing this comment

        //dump_vtable_for_symbol(STR("UScriptStruct::ICppStructOps"));

        //std::unordered_set<File::StringType> names;
        std::unordered_map<File::StringType, SymbolNameInfo> names;
        names.emplace(STR("UObjectBase"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UObjectBase"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UObjectBaseUtility"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UObject"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UScriptStruct::ICppStructOps"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("FOutputDevice"), ReplaceUPrefixWithFPrefix::No);

        // Structs that don't have their own virtual functions
        // They may be overriding base virtual functions but in that case they will be located at the same offset in the vtable as the base
        // As a result, they don't need to have their offset in the vtable dumped
        //names.emplace(STR("FBoolProperty")); // Will come up with nothing in <4.25
        //names.emplace(STR("UBoolProperty")); // Will have something in 4.25+
        //names.emplace(STR("FArrayProperty"));
        //names.emplace(STR("UArrayProperty"));
        //names.emplace(STR("FMapProperty"));
        //names.emplace(STR("UMapProperty"));
        //names.emplace(STR("FDelegateProperty"));
        //names.emplace(STR("UDelegateProperty"));
        //names.emplace(STR("FMulticastInlineDelegateProperty"));
        //names.emplace(STR("UMulticastInlineDelegateProperty"));
        //names.emplace(STR("FMulticastSparseDelegateProperty"));
        //names.emplace(STR("UMulticastSparseDelegateProperty"));
        //names.emplace(STR("FFieldPathProperty")); // 4.25+ only
        //names.emplace(STR("FInterfaceProperty"));
        //names.emplace(STR("UInterfaceProperty"));
        //names.emplace(STR("FObjectProperty"));
        //names.emplace(STR("UObjectProperty"));
        //names.emplace(STR("FWeakObjectProperty"));
        //names.emplace(STR("UWeakObjectProperty"));
        //names.emplace(STR("FLazyObjectProperty"));
        //names.emplace(STR("ULazyObjectProperty"));
        //names.emplace(STR("FSoftObjectProperty"));
        //names.emplace(STR("USoftObjectProperty"));
        //names.emplace(STR("FClassProperty"));
        //names.emplace(STR("UClassProperty"));
        //names.emplace(STR("FSetProperty"));
        //names.emplace(STR("USetProperty"));
        //names.emplace(STR("FNameProperty"));
        //names.emplace(STR("UNameProperty"));
        //names.emplace(STR("FStrProperty"));
        //names.emplace(STR("UStrProperty"));
        //names.emplace(STR("FStructProperty"));
        //names.emplace(STR("UStructProperty"));
        //names.emplace(STR("FEnumProperty"));
        //names.emplace(STR("UEnumProperty"));
        //names.emplace(STR("FSoftClassProperty"));
        //names.emplace(STR("USoftClassProperty"));
        //names.emplace(STR("FTextProperty"));
        //names.emplace(STR("UTextProperty"));

        // Structs that have their own virtual functions and therefore need to have their offset in the vtable dumped
        names.emplace(STR("FProperty"), ReplaceUPrefixWithFPrefix::No); // Will come up with nothing in <4.25
        names.emplace(STR("UProperty"), ReplaceUPrefixWithFPrefix::Yes); // Will come up with nothing in 4.25+
        names.emplace(STR("FField"), ReplaceUPrefixWithFPrefix::No); // Will come up with nothing in <4.25
        names.emplace(STR("UField"), ReplaceUPrefixWithFPrefix::No); // Will have something in 4.25+
        names.emplace(STR("UStruct"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("FMalloc"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("FNumericProperty"), ReplaceUPrefixWithFPrefix::No); // Will come up with nothing in <4.25
        names.emplace(STR("UNumericProperty"), ReplaceUPrefixWithFPrefix::Yes); // Will have something in 4.25+
        names.emplace(STR("FMulticastDelegateProperty"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UMulticastDelegateProperty"), ReplaceUPrefixWithFPrefix::Yes);
        names.emplace(STR("FObjectPropertyBase"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UObjectPropertyBase"), ReplaceUPrefixWithFPrefix::Yes);

        dump_vtable_for_symbol(names);
    }

    auto VTableDumper::experimental_generate_members() -> void
    {
        setup_symbol_loader();

        HRESULT hr{};

        if (hr = dia_session->findChildrenEx(nullptr, SymTagNull, nullptr, nsNone, &dia_global_symbols_enum); hr != S_OK)
            //if (hr = dia_global_symbol->findChildren(SymTagNull, symbol_name_to_look_for.data(), NULL, &dia_global_symbols_enum); hr != S_OK)
            //if (hr = dia_global_symbol->findChildren(SymTagNull, nullptr, NULL, &dia_global_symbols_enum); hr != S_OK)
        {
            throw std::runtime_error{std::format("Call to 'findChildren' failed with error '{}'", HRESULTToString(hr))};
        }

        CComPtr<IDiaSymbol> symbol;
        ULONG celt_fetched;
        hr = dia_global_symbols_enum->Next(1, &symbol, &celt_fetched);
        if (hr != S_OK)
        {
            throw std::runtime_error{std::format("Ran into an error with a symbol while calling 'Next', error: {}\n", HRESULTToString(hr))};
        }

        Output::send(STR("Symbol: {}\n"), get_symbol_name(symbol));

        CComPtr<IDiaEnumSymbols> sub_symbols;
        hr = symbol->findChildrenEx(SymTagNull, nullptr, NULL, &sub_symbols);
        if (hr != S_OK)
        {
            throw std::runtime_error{std::format("Ran into a problem while calling 'findChildren', error: {}\n", HRESULTToString(hr))};
        }

        CComPtr<IDiaSymbol> sub_symbol;
        ULONG num_symbols_fetched;
        hr = sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched);

        for (; hr == S_OK; hr = sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched))
        {
            //BSTR symbol_name_buffer;
            //if (hr = sub_symbol->get_name(&symbol_name_buffer); hr != S_OK)
            //{
            //    SysFreeString(symbol_name_buffer);
            //    sub_symbol = nullptr;
            //    continue;
            //}
            //File::StringType symbol_name{symbol_name_buffer};
            //if (symbol_name != STR("FCoreDelegates")) { continue; }
            //if (symbol_name != STR("UObjectBase"))
            //{
            //    sub_symbol = nullptr;
            //    continue;
            //}

            //if (symbol_name.find(STR("UObjectBase")) == symbol_name.npos)
            //if (symbol_name.find(STR("DeferredRegister")) == symbol_name.npos)
            //{
            //    sub_symbol = nullptr;
            //    continue;
            //}

            //BSTR symbol_undecorated_name_buffer;
            //if (hr = sub_symbol->get_undecoratedName(&symbol_undecorated_name_buffer); hr == S_OK)
            //{
            //    Output::send(STR("has undecorated name\n"));
            //    symbol_name = symbol_undecorated_name_buffer;
            //}
            //else
            //{
            //    Output::send(STR("doesn't have undecorated name\n"));
            //}
//
            //Output::send(STR("Found symbol: '{}'\n"), !symbol_name.empty() ? symbol_name : STR("NoName"));

            CComPtr<IDiaEnumSymbols> sub_sub_symbols;
            if (hr = sub_symbol->findChildrenEx(SymTagVTable, nullptr, NULL, &sub_sub_symbols); hr != S_OK)
            {
                sub_symbol = nullptr;
                sub_sub_symbols = nullptr;
                continue;
            }

            CComPtr<IDiaSymbol> sub_sub_symbol;
            hr = sub_sub_symbols->Next(1, &sub_sub_symbol, &num_symbols_fetched);

            for (; hr == S_OK; hr = sub_sub_symbols->Next(1, &sub_sub_symbol, &num_symbols_fetched))
            {
                DWORD sub_sub_sym_tag;
                sub_sub_symbol->get_symTag(&sub_sub_sym_tag);

                if (sub_sub_sym_tag == SymTagFunction)
                {
                    sub_sub_symbol = nullptr;
                    continue;
                }

                BSTR sub_sub_symbol_name_buffer;
                if (hr = sub_sub_symbol->get_name(&sub_sub_symbol_name_buffer); hr != S_OK)
                {
                    //SysFreeString(sub_sub_symbol_name_buffer);
                    sub_sub_symbol = nullptr;
                    continue;
                }
                File::StringViewType sub_sub_symbol_name{sub_sub_symbol_name_buffer};

                BSTR sub_sub_symbol_undecorated_name_buffer;
                if (hr = sub_sub_symbol->get_undecoratedName(&sub_sub_symbol_undecorated_name_buffer); hr == S_OK)
                {
                    Output::send(STR("    Has undecorated name\n"));
                    Output::send(STR("    Decorated: {}\n"), sub_sub_symbol_name);
                    Output::send(STR("    Undecorated: {}\n"), File::StringViewType{sub_sub_symbol_undecorated_name_buffer});
                    sub_sub_symbol_name = sub_sub_symbol_undecorated_name_buffer;
                }

                LONG offset;
                if (hr = sub_sub_symbol->get_offset(&offset); hr != S_OK)
                {
                    //Output::send(STR("Could not get offset: {}\n"), to_wstring(HRESULTToString(hr)));
                    offset = std::numeric_limits<LONG>::max();
                }

                Output::send(STR("    Found sub symbol: '{}', offset: '0x{:X}', tag: '{}'\n"), !sub_sub_symbol_name.empty() ? sub_sub_symbol_name : STR("NoName"), offset, sym_tag_to_string(sub_sub_sym_tag));

                CComPtr<IDiaSymbol> type;
                if (hr = sub_sub_symbol->get_type(&type); hr == S_OK)
                {
                    Output::send(STR("    HAS TYPE\n"));

                    DWORD type_sym_tag;
                    type->get_symTag(&type_sym_tag);
                    Output::send(STR("    Tag: {}\n"), sym_tag_to_string(type_sym_tag));

                    if (type_sym_tag == SymTagPointerType)
                    {
                        CComPtr<IDiaSymbol> pointer_type;
                        if (hr = type->get_type(&pointer_type); hr == S_OK)
                        {
                            BSTR type_name_buffer;
                            if (hr = pointer_type->get_name(&type_name_buffer); hr == S_OK)
                            {
                                File::StringViewType type_name{type_name_buffer};
                                Output::send(STR("    pointer type name: {}\n"), type_name);
                            }

                            BSTR type_undecorated_name_buffer;
                            if (hr = pointer_type->get_undecoratedName(&type_undecorated_name_buffer); hr == S_OK)
                            {
                                File::StringViewType type_undecorated_name{type_name_buffer};
                                type_undecorated_name = type_undecorated_name_buffer;
                                Output::send(STR("    undecorated pointer type name: {}\n"), type_undecorated_name);
                            }
                        }
                    }
                    else if (type_sym_tag == SymTagBaseType)
                    {
                        // Enumerate get_baseType
                    }

                    // TODO: Look into anything that uses the subscript operator, idk if the [x] will be included in the value symbol name.
                    // TODO: Add * or & depending on if the type symbol is a pointer or a reference because that's not included in the symbol name.
                    // TODO: Look into if const is included in symbol names.

                    BSTR type_name_buffer;
                    if (hr = type->get_name(&type_name_buffer); hr == S_OK)
                    {
                        File::StringViewType type_name{type_name_buffer};
                        Output::send(STR("    type name: {}\n"), type_name);
                    }

                    BSTR type_undecorated_name_buffer;
                    if (hr = type->get_undecoratedName(&type_undecorated_name_buffer); hr == S_OK)
                    {
                        File::StringViewType type_undecorated_name{type_name_buffer};
                        type_undecorated_name = type_undecorated_name_buffer;
                        Output::send(STR("    undecorated type name: {}\n"), type_undecorated_name);
                    }

                    BasicType btBaseType;
                    if (hr = type->get_baseType((DWORD*)&btBaseType); hr == S_OK)
                    {
                        Output::send(STR("    Has type base type: {}\n"), (int)btBaseType);
                    }
                }

                IDiaSymbol* sub_type;
                if (hr = sub_sub_symbol->get_baseSymbol(&sub_type); hr == S_OK)
                {
                    Output::send(STR("    HAS BASE SYMBOL   \n"));
                }

                sub_sub_symbol = nullptr;
            }

            //SysFreeString(symbol_name_buffer);
            //SysFreeString(symbol_undecorated_name_buffer);

            sub_symbol = nullptr;
            sub_sub_symbols = nullptr;
        }

        sub_symbols = nullptr;
        symbol = nullptr;
        dia_global_symbols_enum = nullptr;
    }

    auto main() -> void
    {
        Output::set_default_devices<Output::DebugConsoleDevice>();
        Output::send(STR("Unreal module successfully setup\n"));

        //input_handler.register_keydown_event(Input::Key::K, {Input::ModifierKey::CONTROL}, []() {
        /**/
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_12.pdb"};
                vtable_dumper.generate_code();
                //vtable_dumper.experimental_generate_members();
            }
            CoUninitialize();
        });
        //Output::send(STR("Done\n"));
        //return;
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_13.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_14.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_15.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_16.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_17.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_18.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_19.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_20.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_21.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_22.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_23.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_24.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_25.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_26.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        //*/
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/4_27.pdb"};
                vtable_dumper.generate_code();
                //vtable_dumper.experimental_generate_members();
            }
            CoUninitialize();
        });
        /**/
        TRY([] {
            {
                VTableDumper vtable_dumper{"PDBs/5_00.pdb"};
                vtable_dumper.generate_code();
            }
            CoUninitialize();
        });
        //*/

        generate_files("GeneratedVTables");
        Output::send(STR("Code generated.\n"));
        //});

        auto event_loop_thread = std::thread{&event_loop_update};
        event_loop_thread.join();
    }
}
