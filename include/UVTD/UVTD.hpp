#ifndef RC_UVTD_HPP
#define RC_UVTD_HPP

#include <filesystem>
#include <unordered_set>
#include <set>
#include <map>
#include <utility>

#include <File/File.hpp>
#include <Input/Handler.hpp>
#include <Function/Function.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <dia2.h>
#include <atlbase.h>

namespace RC::UVTD
{
    extern bool processing_events;
    extern Input::Handler input_handler;

    enum class VTableOrMemberVars { VTable, MemberVars };
    auto main(VTableOrMemberVars) -> void;

    enum class ValidForVTable { Yes, No };
    enum class ValidForMemberVars { Yes, No };

    enum class ReplaceUPrefixWithFPrefix { Yes, No };
    struct SymbolNameInfo
    {
        ReplaceUPrefixWithFPrefix replace_u_prefix_with_f_prefix{};
        ValidForVTable valid_for_vtable{};
        ValidForMemberVars valid_for_member_vars{};

        explicit SymbolNameInfo(ReplaceUPrefixWithFPrefix replace_u_prefix_with_f_prefix, ValidForVTable valid_for_vtable, ValidForMemberVars valid_for_member_vars) :
                replace_u_prefix_with_f_prefix(replace_u_prefix_with_f_prefix),
                valid_for_vtable(valid_for_vtable),
                valid_for_member_vars(valid_for_member_vars)
        {
        }
    };

    class VTableDumper
    {
    private:
        std::filesystem::path pdb_file;
        CComPtr<IDiaDataSource> dia_source;
        CComPtr<IDiaSession> dia_session;
        CComPtr<IDiaSymbol> dia_global_symbol;
        CComPtr<IDiaEnumSymbols> dia_global_symbols_enum;
        bool is_425_plus;

        bool are_symbols_cached{};

    public:
        VTableDumper() = delete;
        explicit VTableDumper(std::filesystem::path pdb_file) : pdb_file(std::move(pdb_file))
        {
            auto version_string = this->pdb_file.filename().stem().string();
            auto major_version = std::atoi(version_string.substr(0, 1).c_str());
            auto minor_version = std::atoi(version_string.substr(2).c_str());
            is_425_plus = (major_version > 4) || (major_version == 4 && minor_version >= 25);
        }

    public:
        auto setup_symbol_loader() -> void;
        using EnumEntriesTypeAlias = struct EnumEntry*;
        auto dump_vtable_for_symbol(CComPtr<IDiaSymbol>& symbol, const SymbolNameInfo&, EnumEntriesTypeAlias = nullptr, struct Class* class_entry = nullptr) -> void;
        auto dump_vtable_for_symbol(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void;
        auto dump_member_variable_layouts(CComPtr<IDiaSymbol>& symbol, const SymbolNameInfo&, EnumEntriesTypeAlias = nullptr, struct Class* class_entry = nullptr) -> void;
        auto dump_member_variable_layouts(std::unordered_map<File::StringType, SymbolNameInfo>& names) -> void;
        auto generate_code(VTableOrMemberVars) -> void;
        auto experimental_generate_members() -> void;
    };

    struct MemberVariable
    {
        File::StringType type;
        File::StringType name;
        int32_t offset;
    };

    struct EnumEntry
    {
        File::StringType name;
        File::StringType name_clean;
        std::map<File::StringType, MemberVariable> variables;
    };

    struct EnumEntries
    {
        //std::set<File::StringType> entries;
        std::map<File::StringType, EnumEntry> entries;
    };
    // ClassName => FunctionName
    extern std::unordered_map<File::StringType, EnumEntry> g_enum_entries;

    struct FunctionBody
    {
        File::StringType name;
        File::StringType signature;
        uint32_t offset;
        bool is_overload;
    };
    struct Class
    {
        File::StringType class_name;
        File::StringType class_name_clean;
        std::map<uint32_t, FunctionBody> functions;
        // Key: Variable name
        std::map<File::StringType, MemberVariable> variables;
        //std::map<int32_t, MemberVariable> variables;
        uint32_t last_virtual_offset;
        ValidForVTable valid_for_vtable{ValidForVTable::No};
        ValidForMemberVars valid_for_member_vars{ValidForMemberVars::No};
    };
    struct Classes
    {
        // Key: Class name clean
        std::unordered_map<File::StringType, Class> entries;
    };
    // Key: PDB name (e.g. 4_22)
    extern std::unordered_map<File::StringType, Classes> g_class_entries;

    /*
    class FStructuredArchiveSlot {};
    class FStructuredArchive
    {
    public:
        using FSlot = FStructuredArchiveSlot;
    };

    class UScriptStruct
    {
    public:
        struct ICppStructOps
        {
            // Code generated automatically -> START
            struct VTableOffsets
            {
                static uint32_t ICppStructOps_Destructor;
                static inline uint32_t HasNoopConstructor{0x8};
                static inline uint32_t HasZeroConstructor{0x10};
                static inline uint32_t Construct{0x18};
            };
            // Code generated automatically -> END

            // Code manually created -> START
            // Wrappers should be kept up-to-date with the latest UE version
            // Functions that don't exist in a particular engine version must throw in the wrapper

            // Wrappers
            // Destructor is a special case because we can't use the real name because that makes it an actual destructor
            // One option is to skip the destructor
            auto ICppStructOps_Destructor() -> void;
            auto HasNoopConstructor() -> bool;
            auto HasZeroConstructor() -> bool;
            auto Construct(void *Dest) -> void;
            auto HasDestructor() -> bool;
            auto Destruct(void *Dest) -> void;
            auto HasSerializer() -> bool;
            auto HasStructuredSerializer() -> bool;
            auto Serialize(struct FArchive& Ar, void *Data) -> bool;
            auto Serialize(FStructuredArchive::FSlot Slot, void *Data) -> bool;
            auto HasPostSerialize() -> bool;
            auto PostSerialize(const struct FArchive& Ar, void *Data) -> void;
            auto HasNetSerializer() -> bool;
            auto HasNetSharedSerialization() -> bool;
            auto NetSerialize(struct FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess, void *Data) -> bool;
            auto HasNetDeltaSerializer() -> bool;
            auto NetDeltaSerialize(struct FNetDeltaSerializeInfo& DeltaParms, void* Data) -> bool;
            auto HasPostScriptConstruct() -> bool;
            auto PostScriptConstruct(void *Data) -> void;
            auto IsPlainOldData() -> bool;
            auto HasCopy() -> bool;
            auto Copy(void* Dest, void const* Src, int32_t ArrayDim) -> bool;
            auto HasIdentical() -> bool;
            auto Identical(const void* A, const void* B, uint32_t PortFlags, bool& bOutResult) -> bool;
            auto HasExportTextItem() -> bool;
            auto ExportTextItem(struct FString& ValueStr, const void* PropertyValue, const void* DefaultValue, class UObject* Parent, int32_t PortFlags, class UObject* ExportRootScope) -> bool;
            auto HasImportTextItem() -> bool;
            auto ImportTextItem(const TCHAR*& Buffer, void* Data, int32_t PortFlags, class UObject* OwnerObject, class FOutputDevice* ErrorText) -> bool;
            auto HasAddStructReferencedObjects() -> bool;
            typedef void (*TPointerToAddStructReferencedObjects)(void* A, class FReferenceCollector& Collector);
            auto AddStructReferencedObjects() -> TPointerToAddStructReferencedObjects;
            auto HasSerializeFromMismatchedTag() -> bool;
            auto HasStructuredSerializeFromMismatchedTag() -> bool;
            auto SerializeFromMismatchedTag(struct FPropertyTag const& Tag, FArchive& Ar, void *Data) -> bool;
            auto StructuredSerializeFromMismatchedTag(struct FPropertyTag const& Tag, FStructuredArchive::FSlot Slot, void *Data) -> bool;
            auto HasGetTypeHash() -> bool;
            auto GetStructTypeHash(const void* Src) -> uint32_t;
            auto GetComputedPropertyFlags() const -> enum class EPropertyFlags;
            auto IsAbstract() const -> bool;
            // Code manually created -> END


        };
    };

    class UObject
    {
        // Replace spaces and :: with underscores
        // UObjectBase::`scalar deleting destructor'
        // becomes
        // UObjectBase_`scalar_deleting_destructor'
        //
        // Replace ' and ` with nothing
        // UObjectBase_`scalar_deleting_destructor'
        // becomes
        // UObjectBase_scalar_deleting_destructor

        // Problems:
        // Need to figure out how to generate param names
        // Might need to do incremental names

        //bool CallRemoteFunction(class UFunction*, void*, struct FOutParmRec*, struct FFrame*);

        // UE4 Source:
        // virtual bool CallRemoteFunction( UFunction* Function, void* Parms, struct FOutParmRec* OutParms, FFrame* Stack )
        // From PDB:
        // virtual bool UObject::CallRemoteFunction(class UFunction *,void *,struct FOutParmRec *,struct FFrame *)
        // Need for RC::Function:
        // bool(class UFunction*, void*, struct FOutParmRec*, struct FFrame*)
        // Can we use operator() from RC::Function directly ? I don't think so because we'd have to manually pass 'this'
        // So we need a wrapper function, for which we need:
        // bool CallRemoteFunction(class UFunction*, void*, struct FOutParmRec*, struct FFrame*);
        // and
        // bool UObject::CallRemoteFunction(class UFunction*, void*, struct FOutParmRec*, struct FFrame*);

        // Generator without 'virtual', now:
        // bool UObject::CallRemoteFunction(class UFunction *,void *,struct FOutParmRec *,struct FFrame *)
        // The above looks like it's good enough for the wrapper definition
        // Do some string manipulations to get rid of the 'UObject::' and then it should be good enough for the wrapper declaration
    };

    //bool UObject::CallRemoteFunction(class UFunction*, void*, struct FOutParmRec*, struct FFrame*)
    //{
    //    void** vtable = std::bit_cast<void**>(this);
    //    Function<bool(class UFunction*, void*, struct FOutParmRec*, struct FFrame*)> func = vtable[0x0]; // Automatically generated index
    //    //func();
    //}
    //*/
}

#endif //RC_UVTD_HPP
