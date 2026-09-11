#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winioctl.h>
#include <mmsystem.h>  // ← Tambahan untuk suara

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "winmm.lib")  // ← Tambahan untuk suara

#define MBR_SIZE 512
#define NAMA_FILE_SALINAN L"MBR-ASLI.BIN"

// ==============================================
// FUNGSI: PUTAR SUARA NOTIFIKASI
// ==============================================
void putarSuaraNotifikasi() {
    WCHAR jalurSuara[MAX_PATH];
    GetWindowsDirectoryW(jalurSuara, MAX_PATH);
    wcscat_s(jalurSuara, MAX_PATH, L"\\Media\\Windows Notify.wav");
    PlaySoundW(jalurSuara, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

// ==============================================
// TAMPILKAN PESAN KE PENGGUNA
// ==============================================
void pesan(const wchar_t* teks) {
    putarSuaraNotifikasi();  // ← Bunyikan suara setiap kali muncul pesan!
    MessageBoxW(NULL, teks, L"MBR Guard", MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

// ==============================================
// PERINGATAN AWAL SEBELUM BERJALAN
// ==============================================
void peringatanAwal() {
    pesan(
        L"WARNING!!\n"
        L"This file has been created just to protect the MBR from virus attacks like MEMZ\n"
        L"and also this script is still in Beta so please do this in a VM or vbox"
    );
}

// ==============================================
// DAPATKAN FOLDER TEMPAT PROGRAM BERADA
// ==============================================
void dapatkanFolderSendiri(WCHAR* jalur, DWORD ukuran) {
    GetModuleFileNameW(NULL, jalur, ukuran);
    WCHAR* pemisah = wcsrchr(jalur, L'\\');
    if (pemisah != NULL) {
        *(pemisah + 1) = L'\0';
    }
}

// ==============================================
// BACA MBR DARI HARDDISK FISIK
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

// ==============================================
// TULIS KEMBALI MBR KE HARDDISK
// ==============================================
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
// SIMPAN SALINAN MBR ASLI DI FOLDER INI
// ==============================================
int simpanSalinan(BYTE* buffer) {
    WCHAR jalurPenuh[MAX_PATH];
    dapatkanFolderSendiri(jalurPenuh, MAX_PATH);
    wcscat_s(jalurPenuh, MAX_PATH, NAMA_FILE_SALINAN);
    
    HANDLE berkas = CreateFileW(jalurPenuh, GENERIC_WRITE, 
        0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (berkas == INVALID_HANDLE_VALUE) return 0;
    
    DWORD ditulis;
    BOOL berhasil = WriteFile(berkas, buffer, MBR_SIZE, &ditulis, NULL);
    CloseHandle(berkas);
    
    return (berhasil && ditulis == MBR_SIZE);
}

// ==============================================
// MUAT SALINAN MBR ASLI DARI FILE DI FOLDER INI
// ==============================================
int muatSalinan(BYTE* buffer) {
    WCHAR jalurPenuh[MAX_PATH];
    dapatkanFolderSendiri(jalurPenuh, MAX_PATH);
    wcscat_s(jalurPenuh, MAX_PATH, NAMA_FILE_SALINAN);
    
    HANDLE berkas = CreateFileW(jalurPenuh, GENERIC_READ, 
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (berkas == INVALID_HANDLE_VALUE) return 0;
    
    DWORD dibaca;
    BOOL berhasil = ReadFile(berkas, buffer, MBR_SIZE, &dibaca, NULL);
    CloseHandle(berkas);
    
    return (berhasil && dibaca == MBR_SIZE);
}

// ==============================================
// CEK APAKAH MBR MASIH VALID (ADA TANDA 55 AA DI AKHIR)
// ==============================================
int mbrValid(BYTE* buffer) {
    return (buffer[510] == 0x55 && buffer[511] == 0xAA);
}

// ==============================================
// BANDINGKAN APAKAH ISI MBR SAMA PERSIS
// ==============================================
int samaMBR(BYTE* a, BYTE* b) {
    return (memcmp(a, b, MBR_SIZE) == 0);
}

// ==============================================
// CEK APAKAH PERUBAHAN DARI WINDOWS SENDIRI (AMAN)
// ==============================================
int perubahanDariWindows(BYTE* asli, BYTE* sekarang) {
    if (!mbrValid(sekarang)) return 0;
    return (memcmp(asli, sekarang, 446) == 0);
}

// ==============================================
// FUNGSI UTAMA
// ==============================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nCmdShow) {
    BYTE salinanAsli[MBR_SIZE];
    BYTE mbrSekarang[MBR_SIZE];
    
    // TAMPILKAN PERINGATAN AWAL
    peringatanAwal();
    
    // MUAT CADANGAN ATAU BUAT BARU
    if (!muatSalinan(salinanAsli)) {
        if (!bacaMBR(salinanAsli)) {
            pesan(L"FAILED to read MBR!\nRun this program as ADMINISTRATOR!");
            return 1;
        }
        if (!simpanSalinan(salinanAsli)) {
            pesan(L"FAILED to save backup!\nRun this program as ADMINISTRATOR!");
            return 1;
        }
        pesan(L"✅ Backup MBR saved successfully!\nFile saved in same folder as this program.\nNow watching MBR every 5 seconds...");
    }
    
    // VALIDASI SALINAN YANG DIMUAT
    if (!mbrValid(salinanAsli)) {
        pesan(L"⚠️ Backup file is corrupted!\nDelete MBR-ASLI.BIN and run this program again.");
        return 1;
    }
    
    // PENGAWASAN TERUS-MENERUS SETIAP 5 DETIK
    while (1) {
        if (!bacaMBR(mbrSekarang)) break;
        
        // ⚠️ TANDA MBR RUSAK = SERANGAN TERJADI!
        if (!mbrValid(mbrSekarang)) {
            pesan(L"⚠️ ALERT: MBR SIGNATURE DESTROYED!\nRestoring original MBR NOW...");
            
            // KOSONGKAN DULU → HAPUS JEJAK VIRUS
            BYTE kosong[MBR_SIZE] = {0};
            tulisMBR(kosong);
            Sleep(300);
            
            // PULIHKAN MBR ASLI DALAM 0,3 DETIK!
            tulisMBR(salinanAsli);
            
            pesan(L"✅ MBR RESTORED SUCCESSFULLY!\n\nWindows Defender will catch the virus.\nPlease run FULL SCAN now!");
            break;
        }
        
        // ⚠️ ISI MBR BERUBAH DARI ASLINYA
        if (!samaMBR(salinanAsli, mbrSekarang)) {
            if (perubahanDariWindows(salinanAsli, mbrSekarang)) {
                // Perubahan aman dari Windows → perbarui salinan
                memcpy(salinanAsli, mbrSekarang, MBR_SIZE);
                simpanSalinan(salinanAsli);
                Sleep(5000);
                continue;
            }
            
            // ⚠️ TERDETEKSI SERANGAN VIRUS! PULIHKAN SEKARANG JUGA!
            pesan(L"⚠️ ALERT: MBR HAS BEEN MODIFIED!\nRestoring original MBR...");
            Sleep(1000);
            
            // KOSONGKAN → PULIHKAN → SELESAI!
            BYTE kosong[MBR_SIZE] = {0};
            tulisMBR(kosong);
            Sleep(300);
            tulisMBR(salinanAsli);
            
            pesan(L"✅ MBR RESTORED SUCCESSFULLY!\n\nWindows Defender will catch the virus.\nPlease run FULL SCAN now!");
            break;
        }
        
        // TUNGGU 5 DETIK LAGI
        Sleep(5000);
    }
    return 0;
}
