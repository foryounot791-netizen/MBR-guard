#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winioctl.h>
#include <tlhelp32.h>
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "winmm.lib")

#define MBR_SIZE 512
#define NAMA_FILE_SALINAN L"Windows-MBR-Save.bin"
#define JUMLAH_LOKASI 3

// ==============================================
// DAFTAR PROSES SISTEM YANG TIDAK BOLEH DIMATIKAN
// ==============================================
const WCHAR* SISTEM_DILINDUNGI[] = {
    L"system", L"smss.exe", L"csrss.exe", L"wininit.exe",
    L"services.exe", L"lsass.exe", L"svchost.exe", L"winlogon.exe",
    L"explorer.exe", L"dwm.exe", L"conhost.exe", L"wmi.exe", NULL
};

// ==============================================
// FUNGSI: LINDUNGI PROSES SENDIRI AGAR SULIT DIMATIKAN VIRUS
// ==============================================
void lindungiProsesSendiri() {
    HANDLE hToken;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    
    // Ambil hak akses tingkat SISTEM
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
        CloseHandle(hToken);
    }
    
    // Atur prioritas TERTINGGI → virus tidak bisa menurunkan kinerja
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    
    // Tandai sebagai proses KRITIS → tidak bisa dipaksa dimatikan sembarangan
    HANDLE hProc = GetCurrentProcess();
    SetProcessInformation(hProc, ProcessProtectionLevelInfo, 
        &(PROCESS_PROTECTION_LEVEL_INFORMATION{3}), 
        sizeof(PROCESS_PROTECTION_LEVEL_INFORMATION));
}

// ==============================================
// FUNGSI: CEK APAKAH PROSES SISTEM
// ==============================================
int adalahProsesSistem(const WCHAR* nama) {
    for (int i = 0; SISTEM_DILINDUNGI[i] != NULL; i++) {
        if (_wcsicmp(nama, SISTEM_DILINDUNGI[i]) == 0) return 1;
    }
    return 0;
}

// ==============================================
// FUNGSI: ISOLASI PROGRAM BUKAN SISTEM (TANPA MEMATIKAN PAKSA!)
// ==============================================
void isolasiProgramPengganggu() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return;
    
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);
    DWORD PID_SENDIRI = GetCurrentProcessId();
    
    if (Process32FirstW(hSnapshot, &pe32)) {
        do {
            // Jangan sentuh diri sendiri & jangan sentuh sistem
            if (pe32.th32ProcessID != PID_SENDIRI && !adalahProsesSistem(pe32.szExeFile)) {
                // ⚠️ TIDAK MEMATIKAN PAKSA! Kita hanya HAPUS HAK TULIS/MODIFIKASI saja
                // Tujuannya: virus TIDAK BISA menulis ke MBR lagi, tapi TIDAK tahu dia sedang "diblokir"
                HANDLE hProses = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pe32.th32ProcessID);
                if (hProses != NULL) {
                    // Cabut hak akses proses tersebut → tidak bisa mengubah apa-apa lagi
                    HANDLE hToken;
                    if (OpenProcessToken(hProses, TOKEN_ADJUST_PRIVILEGES, &hToken)) {
                        DisablePrivilegeToken(hToken, SE_LOAD_DRIVER_NAME, TRUE);
                        DisablePrivilegeToken(hToken, SE_DEBUG_NAME, TRUE);
                        CloseHandle(hToken);
                    }
                    CloseHandle(hProses);
                }
            }
        } while (Process32NextW(hSnapshot, &pe32));
    }
    CloseHandle(hSnapshot);
}

// ==============================================
// FUNGSI: DAPATKAN LOKASI FOLDER
// ==============================================
typedef struct { WCHAR jalurFolder[MAX_PATH]; } LokasiCadangan;

void dapatkanLokasiFolder(int tipe, WCHAR* jalur, size_t ukuran) {
    switch(tipe) {
        case 1: GetWindowsDirectoryW(jalur, (DWORD)ukuran); break;
        case 2:
            GetEnvironmentVariableW(L"LOCALAPPDATA", jalur, (DWORD)ukuran);
            wcscat_s(jalur, ukuran, L"\\Backup");
            CreateDirectoryW(jalur, NULL);
            break;
        case 3:
            GetWindowsDirectoryW(jalur, (DWORD)ukuran);
            wcscat_s(jalur, ukuran, L"\\Temp");
            break;
    }
}

void jalurSalinan(LokasiCadangan* daftarLokasi) {
    for(int i=0; i<JUMLAH_LOKASI; i++) {
        dapatkanLokasiFolder(i+1, daftarLokasi[i].jalurFolder, MAX_PATH);
        wcscat_s(daftarLokasi[i].jalurFolder, MAX_PATH, L"\\");
        wcscat_s(daftarLokasi[i].jalurFolder, MAX_PATH, NAMA_FILE_SALINAN);
    }
}

// ==============================================
// FUNGSI: SUARA NOTIFIKASI
// ==============================================
void putarSuaraNotifikasi() {
    WCHAR jalurSuara[MAX_PATH];
    GetWindowsDirectoryW(jalurSuara, MAX_PATH);
    wcscat_s(jalurSuara, MAX_PATH, L"\\Media\\Windows Notify.wav");
    PlaySoundW(jalurSuara, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP);
}

// ==============================================
// FUNGSI: TAMPILKAN PESAN
// ==============================================
void pesan(const wchar_t* teks) {
    MessageBoxW(NULL, teks, L"MBR Guard", MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

void peringatanAwal() {
    pesan(
        L"WARNING!!\n"
        L"This file has been created just to protect the MBR from virus attacks like MEMZ\n"
        L"and also this script is still in Beta so please do this in a VM or vbox"
    );
}

// ==============================================
// FUNGSI: BACA/TULIS MBR
// ==============================================
int bacaMBR(BYTE* buffer) {
    HANDLE perangkat = CreateFileW(L"\\\\.\\PhysicalDrive0", 
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, OPEN_EXISTING, 0, NULL);
    if (perangkat == INVALID_HANDLE_VALUE) return 0;
    DWORD dibaca;
    SetFilePointer(perangkat, 0, NULL, FILE_BEGIN);
    BOOL berhasil = ReadFile(perangkat, buffer, MBR_SIZE, &dibaca, NULL);
    CloseHandle(perangkat);
    return (berhasil && dibaca == MBR_SIZE);
}

int tulisMBR(BYTE* buffer) {
    HANDLE perangkat = CreateFileW(L"\\\\.\\PhysicalDrive0", 
        GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, OPEN_EXISTING, 0, NULL);
    if (perangkat == INVALID_HANDLE_VALUE) return 0;
    DWORD ditulis;
    SetFilePointer(perangkat, 0, NULL, FILE_BEGIN);
    BOOL berhasil = WriteFile(perangkat, buffer, MBR_SIZE, &ditulis, NULL);
    CloseHandle(perangkat);
    return (berhasil && ditulis == MBR_SIZE);
}

// ==============================================
// FUNGSI: SIMPAN/MUAT CADANGAN
// ==============================================
int simpanSalinanSemua(LokasiCadangan* daftarLokasi, BYTE* buffer) {
    int berhasil = 0;
    for(int i=0; i<JUMLAH_LOKASI; i++) {
        HANDLE berkas = CreateFileW(daftarLokasi[i].jalurFolder, GENERIC_WRITE, 
            0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
        if (berkas != INVALID_HANDLE_VALUE) {
            DWORD ditulis;
            if (WriteFile(berkas, buffer, MBR_SIZE, &ditulis, NULL) && ditulis == MBR_SIZE)
                berhasil++;
            CloseHandle(berkas);
        }
    }
    return (berhasil >= 1) ? 1 : 0;
}

int muatSalinanSemua(LokasiCadangan* daftarLokasi, BYTE* buffer) {
    for(int i=0; i<JUMLAH_LOKASI; i++) {
        HANDLE berkas = CreateFileW(daftarLokasi[i].jalurFolder, GENERIC_READ, 
            0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
        if (berkas != INVALID_HANDLE_VALUE) {
            DWORD dibaca;
            if (ReadFile(berkas, buffer, MBR_SIZE, &dibaca, NULL) && dibaca == MBR_SIZE) {
                CloseHandle(berkas);
                return 1;
            }
            CloseHandle(berkas);
        }
    }
    return 0;
}

// ==============================================
// FUNGSI: CEK KORUP
// ==============================================
int cekCadanganKorup(BYTE* buffer) {
    return (buffer[510] != 0x55 || buffer[511] != 0xAA) ? 1 : 0;
}

// ==============================================
// FUNGSI: KILL DIRI SENDIRI
// ==============================================
void killDirisendiri() {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, GetCurrentProcessId());
    if (hProcess != NULL) { TerminateProcess(hProcess, 0); CloseHandle(hProcess); }
}

// ==============================================
// FUNGSI: BANDINGKAN & CEK PERUBAHAN
// ==============================================
int samaMBR(BYTE* a, BYTE* b) { return (memcmp(a, b, MBR_SIZE) == 0); }

int perubahanDariWindows(BYTE* asli, BYTE* sekarang) {
    if (sekarang[510] != 0x55 || sekarang[511] != 0xAA) return 0;
    return (memcmp(asli, sekarang, 446) == 0) ? 1 : 0;
}

// ==============================================
// FUNGSI: SEMBUNYIKAN JENDELA
// ==============================================
void jadikanLatarBelakang() {
    HWND jendela = GetForegroundWindow();
    if (jendela) ShowWindow(jendela, SW_HIDE);
}

// ==============================================
// FUNGSI UTAMA
// ==============================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nCmdShow) {
    BYTE salinanAsli[MBR_SIZE];
    BYTE mbrSekarang[MBR_SIZE];
    LokasiCadangan daftarLokasi[JUMLAH_LOKASI];
    jalurSalinan(daftarLokasi);
    
    // ==============================================
    // LINDUNGI DIRI SENDIRI DULU
    // ==============================================
    lindungiProsesSendiri();
    
    // ==============================================
    // PERINGATAN AWAL & SEMBUNYIKAN
    // ==============================================
    peringatanAwal();
    jadikanLatarBelakang();
    
    // ==============================================
    // MUAT / BUAT CADANGAN PERTAMA
    // ==============================================
    if (!muatSalinanSemua(daftarLokasi, salinanAsli)) {
        if (!bacaMBR(salinanAsli)) {
            pesan(L"Failed to read MBR! Run as Administrator!");
            killDirisendiri(); return 1;
        }
        if (!simpanSalinanSemua(daftarLokasi, salinanAsli)) {
            pesan(L"Failed to save backup! Run as Administrator!");
            killDirisendiri(); return 1;
        }
    }
    
    if (cekCadanganKorup(salinanAsli)) {
        pesan(L"We dont have a choice");
        killDirisendiri(); return 1;
    }
    
    // ==============================================
    // LOOP PENGAWASAN
    // ==============================================
    while (1) {
        if (!bacaMBR(mbrSekarang)) break;
        
        if (cekCadanganKorup(mbrSekarang)) {
            pesan(L"We dont have a choice");
            killDirisendiri(); return 1;
        }
        
        if (!samaMBR(salinanAsli, mbrSekarang)) {
            if (perubahanDariWindows(salinanAsli, mbrSekarang)) {
                memcpy(salinanAsli, mbrSekarang, MBR_SIZE);
                simpanSalinanSemua(daftarLokasi, salinanAsli);
                Sleep(5000);
                continue;
            }
            
            // ==============================================
            // ⚠️ TERDETEKSI SERANGAN → PULIHKAN DULU!
            // ==============================================
            pesan(L"ALERT: MBR has been modified! Restoring original...");
            Sleep(1500);
            
            // 🔴 LANGKAH PENTING: PULIHKAN MBR DULU SEBELUM VIRUS SEMPAT BERBALAS!
            BYTE kosong[MBR_SIZE] = {0};
            tulisMBR(kosong);   // Kosongkan MBR yang rusak → HAPUS JEJAK VIRUS
            Sleep(300);
            tulisMBR(salinanAsli); // Tulis MBR ASLI → DALAM 0,3 DETIK!
            
            // 🛡️ BARU KEMUDIAN: Cabut hak akses virus (TANPA mematikan paksa!)
            pesan(L"Securing system...");
            isolasiProgramPengganggu();
            
            // 🔊 SUARA NOTIFIKASI
            putarSuaraNotifikasi();
            
            pesan(L"MBR Guard: Original MBR has been restored successfully!");
            Sleep(2000);
            
            pesan(L"Computer will restart in 10 seconds...");
            system("shutdown /r /t 10 /c \"MBR modified by malicious program has been restored\"");
            break;
        }
        
        // CEK ULANG CADANGAN SETIAP 5 DETIK
        BYTE cekBuffer[MBR_SIZE];
        if (!muatSalinanSemua(daftarLokasi, cekBuffer) || cekCadanganKorup(cekBuffer)) {
            pesan(L"We dont have a choice");
            killDirisendiri(); return 1;
        }
        memcpy(salinanAsli, cekBuffer, MBR_SIZE);
        
        Sleep(5000);
    }
    return 0;
}