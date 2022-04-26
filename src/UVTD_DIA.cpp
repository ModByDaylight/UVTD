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
    std::unordered_map<File::StringType, EnumEntry> g_enum_entries{};
    std::unordered_map<File::StringType, Classes> g_class_entries{};

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

    auto static kind_to_string(DWORD kind) -> File::StringViewType
    {
        switch (kind)
        {
            case DataIsUnknown:
                return STR("DataIsUnknown");
            case DataIsLocal:
                return STR("DataIsLocal");
            case DataIsStaticLocal:
                return STR("DataIsStaticLocal");
            case DataIsParam:
                return STR("DataIsParam");
            case DataIsObjectPtr:
                return STR("DataIsObjectPtr");
            case DataIsFileStatic:
                return STR("DataIsFileStatic");
            case DataIsGlobal:
                return STR("DataIsGlobal");
            case DataIsMember:
                return STR("DataIsMember");
            case DataIsStaticMember:
                return STR("DataIsStaticMember");
            case DataIsConstant:
                return STR("DataIsConstant");
        }

        return STR("UnhandledKind");
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

    auto static base_type_to_string(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        DWORD sym_tag;
        symbol->get_symTag(&sym_tag);
        if (sym_tag != SymTagBaseType)
        {
            throw std::runtime_error{"base_type_to_string only works on SymTagBaseType"};
        }

        File::StringType name{};

        ULONGLONG type_size;
        symbol->get_length(&type_size);

        BasicType base_type;
        symbol->get_baseType(std::bit_cast<DWORD*>(&base_type));

        switch (base_type)
        {
            case btVoid:
                name.append(STR("void"));
                break;
            case btChar:
            case btWChar:
                name.append(STR("TCHAR"));
                break;
            case btUInt:
                name.append(STR("u"));
            case btInt:
                switch (type_size)
                {
                    case 1:
                        name.append(STR("int8"));
                        break;
                    case 2:
                        name.append(STR("int16"));
                        break;
                    case 4:
                        name.append(STR("int32"));
                        break;
                    case 8:
                        name.append(STR("int64"));
                        break;
                    default:
                        name.append(STR("--unknown-int-size--"));
                        break;
                }
                break;
            case btFloat:
                switch (type_size)
                {
                    case 4:
                        name.append(STR("float"));
                        break;
                    case 8:
                        name.append(STR("double"));
                        break;
                    default:
                        name.append(STR("--unknown-float-size--"));
                        break;
                }
                break;
            case btBool:
                name.append(STR("bool"));
                break;
            case btNoType:
            case btBCD:
            case btLong:
            case btULong:
            case btCurrency:
            case btDate:
            case btVariant:
            case btComplex:
            case btBit:
            case btBSTR:
            case btHresult:
            case btChar16:
            case btChar32:
            case btChar8:
                throw std::runtime_error{"Unsupported SymTagBaseType type."};
                break;
        }

        return name;
    }

    auto static get_symbol_name(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        File::StringType name{};

        DWORD sym_tag;
        symbol->get_symTag(&sym_tag);

        CComPtr<IDiaSymbol> real_symbol;

        HRESULT hr;
        if (sym_tag == SymTagFunctionType || sym_tag == SymTagPointerType)
        {
            if (hr = symbol->get_type(&real_symbol); hr != S_OK)
            {
                throw std::runtime_error{std::format("Call to 'get_type()' failed with error '{}'", HRESULTToString(hr))};
            }
        }
        else
        {
            // Default
            real_symbol = symbol;
        }

        DWORD real_sym_tag;
        real_symbol->get_symTag(&real_sym_tag);

        if (real_sym_tag == SymTagBaseType)
        {
            name.append(base_type_to_string(real_symbol));
        }
        else
        {
            if (real_sym_tag == SymTagPointerType)
            {
                if (hr = real_symbol->get_type(&real_symbol); hr != S_OK)
                {
                    throw std::runtime_error{std::format("Call to 'get_type()' failed with error '{}'", HRESULTToString(hr))};
                }

                real_symbol->get_symTag(&real_sym_tag);
                if (real_sym_tag == SymTagBaseType)
                {
                    name.append(base_type_to_string(real_symbol));
                }
            }

            BSTR name_buffer;
            if (hr = real_symbol->get_name(&name_buffer); hr == S_OK)
            {
                name = name_buffer;
            }

            BSTR undecorated_name_buffer;
            if (hr = real_symbol->get_undecoratedName(&undecorated_name_buffer); hr == S_OK)
            {
                name = undecorated_name_buffer;
            }
        }

        if (name.empty())
        {
            name = STR("NoName");
        }

        return name;
    }

    auto VTableDumper::setup_symbol_loader() -> void
    {
        CoInitialize(nullptr);

        if (!std::filesystem::exists(pdb_file))
        {
            throw std::runtime_error{std::format("PDB '{}' not found", pdb_file.string())};
        }

        auto hr = CoCreateInstance(CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IDiaDataSource), reinterpret_cast<void**>(&dia_source));
        if (FAILED(hr))
        {
            throw std::runtime_error{std::format("CoCreateInstance failed. Register msdia80.dll. Error: {}", HRESULTToString(hr))};
        }

        if (hr = dia_source->loadDataFromPdb(pdb_file.c_str()); FAILED(hr))
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

    auto static generate_pointer_type(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        HRESULT hr;
        DWORD sym_tag;
        symbol->get_symTag(&sym_tag);
        CComPtr<IDiaSymbol> real_symbol;

        if (sym_tag == SymTagFunctionType)
        {
            if (hr = symbol->get_type(&real_symbol); hr != S_OK)
            {
                throw std::runtime_error{std::format("Call to 'get_type()' failed with error '{}'", HRESULTToString(hr))};
            }
        }
        else
        {
            // Default
            real_symbol = symbol;
        }

        DWORD real_sym_tag;
        real_symbol->get_symTag(&real_sym_tag);

        if (real_sym_tag != SymTagPointerType)
        {
            return STR("");
        }

        BOOL is_reference = FALSE;
        if (hr = real_symbol->get_reference(&is_reference); hr != S_OK)
        {
            throw std::runtime_error{std::format("Call to 'get_reference(&is_reference)' failed with error '{}'", HRESULTToString(hr))};
        }

        if (is_reference == TRUE)
        {
            return STR("&");
        }
        else
        {
            return STR("*");
        }
    }

    auto static generate_const_qualifier(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        HRESULT hr;
        CComPtr<IDiaSymbol> real_symbol = symbol;
        DWORD sym_tag;
        real_symbol->get_symTag(&sym_tag);

        // TODO: Fix this. It's currently broken for two reasons.
        //       1. If sym_tag == SymTagFunctionType, we assume that we're looking for the return type but we might be looking for the const qualifier on the function itself.
        //       2. Even if we change this to fix the above problem, for some reason, calling 'get_constType' on the SymTagFunctionType sets the BOOL to FALSE, and calling it on SymTagFunction returns S_FALSE.
        if (sym_tag == SymTagFunctionType)
        {
            if (hr = real_symbol->get_type(&real_symbol); hr != S_OK)
            {
                throw std::runtime_error{std::format("Call to 'get_type()' failed with error '{}'", HRESULTToString(hr))};
            }

            real_symbol->get_symTag(&sym_tag);
            if (sym_tag == SymTagPointerType)
            {
                if (hr = real_symbol->get_type(&real_symbol); hr != S_OK)
                {
                    throw std::runtime_error{std::format("Call to 'get_type()' failed with error '{}'", HRESULTToString(hr))};
                }
            }
        }

        BOOL is_const = FALSE;
        real_symbol->get_constType(&is_const);

        return is_const ? STR("const") : STR("");
    }

    auto static generate_type(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        HRESULT hr;
        CComPtr<IDiaSymbol> function_type_symbol;
        if (hr = symbol->get_type(&function_type_symbol); hr != S_OK)
        {
            function_type_symbol = symbol;
        }

        // TODO: Const for pointers
        //       We only support const for non-pointers and the data the pointer is pointing to
        auto return_type_name = get_symbol_name(function_type_symbol);
        auto pointer_type = generate_pointer_type(function_type_symbol);
        auto const_qualifier = generate_const_qualifier(function_type_symbol);

        return std::format(STR("{}{}{}"), const_qualifier.empty() ? STR("") : std::format(STR("{} "), const_qualifier), return_type_name, pointer_type);
    }

    auto static generate_function_params(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        HRESULT hr;

        CComPtr<IDiaSymbol> function_type_symbol;
        if (hr = symbol->get_type(&function_type_symbol); hr != S_OK)
        {
            throw std::runtime_error{std::format("Call to 'get_type(&function_type_symbol)' failed with error: {}", HRESULTToString(hr))};
        }

        File::StringType params{};

        DWORD param_count;
        function_type_symbol->get_count(&param_count);

        // TODO: Check if we have a 'this' param.
        //       Right now we're assuming that there is a 'this' param, and subtracting 1 from the count to account for it.
        --param_count;

        params.append(std::format(STR("param_count: {}, "), param_count));

        if (param_count == 0) { return STR(""); }

        DWORD tag;
        symbol->get_symTag(&tag);

        DWORD current_param{1};
        CComPtr<IDiaEnumSymbols> sub_symbols;
        if (hr = symbol->findChildren(SymTagNull, nullptr, NULL, &sub_symbols); hr == S_OK)
        {
            //params.append(STR("Has children, "));
            CComPtr<IDiaSymbol> sub_symbol;
            ULONG num_symbols_fetched{};
            //hr = sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched);
            LONG count;
            hr = sub_symbols->get_Count(&count);
            while (sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched) == S_OK && num_symbols_fetched == 1)
            {
                DWORD data_kind;
                sub_symbol->get_dataKind(&data_kind);

                if (data_kind != DataKind::DataIsParam)
                {
                    sub_symbol = nullptr;
                    continue;
                }

                params.append(std::format(STR("{} {}{}"), generate_type(sub_symbol), get_symbol_name(sub_symbol), current_param < param_count ? STR(", ") : STR("")));

                ++current_param;
            }
            sub_symbol = nullptr;
        }
        sub_symbols = nullptr;

        return params;
    }

    auto static generate_function_signature(CComPtr<IDiaSymbol>& symbol) -> File::StringType
    {
        DWORD tag;
        symbol->get_symTag(&tag);
        auto params = generate_function_params(symbol);
        auto return_type = generate_type(symbol);
        File::StringType const_qualifier = generate_const_qualifier(symbol);

        //Output::send(STR("{} {}({}){};\n"), return_type, get_symbol_name(symbol), params, const_qualifier.empty() ? STR("") : std::format(STR(" {}"), const_qualifier));

        return std::format(STR("{} {}({}){};"), return_type, get_symbol_name(symbol), params, const_qualifier.empty() ? STR("") : std::format(STR(" {}"), const_qualifier));
    }

    auto& get_existing_or_create_new_enum_entry(const File::StringType& symbol_name, const File::StringType& symbol_name_clean)
    {
        if (auto it = g_enum_entries.find(symbol_name); it != g_enum_entries.end())
        {
            return it->second;
        }
        else
        {
            return g_enum_entries.emplace(symbol_name, EnumEntry{
                    .name = symbol_name,
                    .name_clean = symbol_name_clean,
                    .variables = {}
            }).first->second;
        }
    }

    auto& get_existing_or_create_new_class_entry(std::filesystem::path& pdb_file, const File::StringType& symbol_name, const File::StringType& symbol_name_clean)
    {
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
    }

    auto get_type_name(CComPtr<IDiaSymbol>& type) -> File::StringType
    {
        DWORD type_tag;
        type->get_symTag(&type_tag);

        if (type_tag == SymTagEnum ||
            type_tag == SymTagUDT)
        {
            return get_symbol_name(type);
        }
        else if (type_tag == SymTagBaseType)
        {
            return base_type_to_string(type);
        }
        else if (type_tag == SymTagPointerType)
        {
            auto type_name = get_symbol_name(type);
            auto pointer_type = generate_pointer_type(type);
            return std::format(STR("{}{}"), type_name, pointer_type);
        }
        else
        {
            throw std::runtime_error{"Unknown type."};
        }
    }

    static std::unordered_set<File::StringType> valid_udt_names{
            STR("UScriptStruct::ICppStructOps"),
            STR("UObjectBase"),
            STR("UObjectBaseUtility"),
            STR("UObject"),
            STR("UStruct"),
            STR("FOutputDevice"),
            STR("FMalloc"),
            STR("UField"),
            STR("FField"),
            STR("FProperty"),
            STR("UProperty"),
            STR("FNumericProperty"),
            STR("UNumericProperty"),
            STR("FMulticastDelegateProperty"),
            STR("UMulticastDelegateProperty"),
            STR("FObjectPropertyBase"),
            STR("UObjectPropertyBase"),
            STR("UWorld"),
    };

    static std::vector<File::StringType> UPrefixToFPrefix{
            STR("UProperty"),
            STR("UMulticastDelegateProperty"),
            STR("UObjectPropertyBase"),
    };

    auto VTableDumper::dump_member_variable_layouts(CComPtr<IDiaSymbol>& symbol, ReplaceUPrefixWithFPrefix replace_u_prefix_with_f_prefix, EnumEntriesTypeAlias enum_entry, Class* class_entry) -> void
    {
        auto symbol_name = get_symbol_name(symbol);

        // Replace 'U' with 'F' for everything except UField & FField
        if (replace_u_prefix_with_f_prefix == ReplaceUPrefixWithFPrefix::Yes)
        {
            symbol_name.replace(0, 1, STR("F"));
        }

        File::StringType symbol_name_clean{symbol_name};
        std::replace(symbol_name_clean.begin(), symbol_name_clean.end(), ':', '_');
        std::replace(symbol_name_clean.begin(), symbol_name_clean.end(), '~', '$');

        DWORD sym_tag;
        symbol->get_symTag(&sym_tag);

        HRESULT hr;
        if (sym_tag == SymTagUDT)
        {
            if (valid_udt_names.find(symbol_name) == valid_udt_names.end()) { return; }
            auto& local_class_entry = get_existing_or_create_new_class_entry(pdb_file, symbol_name, symbol_name_clean);
            auto& local_enum_entry = get_existing_or_create_new_enum_entry(symbol_name, symbol_name_clean);

            CComPtr<IDiaEnumSymbols> sub_symbols;
            if (hr = symbol->findChildren(SymTagNull, nullptr, NULL, &sub_symbols); hr == S_OK)
            {
                CComPtr<IDiaSymbol> sub_symbol;
                ULONG num_symbols_fetched{};
                while (sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched) == S_OK && num_symbols_fetched == 1)
                {
                    dump_member_variable_layouts(sub_symbol, ReplaceUPrefixWithFPrefix::No, &local_enum_entry, &local_class_entry);
                }
                sub_symbol = nullptr;
            }
            sub_symbols = nullptr;
        }
        else if (sym_tag == SymTagData)
        {
            if (!enum_entry) { throw std::runtime_error{"enum_entries is nullptr"}; }
            if (!class_entry) { throw std::runtime_error{"class_entry is nullptr"}; }

            DWORD kind;
            symbol->get_dataKind(&kind);

            if (kind != DataKind::DataIsMember)
            {
                return;
            }

            CComPtr<IDiaSymbol> type;
            if (hr = symbol->get_type(&type); hr != S_OK)
            {
                throw std::runtime_error{"Could not get type\n"};
            }

            DWORD type_tag;
            type->get_symTag(&type_tag);

            auto type_name = get_type_name(type);

            if (type_name.find(STR("TTuple")) != type_name.npos ||
                type_name.find(STR("FUnversionedStructSchema")) != type_name.npos ||
                type_name.find(STR("ELifetimeCondition")) != type_name.npos ||
                type_name.find(STR("UAISystemBase")) != type_name.npos ||
                type_name.find(STR("FLevelCollection")) != type_name.npos ||
                type_name.find(STR("FThreadSafeCounter")) != type_name.npos ||
                type_name.find(STR("FWorldAsyncTraceState")) != type_name.npos ||
                type_name.find(STR("FDelegateHandle")) != type_name.npos ||
                type_name.find(STR("AGameMode")) != type_name.npos ||
                type_name.find(STR("UAvoidanceManager")) != type_name.npos ||
                type_name.find(STR("FOnBeginTearingDownEvent")) != type_name.npos ||
                type_name.find(STR("UBlueprint")) != type_name.npos ||
                type_name.find(STR("UCanvas")) != type_name.npos ||
                type_name.find(STR("UActorComponent")) != type_name.npos ||
                type_name.find(STR("AController")) != type_name.npos ||
                type_name.find(STR("ULevel")) != type_name.npos ||
                type_name.find(STR("FPhysScene_Chaos")) != type_name.npos ||
                type_name.find(STR("APhysicsVolume")) != type_name.npos ||
                type_name.find(STR("UDemoNetDriver")) != type_name.npos ||
                type_name.find(STR("FEndPhysicsTickFunction")) != type_name.npos ||
                type_name.find(STR("FFXSystemInterface")) != type_name.npos ||
                type_name.find(STR("ERHIFeatureLevel")) != type_name.npos ||
                type_name.find(STR("EFlushLevelStreamingType")) != type_name.npos ||
                type_name.find(STR("ULineBatchComponent")) != type_name.npos ||
                type_name.find(STR("AGameState")) != type_name.npos ||
                type_name.find(STR("FOnGameStateSetEvent")) != type_name.npos ||
                type_name.find(STR("AAudioVolume")) != type_name.npos ||
                type_name.find(STR("FLatentActionManager")) != type_name.npos ||
                type_name.find(STR("FOnLevelsChangedEvent")) != type_name.npos ||
                type_name.find(STR("AParticleEventManager")) != type_name.npos ||
                type_name.find(STR("UNavigationSystem")) != type_name.npos ||
                type_name.find(STR("UNetDriver")) != type_name.npos ||
                type_name.find(STR("AGameNetworkManager")) != type_name.npos ||
                type_name.find(STR("ETravelType")) != type_name.npos ||
                type_name.find(STR("FDefaultDelegateUserPolicy")) != type_name.npos ||
                type_name.find(STR("TMulticastDelegate")) != type_name.npos ||
                type_name.find(STR("FActorsInitializedParams")) != type_name.npos ||
                type_name.find(STR("FOnBeginPostProcessSettings")) != type_name.npos ||
                type_name.find(STR("FIntVector")) != type_name.npos ||
                type_name.find(STR("UGameInstance")) != type_name.npos ||
                type_name.find(STR("FWorldPSCPool")) != type_name.npos ||
                type_name.find(STR("UMaterialParameterCollectionInstance")) != type_name.npos ||
                type_name.find(STR("FParticlePerfStats")) != type_name.npos ||
                type_name.find(STR("FWorldInGamePerformanceTrackers")) != type_name.npos ||
                type_name.find(STR("UPhysicsCollisionHandler")) != type_name.npos ||
                type_name.find(STR("UPhysicsFieldComponent")) != type_name.npos ||
                type_name.find(STR("FPhysScene")) != type_name.npos ||
                type_name.find(STR("APlayerController")) != type_name.npos ||
                type_name.find(STR("IInterface_PostProcessVolume")) != type_name.npos ||
                type_name.find(STR("FOnTickFlushEvent")) != type_name.npos ||
                type_name.find(STR("FSceneInterface")) != type_name.npos ||
                type_name.find(STR("FStartAsyncSimulationFunction")) != type_name.npos ||
                type_name.find(STR("FStartPhysicsTickFunction")) != type_name.npos ||
                type_name.find(STR("FOnNetTickEvent")) != type_name.npos ||
                type_name.find(STR("ETickingGroup")) != type_name.npos ||
                type_name.find(STR("FTickTaskLevel")) != type_name.npos ||
                type_name.find(STR("FTimerManager")) != type_name.npos ||
                type_name.find(STR("FURL")) != type_name.npos ||
                type_name.find(STR("UWorldComposition")) != type_name.npos ||
                type_name.find(STR("EWorldType")) != type_name.npos ||
                type_name.find(STR("FSubsystemCollection")) != type_name.npos ||
                type_name.find(STR("UWorldSubsystem")) != type_name.npos ||
                type_name.find(STR("FStreamingLevelsToConsider")) != type_name.npos ||
                type_name.find(STR("APawn")) != type_name.npos ||
                type_name.find(STR("ACameraActor")) != type_name.npos)
            {
                // These types are not currently supported in RC::Unreal, so we must prevent code from being generated.
                return;
            }

            for (const auto& UPrefixed : UPrefixToFPrefix)
            {
                for (size_t i = type_name.find(UPrefixed); i != type_name.npos; i = type_name.find(UPrefixed))
                {
                    type_name.replace(i, 1, STR("F"));
                    ++i;
                }
            }

            LONG offset;
            symbol->get_offset(&offset);

            enum_entry->variables.emplace(symbol_name, MemberVariable{
                .type = type_name,
                .name = symbol_name,
                .offset = offset
            });

            Output::send(STR("{} {} ({}, {}); 0x{:X}\n"), type_name, symbol_name, sym_tag_to_string(type_tag), kind_to_string(kind), offset);
            class_entry->variables.emplace(symbol_name, MemberVariable{
                .type = type_name,
                .name = symbol_name,
                .offset = offset
            });
        }
    }

    auto VTableDumper::dump_member_variable_layouts(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void
    {
        Output::send(STR("Dumping {} symbols for {}\n"), names.size(), pdb_file.filename().stem().wstring());

        for (const auto&[name, name_info] : names)
        {
            Output::send(STR("{}...\n"), name);
            HRESULT hr{};
            if (hr = dia_session->findChildren(nullptr, SymTagNull, nullptr, nsNone, &dia_global_symbols_enum); hr != S_OK)
            {
                throw std::runtime_error{std::format("Call to 'findChildren' failed with error '{}'", HRESULTToString(hr))};
            }

            CComPtr<IDiaSymbol> symbol;
            ULONG celt_fetched;
            if (hr = dia_global_symbols_enum->Next(1, &symbol, &celt_fetched); hr != S_OK)
            {
                throw std::runtime_error{std::format("Ran into an error with a symbol while calling 'Next', error: {}\n", HRESULTToString(hr))};
            }

            CComPtr<IDiaEnumSymbols> sub_symbols;
            hr = symbol->findChildren(SymTagNull, name.c_str(), NULL, &sub_symbols);
            if (hr != S_OK)
            {
                throw std::runtime_error{std::format("Ran into a problem while calling 'findChildren', error: {}\n", HRESULTToString(hr))};
            }

            CComPtr<IDiaSymbol> sub_symbol;
            ULONG num_symbols_fetched;

            sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched);
            if (!sub_symbol)
            {
                //Output::send(STR("Missed symbol '{}'\n"), name);
                symbol = nullptr;
                sub_symbols = nullptr;
                dia_global_symbols_enum = nullptr;
                continue;
            }

            dump_member_variable_layouts(sub_symbol, name_info.replace_u_prefix_with_f_prefix);
            Output::send(STR("\n"));
        }
    }

    auto VTableDumper::dump_vtable_for_symbol(CComPtr<IDiaSymbol>& symbol, ReplaceUPrefixWithFPrefix replace_u_prefix_with_f_prefix, EnumEntriesTypeAlias enum_entries, Class* class_entry) -> void
    {
        // Symbol name => Offset in vtable
        static std::unordered_map<File::StringType, uint32_t> functions_already_dumped{};

        auto symbol_name = get_symbol_name(symbol);

        // Replace 'U' with 'F' for everything except UField & FField
        if (replace_u_prefix_with_f_prefix == ReplaceUPrefixWithFPrefix::Yes)
            //if (symbol_name.starts_with('U') && symbol_name != STR("UField") && symbol_name != STR("FField"))
        {
            symbol_name.replace(0, 1, STR("F"));
        }

        bool is_overload{};
        if (auto it = functions_already_dumped.find(symbol_name); it != functions_already_dumped.end())
        {
            symbol_name.append(std::format(STR("_{}"), ++it->second));
            is_overload = true;
        }

        File::StringType symbol_name_clean{symbol_name};
        std::replace(symbol_name_clean.begin(), symbol_name_clean.end(), ':', '_');
        std::replace(symbol_name_clean.begin(), symbol_name_clean.end(), '~', '$');

        DWORD sym_tag;
        symbol->get_symTag(&sym_tag);

        if (sym_tag == SymTagUDT)
        {
            if (valid_udt_names.find(symbol_name) == valid_udt_names.end()) { return; }
            functions_already_dumped.clear();
            //Output::send(STR("Dumping vtable for symbol '{}', tag: '{}'\n"), symbol_name, sym_tag_to_string(sym_tag));

            auto local_enum_entries = &g_enum_entries[symbol_name_clean];

            //auto& local_class_entry = [&]() -> Class& {
            //    auto pdb_file_name = pdb_file.filename().stem();
            //    if (g_class_entries.contains(pdb_file_name))
            //    {
            //        auto& classes = g_class_entries[pdb_file_name];
            //        if (classes.entries.contains(symbol_name_clean))
            //        {
            //            return classes.entries[symbol_name_clean];
            //        }
            //        else
            //        {
            //            return classes.entries.emplace(symbol_name_clean, Class{
            //                    .class_name = File::StringType{symbol_name},
            //                    .class_name_clean = symbol_name_clean
            //            }).first->second;
            //        }
            //    }
            //    else
            //    {
            //        return g_class_entries[pdb_file_name].entries[symbol_name_clean] = Class{
            //                .class_name = File::StringType{symbol_name},
            //                .class_name_clean = symbol_name_clean
            //        };
            //    }
            //}();
            auto& local_class_entry = get_existing_or_create_new_class_entry(pdb_file, symbol_name, symbol_name_clean);

            HRESULT hr;
            CComPtr<IDiaEnumSymbols> sub_symbols;
            if (hr = symbol->findChildren(SymTagNull, nullptr, NULL, &sub_symbols); hr == S_OK)
            {
                CComPtr<IDiaSymbol> sub_symbol;
                ULONG num_symbols_fetched{};
                while (sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched) == S_OK && num_symbols_fetched == 1)
                {
                    // TODO:  The 'ReplaceUPrefixWithFPrefix' thing needs to be removed I think.
                    //        This is because we can't keep track of it with a recursive design.
                    //        We could hard-code the value to the symbol name instead.
                    dump_vtable_for_symbol(sub_symbol, ReplaceUPrefixWithFPrefix::No, local_enum_entries, &local_class_entry);
                }
                sub_symbol = nullptr;
            }
            sub_symbols = nullptr;
        }
        else if (sym_tag == SymTagBaseClass)
        {
            return;
        }
        else if (sym_tag == SymTagFunction)
        {
            if (!enum_entries) { throw std::runtime_error{"enum_entries is nullptr"}; }
            if (!class_entry) { throw std::runtime_error{"class_entry is nullptr"}; }

            BOOL is_virtual = FALSE;
            symbol->get_virtual(&is_virtual);
            if (is_virtual == FALSE) { return; }


            HRESULT hr;
            DWORD offset_in_vtable = 0;
            if (hr = symbol->get_virtualBaseOffset(&offset_in_vtable); hr != S_OK)
            {
                throw std::runtime_error{std::format("Call to 'get_virtualBaseOffset' failed with error '{}'", HRESULTToString(hr))};
            }

            //Output::send(STR("Dumping virtual function for symbol '{}', tag: '{}', offset: '{}'\n"), symbol_name, sym_tag_to_string(sym_tag), offset_in_vtable / 8);

            auto& function = class_entry->functions[offset_in_vtable];
            function.name = symbol_name_clean;
            function.signature = generate_function_signature(symbol);
            function.offset = offset_in_vtable;
            function.is_overload = is_overload;
            functions_already_dumped.emplace(symbol_name, 1);
        }
    }

    auto VTableDumper::dump_vtable_for_symbol(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void
    {
        Output::send(STR("Dumping {} struct symbols for {}\n"), names.size(), pdb_file.filename().stem().wstring());

        for (const auto&[name, name_info] : names)
        {
            HRESULT hr{};
            if (hr = dia_session->findChildren(nullptr, SymTagNull, nullptr, nsNone, &dia_global_symbols_enum); hr != S_OK)
            {
                throw std::runtime_error{std::format("Call to 'findChildren' failed with error '{}'", HRESULTToString(hr))};
            }

            CComPtr<IDiaSymbol> symbol;
            ULONG celt_fetched;
            if (hr = dia_global_symbols_enum->Next(1, &symbol, &celt_fetched); hr != S_OK)
            {
                throw std::runtime_error{std::format("Ran into an error with a symbol while calling 'Next', error: {}\n", HRESULTToString(hr))};
            }

            CComPtr<IDiaEnumSymbols> sub_symbols;
            hr = symbol->findChildren(SymTagNull, name.c_str(), NULL, &sub_symbols);
            if (hr != S_OK)
            {
                throw std::runtime_error{std::format("Ran into a problem while calling 'findChildren', error: {}\n", HRESULTToString(hr))};
            }

            CComPtr<IDiaSymbol> sub_symbol;
            ULONG num_symbols_fetched;

            sub_symbols->Next(1, &sub_symbol, &num_symbols_fetched);
            if (!sub_symbol)
            {
                //Output::send(STR("Missed symbol '{}'\n"), name);
                symbol = nullptr;
                sub_symbols = nullptr;
                dia_global_symbols_enum = nullptr;
                continue;
            }
            dump_vtable_for_symbol(sub_symbol, name_info.replace_u_prefix_with_f_prefix);
        }
    }

    auto static generate_files(const std::filesystem::path& output_dir, VTableOrMemberVars vtable_or_member_vars) -> void
    {
        static std::filesystem::path vtable_gen_output_path = "GeneratedVTables";
        static std::filesystem::path vtable_gen_output_include_path = vtable_gen_output_path / "generated_include";
        static std::filesystem::path vtable_gen_output_src_path = vtable_gen_output_path / "generated_src";
        static std::filesystem::path vtable_gen_output_function_bodies_path = vtable_gen_output_include_path / "FunctionBodies";
        static std::filesystem::path vtable_templates_output_path = "VTableLayoutTemplates";
        static std::filesystem::path member_variable_layouts_gen_output_path = "GeneratedMemberVariableLayouts";
        static std::filesystem::path member_variable_layouts_gen_output_include_path = member_variable_layouts_gen_output_path / "generated_include";
        static std::filesystem::path member_variable_layouts_gen_output_src_path = member_variable_layouts_gen_output_path / "generated_src";
        static std::filesystem::path member_variable_layouts_gen_function_bodies_path = member_variable_layouts_gen_output_include_path / "FunctionBodies";
        static std::filesystem::path member_variable_layouts_templates_output_path = "MemberVarLayoutTemplates";

        if (!std::filesystem::exists(output_dir))
        {
            std::filesystem::create_directory(output_dir);
        }

        if (vtable_or_member_vars == VTableOrMemberVars::VTable)
        {
            if (std::filesystem::exists(vtable_gen_output_include_path))
            {
                for (const auto& item : std::filesystem::directory_iterator(vtable_gen_output_include_path))
                {
                    if (item.is_directory()) { continue; }
                    if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp")) { continue; }

                    File::delete_file(item.path());
                }
            }

            if (std::filesystem::exists(vtable_gen_output_function_bodies_path))
            {
                for (const auto& item : std::filesystem::directory_iterator(vtable_gen_output_function_bodies_path))
                {
                    if (item.is_directory()) { continue; }
                    if (item.path().extension() != STR(".hpp")) { continue; }

                    File::delete_file(item.path());
                }
            }

            if (std::filesystem::exists(vtable_gen_output_src_path))
            {
                for (const auto& item : std::filesystem::directory_iterator(vtable_gen_output_src_path))
                {
                    if (item.is_directory()) { continue; }
                    if (item.path().extension() != STR(".cpp")) { continue; }

                    File::delete_file(item.path());
                }
            }

            for (const auto&[pdb_name, classes] : g_class_entries)
            {
                for (const auto&[class_name, class_entry] : classes.entries)
                {
                    Output::send(STR("Generating file '{}_VTableOffsets_{}_FunctionBody.cpp'\n"), pdb_name, class_entry.class_name_clean);
                    Output::Targets<Output::NewFileDevice> function_body_dumper;
                    auto& function_body_file_device = function_body_dumper.get_device<Output::NewFileDevice>();
                    function_body_file_device.set_file_name_and_path(vtable_gen_output_function_bodies_path / std::format(STR("{}_VTableOffsets_{}_FunctionBody.cpp"), pdb_name, class_name));
                    function_body_file_device.set_formatter([](File::StringViewType string) {
                        return File::StringType{string};
                    });

                    for (const auto&[function_index, function_entry] : class_entry.functions)
                    {
                        auto local_class_name = class_entry.class_name;
                        if (auto pos = local_class_name.find(STR("Property")); pos != local_class_name.npos)
                        {
                            local_class_name.replace(0, 1, STR("F"));
                        }

                        function_body_dumper.send(STR("if (auto it = {}::VTableLayoutMap.find(STR(\"{}\")); it == {}::VTableLayoutMap.end())\n"), local_class_name, function_entry.name, local_class_name);
                        function_body_dumper.send(STR("{\n"));
                        function_body_dumper.send(STR("    {}::VTableLayoutMap.emplace(STR(\"{}\"), 0x{:X});\n"), local_class_name, function_entry.name, function_entry.offset);
                        function_body_dumper.send(STR("}\n\n"));
                    }
                }
            }

            for (const auto&[pdb_name, classes] : g_class_entries)
            {
                auto template_file = std::format(STR("VTableLayout_{}_Template.ini"), pdb_name);
                Output::send(STR("Generating file '{}'\n"), template_file);
                Output::Targets<Output::NewFileDevice> ini_dumper;
                auto& ini_file_device = ini_dumper.get_device<Output::NewFileDevice>();
                ini_file_device.set_file_name_and_path(vtable_templates_output_path / template_file);
                ini_file_device.set_formatter([](File::StringViewType string) {
                    return File::StringType{string};
                });

                for (const auto&[class_name, class_entry] : classes.entries)
                {
                    ini_dumper.send(STR("[{}]\n"), class_entry.class_name);

                    for (const auto&[function_index, function_entry] : class_entry.functions)
                    {
                        if (function_entry.is_overload)
                        {
                            ini_dumper.send(STR("; {}\n"), function_entry.signature);
                        }
                        ini_dumper.send(STR("{}\n"), function_entry.name);
                    }

                    ini_dumper.send(STR("\n"));
                }
            }
        }
        else
        {
            if (std::filesystem::exists(member_variable_layouts_gen_output_include_path))
            {
                for (const auto& item : std::filesystem::directory_iterator(member_variable_layouts_gen_output_include_path))
                {
                    if (item.is_directory()) { continue; }
                    if (item.path().extension() != STR(".hpp")) { continue; }

                    File::delete_file(item.path());
                }
            }

            if (std::filesystem::exists(member_variable_layouts_gen_function_bodies_path))
            {
                for (const auto& item : std::filesystem::directory_iterator(member_variable_layouts_gen_function_bodies_path))
                {
                    if (item.is_directory()) { continue; }
                    if (item.path().extension() != STR(".hpp") && item.path().extension() != STR(".cpp")) { continue; }

                    File::delete_file(item.path());
                }
            }

            if (std::filesystem::exists(member_variable_layouts_gen_output_src_path))
            {
                for (const auto& item : std::filesystem::directory_iterator(member_variable_layouts_gen_output_src_path))
                {
                    if (item.is_directory()) { continue; }
                    if (item.path().extension() != STR(".cpp")) { continue; }

                    File::delete_file(item.path());
                }
            }

            for (const auto&[pdb_name, classes] : g_class_entries)
            {
                auto template_file = std::format(STR("MemberVariableLayout_{}_Template.ini"), pdb_name);
                Output::send(STR("Generating file '{}'\n"), template_file);
                Output::Targets<Output::NewFileDevice> ini_dumper;
                auto& ini_file_device = ini_dumper.get_device<Output::NewFileDevice>();
                ini_file_device.set_file_name_and_path(member_variable_layouts_templates_output_path / template_file);
                ini_file_device.set_formatter([](File::StringViewType string) {
                    return File::StringType{string};
                });

                for (const auto&[class_name, class_entry] : classes.entries)
                {
                    if (class_entry.variables.empty()) { continue; }

                    auto default_setter_src_file = member_variable_layouts_gen_function_bodies_path / std::format(STR("{}_MemberVariableLayout_DefaultSetter_{}.cpp"), pdb_name, class_entry.class_name_clean);
                    Output::send(STR("Generating file '{}'\n"), default_setter_src_file.wstring());
                    Output::Targets<Output::NewFileDevice> default_setter_src_dumper;
                    auto& default_setter_src_file_device = default_setter_src_dumper.get_device<Output::NewFileDevice>();
                    default_setter_src_file_device.set_file_name_and_path(default_setter_src_file);
                    default_setter_src_file_device.set_formatter([](File::StringViewType string) {
                        return File::StringType{string};
                    });

                    ini_dumper.send(STR("[{}]\n"), class_entry.class_name);

                    for (const auto&[variable_name, variable] : class_entry.variables)
                    {
                        ini_dumper.send(STR("{} = 0x{:X}\n"), variable.name, variable.offset);
                        default_setter_src_dumper.send(STR("if (auto it = {}::MemberOffsets.find(STR(\"{}\")); it == {}::MemberOffsets.end())\n"), class_entry.class_name, variable.name, class_entry.class_name);
                        default_setter_src_dumper.send(STR("{\n"));
                        default_setter_src_dumper.send(STR("    {}::MemberOffsets.emplace(STR(\"{}\"), 0x{:X});\n"), class_entry.class_name, variable.name, variable.offset);
                        default_setter_src_dumper.send(STR("}\n\n"));
                    }

                    ini_dumper.send(STR("\n"));
                }
            }

            for (const auto&[class_name, enum_entry] : g_enum_entries)
            {
                if (enum_entry.variables.empty()) { continue; }

                auto wrapper_header_file = member_variable_layouts_gen_output_include_path / std::format(STR("MemberVariableLayout_HeaderWrapper_{}.hpp"), enum_entry.name_clean);
                Output::send(STR("Generating file '{}'\n"), wrapper_header_file.wstring());
                Output::Targets<Output::NewFileDevice> header_wrapper_dumper;
                auto& wrapper_header_file_device = header_wrapper_dumper.get_device<Output::NewFileDevice>();
                wrapper_header_file_device.set_file_name_and_path(wrapper_header_file);
                wrapper_header_file_device.set_formatter([](File::StringViewType string) {
                    return File::StringType{string};
                });

                auto wrapper_src_file = member_variable_layouts_gen_output_include_path / std::format(STR("MemberVariableLayout_SrcWrapper_{}.hpp"), enum_entry.name_clean);
                Output::send(STR("Generating file '{}'\n"), wrapper_src_file.wstring());
                Output::Targets<Output::NewFileDevice> wrapper_src_dumper;
                auto& wrapper_src_file_device = wrapper_src_dumper.get_device<Output::NewFileDevice>();
                wrapper_src_file_device.set_file_name_and_path(wrapper_src_file);
                wrapper_src_file_device.set_formatter([](File::StringViewType string) {
                    return File::StringType{string};
                });

                header_wrapper_dumper.send(STR("static std::unordered_map<File::StringType, int32_t> MemberOffsets;\n"));
                wrapper_src_dumper.send(STR("std::unordered_map<File::StringType, int32_t> {}::MemberOffsets{{}};\n"), enum_entry.name);

                for (const auto&[variable_name, variable] : enum_entry.variables)
                {
                    header_wrapper_dumper.send(STR("{} Get{}();\n"), variable.type, variable.name);
                    header_wrapper_dumper.send(STR("const {} Get{}() const;\n"), variable.type, variable.name);
                    wrapper_src_dumper.send(STR("{} {}::Get{}()\n"), variable.type, enum_entry.name, variable.name);
                    wrapper_src_dumper.send(STR("{\n"));
                    wrapper_src_dumper.send(STR("    static auto offset = MemberOffsets.find(STR(\"{}\"));\n"), variable.name);
                    wrapper_src_dumper.send(STR("    if (offset == MemberOffsets.end()) {{ throw std::runtime_error{{\"Tried getting member variable '{}::{}' that doesn't exist in this engine version.\"}}; }}\n"), enum_entry.name, variable.name);
                    wrapper_src_dumper.send(STR("    return Helper::Casting::ptr_cast_deref<{}>(this, offset->second);\n"), variable.type);
                    wrapper_src_dumper.send(STR("}\n"));
                    wrapper_src_dumper.send(STR("const {} {}::Get{}() const\n"), variable.type, enum_entry.name, variable.name);
                    wrapper_src_dumper.send(STR("{\n"));
                    wrapper_src_dumper.send(STR("    static auto offset = MemberOffsets.find(STR(\"{}\"));\n"), variable.name);
                    wrapper_src_dumper.send(STR("    if (offset == MemberOffsets.end()) {{ throw std::runtime_error{{\"Tried getting member variable '{}::{}' that doesn't exist in this engine version.\"}}; }}\n"), enum_entry.name, variable.name);
                    wrapper_src_dumper.send(STR("    return Helper::Casting::ptr_cast_deref<const {}>(this, offset->second);\n"), variable.type);
                    wrapper_src_dumper.send(STR("}\n\n"));
                }
            }
        }
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

    auto VTableDumper::generate_code(VTableOrMemberVars vtable_or_member_vars) -> void
    {
        setup_symbol_loader();

        // TODO: Figure out what to do if a legacy struct still exists alongside the new struct
        //       An example of this is UField and FField. Both exist in 4.25+
        //       FField & UField are special and will not be combined
        //       All other types, that only differ by the prefix, must have their generated non-function-body files merged into one
        //       This should be done, confirm it before removing this comment

        //std::unordered_set<File::StringType> names;
        std::unordered_map<File::StringType, SymbolNameInfo> names;
        names.emplace(STR("UObjectBase"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UObjectBaseUtility"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UObject"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("UScriptStruct::ICppStructOps"), ReplaceUPrefixWithFPrefix::No);
        names.emplace(STR("FOutputDevice"), ReplaceUPrefixWithFPrefix::No);
        //names.emplace(STR("UObject::Serialize"), ReplaceUPrefixWithFPrefix::No);

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

        if (vtable_or_member_vars == VTableOrMemberVars::VTable)
        {
            dump_vtable_for_symbol(names);
        }
        else
        {
            names.emplace(STR("UWorld"), ReplaceUPrefixWithFPrefix::No);

            //experimental_generate_members();
            dump_member_variable_layouts(names);
        }
    }

    auto main(VTableOrMemberVars vtable_or_member_vars) -> void
    {
        /**/
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_12.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        //Output::send(STR("Done\n"));
        //return;
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_13.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_14.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_15.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_16.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_17.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_18.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_19.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_20.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_21.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_22.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_23.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_24.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_25.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_26.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        //*/
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_27.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        /**/
        TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/5_00.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });
        //*/

        TRY([&] {
            generate_files("GeneratedVTables", vtable_or_member_vars);
            Output::send(STR("Code generated.\n"));
        });
        //});
    }
}
