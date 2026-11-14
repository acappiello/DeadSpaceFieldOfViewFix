#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*
 * Dead Space (2008, Steam) FOV Fix for Linux / Proton
 *
 * Supports two installation modes using the same binary:
 *   1. Alongside Dead Space Mouse Fix:
 *      <game_dir>/dinput8_Remote/DeadSpaceFovFix.dll
 *      <game_dir>/dinput8_Remote/DeadSpaceFovFix.ini
 *   2. Standalone as dinput8.dll (without Dead Space Mouse Fix):
 *      <game_dir>/dinput8.dll
 *      <game_dir>/DeadSpaceFovFix.ini
 *      (forwards DirectInput8Create to the system dinput8.dll)
 *
 * Hooks Camera_SetPerspective (0x006FB570) at 0x006FB581:
 *   movss dword ptr [edi+0x14C], xmm0   (F3 0F 11 87 4C 01 00 00)
 * and adds g_fov_add (in radians, horizontal FOV) to the game-computed FOV,
 * matching Widescreen Fixer's exact behavior.
 */

#define TARGET_FUNC_ADDR 0x006FB570u
#define HOOK_SITE_ADDR   0x006FB581u
#define HOOK_SIZE        8u

#define POLL_SIG_INTERVAL_MS    50u
#define POLL_SIG_TIMEOUT_MS  30000u
#define POLL_CFG_INTERVAL_MS   250u

/*
 * 25-byte signature starting at 0x006FB570 (17-byte prologue + 8-byte movss):
 *   006FB570: 55                         push ebp
 *   006FB571: 8B EC                      mov  ebp, esp
 *   006FB573: 83 E4 F0                   and  esp, 0xFFFFFFF0
 *   006FB576: F3 0F 10 45 08             movss xmm0, dword ptr [ebp+0x8]
 *   006FB57B: 81 EC 8C 00 00 00          sub  esp, 0x8C
 *   006FB581: F3 0F 11 87 4C 01 00 00    movss dword ptr [edi+0x14C], xmm0
 */
static const uint8_t EXPECTED_SIG[25] = {
    0x55,
    0x8B, 0xEC,
    0x83, 0xE4, 0xF0,
    0xF3, 0x0F, 0x10, 0x45, 0x08,
    0x81, 0xEC, 0x8C, 0x00, 0x00, 0x00,
    0xF3, 0x0F, 0x11, 0x87, 0x4C, 0x01, 0x00, 0x00
};

static const uint8_t ORIGINAL_HOOK_BYTES[HOOK_SIZE] = {
    0xF3, 0x0F, 0x11, 0x87, 0x4C, 0x01, 0x00, 0x00
};

/*
 * Global FOV offset (radians) read by the assembly hook thunk.
 * 4-byte aligned float writes are atomic on x86.
 */
volatile float g_fov_add __attribute__((used, aligned(4))) = 0.0f;

static HMODULE g_hModule = NULL;
static HMODULE g_hRealDinput8 = NULL;
static HANDLE  g_hThread = NULL;
static volatile LONG g_stop_thread = 0;
static volatile LONG g_hook_installed = 0;

typedef HRESULT (WINAPI *PFN_DirectInput8Create)(
    HINSTANCE hinst,
    DWORD dwVersion,
    const void *riidltf,
    LPVOID *ppvOut,
    LPVOID punkOuter
);
static PFN_DirectInput8Create g_pfnDirectInput8Create = NULL;

static WCHAR g_config_path[MAX_PATH];
static WCHAR g_log_path[MAX_PATH];

/*
 * Hook thunk called from 0x006FB581.
 * Preserves xmm0, edi, all GPRs, and EFLAGS, and leaves the x87 stack balanced.
 */
void __attribute__((naked, used)) fov_hook_thunk(void)
{
    __asm__ volatile (
        "movss dword ptr [edi + 0x14C], xmm0\n\t"
        "fld   dword ptr [edi + 0x14C]\n\t"
        "fadd  dword ptr [_g_fov_add]\n\t"
        "fstp  dword ptr [edi + 0x14C]\n\t"
        "ret\n\t"
    );
}

static void log_msg(const char *fmt, ...)
{
    if (g_log_path[0] == L'\0') {
        return;
    }

    HANDLE hFile = CreateFileW(
        g_log_path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    char buf[512];
    int prefix_len = snprintf(
        buf, sizeof(buf),
        "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds
    );
    if (prefix_len < 0 || (size_t)prefix_len >= sizeof(buf)) {
        CloseHandle(hFile);
        return;
    }

    va_list args;
    va_start(args, fmt);
    int body_len = vsnprintf(buf + prefix_len, sizeof(buf) - (size_t)prefix_len, fmt, args);
    va_end(args);

    if (body_len > 0) {
        size_t total = (size_t)prefix_len + (size_t)body_len;
        if (total >= sizeof(buf)) {
            total = sizeof(buf) - 1;
        }
        DWORD written = 0;
        WriteFile(hFile, buf, (DWORD)total, &written, NULL);
    }

    CloseHandle(hFile);
}

static void init_paths(void)
{
    WCHAR mod_path[MAX_PATH];
    DWORD len = GetModuleFileNameW(g_hModule, mod_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return;
    }

    /* Find last path separator. */
    WCHAR *last_sep = NULL;
    for (DWORD i = 0; i < len; ++i) {
        if (mod_path[i] == L'\\' || mod_path[i] == L'/') {
            last_sep = &mod_path[i];
        }
    }
    if (last_sep != NULL) {
        *(last_sep + 1) = L'\0';
    } else {
        mod_path[0] = L'\0';
    }

    wcsncpy(g_config_path, mod_path, MAX_PATH - 1);
    g_config_path[MAX_PATH - 1] = L'\0';
    wcsncat(g_config_path, L"DeadSpaceFovFix.ini", MAX_PATH - wcslen(g_config_path) - 1);

    wcsncpy(g_log_path, mod_path, MAX_PATH - 1);
    g_log_path[MAX_PATH - 1] = L'\0';
    wcsncat(g_log_path, L"DeadSpaceFovFix.log", MAX_PATH - wcslen(g_log_path) - 1);

    /* Truncate log file on fresh startup. */
    HANDLE hFile = CreateFileW(
        g_log_path,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }
}

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char to_lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static int str_eq_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        if (to_lower_ascii(*a) != to_lower_ascii(*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * Locale-independent float parser (supports optional +/-, digits, and '.' or ',').
 */
static int parse_float_value(const char *s, float *out_val)
{
    while (is_space(*s)) {
        ++s;
    }
    if (*s == '\0') {
        return 0;
    }

    float sign = 1.0f;
    if (*s == '-') {
        sign = -1.0f;
        ++s;
    } else if (*s == '+') {
        ++s;
    }

    int has_digits = 0;
    float result = 0.0f;
    while (*s >= '0' && *s <= '9') {
        has_digits = 1;
        result = result * 10.0f + (float)(*s - '0');
        ++s;
    }

    if (*s == '.' || *s == ',') {
        ++s;
        float scale = 0.1f;
        while (*s >= '0' && *s <= '9') {
            has_digits = 1;
            result += (float)(*s - '0') * scale;
            scale *= 0.1f;
            ++s;
        }
    }

    /* Optional trailing 'f' or 'F' and whitespace/comments. */
    if (*s == 'f' || *s == 'F') {
        ++s;
    }
    while (is_space(*s)) {
        ++s;
    }
    if (*s != '\0' && *s != '#' && *s != ';') {
        return 0;
    }
    if (!has_digits) {
        return 0;
    }

    *out_val = sign * result;
    return 1;
}

static int load_config(float *out_fov_add)
{
    HANDLE hFile = CreateFileW(
        g_config_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    char buf[4096];
    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, buf, sizeof(buf) - 1, &bytes_read, NULL);
    CloseHandle(hFile);
    if (!ok) {
        return 0;
    }
    buf[bytes_read] = '\0';

    int found = 0;
    char *line = buf;
    while (*line != '\0') {
        char *next = line;
        while (*next != '\0' && *next != '\n') {
            ++next;
        }
        if (*next == '\n') {
            *next = '\0';
            ++next;
        }

        /* Trim leading whitespace. */
        while (is_space(*line)) {
            ++line;
        }
        /* Skip empty lines, comments, and section headers. */
        if (*line == '\0' || *line == '#' || *line == ';' || *line == '[') {
            line = next;
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;

            /* Trim trailing whitespace on key. */
            char *key_end = eq - 1;
            while (key_end >= key && is_space(*key_end)) {
                *key_end = '\0';
                --key_end;
            }

            if (str_eq_nocase(key, "fov_add")) {
                float parsed = 0.0f;
                if (parse_float_value(val, &parsed)) {
                    *out_fov_add = parsed;
                    found = 1;
                }
            }
        }

        line = next;
    }

    return found;
}

static int is_signature_ready(void)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)TARGET_FUNC_ADDR, &mbi, sizeof(mbi)) != sizeof(mbi)) {
        return 0;
    }
    if (mbi.State != MEM_COMMIT) {
        return 0;
    }
    const DWORD readable_mask =
        PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
        PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if ((mbi.Protect & readable_mask) == 0 || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return 0;
    }

    return memcmp((const void *)TARGET_FUNC_ADDR, EXPECTED_SIG, sizeof(EXPECTED_SIG)) == 0;
}

static int install_hook(void)
{
    uint8_t patch[HOOK_SIZE];
    uintptr_t hook_addr  = (uintptr_t)HOOK_SITE_ADDR;
    uintptr_t thunk_addr = (uintptr_t)&fov_hook_thunk;
    int32_t rel32 = (int32_t)(thunk_addr - (hook_addr + 5u));

    patch[0] = 0xE8; /* call rel32 */
    memcpy(&patch[1], &rel32, sizeof(rel32));
    patch[5] = 0x90; /* nop */
    patch[6] = 0x90; /* nop */
    patch[7] = 0x90; /* nop */

    DWORD old_protect = 0;
    if (!VirtualProtect((LPVOID)HOOK_SITE_ADDR, HOOK_SIZE, PAGE_EXECUTE_READWRITE, &old_protect)) {
        log_msg("ERROR: VirtualProtect failed at 0x%08X (err=%lu)\n",
                HOOK_SITE_ADDR, GetLastError());
        return 0;
    }

    memcpy((void *)HOOK_SITE_ADDR, patch, HOOK_SIZE);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)HOOK_SITE_ADDR, HOOK_SIZE);

    DWORD dummy = 0;
    VirtualProtect((LPVOID)HOOK_SITE_ADDR, HOOK_SIZE, old_protect, &dummy);

    InterlockedExchange(&g_hook_installed, 1);
    log_msg("Installed FOV hook at 0x%08X -> thunk 0x%08X (rel32=0x%08X)\n",
            (unsigned)hook_addr, (unsigned)thunk_addr, (unsigned)rel32);
    return 1;
}

static void remove_hook(void)
{
    if (InterlockedCompareExchange(&g_hook_installed, 0, 1) != 1) {
        return;
    }

    DWORD old_protect = 0;
    if (VirtualProtect((LPVOID)HOOK_SITE_ADDR, HOOK_SIZE, PAGE_EXECUTE_READWRITE, &old_protect)) {
        memcpy((void *)HOOK_SITE_ADDR, ORIGINAL_HOOK_BYTES, HOOK_SIZE);
        FlushInstructionCache(GetCurrentProcess(), (LPCVOID)HOOK_SITE_ADDR, HOOK_SIZE);
        DWORD dummy = 0;
        VirtualProtect((LPVOID)HOOK_SITE_ADDR, HOOK_SIZE, old_protect, &dummy);
        log_msg("Restored original bytes at 0x%08X\n", HOOK_SITE_ADDR);
    }
}

static int get_config_stamp(FILETIME *out_mtime, DWORD *out_size)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(g_config_path, GetFileExInfoStandard, &fad)) {
        return 0;
    }
    *out_mtime = fad.ftLastWriteTime;
    *out_size  = fad.nFileSizeLow;
    return 1;
}

/*
 * Exported DirectInput8Create proxy for standalone mode (when installed directly
 * as <game_dir>/dinput8.dll without Dead Space Mouse Fix).
 */
HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD dwVersion,
    const void *riidltf,
    LPVOID *ppvOut,
    LPVOID punkOuter)
{
    if (g_pfnDirectInput8Create == NULL) {
        WCHAR sys_path[MAX_PATH];
        UINT len = GetSystemDirectoryW(sys_path, MAX_PATH);
        if (len == 0 || len + 13 >= MAX_PATH) {
            log_msg("ERROR: GetSystemDirectoryW failed in DirectInput8Create\n");
            return E_FAIL;
        }
        if (sys_path[len - 1] != L'\\' && sys_path[len - 1] != L'/') {
            wcsncat(sys_path, L"\\", MAX_PATH - wcslen(sys_path) - 1);
        }
        wcsncat(sys_path, L"dinput8.dll", MAX_PATH - wcslen(sys_path) - 1);

        g_hRealDinput8 = LoadLibraryW(sys_path);
        if (g_hRealDinput8 == NULL) {
            log_msg("ERROR: Failed to load system dinput8.dll (err=%lu)\n", GetLastError());
            return E_FAIL;
        }

        g_pfnDirectInput8Create = (PFN_DirectInput8Create)(uintptr_t)
            GetProcAddress(g_hRealDinput8, "DirectInput8Create");
        if (g_pfnDirectInput8Create == NULL) {
            log_msg("ERROR: GetProcAddress(DirectInput8Create) failed (err=%lu)\n", GetLastError());
            return E_FAIL;
        }
        log_msg("Standalone proxy: forwarding DirectInput8Create to system dinput8.dll (0x%08X)\n",
                (unsigned)(uintptr_t)g_hRealDinput8);
    }

    return g_pfnDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

static DWORD WINAPI worker_thread(LPVOID lpParam)
{
    (void)lpParam;
    log_msg("DeadSpaceFovFix loaded (module base=0x%08X)\n", (unsigned)(uintptr_t)g_hModule);

    /* Load initial config value before installing the hook. */
    FILETIME last_mtime = {0, 0};
    DWORD last_size = 0;
    int had_config = get_config_stamp(&last_mtime, &last_size);

    float initial_fov = 0.0f;
    if (load_config(&initial_fov)) {
        g_fov_add = initial_fov;
        log_msg("Loaded initial config: fov_add = %.4f rad (%.2f deg)\n",
                (double)g_fov_add, (double)(g_fov_add * 57.2957795f));
    } else {
        log_msg("Config file not found or invalid; defaulting to fov_add = 0.0\n");
    }

    /* Wait for SteamStub to finish unpacking .text in memory. */
    DWORD waited_ms = 0;
    while (!InterlockedCompareExchange(&g_stop_thread, 0, 0)) {
        if (is_signature_ready()) {
            break;
        }
        if (waited_ms >= POLL_SIG_TIMEOUT_MS) {
            log_msg("ERROR: Timed out after %lu ms waiting for signature at 0x%08X\n",
                    (unsigned long)waited_ms, TARGET_FUNC_ADDR);
            return 1;
        }
        Sleep(POLL_SIG_INTERVAL_MS);
        waited_ms += POLL_SIG_INTERVAL_MS;
    }

    if (InterlockedCompareExchange(&g_stop_thread, 0, 0)) {
        return 0;
    }

    log_msg("Matched Camera_SetPerspective signature at 0x%08X after %lu ms\n",
            TARGET_FUNC_ADDR, (unsigned long)waited_ms);

    if (!install_hook()) {
        return 1;
    }

    /* Watch DeadSpaceFovFix.ini for live changes. */
    while (!InterlockedCompareExchange(&g_stop_thread, 0, 0)) {
        Sleep(POLL_CFG_INTERVAL_MS);

        FILETIME cur_mtime = {0, 0};
        DWORD cur_size = 0;
        if (!get_config_stamp(&cur_mtime, &cur_size)) {
            continue;
        }

        if (!had_config ||
            CompareFileTime(&cur_mtime, &last_mtime) != 0 ||
            cur_size != last_size) {
            /* Brief delay in case the editor is mid-write. */
            Sleep(20);
            get_config_stamp(&cur_mtime, &cur_size);

            float new_fov = 0.0f;
            if (load_config(&new_fov)) {
                last_mtime = cur_mtime;
                last_size  = cur_size;
                had_config = 1;
                if (new_fov != g_fov_add) {
                    g_fov_add = new_fov;
                    log_msg("Reloaded config: fov_add = %.4f rad (%.2f deg)\n",
                            (double)g_fov_add, (double)(g_fov_add * 57.2957795f));
                }
            }
        }
    }

    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        init_paths();
        g_hThread = CreateThread(NULL, 0, worker_thread, NULL, 0, NULL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop_thread, 1);
        if (lpvReserved == NULL) {
            /* Dynamic unload: wait briefly for worker thread and restore original code. */
            if (g_hThread != NULL) {
                WaitForSingleObject(g_hThread, 500);
                CloseHandle(g_hThread);
                g_hThread = NULL;
            }
            remove_hook();
            if (g_hRealDinput8 != NULL) {
                FreeLibrary(g_hRealDinput8);
                g_hRealDinput8 = NULL;
                g_pfnDirectInput8Create = NULL;
            }
        }
    }
    return TRUE;
}
