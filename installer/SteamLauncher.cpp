#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>

// Steam owns this process. Keeping it alive until the owned child exits lets
// the normal library button retain process lifetime and inherited Steam state.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::vector<wchar_t> buffer(32768);
    DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size()) return 2;
    std::wstring directory(buffer.data(), count);
    directory.resize(directory.find_last_of(L"\\/"));
    directory += L"\\SA";
    const auto executable = directory + L"\\gta_sa.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(nullptr, L"No se encuentra la copia instalada. Vuelve a abrir SA-Delirium-Setup para reparar o restaurar Steam.", L"SA Delirium", MB_OK | MB_ICONERROR);
        return 3;
    }
    // GetCommandLine includes the wrapper's quoted path. Forward only arguments,
    // using CommandLineToArgvW in the installer tests to verify space handling.
    const wchar_t* tail = GetCommandLineW();
    if (*tail == L'\"') { ++tail; while (*tail && *tail != L'\"') ++tail; if (*tail) ++tail; }
    else { while (*tail && *tail != L' ' && *tail != L'\t') ++tail; }
    std::wstring command = L"\"" + executable + L"\"" + tail;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, FALSE, 0,
                        nullptr, directory.c_str(), &startup, &child)) {
        MessageBoxW(nullptr, L"No se ha podido iniciar GTA San Andreas. Revisa la instalación con SA-Delirium-Setup.", L"SA Delirium", MB_OK | MB_ICONERROR);
        return 4;
    }
    CloseHandle(child.hThread);
    WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hProcess);
    return static_cast<int>(code);
}
