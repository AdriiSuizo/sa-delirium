#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <msdelta.h>
#include <cstdio>
#include <cwchar>

// Version/data changes are shipped as deltas against verified owned files.
// SHA-256 input/output verification is performed by the recipe runner; MSDelta
// additionally validates the delta's own hash. Never distribute the input or
// reconstructed game executables with this utility.
int wmain(int argc, wchar_t** argv) {
    if (argc != 5) {
        fwprintf(stderr, L"Usage: DeltaTool make|apply source target-or-delta output\n");
        return 2;
    }
    if (GetFileAttributesW(argv[4]) != INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"Output already exists\n"); return 3;
    }
    BOOL ok = FALSE;
    if (!wcscmp(argv[1], L"make")) {
        DELTA_INPUT empty{};
        ok = CreateDeltaW(DELTA_FILE_TYPE_SET_RAW_ONLY, DELTA_FLAG_NONE, DELTA_FLAG_NONE,
                          argv[2], argv[3], nullptr, nullptr, empty, nullptr, 0, argv[4]);
    } else if (!wcscmp(argv[1], L"apply")) {
        ok = ApplyDeltaW(DELTA_FLAG_NONE, argv[2], argv[3], argv[4]);
    } else return 2;
    if (!ok) { fwprintf(stderr, L"MSDelta error %lu\n", GetLastError()); return 1; }
    return 0;
}
