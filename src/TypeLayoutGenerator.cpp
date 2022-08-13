#include <string>
#include <fstream>
#include <Windows.h>
#include <Psapi.h>
#include <atlbase.h>
#include <dia2.h>
#include <vector>
#include <ImageHlp.h>
#include <sstream>
#include <iostream>
#include <assert.h>

std::wstring PrintfVarargs(const wchar_t* Fmt, va_list varargs) {
    size_t CurrentBufferSize = 1024;
    auto* CharacterBuffer = (wchar_t*) malloc(sizeof(wchar_t) * CurrentBufferSize);

    ///Resize the buffer and try again as long as the function says we don't have enough space
    while (_vsnwprintf_s(CharacterBuffer, CurrentBufferSize, CurrentBufferSize, Fmt, varargs) == -1) {
        CurrentBufferSize = CurrentBufferSize * 2;
        CharacterBuffer = (wchar_t*) realloc(CharacterBuffer, sizeof(wchar_t) * CurrentBufferSize);
    }

    std::wstring ResultString = CharacterBuffer;
    free(CharacterBuffer);

    return ResultString;
}

std::wstring Printf(const wchar_t* Fmt, ...) {
    va_list varargs;
    va_start(varargs, Fmt);
    std::wstring Result = PrintfVarargs(Fmt, varargs);
    va_end(varargs);
    return Result;
}

class FGeneratedFile {
private:
    std::wstring FileName;
    std::wstring OutputFilePath;
    std::wstring FileOutputBuffer;
    bool bAutoEmitNewline;
    int IndentationLevel;
public:
    explicit FGeneratedFile(const std::wstring& OutputDirectory, const std::wstring& InFileName) {
        this->FileName = InFileName;
        this->OutputFilePath = Printf(TEXT("%s/%s.h"), OutputDirectory.c_str(), InFileName.c_str());
        this->bAutoEmitNewline = true;
        this->IndentationLevel = 0;
    }

    FORCEINLINE const std::wstring& GetFileName() const {
        return FileName;
    }

    FORCEINLINE void BeginIndentLevel() {
        this->IndentationLevel++;
    }

    FORCEINLINE void EndIndentLevel() {
        this->IndentationLevel--;
    }

    FORCEINLINE void SetAutoEmitNewline(bool bNewAutoEmitNewline) {
        this->bAutoEmitNewline = bNewAutoEmitNewline;
    }

    FORCEINLINE void Logf(const wchar_t* Fmt, ...) {
        va_list varargs;
        va_start(varargs, Fmt);

        if (IndentationLevel != 0) {
            FileOutputBuffer.append(IndentationLevel * 4, ' ');
        }
        FileOutputBuffer.append(PrintfVarargs(Fmt, varargs));
        if (bAutoEmitNewline) {
            FileOutputBuffer.push_back('\n');
        }

        va_end(varargs);
    }

    FORCEINLINE void WriteFile() {
        std::wofstream OutputStream{OutputFilePath, std::ios_base::out};
        if (!OutputStream.good()) {
            throw std::exception{"Cannot open file for writing"};
        }
        OutputStream << FileOutputBuffer;
        OutputStream.close();
    }
};

enum class EMemberAccess {
    Unspecified = 0,
    Private = 1,
    Protected = 2,
    Public = 3
};

struct FMemberVariable {
    std::wstring VariableName{};
    std::wstring VariableType{};
    int32_t VariableOffset{0};
    int32_t VariableSize{0};
    EMemberAccess VariableAccess{EMemberAccess::Public};
    bool bIsBitfield{false};
    bool bIsArray{false};
    bool bIsUDT{false};
    int32_t BitfieldBitPosition{0};
    int32_t BitfieldBitSize{0};
    int32_t ArraySize{0};
    /**
     * True if the property needs value initialization.
     * Value initialization is generally needed for all integral types and pointer types,
     * and also for UDTs with no default constructor
     * If you do not initialize these types explicitly, they will have garbage value
     */
    bool bNeedsValueInit{false};

    /** Value to populate the variable with for default value init */
    std::wstring ValueInitDefaultValue{};

    /**
     * True if the property needs the NoInit constructor call
     * This is used to prevent the default initialization in places where it shouldn't happen
     * and generally speaking constructor should do nothing
     */
    bool bNeedsNoInitConstructorCall{false};
};

struct FVirtualFunctionDeclaration {
    std::wstring FunctionName{};
    std::wstring FunctionDeclaration{};
    int32_t VirtualTableOffset{0};
    EMemberAccess FunctionAccess{EMemberAccess::Public};
};

struct FParentClassInfo {
    std::wstring ClassName;
    EMemberAccess ClassAccess{EMemberAccess::Unspecified};
    int32_t ClassDataOffset{0};
    int32_t ClassSize{0};
    bool bHasConstructor{false};
};

struct FUserDefinedTypeLayout {
    std::wstring ClassName{};
    std::vector<FParentClassInfo> ParentClasses{};
    std::vector<FMemberVariable> MemberVariables{};
    std::vector<FVirtualFunctionDeclaration> VirtualFunctions{};
    int32_t VirtualTableEntriesCount{0};
    int32_t TotalTypeSize{0};
};

std::wstring CreateBasicTypeName(DWORD BasicType, ULONGLONG TypeSize) {
    switch (BasicType) {
        case btVoid:
            assert(TypeSize == 0);
            return TEXT("void");
        case btChar:
            assert(TypeSize == 1);
            return TEXT("ANSICHAR");
        case btWChar:
            assert(TypeSize == 2);
            return TEXT("TCHAR");
        case btBool:
            assert(TypeSize == 1);
            return TEXT("bool");
            ///MSVC does not differentiate between long and int,
            ///And neither does UE in fact, because some of the types below are defined
            ///as int and some of them are defined as long (namely uint64)
        case btInt:
        case btLong:
            switch (TypeSize) {
                case 1: return TEXT("int8");
                case 2: return TEXT("int16");
                case 4: return TEXT("int32");
                case 8: return TEXT("int64");
                default: assert(0);
            }
            ///MSVC does not differentiate between long and int,
            ///And neither does UE in fact, because some of the types below are defined
            ///as int and some of them are defined as long (namely uint64)
        case btUInt:
        case btULong:
            switch (TypeSize) {
                case 1: return TEXT("uint8");
                case 2: return TEXT("uint16");
                case 4: return TEXT("uint32");
                case 8: return TEXT("uint64");
                default: assert(0);
            }
        case btFloat:
            switch (TypeSize) {
                case 4: return TEXT("float");
                case 8: return TEXT("double");
                default: assert(0);
            }
        case btChar8:
            assert(TypeSize == 1);
            return TEXT("CHAR8");
        case btChar16:
            assert(TypeSize == 2);
            return TEXT("CHAR16");
        case btChar32:
            assert(TypeSize == 4);
            return TEXT("CHAR32");
        default: assert(0);
    }
    assert(0);
    return Printf(TEXT("<unknown basic type %d: %d bytes>"), BasicType, TypeSize);
}

void AppendConstVolatileModifiers(const CComPtr<IDiaSymbol>& TypeSymbol, std::wstring& OutputString, bool bPushSpaceBefore, bool bPushSpaceAfter) {
    BOOL bIsConstantType = FALSE;
    if (SUCCEEDED(TypeSymbol->get_constType(&bIsConstantType)) && bIsConstantType) {
        if (bPushSpaceBefore) {
            OutputString.push_back(TEXT(' '));
        }
        OutputString.append(TEXT("const"));
        if (bPushSpaceAfter) {
            OutputString.push_back(TEXT(' '));
        }
    }
    BOOL bIsVolatileType = FALSE;
    if (SUCCEEDED(TypeSymbol->get_volatileType(&bIsVolatileType)) && bIsVolatileType) {
        ///No need to push the second space if we have const and it has pushed the after space already
        ///On the other hand, if we have not asked any spaces and we have const, we need to push one regardless
        if ((bPushSpaceBefore && (!bPushSpaceAfter || !bIsConstantType)) || (bIsConstantType && !bPushSpaceAfter && !bPushSpaceBefore)) {
            OutputString.push_back(TEXT(' '));
        }
        OutputString.append(TEXT("volatile"));
        if (bPushSpaceAfter) {
            OutputString.push_back(TEXT(' '));
        }
    }
}

std::wstring GenerateTypeDeclarationForSymbol(const CComPtr<IDiaSymbol>& TypeSymbol);

std::wstring GenerateFunctionArgumentList(const CComPtr<IDiaSymbol>& FunctionTypeSymbol) {
    std::wstring ResultArgumentList;

    CComPtr<IDiaEnumSymbols> FunctionArgEnumerator{};
    if (SUCCEEDED(FunctionTypeSymbol->findChildrenEx(SymTagFunctionArgType, nullptr, nsNone, &FunctionArgEnumerator))) {
        LONG ArgumentCount = 0L;
        FunctionArgEnumerator->get_Count(&ArgumentCount);

        for (LONG i = 0; i < ArgumentCount; i++) {
            CComPtr<IDiaSymbol> FunctionArgument{};
            FunctionArgEnumerator->Item(i, &FunctionArgument);

            CComPtr<IDiaSymbol> ArgumentTypeSymbol{};
            if (SUCCEEDED(FunctionArgument->get_type(&ArgumentTypeSymbol)) && ArgumentTypeSymbol) {
                ResultArgumentList.append(GenerateTypeDeclarationForSymbol(ArgumentTypeSymbol));

                if ((i + 1) != ArgumentCount) {
                    ResultArgumentList.append(TEXT(", "));
                }
            }
        }
    }
    return ResultArgumentList;
}

std::wstring GenerateFunctionTypeDeclarationForSymbol(const CComPtr<IDiaSymbol>& TypeSymbol, bool bGenerateFunctionPointerType) {
    CComPtr<IDiaSymbol> FunctionReturnType{};
    if (FAILED(TypeSymbol->get_type(&FunctionReturnType))) {
        return TEXT("<unknown function type>");
    }
    std::wstring ReturnTypeName = TEXT("void");
    if (FunctionReturnType) {
        ReturnTypeName = GenerateTypeDeclarationForSymbol(FunctionReturnType);
    }
    std::wstring ResultFunctionName = ReturnTypeName;

    if (bGenerateFunctionPointerType) {
        ResultFunctionName.push_back(TEXT('('));
    }

    CComPtr<IDiaSymbol> FunctionClassParent{};
    BOOL bIsFunctionConst = FALSE;

    if (SUCCEEDED(TypeSymbol->get_classParent(&FunctionClassParent)) && FunctionClassParent) {
        if (bGenerateFunctionPointerType) {
            BSTR ClassParentName{};
            if (SUCCEEDED(FunctionClassParent->get_name(&ClassParentName)) && ClassParentName) {
                ResultFunctionName.append(ClassParentName);
                ResultFunctionName.append(TEXT("::"));

                SysFreeString(ClassParentName);
            }
        }
        //TODO: It might be wrong and probably is wrong, I think const-ness of the object pointer should be checked instead
        //TODO: Need more samples though, and function types are really not that important, let's be real
        FunctionClassParent->get_constType(&bIsFunctionConst);
    }

    if (bGenerateFunctionPointerType) {
        ResultFunctionName.push_back(TEXT('*'));
        AppendConstVolatileModifiers(TypeSymbol, ResultFunctionName, false, false);

        ResultFunctionName.push_back(TEXT(')'));
    }

    ResultFunctionName.push_back(TEXT('('));
    ResultFunctionName.append(GenerateFunctionArgumentList(TypeSymbol));
    ResultFunctionName.push_back(TEXT(')'));

    if (bIsFunctionConst) {
        ResultFunctionName.append(TEXT(" const"));
    }
    return ResultFunctionName;
}

std::wstring GenerateUDTTypeDeclarationForSymbol(const CComPtr<IDiaSymbol>& TypeSymbol, bool bGenerateCSU) {
    std::wstring TypeName;
    AppendConstVolatileModifiers(TypeSymbol, TypeName, false, true);

    if (bGenerateCSU) {
        DWORD TypeKind = UdtClass;
        if (SUCCEEDED(TypeSymbol->get_udtKind(&TypeKind))) {
            if (TypeKind == UdtClass) {
                TypeName.append(TEXT("class "));
            } else if (TypeKind == UdtStruct) {
                TypeName.append(TEXT("struct "));
            } else if (TypeKind == UdtUnion) {
                TypeName.append(TEXT("union "));
            }
        }
    }

    BSTR TypeNameString{};
    if (SUCCEEDED(TypeSymbol->get_name(&TypeNameString)) && TypeNameString) {
        TypeName.append(TypeNameString);
        SysFreeString(TypeNameString);
    }
    return TypeName;
}

std::wstring GenerateTypeDeclarationForSymbol(const CComPtr<IDiaSymbol>& TypeSymbol) {
    DWORD SymbolTag = SymTagNull;
    if (!SUCCEEDED(TypeSymbol->get_symTag(&SymbolTag))) {
        return TEXT("<unknown symbol type>");
    }

    ///Base types, like ints, longs, characters and so on
    if (SymbolTag == SymTagBaseType) {
        DWORD BaseType = btNoType;
        ULONGLONG TypeSize = 0L;
        if (FAILED(TypeSymbol->get_baseType(&BaseType)) || FAILED(TypeSymbol->get_length(&TypeSize))) {
            return TEXT("<unknown base type symbol>");
        }
        std::wstring BasicTypeName;
        AppendConstVolatileModifiers(TypeSymbol, BasicTypeName, false, true);
        BasicTypeName.append(CreateBasicTypeName(BaseType, TypeSize));

        return BasicTypeName;
    }

    ///Pointer types, and also the reference types
    if (SymbolTag == SymTagPointerType) {
        CComPtr<IDiaSymbol> PointedType{};
        if (FAILED(TypeSymbol->get_type(&PointedType))) {
            return TEXT("<unknown pointer type>");
        }

        DWORD PointedTypeSymTag = SymTagNull;
        PointedType->get_symTag(&PointedTypeSymTag);

        ///Special case: If we are pointing to the function type, generate the function pointer type
        if (PointedTypeSymTag == SymTagFunctionType) {
            return GenerateFunctionTypeDeclarationForSymbol(PointedType, true);
        }

        std::wstring ResultPointerName;
        ///Special case: If we are pointing to the UDT, append the CSU prefix so we do not have to make any pre-declarations
        if (PointedTypeSymTag == SymTagUDT) {
            ResultPointerName = GenerateUDTTypeDeclarationForSymbol(PointedType, true);
        } else {
            ResultPointerName = GenerateTypeDeclarationForSymbol(PointedType);
        }

        BOOL bIsReferenceType = FALSE;
        TypeSymbol->get_reference(&bIsReferenceType);

        if (bIsReferenceType) {
            ResultPointerName.append(TEXT("&"));
        } else {
            ResultPointerName.append(TEXT("*"));
        }
        AppendConstVolatileModifiers(TypeSymbol, ResultPointerName, false, false);
        return ResultPointerName;
    }

    ///C-style statically sized arrays
    if (SymbolTag == SymTagArrayType) {
        CComPtr<IDiaSymbol> ElementType{};
        if (FAILED(TypeSymbol->get_type(&ElementType))) {
            return TEXT("<unknown array type>");
        }

        std::wstring ResultArrayName = GenerateTypeDeclarationForSymbol(ElementType);
        ResultArrayName.push_back(TEXT('['));

        DWORD ArrayElementCount = 0;
        //TODO: How non-sized arrays are represented (e.g. char[])
        if (SUCCEEDED(TypeSymbol->get_count(&ArrayElementCount))) {
            ResultArrayName.append(std::to_wstring(ArrayElementCount));
        }
        ResultArrayName.push_back(TEXT(']'));
        AppendConstVolatileModifiers(TypeSymbol, ResultArrayName, true, false);

        ///We need to wrap the type into the identity because otherwise the array syntax is not valid
        return Printf(TEXT("TIdentity<%s>::Type"), ResultArrayName.c_str());
    }

    ///Typedefs. For them we just use the name of the typedef and assume it is defined and valid
    if (SymbolTag == SymTagTypedef) {
        std::wstring TypedefNameString;
        AppendConstVolatileModifiers(TypeSymbol, TypedefNameString, false, true);

        BSTR TypedefName{};
        if (FAILED(TypeSymbol->get_name(&TypedefName)) || !TypedefName) {
           return TEXT("<unknown typedef>");
        }
        TypedefNameString.append(TypedefName);
        SysFreeString(TypedefName);
        return TypedefNameString;
    }

    ///Enumerators. We use their types, but realistically speaking, we could use the underlying types too
    ///But since IDA doesn't seem to know which types are declared using enum and which are enum classes,
    ///we're gonna always go with an enum type
    if (SymbolTag == SymTagEnum) {
        std::wstring EnumNameString;
        AppendConstVolatileModifiers(TypeSymbol, EnumNameString, false, true);

        BSTR EnumName{};
        if (FAILED(TypeSymbol->get_name(&EnumName)) || !EnumName) {
            return TEXT("<unknown enum type>");
        }
        EnumNameString.append(EnumName);
        SysFreeString(EnumName);

        return EnumNameString;
    }

    ///User defined types. We reference them by names
    if (SymbolTag == SymTagUDT) {
        return GenerateUDTTypeDeclarationForSymbol(TypeSymbol, false);
    }

    ///Function signature types
    if (SymbolTag == SymTagFunctionType) {
        return GenerateFunctionTypeDeclarationForSymbol(TypeSymbol, false);
    }

    ///Virtual table. We just emit the placeholder that will cause a compilation error
    ///Realistically we should never generate variables of these types
    if (SymbolTag == SymTagVTable) {
        CComPtr<IDiaSymbol> ClassParentSymbol{};
        if (FAILED(TypeSymbol->get_classParent(&ClassParentSymbol))) {
            return TEXT("<unknown vtable type>");
        }

        std::wstring ConstVolatilePrefix;
        AppendConstVolatileModifiers(TypeSymbol, ConstVolatilePrefix, false, true);

        std::wstring ClassName = TEXT("<Unknown Class>");
        BSTR ClassNameString{};
        if (SUCCEEDED(ClassParentSymbol->get_name(&ClassNameString)) && ClassNameString) {
            ClassName = ClassNameString;
            SysFreeString(ClassNameString);
        }

        return Printf(TEXT("<%sVTable of Class %s>"), ConstVolatilePrefix.c_str(), ClassName.c_str());
    }

    ///Some unhandled symbol type. We assert, and try to print the placeholder otherwise
    assert(0);
    return Printf(TEXT("<unhandled symbol type with tag %lu>"), SymbolTag);
}

std::wstring GenerateDefaultValueForType(const CComPtr<IDiaSymbol>& TypeSymbol) {
    DWORD SymbolTag = SymTagNull;
    if (!SUCCEEDED(TypeSymbol->get_symTag(&SymbolTag))) {
        return TEXT("<unknown symbol type>");
    }

    ///All basic types are convertible to numbers and back so you can use zero as an universal return value
    if (SymbolTag == SymTagBaseType) {
        return TEXT("0");
    }
    ///For pointer types nullptr is probably the most universal value
    if (SymbolTag == SymTagPointerType) {
        return TEXT("nullptr");
    }

    ///For arrays it depends on whenever it's the trailing array or fixed style array
    ///Since I absolutely have no clue how trailing arrays are represented, I assume fixed-size ones
    ///And fixed size arrays can be initialized using the bracket initializer
    ///To be completely fair, C-style arrays cannot even be returned by functions, so it's not like it matters
    //TODO: How non-sized arrays are represented (e.g. char[])
    if (SymbolTag == SymTagArrayType) {
        return TEXT("{}");
    }

    ///For typedefs we need to look up the underlying type default value
    if (SymbolTag == SymTagTypedef) {
        CComPtr<IDiaSymbol> UnderlyingTypeSymbol{};
        if (FAILED(TypeSymbol->get_type(&UnderlyingTypeSymbol))) {
            return TEXT("<unknown typedef value>");
        }
        return GenerateDefaultValueForType(UnderlyingTypeSymbol);
    }

    ///For enumerators it really depends on whenever they're scoped or not
    ///We could probably return 0 and then cast it to enum type, but I think a cleaner solution
    ///Would be to return the first entry in the enumerator
    //TODO: Nah, we just return 0 casted to the enumeration type, because DIA SDK
    //TODO: Tells you that enum values are of type SymTagConstant, BUT THAT TYPE DOES NOT EVEN EXIST LMAO
    if (SymbolTag == SymTagEnum) {
        BSTR EnumName{};
        if (FAILED(TypeSymbol->get_name(&EnumName)) || !EnumName) {
            return TEXT("<unknown enum type default value>");
        }
        std::wstring EnumerationName = EnumName;
        SysFreeString(EnumName);
        return Printf(TEXT("(%s) 0"), EnumerationName.c_str());
    }

    ///User defined types are actually pretty tricky. We can dereference nullptr or try to default construct them
    ///We're making a best effort and just trying to default construct the value
    if (SymbolTag == SymTagUDT) {
        std::wstring TypeName;

        BSTR TypeNameString{};
        if (SUCCEEDED(TypeSymbol->get_name(&TypeNameString)) && TypeNameString) {
            TypeName.append(TypeNameString);
            SysFreeString(TypeNameString);
        }
        return Printf(TEXT("%s{}"), TypeName.c_str());
    }

    ///Just use nullptr for function signatures
    if (SymbolTag == SymTagFunctionType) {
        return TEXT("nullptr");
    }

    ///Some unhandled symbol type. We assert, and try to print the placeholder otherwise
    assert(0);
    return Printf(TEXT("<unhandled symbol type with tag %lu>"), SymbolTag);
}

bool DoesTypeNeedValueInitialization(const CComPtr<IDiaSymbol>& TypeSymbol, std::wstring& OutValueInitDefaultValue) {
    DWORD SymbolTag = SymTagNull;
    if (!SUCCEEDED(TypeSymbol->get_symTag(&SymbolTag))) {
        return false;
    }

    ///All basic types need value initialization, or they will have trash as value
    if (SymbolTag == SymTagBaseType) {
        OutValueInitDefaultValue = TEXT("0");
        return true;
    }
    ///Same applies to pointers, they need to be default initialized to nullptr
    if (SymbolTag == SymTagPointerType) {
        OutValueInitDefaultValue = TEXT("nullptr");
        return true;
    }
    ///Whenever arrays need to be default initialized or not depends on the underlying element type
    if (SymbolTag == SymTagArrayType) {
        CComPtr<IDiaSymbol> ElementType{};
        if (FAILED(TypeSymbol->get_type(&ElementType))) {
            return false;
        }
        return DoesTypeNeedValueInitialization(ElementType, OutValueInitDefaultValue);
    }
    ///For typedefs we need to look up the underlying type to check if they need anything
    if (SymbolTag == SymTagTypedef) {
        CComPtr<IDiaSymbol> UnderlyingTypeSymbol{};
        if (FAILED(TypeSymbol->get_type(&UnderlyingTypeSymbol))) {
            return false;
        }
        return DoesTypeNeedValueInitialization(UnderlyingTypeSymbol, OutValueInitDefaultValue);
    }
    ///Enumerations need value instantiation, which will give them 0 value of underlying type
    if (SymbolTag == SymTagEnum) {
        std::wstring EnumTypeName = GenerateUDTTypeDeclarationForSymbol(TypeSymbol, false);
        OutValueInitDefaultValue = Printf(TEXT("(%s) 0"), EnumTypeName.c_str());
        return true;
    }
    ///User defined types need value initialization if they do not have a default constructor
    ///TODO: There are also special cases for classes that lack default constructor that properly initializes them
    if (SymbolTag == SymTagUDT) {
        OutValueInitDefaultValue = TEXT("");
        BOOL bTypeHasConstructor = FALSE;
        TypeSymbol->get_constructor(&bTypeHasConstructor);
        return !bTypeHasConstructor;
    }
    ///Everything else totally does not default initialization
    return false;
}

bool DoesTypeNeedNoInitConstruction(const CComPtr<IDiaSymbol>& TypeSymbol) {
    DWORD SymbolTag = SymTagNull;
    if (!SUCCEEDED(TypeSymbol->get_symTag(&SymbolTag))) {
        return false;
    }

    ///Whenever arrays need their elements to be NoInit initialized or not depends on the underlying element type
    if (SymbolTag == SymTagArrayType) {
        CComPtr<IDiaSymbol> ElementType{};
        if (FAILED(TypeSymbol->get_type(&ElementType))) {
            return false;
        }
        return DoesTypeNeedNoInitConstruction(ElementType);
    }
    ///For typedefs we need to look up the underlying type to check if they need NoInit call
    if (SymbolTag == SymTagTypedef) {
        CComPtr<IDiaSymbol> UnderlyingTypeSymbol{};
        if (FAILED(TypeSymbol->get_type(&UnderlyingTypeSymbol))) {
            return false;
        }
        return DoesTypeNeedNoInitConstruction(UnderlyingTypeSymbol);
    }
    ///User defined types need NoInit constructor calls if they have a constructor
    if (SymbolTag == SymTagUDT) {
        BOOL bTypeHasConstructor = FALSE;
        TypeSymbol->get_constructor(&bTypeHasConstructor);
        return bTypeHasConstructor;
    }
    ///Everything else does not need NoInit constructor calls
    return false;
}

bool IsSymbolUserDefinedType(const CComPtr<IDiaSymbol>& TypeSymbol) {
    DWORD SymbolTag = SymTagNull;
    if (!SUCCEEDED(TypeSymbol->get_symTag(&SymbolTag))) {
        return false;
    }
    if (SymbolTag == SymTagTypedef) {
        CComPtr<IDiaSymbol> UnderlyingTypeSymbol{};
        if (FAILED(TypeSymbol->get_type(&UnderlyingTypeSymbol))) {
            return false;
        }
        return IsSymbolUserDefinedType(UnderlyingTypeSymbol);
    }
    if (SymbolTag == SymTagUDT) {
        return true;
    }
    return false;
}

std::wstring GenerateFunctionDeclaration(const CComPtr<IDiaSymbol>& FunctionSymbol) {
    CComPtr<IDiaSymbol> FunctionTypeSymbol;
    if (FAILED(FunctionSymbol->get_type(&FunctionTypeSymbol)) || !FunctionTypeSymbol) {
        return TEXT("<unknown function declaration>");
    }

    std::wstring FunctionDeclarationString;

    BOOL bIsVirtualFunction = FALSE;
    if (SUCCEEDED(FunctionSymbol->get_virtual(&bIsVirtualFunction)) && bIsVirtualFunction) {
        FunctionDeclarationString.append(TEXT("virtual "));
    }

    BOOL bIsStaticFunction = FALSE;
    if (SUCCEEDED(FunctionSymbol->get_isStatic(&bIsStaticFunction)) && bIsStaticFunction) {
        FunctionDeclarationString.append(TEXT("static "));
    }

    std::wstring ReturnTypeString = TEXT("void");
    CComPtr<IDiaSymbol> ReturnTypeSymbol{};
    if (SUCCEEDED(FunctionTypeSymbol->get_type(&ReturnTypeSymbol)) && ReturnTypeSymbol) {
        ReturnTypeString = GenerateTypeDeclarationForSymbol(ReturnTypeSymbol);
    }
    FunctionDeclarationString.append(ReturnTypeString);

    std::wstring FunctionName = TEXT("<unknown function name>");
    BSTR FunctionNameString{};
    if (SUCCEEDED(FunctionSymbol->get_name(&FunctionNameString)) && FunctionNameString) {
        FunctionName = FunctionNameString;
        SysFreeString(FunctionNameString);
    }
    FunctionDeclarationString.push_back(TEXT(' '));
    FunctionDeclarationString.append(FunctionName);

    FunctionDeclarationString.push_back(TEXT('('));
    FunctionDeclarationString.append(GenerateFunctionArgumentList(FunctionTypeSymbol));
    FunctionDeclarationString.push_back(TEXT(')'));

    ///Append const to the member function if it is marked const
    BOOL bIsConstFunction = FALSE;
    if (SUCCEEDED(FunctionSymbol->get_constType(&bIsConstFunction)) && bIsConstFunction) {
        FunctionDeclarationString.append(TEXT(" const"));
    }

    ///If the function is virtual but is not intro virtual, append the override specifier
    BOOL bIsIntroVirtual = FALSE;
    if (SUCCEEDED(FunctionSymbol->get_intro(&bIsIntroVirtual)) && (bIsVirtualFunction && !bIsIntroVirtual)) {
        FunctionDeclarationString.append(TEXT(" override"));
    }

    BOOL bIsPureVirtual = FALSE;
    FunctionSymbol->get_pure(&bIsPureVirtual);
    if (bIsPureVirtual) {
        FunctionDeclarationString.append(TEXT(" = 0;"));
    } else {
        FunctionDeclarationString.append(TEXT(" {"));

        ///Generate dummy return statement if this function is not returning void
        if (ReturnTypeString != TEXT("void")) {
            std::wstring DummyReturnType = GenerateDefaultValueForType(ReturnTypeSymbol);
            FunctionDeclarationString.append(Printf(TEXT(" return %s; "), DummyReturnType.c_str()));
        }
        FunctionDeclarationString.append(TEXT("};"));
    }
    return FunctionDeclarationString;
}

void GenerateUserDefinedTypeLayout(const CComPtr<IDiaSymbol>& UDTSymbol, FUserDefinedTypeLayout& OutLayout) {
    BSTR SymbolName{};
    if (SUCCEEDED(UDTSymbol->get_name(&SymbolName))) {
        OutLayout.ClassName = SymbolName;
        SysFreeString(SymbolName);
    }

    ///Iterate the base classes of the user defined type
    CComPtr<IDiaEnumSymbols> BaseClassSymbols{};
    if (SUCCEEDED(UDTSymbol->findChildrenEx(SymTagBaseClass, NULL, nsNone, &BaseClassSymbols))) {
        LONG SymbolCount = 0;
        BaseClassSymbols->get_Count(&SymbolCount);

        for (LONG i = 0; i < SymbolCount; i++) {
            CComPtr<IDiaSymbol> BaseClassSymbol{};
            BaseClassSymbols->Item(i, &BaseClassSymbol);
            FParentClassInfo ParentClassInfo{};

            BSTR ParentClassName{};
            if (SUCCEEDED(BaseClassSymbol->get_name(&ParentClassName)) && ParentClassName) {
                ParentClassInfo.ClassName = ParentClassName;
                SysFreeString(ParentClassName);
            }

            LONG ParentClassDataOffset{0};
            if (SUCCEEDED(BaseClassSymbol->get_offset(&ParentClassDataOffset))) {
                ParentClassInfo.ClassDataOffset = (int32_t) ParentClassDataOffset;
            }

            ULONGLONG ParentClassSize{0};
            if (SUCCEEDED(BaseClassSymbol->get_length(&ParentClassSize))) {
                ParentClassInfo.ClassSize = (int32_t) ParentClassSize;
            }

            DWORD VariableAccess{};
            if (SUCCEEDED(BaseClassSymbol->get_access(&VariableAccess))) {
                if (VariableAccess == CV_private) {
                    ParentClassInfo.ClassAccess = EMemberAccess::Private;
                } else if (VariableAccess == CV_protected) {
                    ParentClassInfo.ClassAccess = EMemberAccess::Protected;
                } else if (VariableAccess == CV_public) {
                    ParentClassInfo.ClassAccess = EMemberAccess::Public;
                }
            }

            BOOL bHasConstructor{false};
            if (SUCCEEDED(BaseClassSymbol->get_constructor(&bHasConstructor))) {
                ParentClassInfo.bHasConstructor = bHasConstructor;
            }

            OutLayout.ParentClasses.push_back(ParentClassInfo);
        }
    }

    uint64_t TypeSizeInBytes{};
    if (SUCCEEDED(UDTSymbol->get_length(&TypeSizeInBytes))) {
        OutLayout.TotalTypeSize = (int32_t) TypeSizeInBytes;
    }

    ///Iterate the member variables of the user defined type
    CComPtr<IDiaEnumSymbols> DataSymbols{};
    if (SUCCEEDED(UDTSymbol->findChildrenEx(SymTagData, NULL, nsNone, &DataSymbols))) {
        LONG SymbolCount = 0;
        DataSymbols->get_Count(&SymbolCount);

        for(LONG i = 0; i < SymbolCount; i++) {
            CComPtr<IDiaSymbol> ChildDataSymbol{};
            DataSymbols->Item(i, &ChildDataSymbol);

            DWORD SymbolDataKind{};
            DWORD SymbolLocationType{};

            if (FAILED(ChildDataSymbol->get_dataKind(&SymbolDataKind)) ||
                FAILED(ChildDataSymbol->get_locationType(&SymbolLocationType))) {
                ChildDataSymbol.Release();
                continue;
            }

            ///Skip over variables that are not member variables of the UDT
            ///Or variables that are not this relative/bit fields
            if (SymbolDataKind != DataKind::DataIsMember ||
                (SymbolLocationType != LocIsThisRel && SymbolLocationType != LocIsBitField)) {
                ChildDataSymbol.Release();
                continue;
            }

            ///Skip over the compiler generated properties
            BOOL bIsCompilerGenerated = FALSE;
            if (SUCCEEDED(ChildDataSymbol->get_compilerGenerated(&bIsCompilerGenerated)) && bIsCompilerGenerated) {
                ChildDataSymbol.Release();
                continue;
            }

            FMemberVariable MemberVariable{};

            BSTR VariableName{};
            if (SUCCEEDED(ChildDataSymbol->get_name(&VariableName))) {
                MemberVariable.VariableName = VariableName;
                SysFreeString(VariableName);
            }

            CComPtr<IDiaSymbol> VariableType{};
            if (SUCCEEDED(ChildDataSymbol->get_type(&VariableType))) {
                ///If variable type is an array type, we want variable type to be an array element type instead
                DWORD VariableTypeSymTag = SymTagNull;
                if (SUCCEEDED(VariableType->get_symTag(&VariableTypeSymTag)) && VariableTypeSymTag == SymTagArrayType) {
                    MemberVariable.bIsArray = true;

                    CComPtr<IDiaSymbol> ElementType{};
                    if (SUCCEEDED(VariableType->get_type(&ElementType))) {
                        MemberVariable.VariableType = GenerateTypeDeclarationForSymbol(ElementType);
                        MemberVariable.bIsUDT = IsSymbolUserDefinedType(ElementType);
                    }

                    DWORD ArrayElementCount = 0;
                    if (SUCCEEDED(VariableType->get_count(&ArrayElementCount))) {
                        MemberVariable.ArraySize = (int32_t) ArrayElementCount;
                    }
                } else {
                    MemberVariable.VariableType = GenerateTypeDeclarationForSymbol(VariableType);
                    MemberVariable.bIsUDT = IsSymbolUserDefinedType(VariableType);
                }

                MemberVariable.bNeedsValueInit = DoesTypeNeedValueInitialization(VariableType, MemberVariable.ValueInitDefaultValue);
                MemberVariable.bNeedsNoInitConstructorCall = DoesTypeNeedNoInitConstruction(VariableType);
            }

            DWORD VariableAccess{};
            if (SUCCEEDED(ChildDataSymbol->get_access(&VariableAccess))) {
                if (VariableAccess == CV_private) {
                    MemberVariable.VariableAccess = EMemberAccess::Private;
                } else if (VariableAccess == CV_protected) {
                    MemberVariable.VariableAccess = EMemberAccess::Protected;
                } else if (VariableAccess == CV_public) {
                    MemberVariable.VariableAccess = EMemberAccess::Public;
                }
            }

            ///We can always call get_offset because the location is either a bitfield or a this relative variable
            LONG VariableOffset{};
            if (SUCCEEDED(ChildDataSymbol->get_offset(&VariableOffset))) {
                MemberVariable.VariableOffset = VariableOffset;
            }

            ///Bitfields have bitPosition and length in bits
            if (SymbolLocationType == LocIsBitField) {
                MemberVariable.bIsBitfield = true;

                DWORD BitPosition{};
                if (SUCCEEDED(ChildDataSymbol->get_bitPosition(&BitPosition))) {
                    MemberVariable.BitfieldBitPosition = (int32_t) BitPosition;
                }
                ULONGLONG BitSize{};
                if (SUCCEEDED(ChildDataSymbol->get_length(&BitSize))) {
                    MemberVariable.BitfieldBitSize = (int32_t) BitSize;
                }

                ULONGLONG VariableSize{};
                if (SUCCEEDED(VariableType->get_length(&VariableSize))) {
                    MemberVariable.VariableSize = (int32_t) VariableSize;
                }
            } else {
                //Retrieve normal size
                ULONGLONG VariableSize{};
                if (SUCCEEDED(ChildDataSymbol->get_length(&VariableSize))) {
                    MemberVariable.VariableSize = (int32_t) VariableSize;
                }
            }

            OutLayout.MemberVariables.push_back(MemberVariable);
            ChildDataSymbol.Release();
        }
    }

    ///Iterate the functions defined on the type
    CComPtr<IDiaEnumSymbols> FunctionSymbols{};
    if (SUCCEEDED(UDTSymbol->findChildrenEx(SymTagFunction, NULL, nsNone, &FunctionSymbols))) {
        LONG SymbolCount = 0L;
        FunctionSymbols->get_Count(&SymbolCount);

        for(LONG i = 0; i < SymbolCount; i++) {
            CComPtr<IDiaSymbol> ChildFunctionSymbol{};
            FunctionSymbols->Item(i, &ChildFunctionSymbol);

            BOOL bIsFunctionVirtual = 0;
            BOOL bIsIntroVirtual = 0;

            ///We skip over the functions that are not marked as intro virtuals
            ///If function is not marked as intro it's not the first declaration of the virtual function but rather
            ///an override, and we do not really care about overrides
            if (FAILED(ChildFunctionSymbol->get_virtual(&bIsFunctionVirtual)) ||
                FAILED(ChildFunctionSymbol->get_intro(&bIsIntroVirtual))) {
                ChildFunctionSymbol.Release();
                continue;
            }

            ///Skip over non-intro virtual functions, we do not care about them
            if (!bIsFunctionVirtual || !bIsIntroVirtual) {
                ChildFunctionSymbol.Release();
                continue;
            }

            ///Skip over the compiler generated functions, like vector destructors
            BOOL bIsCompilerGenerated = FALSE;
            if (SUCCEEDED(ChildFunctionSymbol->get_compilerGenerated(&bIsCompilerGenerated)) && bIsCompilerGenerated) {
                ChildFunctionSymbol.Release();
                continue;
            }

            FVirtualFunctionDeclaration FuncDeclaration{};

            BSTR FunctionName;
            if (SUCCEEDED(ChildFunctionSymbol->get_name(&FunctionName))) {
                FuncDeclaration.FunctionName = FunctionName;
                SysFreeString(FunctionName);
            }

            FuncDeclaration.FunctionDeclaration = GenerateFunctionDeclaration(ChildFunctionSymbol);

            DWORD AccessModifier;
            if (SUCCEEDED(ChildFunctionSymbol->get_access(&AccessModifier))) {
                if (AccessModifier == CV_public) {
                    FuncDeclaration.FunctionAccess = EMemberAccess::Public;
                } else if (AccessModifier == CV_protected) {
                    FuncDeclaration.FunctionAccess = EMemberAccess::Protected;
                } else if (AccessModifier == CV_private) {
                    FuncDeclaration.FunctionAccess = EMemberAccess::Private;
                }
            }

            DWORD VirtualBaseOffset;
            if (SUCCEEDED(ChildFunctionSymbol->get_virtualBaseOffset(&VirtualBaseOffset))) {
                FuncDeclaration.VirtualTableOffset = (int32_t) VirtualBaseOffset;
            }

            OutLayout.VirtualFunctions.push_back(FuncDeclaration);
            ChildFunctionSymbol.Release();
        }
    }

    CComPtr<IDiaSymbol> VirtualTableShape{};
    if (SUCCEEDED(UDTSymbol->get_virtualTableShape(&VirtualTableShape)) && VirtualTableShape) {
        DWORD VirtualTableEntriesCount = 0;
        if (SUCCEEDED(VirtualTableShape->get_count(&VirtualTableEntriesCount))) {
            OutLayout.VirtualTableEntriesCount = (int32_t) VirtualTableEntriesCount;
        }
    }
}

void ReplaceAllOccurrences(std::wstring& s, const std::wstring& toReplace, const std::wstring& replaceWith) {
    std::wostringstream oss;
    std::size_t pos = 0;
    std::size_t prevPos = pos;

    while (true) {
        prevPos = pos;
        pos = s.find(toReplace, pos);
        if (pos == std::string::npos)
            break;
        oss << s.substr(prevPos, pos - prevPos);
        oss << replaceWith;
        pos += toReplace.size();
    }

    oss << s.substr(prevPos);
    s = oss.str();
}

///Sanitizes CPP identifier by replacing :: with __, making it usable in filenames and macros
std::wstring SanitizeCppIdentifier(const std::wstring& Identifier) {
    std::wstring Result = Identifier;
    ReplaceAllOccurrences(Result, TEXT("::"), TEXT("__"));
    return Result;
}

void GenerateMemberVariableLayout(FGeneratedFile& GeneratedFile, const FUserDefinedTypeLayout& TypeLayout) {
    GeneratedFile.Logf(TEXT("#define IMPLEMENT_MEMBER_VARIABLE_LAYOUT_%s \\"), SanitizeCppIdentifier(TypeLayout.ClassName).c_str());
    EMemberAccess CurrentAccess = EMemberAccess::Unspecified;

    for (const FMemberVariable& Variable : TypeLayout.MemberVariables) {
        if (Variable.VariableAccess != CurrentAccess) {
            CurrentAccess = Variable.VariableAccess;

            if (CurrentAccess == EMemberAccess::Public) {
                GeneratedFile.Logf(TEXT("public: \\"));
            } else if (CurrentAccess == EMemberAccess::Protected) {
                GeneratedFile.Logf(TEXT("protected: \\"));
            } else if (CurrentAccess == EMemberAccess::Private) {
                GeneratedFile.Logf(TEXT("private: \\"));
            }
        }

        GeneratedFile.BeginIndentLevel();
        if (Variable.bIsBitfield) {
            ///Generate a bitfield
            GeneratedFile.Logf(TEXT("%s %s: %d; \\"), Variable.VariableType.c_str(), Variable.VariableName.c_str(), Variable.BitfieldBitSize);
        } else if (Variable.bIsArray) {
            ///Generate an array field
            GeneratedFile.Logf(TEXT("%s %s[%d]; \\"), Variable.VariableType.c_str(), Variable.VariableName.c_str(), Variable.ArraySize);
        } else {
            ///Generate a normal field
            GeneratedFile.Logf(TEXT("%s %s; \\"), Variable.VariableType.c_str(), Variable.VariableName.c_str());
        }
        GeneratedFile.EndIndentLevel();
    }
    GeneratedFile.Logf(TEXT(""));
}

void GenerateVirtualTableLayout(FGeneratedFile& GeneratedFile, const FUserDefinedTypeLayout& TypeLayout) {
    GeneratedFile.Logf(TEXT("#define IMPLEMENT_VIRTUAL_TABLE_LAYOUT_%s \\"), SanitizeCppIdentifier(TypeLayout.ClassName).c_str());
    EMemberAccess CurrentAccess = EMemberAccess::Unspecified;

    for (const FVirtualFunctionDeclaration& Function : TypeLayout.VirtualFunctions) {
        if (Function.FunctionAccess != CurrentAccess) {
            CurrentAccess = Function.FunctionAccess;

            if (CurrentAccess == EMemberAccess::Public) {
                GeneratedFile.Logf(TEXT("public: \\"));
            } else if (CurrentAccess == EMemberAccess::Protected) {
                GeneratedFile.Logf(TEXT("protected: \\"));
            } else if (CurrentAccess == EMemberAccess::Private) {
                GeneratedFile.Logf(TEXT("private: \\"));
            }
        }

        GeneratedFile.BeginIndentLevel();

        std::wstring FunctionDeclaration = Function.FunctionDeclaration;
        ReplaceAllOccurrences(FunctionDeclaration, Printf(TEXT("%s::"), TypeLayout.ClassName.c_str()), TEXT(""));
        GeneratedFile.Logf(TEXT("%s \\"), FunctionDeclaration.c_str());

        GeneratedFile.EndIndentLevel();
    }
    GeneratedFile.Logf(TEXT(""));
}

void GenerateTopLevelMacroDefinitions(FGeneratedFile& GeneratedFile, const FUserDefinedTypeLayout& TypeLayout) {
    GeneratedFile.Logf(TEXT("#define VIRTUAL_FUNCTION_COUNT_%s %d"), SanitizeCppIdentifier(TypeLayout.ClassName).c_str(), TypeLayout.VirtualTableEntriesCount);
}

enum ENoInit { NoInit };

struct Bar {
    int Bar1;
};

struct Foo : Bar {
    Foo() : Bar() {

    }
};

void GenerateTypeLayoutNoInitConstructor(FGeneratedFile& GeneratedFile, const FUserDefinedTypeLayout& TypeLayout) {
    GeneratedFile.Logf(TEXT("#define IMPLEMENT_NO_INIT_CONSTRUCTOR_%s \\"), SanitizeCppIdentifier(TypeLayout.ClassName).c_str());
    GeneratedFile.BeginIndentLevel();

    int32_t NoInitConstructorsNeeded = 0;
    for (const FParentClassInfo& ParentClass : TypeLayout.ParentClasses) {
        NoInitConstructorsNeeded += ParentClass.bHasConstructor;
    }
    for (const FMemberVariable& MemberVariable : TypeLayout.MemberVariables) {
        NoInitConstructorsNeeded += MemberVariable.bNeedsNoInitConstructorCall;
    }

    GeneratedFile.Logf(TEXT("explicit inline %s(ENoInit)%s \\"), TypeLayout.ClassName.c_str(), NoInitConstructorsNeeded ? TEXT(" :") : TEXT(""));

    if (NoInitConstructorsNeeded) {
        GeneratedFile.BeginIndentLevel();

        int32_t NoInitConstructorsCalled = 0;

        ///We need to explicitly NoInit base class if it has a constructor defined, or it will be implicitly called
        for (const FParentClassInfo& ParentClass : TypeLayout.ParentClasses) {
            if (ParentClass.bHasConstructor) {
                NoInitConstructorsCalled++;
                const wchar_t* OptionalComma = NoInitConstructorsCalled < NoInitConstructorsNeeded ? TEXT(",") : TEXT("");
                GeneratedFile.Logf(TEXT("%s(NoInit)%s \\"), ParentClass.ClassName.c_str(), OptionalComma);
            }
        }

        for (const FMemberVariable& MemberVariable : TypeLayout.MemberVariables) {
            if (MemberVariable.bNeedsNoInitConstructorCall) {
                NoInitConstructorsCalled++;
                const wchar_t* OptionalComma = NoInitConstructorsCalled < NoInitConstructorsNeeded ? TEXT(",") : TEXT("");

                ///Arrays need NoInit constructor called on each of their elements, or it will call default constructor instead
                ///Which is definitely not what we want there
                if (MemberVariable.bIsArray && MemberVariable.bIsUDT) {
                    std::wstring ArrayElementsInitializer;
                    for (int32_t i = 0; i < MemberVariable.ArraySize; i++) {
                        ArrayElementsInitializer.append(Printf(TEXT("%s(NoInit)"), MemberVariable.VariableType.c_str()));

                        if ((i + 1) != MemberVariable.ArraySize) {
                            ArrayElementsInitializer.append(TEXT(", "));
                        }
                    }
                    ///Array initializers need to be initializer lists, normal curly brackets are not allowed
                    GeneratedFile.Logf(TEXT("%s{%s}%s \\"), MemberVariable.VariableName.c_str(), ArrayElementsInitializer.c_str(), OptionalComma);
                } else {
                    GeneratedFile.Logf(TEXT("%s(NoInit)%s \\"), MemberVariable.VariableName.c_str(), OptionalComma);
                }
            }
        }
        GeneratedFile.EndIndentLevel();
    }

    GeneratedFile.Logf(TEXT("{} \\"));
    GeneratedFile.EndIndentLevel();
}

void GenerateTypeLayoutForceInitConstructor(FGeneratedFile& GeneratedFile, const FUserDefinedTypeLayout& TypeLayout) {
    GeneratedFile.Logf(TEXT("#define IMPLEMENT_FORCE_INIT_CONSTRUCTOR_%s \\"), SanitizeCppIdentifier(TypeLayout.ClassName).c_str());
    GeneratedFile.BeginIndentLevel();

    int32_t ForceInitConstructorsNeeded = 0;
    for (const FParentClassInfo& ParentClass : TypeLayout.ParentClasses) {
        ForceInitConstructorsNeeded += !ParentClass.bHasConstructor;
    }
    for (const FMemberVariable& MemberVariable : TypeLayout.MemberVariables) {
        ForceInitConstructorsNeeded += MemberVariable.bNeedsValueInit;
    }

    GeneratedFile.Logf(TEXT("explicit inline %s(EForceInit)%s \\"), TypeLayout.ClassName.c_str(), ForceInitConstructorsNeeded ? TEXT(" :") : TEXT(""));

    if (ForceInitConstructorsNeeded) {
        GeneratedFile.BeginIndentLevel();

        int32_t ForceInitConstructorsCalled = 0;

        ///We need to explicitly default init the parent class if it does not have an explicit constructor
        for (const FParentClassInfo& ParentClass : TypeLayout.ParentClasses) {
            if (!ParentClass.bHasConstructor) {
                ForceInitConstructorsCalled++;
                const wchar_t* OptionalComma = ForceInitConstructorsCalled < ForceInitConstructorsNeeded ? TEXT(",") : TEXT("");
                GeneratedFile.Logf(TEXT("%s()%s \\"), ParentClass.ClassName.c_str(), OptionalComma);
            }
        }

        for (const FMemberVariable& MemberVariable : TypeLayout.MemberVariables) {
            if (MemberVariable.bNeedsValueInit) {
                ForceInitConstructorsCalled++;
                const wchar_t* OptionalComma = ForceInitConstructorsCalled < ForceInitConstructorsNeeded ? TEXT(",") : TEXT("");

                ///Arrays need initializer list used instead of brackets initializer
                if (MemberVariable.bIsArray) {
                    std::wstring ArrayElementsInitializer;
                    for (int32_t i = 0; i < MemberVariable.ArraySize; i++) {
                        ///Only call constructors on UDTs, otherwise substitute the default value directly
                        if (MemberVariable.bIsUDT) {
                            ArrayElementsInitializer.append(Printf(TEXT("%s(%s)"), MemberVariable.VariableType.c_str(), MemberVariable.ValueInitDefaultValue.c_str()));
                        } else {
                            ArrayElementsInitializer.append(MemberVariable.ValueInitDefaultValue);
                        }
                        if ((i + 1) != MemberVariable.ArraySize) {
                            ArrayElementsInitializer.append(TEXT(", "));
                        }
                    }
                    GeneratedFile.Logf(TEXT("%s{%s}%s \\"), MemberVariable.VariableName.c_str(), ArrayElementsInitializer.c_str(), OptionalComma);
                } else {
                    GeneratedFile.Logf(TEXT("%s(%s)%s \\"), MemberVariable.VariableName.c_str(), MemberVariable.ValueInitDefaultValue.c_str(), OptionalComma);
                }
            }
        }
        GeneratedFile.EndIndentLevel();
    }

    GeneratedFile.Logf(TEXT("{} \\"));
    GeneratedFile.EndIndentLevel();
}

bool GenerateTypeLayoutFile(const std::wstring& OutputDirectory, const CComPtr<IDiaSymbol>& GlobalScope, const std::wstring& UDTName) {
    CComPtr<IDiaEnumSymbols> SymbolsEnumerator;
    GlobalScope->findChildrenEx(SymTagUDT, UDTName.c_str(), nsfUndecoratedName, &SymbolsEnumerator);

    CComPtr<IDiaSymbol> UDTSymbol;
    SymbolsEnumerator->Item(0, &UDTSymbol);

    if (UDTSymbol == NULL) {
        return false;
    }

    FUserDefinedTypeLayout TypeLayout{};
    GenerateUserDefinedTypeLayout(UDTSymbol, TypeLayout);

    FGeneratedFile GeneratedFile{OutputDirectory, SanitizeCppIdentifier(TypeLayout.ClassName)};
    GeneratedFile.Logf(TEXT("/* Generated file for UDT '%s' */"), TypeLayout.ClassName.c_str());
    GeneratedFile.Logf(TEXT(""));
    GenerateTopLevelMacroDefinitions(GeneratedFile, TypeLayout);
    GeneratedFile.Logf(TEXT(""));
    GenerateMemberVariableLayout(GeneratedFile, TypeLayout);
    GeneratedFile.Logf(TEXT(""));
    GenerateVirtualTableLayout(GeneratedFile, TypeLayout);
    GeneratedFile.Logf(TEXT(""));
    GenerateTypeLayoutNoInitConstructor(GeneratedFile, TypeLayout);
    GeneratedFile.Logf(TEXT(""));
    GenerateTypeLayoutForceInitConstructor(GeneratedFile, TypeLayout);
    GeneratedFile.Logf(TEXT(""));
    GeneratedFile.WriteFile();

    return true;
}