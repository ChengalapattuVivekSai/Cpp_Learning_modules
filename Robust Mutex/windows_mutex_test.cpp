
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cwchar>

constexpr wchar_t MUTEX_NAME[] = L"Local\\VickyNormalMutexTest";
constexpr wchar_t SHM_NAME[]   = L"Local\\VickySharedMemoryTest";

struct SharedData {
    int value;
};

void print_error(const wchar_t* operation) {
    fwprintf(stderr, L"%ls failed. Error = %lu\n",
             operation, GetLastError());
}

bool launch_child(const wchar_t* mode, PROCESS_INFORMATION& pi) {
    wchar_t exe[MAX_PATH];

    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        print_error(L"GetModuleFileNameW");
        return false;
    }

    wchar_t command[MAX_PATH + 64];
    swprintf_s(command, MAX_PATH + 64, L"\"%ls\" %ls", exe, mode);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    pi = {};

    if (!CreateProcessW(
            nullptr, command, nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &si, &pi)) {
        print_error(L"CreateProcessW");
        return false;
    }

    return true;
}

int run_writer() {
    HANDLE mutex = OpenMutexW(
        SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, MUTEX_NAME);

    HANDLE mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);

    if (!mutex || !mapping) {
        print_error(L"Open named objects");
        if (mutex) CloseHandle(mutex);
        if (mapping) CloseHandle(mapping);
        return 1;
    }

    auto* data = static_cast<SharedData*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));

    if (!data) {
        print_error(L"MapViewOfFile");
        CloseHandle(mapping);
        CloseHandle(mutex);
        return 1;
    }

    DWORD result = WaitForSingleObject(mutex, INFINITE);

    if (result != WAIT_OBJECT_0) {
        fwprintf(stderr, L"Writer could not acquire mutex: %lu\n",
                 result);
        UnmapViewOfFile(data);
        CloseHandle(mapping);
        CloseHandle(mutex);
        return 1;
    }

    wprintf(L"P1: Mutex acquired.\n");
    data->value = 100;
    wprintf(L"P1: Wrote shared value = %d\n", data->value);
    wprintf(L"P1: Exiting WITHOUT releasing mutex.\n");
    fflush(stdout);

    // Deliberately terminate this process while owning the mutex.
    // Windows will mark the mutex abandoned when its owner thread exits.
    ExitProcess(0);
}

int run_reader() {
    HANDLE mutex = OpenMutexW(
        SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, MUTEX_NAME);

    HANDLE mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME);

    if (!mutex || !mapping) {
        print_error(L"Open named objects");
        if (mutex) CloseHandle(mutex);
        if (mapping) CloseHandle(mapping);
        return 1;
    }

    auto* data = static_cast<SharedData*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));

    if (!data) {
        print_error(L"MapViewOfFile");
        CloseHandle(mapping);
        CloseHandle(mutex);
        return 1;
    }

    wprintf(L"P2: Trying to acquire mutex...\n");
    fflush(stdout);

    DWORD result = WaitForSingleObject(mutex, 5000);

    if (result == WAIT_ABANDONED) {
        wprintf(L"P2: WAIT_ABANDONED received!\n");
        wprintf(L"P2: Mutex acquired, but previous owner died.\n");
        wprintf(L"P2: Shared value = %d\n", data->value);
        wprintf(L"P2: Application must check/repair shared data.\n");

        // WAIT_ABANDONED also grants ownership, so release it.
        ReleaseMutex(mutex);
    } else if (result == WAIT_OBJECT_0) {
        wprintf(L"P2: Mutex acquired normally.\n");
        wprintf(L"P2: Shared value = %d\n", data->value);
        ReleaseMutex(mutex);
    } else if (result == WAIT_TIMEOUT) {
        wprintf(L"P2: Timed out waiting for mutex.\n");
    } else {
        print_error(L"WaitForSingleObject");
    }

    UnmapViewOfFile(data);
    CloseHandle(mapping);
    CloseHandle(mutex);
    return 0;
}

int run_parent() {
    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(SharedData), SHM_NAME);

    if (!mapping) {
        print_error(L"CreateFileMappingW");
        return 1;
    }

    auto* data = static_cast<SharedData*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0));

    if (!data) {
        print_error(L"MapViewOfFile");
        CloseHandle(mapping);
        return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, MUTEX_NAME);

    if (!mutex) {
        print_error(L"CreateMutexW");
        UnmapViewOfFile(data);
        CloseHandle(mapping);
        return 1;
    }

    data->value = 0;
    wprintf(L"Parent: Shared memory initialized. value = 0\n");

    PROCESS_INFORMATION writer{};
    if (!launch_child(L"writer", writer)) {
        CloseHandle(mutex);
        UnmapViewOfFile(data);
        CloseHandle(mapping);
        return 1;
    }

    WaitForSingleObject(writer.hProcess, INFINITE);
    DWORD writer_exit_code = 0;
    GetExitCodeProcess(writer.hProcess, &writer_exit_code);

    wprintf(L"Parent: P1 exited with code %lu.\n", writer_exit_code);
    wprintf(L"Parent: Shared value = %d\n", data->value);

    CloseHandle(writer.hThread);
    CloseHandle(writer.hProcess);

    PROCESS_INFORMATION reader{};
    if (!launch_child(L"reader", reader)) {
        CloseHandle(mutex);
        UnmapViewOfFile(data);
        CloseHandle(mapping);
        return 1;
    }

    WaitForSingleObject(reader.hProcess, INFINITE);
    CloseHandle(reader.hThread);
    CloseHandle(reader.hProcess);

    CloseHandle(mutex);
    UnmapViewOfFile(data);
    CloseHandle(mapping);

    wprintf(L"Parent: Experiment completed.\n");
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && wcscmp(argv[1], L"writer") == 0)
        return run_writer();

    if (argc == 2 && wcscmp(argv[1], L"reader") == 0)
        return run_reader();

    return run_parent();
}
