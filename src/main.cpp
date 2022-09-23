#include <string>
#include <fstream>
#include <Windows.h>
#include <Psapi.h>
#include <strsafe.h>
#include <atlbase.h>
#include <dia2.h>
#include <DbgHelp.h>
#include <filesystem>
#include <iostream>

///From TypeLayoutGenerator.cpp
///Yes I know I am a bad person for not making a header file
bool GenerateTypeLayoutFile(const std::wstring& OutputDirectory, const CComPtr<IDiaSymbol>& GlobalScope, const std::wstring& UDTName);

HRESULT CoCreateDiaDataSource(HMODULE diaDllHandle, CComPtr<IDiaDataSource>& OutDataSource) {
    auto DllGetClassObject = (BOOL (WINAPI*)(REFCLSID, REFIID, LPVOID *)) GetProcAddress(diaDllHandle, "DllGetClassObject");
    if (!DllGetClassObject) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CComPtr<IClassFactory> pClassFactory;
    HRESULT hr = DllGetClassObject(CLSID_DiaSource, IID_IClassFactory, reinterpret_cast<LPVOID *>(&pClassFactory));
    if (FAILED(hr)) {
        return hr;
    }
    hr = pClassFactory->CreateInstance(nullptr, IID_IDiaDataSource, (void **) &OutDataSource);
    if (FAILED(hr)) {
        return hr;
    }
    return S_OK;
}

enum class ETypeSelectorImportance {
    Normal,
    Important,
    Optional
};

struct FTypeSelector {
    std::wstring TypeName;
    ETypeSelectorImportance Importance;
};

bool ReadTypesToDump(const std::wstring& FileName, std::vector<FTypeSelector>& OutTypesToDump) {
    std::wifstream FileStream{FileName};
    if (!FileStream.good()) {
        return false;
    }

    while (!FileStream.eof()) {
        std::wstring FileReadLine;
        std::getline(FileStream, FileReadLine);

        ///Skip empty lines and comments starting with a #
        if (!FileReadLine.empty() && FileReadLine[0] != '#') {
            ETypeSelectorImportance Importance = ETypeSelectorImportance::Normal;
            wchar_t FirstSymbol = FileReadLine[0];

            ///Optional type declarations start with a question mark
            if (FirstSymbol == TEXT('?')) {
                Importance = ETypeSelectorImportance::Optional;
                FileReadLine.erase(0, 1);
            }
            ///Important type declarations are prefixed with an exclamination mark
            if (FirstSymbol == TEXT('!')) {
                Importance = ETypeSelectorImportance::Important;
                FileReadLine.erase(0, 1);
            }
            OutTypesToDump.push_back(FTypeSelector{FileReadLine, Importance});
        }
    }
    return !OutTypesToDump.empty();
}

bool DumpTypesForDebugFile(const std::filesystem::path& PDBFilePath, const std::filesystem::path& OutputFolderPath, HMODULE DiaModuleHandle, const std::vector<FTypeSelector>& TypesToDump) {
    CComPtr<IDiaDataSource> DiaDataSource;

    if (FAILED(CoCreateDiaDataSource(DiaModuleHandle, DiaDataSource))) {
        std::wcerr << TEXT("Failed to create DIA data source from dia DLL handle") << std::endl;
        exit(1);
    }

    if (FAILED(DiaDataSource->loadDataFromPdb(PDBFilePath.wstring().c_str()))) {
        std::wcerr << TEXT("Failed to load data from PDB file ") << PDBFilePath.wstring() << std::endl;
        return false;
    }

    CComPtr<IDiaSession> DiaSession;
    if (FAILED(DiaDataSource->openSession(&DiaSession))) {
        std::wcerr << TEXT("Failed to open DIA session for PDB file ") << PDBFilePath.wstring() << std::endl;
        return false;
    }

    CComPtr<IDiaSymbol> GlobalScopeSymbol;
    if (FAILED(DiaSession->get_globalScope(&GlobalScopeSymbol))) {
        std::wcerr << TEXT("Failed to retrieve DIA global scope symbol for PDB file ") << PDBFilePath.wstring() << std::endl;
        return false;
    }

    std::filesystem::path OutputDir = OutputFolderPath / PDBFilePath.filename().replace_extension();
    create_directories(OutputDir);

    std::wcout << TEXT("Begin dumping types for PDB file ") << PDBFilePath.filename().wstring() << std::endl;
    for (const FTypeSelector& TypeName : TypesToDump) {
        std::wcout << TEXT("Dumping type ") << TypeName.TypeName << std::endl;
        if (!GenerateTypeLayoutFile(OutputDir.wstring(), GlobalScopeSymbol, TypeName.TypeName)) {
            if (TypeName.Importance != ETypeSelectorImportance::Optional) {
                std::wcout << TEXT("Failed to dump type ") << TypeName.TypeName << std::endl;
            }
            if (TypeName.Importance == ETypeSelectorImportance::Important) {
                std::wcout << TEXT("Type was marked as Important (!). Aborting the dump.") << std::endl;
                return false;
            }
        }
    }

    std::wcout << TEXT("Finished dumping types for PDB file ") << PDBFilePath.filename().wstring() << std::endl;
    return true;
}

std::wstring GetLastErrorString(LPCTSTR lpszFunction)
{
    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    DWORD dw = GetLastError();

    FormatMessage(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            dw,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&lpMsgBuf,
            0, NULL);

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
                                      (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    StringCchPrintf((LPTSTR)lpDisplayBuf,
                    LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                    TEXT("%s failed with error %d: %s"),
                    lpszFunction, dw, lpMsgBuf);
    auto ErrorAsString = std::wstring{(LPCTSTR)lpDisplayBuf};

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    return ErrorAsString;
}

int main(int argc, const char** argv) {
    std::wcout << TEXT("Starting the UVTD") << std::endl;
    std::filesystem::path CurrentDirectory = std::filesystem::absolute(TEXT("."));
    std::wcout << TEXT("Run Directory: ") << CurrentDirectory.wstring() << std::endl;

    std::filesystem::path InputPDBsFolder = CurrentDirectory / TEXT("GameDebugFiles");
    std::filesystem::path OutputFolder = CurrentDirectory / TEXT("Output");
    std::filesystem::path DiaDllPath = CurrentDirectory / TEXT("msdia140.dll");

    HMODULE DiaDllHandle = LoadLibraryW(DiaDllPath.wstring().c_str());
    if (DiaDllHandle == nullptr) {
        std::wcout << TEXT("Failed to load msdia140.dll file, make sure it's in the run director at ") << DiaDllPath << std::endl;
        std::wcout << GetLastErrorString(TEXT("LoadLibrary")) << std::endl;
        return 1;
    }

    std::vector<FTypeSelector> TypesToDump;
    if (!ReadTypesToDump(TEXT("TypesToDump.txt"), TypesToDump)) {
        std::wcout << TEXT("Failed to read a list of types to dump from TypesToDump.txt") << std::endl;
        return 1;
    }

    std::wcout << TEXT("Scanning the input directory ") << InputPDBsFolder.wstring() << TEXT(" for PDB files") << std::endl;
    for (auto& DirectoryEntry : std::filesystem::directory_iterator{InputPDBsFolder}) {
        ///We are only interested in regular PDB files
        if (DirectoryEntry.is_regular_file() && DirectoryEntry.path().extension() == TEXT(".pdb")) {

            if (!DumpTypesForDebugFile(DirectoryEntry.path(), OutputFolder, DiaDllHandle, TypesToDump)) {
                std::wcout << TEXT("Failed to dump types for debug file ") << DirectoryEntry.path().wstring() << std::endl;
                return 1;
            }
        }
    }
    return 0;
}