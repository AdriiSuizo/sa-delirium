#pragma once

namespace vehicle_menu {
constexpr unsigned optionsPage = 33;
constexpr uintptr_t frontend = 0xBA6748;
constexpr uintptr_t processEntry = 0x576FE0;
constexpr uintptr_t labelCall = 0x579D73;
constexpr char key[] = "DLM_DRV";
constexpr char movementKey[] = "DLM_MOV";
constexpr char combatKey[] = "DLM_CBT";
constexpr char deformationKey[] = "DLM_DNT";

struct Entry {
    unsigned char action;
    char label[8];
    unsigned char type, target;
    unsigned short x, y;
    unsigned char align;
};
struct Page {
    char title[8];
    unsigned char parent, start;
    Entry entries[12];
};
static_assert(sizeof(Entry) == 0x12 && sizeof(Page) == 0xE2 && offsetof(Page, entries) == 10);

static void (__thiscall* nativeProcess)(void*, signed char, bool*, bool) = nullptr;
static const char* (__thiscall* nativeText)(void*, const char*) = nullptr;
static std::filesystem::path config;
enum class Mode { Classic, Izzy, Iv };
static Mode active = Mode::Classic, selected = Mode::Classic;
static std::filesystem::path izzy;
static bool externalIzzy = false, izzyStarted = false, izzyFailed = false;
static bool movementSelected = false;
static bool combatAtStart = false, combatSelected = false;
static Page* seenPage = nullptr;
static bool failed = false;

static void readMode(const std::filesystem::path& ini, bool previouslyEnabled) {
    izzy = ini.parent_path() / "DeliriumDrivers" / "Izzy" / "LivelyVehiclePhysics.asi";
    externalIzzy = GetModuleHandleW(L"LivelyVehiclePhysics.asi") != nullptr;
    wchar_t value[16]{};
    GetPrivateProfileStringW(L"VehicleLab", L"Mode", previouslyEnabled ? L"IV" : L"Classic",
                             value, DWORD(std::size(value)), ini.c_str());
    if (externalIzzy || (_wcsicmp(value, L"Izzy") == 0 && GetFileAttributesW(izzy.c_str()) != INVALID_FILE_ATTRIBUTES))
        active = Mode::Izzy;
    else if (_wcsicmp(value, L"IV") == 0) active = Mode::Iv;
    selected = active;
    movementSelected = GetPrivateProfileIntW(L"Movement", L"NikoOnFoot", 0, ini.c_str()) != 0;
    combatAtStart = combatSelected = GetPrivateProfileIntW(L"Combat", L"Enabled", 0, ini.c_str()) != 0;
}

static void startIzzy() {
    if (active != Mode::Izzy && GetModuleHandleW(L"LivelyVehiclePhysics.asi")) {
        setTrafficDriving(false);
        classic_body_motion::setEnabled(false);
        externalIzzy = true;
        active = selected = Mode::Izzy;
        event("vehicle_driving_mode", -1, -1, "external_izzy");
    }
    if (active != Mode::Izzy || externalIzzy || izzyStarted) return;
    izzyStarted = true;
    const auto module = LoadLibraryW(izzy.c_str());
    HMODULE pinned = nullptr;
    if (module && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                    reinterpret_cast<LPCWSTR>(module), &pinned)) {
        event("vehicle_izzy_module_pinned");
        event("vehicle_driving_mode", -1, -1, "izzy");
    } else {
        izzyFailed = true;
        event("vehicle_izzy_load_failed", -1, -1, std::to_string(GetLastError()));
    }
}

static Page* activeOptionsPage() {
    // MOVZX in ProcessMenuOptions reads from aScreens[0].entries[0].action.
    // Mod Loader relocates that table and updates this exact operand.
    constexpr unsigned char operand[] = {0x0F, 0xB6, 0xB9};
    const auto* code = reinterpret_cast<const unsigned char*>(0x577014);
    if (std::memcmp(code, operand, sizeof(operand)) != 0) return nullptr;
    uintptr_t entries = 0;
    std::memcpy(&entries, code + sizeof(operand), sizeof(entries));
    if (entries < offsetof(Page, entries)) return nullptr;
    const uintptr_t address = entries - offsetof(Page, entries) + optionsPage * sizeof(Page);
    MEMORY_BASIC_INFORMATION memory{};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        address + sizeof(Page) > reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize) return nullptr;
    auto* page = reinterpret_cast<Page*>(address);
    return std::memcmp(page->title, "FET_OPT", 8) == 0 ? page : nullptr;
}

static int itemIndex(const Page& page, const char (&label)[8] = key) {
    for (int i = 0; i < int(std::size(page.entries)); ++i)
        if (std::memcmp(page.entries[i].label, label, sizeof(label)) == 0) return i;
    return -1;
}

static bool refresh() {
    auto* page = activeOptionsPage();
    if (!page) return false;
    if (page == seenPage && itemIndex(*page) >= 0) return true;
    if (itemIndex(*page) >= 0) { seenPage = page; return true; }

    // Insert before Back, including when Mod Loader has added its own row.
    int back = -1, count = 0;
    for (; count < int(std::size(page->entries)) && page->entries[count].action; ++count)
        if (page->entries[count].action == 2 && std::memcmp(page->entries[count].label, "FEDS_TB", 8) == 0)
            back = count;
    constexpr int added = 4;
    if (back <= 0 || count + added >= int(std::size(page->entries))) return false;
    for (int i = count; i < int(std::size(page->entries)); ++i)
        if (page->entries[i].action || page->entries[i].label[0]) return false;

    DWORD protection = 0;
    if (!VirtualProtect(page, sizeof(*page), PAGE_READWRITE, &protection)) return false;
    std::memmove(page->entries + back + added, page->entries + back, (count - back) * sizeof(Entry));
    page->entries[back] = {0x7E, "DLM_DRV", 12, 0, 0, 0, 3};
    page->entries[back + 1] = {0x7E, "DLM_MOV", 12, 0, 0, 0, 3};
    page->entries[back + 2] = {0x7E, "DLM_CBT", 12, 0, 0, 0, 3};
    page->entries[back + 3] = {0x7E, "DLM_DNT", 12, 0, 0, 0, 3};
    DWORD unused = 0;
    VirtualProtect(page, sizeof(*page), protection, &unused);
    seenPage = page;
    return true;
}

static const char* __fastcall text(void* self, void*, const char* entry) {
    void* manager = reinterpret_cast<void*>(frontend);
    if (at<unsigned char>(manager, 0x15D) == optionsPage) {
        if (std::strcmp(entry, "FEO_CON") == 0 && !refresh() && !failed) {
            failed = true;
            event("vehicle_menu_unavailable");
        }
        const bool spanish = at<unsigned char>(manager, 0x84) == 4;
        if (std::strcmp(entry, deformationKey) == 0) {
            if (!vehicle_deformation::installed) return spanish ? "Deformacion: no disponible" : "Deformation: unavailable";
            if (spanish) return vehicle_deformation::enabled ? "Deformacion: si" : "Deformacion: no";
            return vehicle_deformation::enabled ? "Deformation: on" : "Deformation: off";
        }
        if (std::strcmp(entry, movementKey) == 0) {
            if (spanish) return movementSelected ? "Movimiento CJ: IV" : "Movimiento CJ: SA";
            return movementSelected ? "CJ movement: IV" : "CJ movement: SA";
        }
        if (std::strcmp(entry, combatKey) == 0) {
            if (combatSelected != combatAtStart)
                return spanish ? "Combate (reinicia)" : "Combat (restart)";
            if (spanish) return combatSelected ? "Combate: Delirium" : "Combate: SA";
            return combatSelected ? "Combat: Delirium" : "Combat: SA";
        }
        if (std::strcmp(entry, key) == 0) {
            if (externalIzzy) return spanish ? "Manejo: Izzy externo" : "Driving: external Izzy";
            if (selected != active) {
                if (selected == Mode::Izzy) return spanish ? "Izzy (reinicia)" : "Izzy (restart)";
                if (selected == Mode::Iv) return spanish ? "Delirium IV (reinicia)" : "Delirium IV (restart)";
                return spanish ? "Clasico (reinicia)" : "Classic (restart)";
            }
            if (selected == Mode::Izzy) return izzyFailed ? "Izzy: error" : "Manejo: Izzy";
            if (selected == Mode::Iv) return spanish ? "Manejo: Delirium GTA IV" : "Driving: Delirium GTA IV";
            return spanish ? "Manejo: Clasico retocado" : "Driving: Enhanced Classic";
        }
    }
    return nativeText(self, entry);
}

static void __fastcall process(void* manager, void*, signed char direction, bool* back, bool accept) {
    if (at<unsigned char>(manager, 0x15D) == optionsPage) {
        const auto* page = activeOptionsPage();
        const int row = at<int>(manager, 0x54);
        if (page && row >= 0) {
            if (itemIndex(*page, deformationKey) == row) {
                if (vehicle_deformation::installed && (accept || direction) && (!back || !*back)) {
                    const bool value = !vehicle_deformation::enabled;
                    if (WritePrivateProfileStringW(L"VehicleDeformation", L"Enabled", value ? L"1" : L"0", config.c_str()))
                        vehicle_deformation::setEnabled(value);
                    else event("gameplay_setting_write_failed", -1, -1, std::to_string(GetLastError()));
                }
                return;
            }
            const bool movement = itemIndex(*page, movementKey) == row;
            const bool combat = itemIndex(*page, combatKey) == row;
            if (movement || combat) {
                if ((accept || direction) && (!back || !*back)) {
                    bool& value = movement ? movementSelected : combatSelected;
                    if (WritePrivateProfileStringW(movement ? L"Movement" : L"Combat",
                        movement ? L"NikoOnFoot" : L"Enabled", value ? L"0" : L"1", config.c_str()))
                    {
                        value = !value;
                        if (movement) setNikoMovementEnabled(value);
                    } else event("gameplay_setting_write_failed", -1, -1, std::to_string(GetLastError()));
                }
                return;
            }
        }
        if (page && row >= 0 && itemIndex(*page) == row) {
            if (!externalIzzy && (accept || direction) && (!back || !*back)) {
                const int step = direction < 0 ? 2 : 1;
                auto next = Mode((int(selected) + step) % 3);
                if (next == Mode::Izzy && GetFileAttributesW(izzy.c_str()) == INVALID_FILE_ATTRIBUTES)
                    next = Mode((int(next) + step) % 3);
                const wchar_t* value = next == Mode::Iv ? L"IV" : next == Mode::Izzy ? L"Izzy" : L"Classic";
                if (WritePrivateProfileStringW(L"VehicleLab", L"Mode", value, config.c_str())) {
                    selected = next;
                    if (active != Mode::Izzy && selected != Mode::Izzy) {
                        active = selected;
                        setTrafficDriving(active == Mode::Iv);
                    }
                } else {
                    event("vehicle_driving_setting_write_failed", -1, -1, std::to_string(GetLastError()));
                }
            }
            return;
        }
    }
    nativeProcess(manager, direction, back, accept);
}

static bool install(const std::filesystem::path& ini) {
    constexpr unsigned char processCode[] = {0x8B, 0x44, 0x24, 0x0C, 0x53, 0x8B, 0x5C, 0x24, 0x08};
    constexpr unsigned char callCode[] = {0x57, 0xE8, 0xD8, 0x62, 0x12, 0x00, 0x8B, 0xD8};
    auto* entry = reinterpret_cast<void*>(processEntry);
    auto* call = reinterpret_cast<unsigned char*>(labelCall);
    if (std::memcmp(entry, processCode, sizeof(processCode)) != 0 ||
        std::memcmp(call - 1, callCode, sizeof(callCode)) != 0) return false;
    config = ini;
    nativeText = reinterpret_cast<decltype(nativeText)>(0x6A0050);
    if (MH_CreateHook(entry, reinterpret_cast<void*>(&process), reinterpret_cast<void**>(&nativeProcess)) != MH_OK)
        return false;
    DWORD protection = 0;
    if (!VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &protection)) {
        MH_RemoveHook(entry);
        return false;
    }
    bool patched = false;
    {
        const std::array ranges{CodeRange{labelCall, labelCall + 5}};
        FrozenThreads frozen(ranges);
        if (frozen.ok() && MH_QueueEnableHook(entry) == MH_OK) {
            const auto offset = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&text) - (labelCall + 5));
            std::memcpy(call + 1, &offset, 4);
            FlushInstructionCache(GetCurrentProcess(), call, 5);
            patched = true;
        }
    }
    DWORD unused = 0;
    VirtualProtect(call + 1, 4, protection, &unused);
    if (!patched) MH_RemoveHook(entry);
    return patched;
}
}
