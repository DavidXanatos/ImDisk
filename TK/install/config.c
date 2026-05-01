#define _WIN32_WINNT 0x0600
#define OEMRESOURCE
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>
#include <setupapi.h>
#include <intshcut.h>
#include <ntsecapi.h>
#include "resource.h"
#include "..\inc\imdisk.h"
#include "..\inc\build.h"

#define HEADER_BACKGROUND RGB(255, 255, 255)

static OSVERSIONINFO os_ver;
static HINSTANCE hinst;
static HICON hIcon, hIconWarn;
static HBRUSH hBrush;
static RECT icon_coord, iconwarn_coord;
static HWND hwnd_check[6], hwnd_text1, hwnd_static1, hwnd_static2, hwnd_static3, hwnd_uninst;
static WPARAM prev_wparam = 0;
static int prev_mark = 0;
static _Bool copy_error = FALSE, reboot = FALSE, process_uninst = FALSE;
static DWORD hid_drive_ini;
static HANDLE h_file;
static HKEY reg_key;

static WCHAR path[MAX_PATH + 100] = {}, *path_name_ptr;
static WCHAR install_path[MAX_PATH], *path_cmdline = NULL;
static WCHAR path_prev[MAX_PATH + 30] = {};
static WCHAR desk[MAX_PATH + 100], *desk_ptr, desk_all[MAX_PATH + 100], *desk_all_ptr, startmenu[MAX_PATH + 100], *startmenu_ptr, startmenu_all[MAX_PATH + 100], *startmenu_all_ptr;
static WCHAR cmd[32768];
static WCHAR *file_list[] = {L"ImDisk-Dlg.exe", L"ImDiskTk-svc.exe", L"lang.txt", L"MountImg.exe", L"RamDiskUI.exe", L"RamDyn.exe", L"config.exe"};
static WCHAR *obsolete_list[] = {L"DiscUtils.Core.dll", L"DiscUtils.Dmg.dll", L"DiscUtils.Streams.dll", L"DiscUtils.Vdi.dll", L"DiscUtils.Vhd.dll", L"DiscUtils.Vhdx.dll", L"DiscUtils.Vmdk.dll", L"DiscUtils.Xva.dll",
								 L"DevioNet.dll", L"DiscUtilsDevio.exe", L"ImDiskNet.dll", L"DiscUtils.dll", L"ImDisk-UAC.exe", L"setup.exe"};

static WCHAR version_str[] = L"ImDisk Toolkit\n" APP_VERSION;

static char reg_disp_name_tk[] = "ImDisk Toolkit";
static char reg_disp_name_drv[] = "ImDisk Virtual Disk Driver";
static DWORD EstimatedSize = 1813;
static WCHAR *driver_svc_list[] = {L"ImDskSvc", L"DevIoDrv", L"AWEAlloc", L"ImDisk"};
static WCHAR *tk_svc_list[] = {L"ImDiskRD", L"ImDiskTk-svc", L"ImDiskImg"};

static WCHAR *lang_list[] = {L"english", L"deutsch", L"español", L"français", L"italiano", L"magyar", L"português brasileiro", L"русский", L"suomi", L"svenska", L"türkçe", L"简体中文", L"正體中文 (香港)", L"한국어"};
static WCHAR *lang_file_list[] = {L"english", L"german", L"spanish", L"french", L"italian", L"hungarian", L"brazilian-portuguese", L"russian", L"finnish", L"swedish", L"turkish", L"schinese", L"tchinese", L"korean"};
static int n_lang = 0;

static _Bool lang_cmdline = FALSE;
static int silent = 0;
static _Bool silent_uninstall = FALSE;
static WCHAR opt_cmd_mountimg = 0, opt_cmd_ramdiskui = 0, opt_cmd_menus = 0, opt_cmd_shortcutdesk = 0, opt_cmd_shortcutall = 0;

static GUID _CLSID_ShellLink = {0x00021401, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static GUID _CLSID_InternetShortcut = {0xfbf23b40, 0xe3f0, 0x101b, {0x84, 0x88, 0x00, 0xaa, 0x00, 0x3e, 0x56, 0xf8}};

enum {
	TITLE,
	TXT_1, TXT_2, TXT_3,
	COMP_0, COMP_1, COMP_2, COMP_3,
	OPT_0, OPT_1, OPT_2, OPT_3,
	LANG_TXT,
	DESC_0, DESC_1, DESC_2, DESC_3, DESC_4, DESC_5, DESC_6,
	CTRL_1, CTRL_2, CTRL_3, CTRL_4,
	ERR_1, ERR_2, ERR_3,
	PREV_TXT,
	FIN_1, FIN_2, FIN_3,
	CRED_0, CRED_1, CRED_2, CRED_3, TRANS_0, TRANS_1, TRANS_2, TRANS_3, TRANS_4, TRANS_5, TRANS_6, TRANS_7, TRANS_8, TRANS_9, TRANS_10, TRANS_11, TRANS_12, TRANS_MAX,
	SHORTCUT_1, SHORTCUT_2, SHORTCUT_3, SHORTCUT_4, SHORTCUT_5,
	CONTEXT_1, CONTEXT_2, CONTEXT_3,

	U_TITLE,
	U_CTRL_1, U_CTRL_2, U_CTRL_3, U_CTRL_4, U_CTRL_5,
	U_MSG_1, U_MSG_2, U_MSG_3, U_MSG_4,

	S_TITLE,
	S_CTRL_0, S_CTRL_1, S_CTRL_2, S_CTRL_3, S_CTRL_4, S_CTRL_5, S_CTRL_6, S_CTRL_7, S_CTRL_8, S_CTRL_9,

	NB_TXT
};
static WCHAR *t[NB_TXT] = {};
static WCHAR *lang_buf = NULL;

static void load_lang(WCHAR *file)
{
	HANDLE h;
	LARGE_INTEGER size;
	WCHAR *current_str, *context;
	DWORD dw;
	int i;

	if ((h = CreateFile(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) return;
	GetFileSizeEx(h, &size);
	if (size.QuadPart > 128 << 10) {
		CloseHandle(h);
		return;
	}
	if (lang_buf) VirtualFree(lang_buf, 0, MEM_RELEASE);
	lang_buf = VirtualAlloc(NULL, size.QuadPart + sizeof(WCHAR), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	ReadFile(h, lang_buf, size.LowPart, &dw, NULL);
	CloseHandle(h);
	if (!(current_str = wcsstr(lang_buf, L"[Setup]"))) return;
	wcstok(current_str, L"\r\n", &context);
	for (i = 0; i < NB_TXT; i++) {
		t[i] = wcstok(NULL, L"\r\n", &context);
		if (wcslen(t[i]) >= 1024) t[i][1024] = 0;
	}
	size.LowPart /= sizeof(WCHAR);
	for (i = 0; i < size.LowPart; i++)
		if (lang_buf[i] == '#') lang_buf[i] = '\n';
}


static _Bool is_arm64(void)
{
	typedef BOOL (WINAPI *IsWow64Process2_t)(HANDLE, USHORT*, USHORT*);
	IsWow64Process2_t pIsWow64Process2;
	HMODULE hKernel32;
	USHORT processMachine, nativeMachine;

	hKernel32 = GetModuleHandleA("kernel32");
	if (!hKernel32) return FALSE;

	pIsWow64Process2 = (IsWow64Process2_t)GetProcAddress(hKernel32, "IsWow64Process2");
	if (!pIsWow64Process2) return FALSE; // Windows 7/8 - no ARM64 support

	if (pIsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine))
		return nativeMachine == 0xAA64; // IMAGE_FILE_MACHINE_ARM64

	return FALSE;
}


static void start_process(BYTE wait)
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	BOOL result;

#ifndef _WIN64
	FARPROC lpFunc;
	void *ptr;
	HMODULE hDLL = GetModuleHandleA("kernel32");
	if ((lpFunc = GetProcAddress(hDLL, "Wow64DisableWow64FsRedirection"))) lpFunc(&ptr);
#endif
	result = CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
#ifndef _WIN64
	if (lpFunc) GetProcAddress(hDLL, "Wow64RevertWow64FsRedirection")(ptr);
#endif

	if (result) {
		if (wait) WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
}


static void shortcut(WCHAR *path, WCHAR *arg)
{
	GUID IID_IShellLinkW;
	IShellLink *psl;
	IPersistFile *ppf;

	memcpy(&IID_IShellLinkW, &_CLSID_ShellLink, sizeof(GUID));
	*(unsigned char*)&IID_IShellLinkW = 0xf9;

	if (SUCCEEDED(CoCreateInstance(&_CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&psl))) {
		psl->lpVtbl->SetPath(psl, path);
		psl->lpVtbl->SetArguments(psl, arg);
		IID_IShellLinkW.Data1 = 0x0000010b; // IID_IShellLinkW = IID_IPersistFile;
		if (SUCCEEDED(psl->lpVtbl->QueryInterface(psl, &IID_IShellLinkW, (void**)&ppf))) {
			ppf->lpVtbl->Save(ppf, startmenu, TRUE);
			ppf->lpVtbl->Release(ppf);
		}
		psl->lpVtbl->Release(psl);
	}
}

static void del_key(HKEY hKey, char *key)
{
	HMODULE hDLL;
	FARPROC lpFunc;

	hDLL = GetModuleHandleA("advapi32");
	if ((lpFunc = GetProcAddress(hDLL, "RegDeleteKeyExA")))
		lpFunc(hKey, key, KEY_WOW64_64KEY, 0);
	else if ((lpFunc = GetProcAddress(hDLL, "RegDeleteKeyA")))
		lpFunc(hKey, key);
}

static void del_command_key(char *key)
{
	char name[50];

	del_key(HKEY_CLASSES_ROOT, strcat(strcpy(name, key), "\\command"));
	del_key(HKEY_CLASSES_ROOT, key);
}

static void write_key(char *key, WCHAR *name, WCHAR *value)
{
	HKEY h_key;

	RegCreateKeyExA(HKEY_CLASSES_ROOT, key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
	RegSetValueEx(h_key, name, 0, REG_SZ, (void*)value, (wcslen(value) + 1) * sizeof(WCHAR));
	RegCloseKey(h_key);
}

static void add_icon(char *key, _Bool use_cpl)
{
	HKEY h_key;

	if (!use_cpl) {
		_snwprintf(cmd, _countof(cmd) - 1, L"\"%sconfig.exe\"", path);
		write_key(key, L"Icon", cmd);
	} else if (RegOpenKeyExA(HKEY_CLASSES_ROOT, key, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
		RegDeleteValueA(h_key, "Icon");
		RegCloseKey(h_key);
	}
}

static void write_context_menu(WCHAR *path, _Bool use_cpl)
{
	WCHAR path_test[MAX_PATH + 20];

	write_key("*\\shell\\ImDiskMountFile", NULL, t[CONTEXT_1]);
	_snwprintf(path_test, _countof(path_test), L"%sMountImg.exe", path);
	_snwprintf(cmd, _countof(cmd) - 1, L"\"%sMountImg.exe\" \"%%L\"", path);
	write_key("*\\shell\\ImDiskMountFile\\command", NULL, !use_cpl && PathFileExists(path_test) ? cmd : L"rundll32.exe imdisk.cpl,RunDLL_MountFile %L");
	add_icon("*\\shell\\ImDiskMountFile", use_cpl);

	write_key("Drive\\shell\\ImDiskSaveImage", NULL, t[CONTEXT_2]);
	_snwprintf(cmd, _countof(cmd) - 1, L"\"%sImDisk-Dlg.exe\" CP %%L", path);
	write_key("Drive\\shell\\ImDiskSaveImage\\command", NULL, !use_cpl ? cmd : L"rundll32.exe imdisk.cpl,RunDLL_SaveImageFile %L");
	add_icon("Drive\\shell\\ImDiskSaveImage", use_cpl);

	write_key("Drive\\shell\\ImDiskUnmount", NULL, t[CONTEXT_3]);
	_snwprintf(cmd, _countof(cmd) - 1, L"\"%sImDisk-Dlg.exe\" RM %%L", path);
	write_key("Drive\\shell\\ImDiskUnmount\\command", NULL, !use_cpl ? cmd : L"rundll32.exe imdisk.cpl,RunDLL_RemoveDevice %L");
	add_icon("Drive\\shell\\ImDiskUnmount", use_cpl);
}

static void move(WCHAR *file)
{
	WCHAR name_old[MAX_PATH + 100];

	_snwprintf(path_name_ptr, 99, L"%.98s", file);
	if (CopyFile(file, path, FALSE)) return;
	Sleep(100);
	if (CopyFile(file, path, FALSE)) return;
	wcscpy(name_old, path);
	wcscpy(&name_old[wcslen(name_old) - 3], L"old");
	MoveFileEx(path, name_old, MOVEFILE_REPLACE_EXISTING);
	MoveFileEx(name_old, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	if (CopyFile(file, path, FALSE))
		reboot = TRUE;
	else {
		if (!copy_error) MessageBox(NULL, t[ERR_1], t[TITLE], MB_ICONERROR);
		copy_error = TRUE;
	}
}

static BOOL del(WCHAR *file)
{
	_snwprintf(path_name_ptr, 99, L"%.98s", file);
	return DeleteFile(path);
}

static _Bool remove_discutils() {
	WIN32_FIND_DATA entry;
	HANDLE fff;
	_Bool err = FALSE;
	WCHAR du_str[100];

	du_str[99] = 0;
	wcscpy(path_name_ptr, L"DiscUtils\\*");
	fff = FindFirstFile(path, &entry);
	do {
		if (entry.cFileName[0] == '.') continue;
		_snwprintf(du_str, 99, L"DiscUtils\\%s", entry.cFileName);
		if (!del(du_str) && GetLastError() == ERROR_ACCESS_DENIED) {
			MoveFileEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
			err = TRUE;
		}
	} while (FindNextFile(fff, &entry));
	FindClose(fff);
	path_name_ptr[9] = 0;
	if (!RemoveDirectory(path)) {
		MoveFileEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
		err = TRUE;
	}
	return err;
}

static INT_PTR __stdcall CreditsProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int i, j, k;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			SetWindowText(hDlg, t[CRED_0]);
			SetDlgItemText(hDlg, IDOK, t[FIN_3]);
			i = 0;
			for (k = CRED_1; k <= TRANS_MAX; k++) {
				if ((j = _snwprintf(cmd + i, _countof(cmd) - 1, k < TRANS_0 ? L"%.200s\r\n\r\n" : L"%.99s\r\n", t[k])) < 0) break;
				i += j;
			}
			SetDlgItemText(hDlg, ID_EDIT1, cmd);

			SetFocus(GetDlgItem(hDlg, IDOK));
			return FALSE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);
			return TRUE;

		default:
			return FALSE;
	}
}


static INT_PTR __stdcall DotNetProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT paint;
	SHELLEXECUTEINFO ShExInf;

	switch (Msg)
	{
		case WM_INITDIALOG:
			// set localized strings
			if (t[0]) {
				SetWindowText(hDlg, t[TITLE]);
				SetDlgItemText(hDlg, ID_LINK, t[FIN_2]);
				SetDlgItemText(hDlg, IDOK, t[FIN_3]);
			}

			hIconWarn = LoadImage(NULL, MAKEINTRESOURCE(OIC_WARNING), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			iconwarn_coord.left = 14;
			iconwarn_coord.top = 18;
			iconwarn_coord.right = iconwarn_coord.bottom = 0;
			MapDialogRect(hDlg, &iconwarn_coord);
			MessageBeep(MB_ICONWARNING);
			return TRUE;

		case WM_PAINT:
			DrawIcon(BeginPaint(hDlg, &paint), iconwarn_coord.left, iconwarn_coord.top, hIconWarn);
			EndPaint(hDlg, &paint);
			return TRUE;

		case WM_NOTIFY:
			if ((((NMHDR*)lParam)->code == NM_CLICK || ((NMHDR*)lParam)->code == NM_RETURN) && (((NMHDR*)lParam)->hwndFrom == GetDlgItem(hDlg, ID_LINK))) {
				ZeroMemory(&ShExInf, sizeof ShExInf);
				ShExInf.cbSize = sizeof ShExInf;
				ShExInf.fMask = SEE_MASK_CLASSNAME;
				ShExInf.lpFile = ((NMLINK*)lParam)->item.szUrl;
				ShExInf.nShow = SW_SHOWNORMAL;
				ShExInf.lpClass = L"https";
				ShellExecuteEx(&ShExInf);
			}
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);
			return TRUE;

		default:
			return FALSE;
	}
}


static void install(HWND hDlg)
{
	SERVICE_DESCRIPTION svc_description;
	SERVICE_PRESHUTDOWN_INFO svc_preshutdown_info;
	SC_HANDLE scman_handle, svc_handle;
	QUERY_SERVICE_CONFIG *qsc;
	HKEY h_key;
	WCHAR image_file[MAX_PATH], *param_name_ptr;
	BOOL driver_ok, desk_lnk, show_dotnet = FALSE, priv_req, sync;
	DWORD data_size, net_release, RD_found, awealloc, dynamic, sync_flags;
	WIN32_FIND_DATA entry;
	HANDLE fff;
	WCHAR privilege_name[] = L"SeLockMemoryPrivilege";
	HANDLE token = INVALID_HANDLE_VALUE;
	TOKEN_PRIVILEGES tok_priv;
	LSA_HANDLE lsa_h = INVALID_HANDLE_VALUE;
	LSA_OBJECT_ATTRIBUTES lsa_oa = {};
	unsigned char sid[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof sid;
	LSA_UNICODE_STRING lsa_str = {sizeof privilege_name - sizeof(WCHAR), sizeof privilege_name, privilege_name};
	GUID IID_IUniformResourceLocatorA, IID_IPersistFile;
	IUniformResourceLocatorA *purl;
	IPersistFile *ppf;
	int i, max;

	GetDlgItemText(hDlg, ID_EDIT1, path, MAX_PATH);
	if (!CreateDirectory(path, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS)) {
bad_dir:
		if (silent < 2) MessageBox(hDlg, t[ERR_2], t[TITLE], MB_ICONERROR);
		return;
	}
	wcscpy(install_path, path);
	path_name_ptr = PathAddBackslash(path);
	wcscpy(path_name_ptr, L"DiscUtils");
	if (!CreateDirectory(path, NULL) && (GetLastError() != ERROR_ALREADY_EXISTS))
		goto bad_dir;

	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
	SetDlgItemText(hDlg, IDOK, t[CTRL_4]);
	SetCursor(LoadImage(NULL, MAKEINTRESOURCE(OCR_WAIT), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));

	// remove obsolete items
	for (i = 0; i < _countof(obsolete_list); i++)
		del(obsolete_list[i]);
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
		RegDeleteValueA(h_key, "ImDisk_notif");
		RegCloseKey(h_key);
	}

	_snwprintf(cmd, _countof(cmd) - 1, L"lang\\%s.txt", lang_file_list[n_lang]);
	MoveFileEx(cmd, L"lang.txt", 0);
	move(L"lang.txt");
	move(L"ImDisk-Dlg.exe");
	move(L"config.exe");

	// shortcuts
	if (IsDlgButtonChecked(hDlg, ID_CHECK6)) {
		desk_ptr = &desk[desk_all_ptr - desk_all];
		desk_all_ptr = &desk_all[desk_ptr - desk];
		wcscpy(cmd, desk);
		wcscpy(desk, desk_all);
		wcscpy(desk_all, cmd);
		wcscpy(cmd, startmenu);
		wcscpy(startmenu, startmenu_all);
		wcscpy(startmenu_all, cmd);
	}
	startmenu_all_ptr = PathAddBackslash(startmenu_all);
	for (i = SHORTCUT_1; i <= SHORTCUT_5 + 1; i++) {
		_snwprintf(startmenu_all_ptr, 99, i > SHORTCUT_5 ? L"ImDisk Virtual Disk Driver.lnk" : i == SHORTCUT_3 ? L"%.94s.url" : L"%.94s.lnk", t[i]);
		DeleteFile(startmenu_all);
		if (i >= SHORTCUT_4) {
			wcscpy(desk_all_ptr, startmenu_all_ptr);
			DeleteFile(desk_all);
		}
	}
	*(startmenu_all_ptr - 1) = 0;
	RemoveDirectory(startmenu_all);

	CreateDirectory(startmenu, NULL);
	startmenu_ptr = PathAddBackslash(startmenu);
	_snwprintf(startmenu_ptr, 99, L"%.94s.lnk", t[SHORTCUT_2]);
	shortcut(path, L" /u");
	_snwprintf(startmenu_ptr, 99, L"%.94s.lnk", t[SHORTCUT_1]);
	shortcut(path, NULL);

	_snwprintf(startmenu_ptr, 99, L"%.94s.url", t[SHORTCUT_3]);
	memcpy(&IID_IUniformResourceLocatorA, &_CLSID_InternetShortcut, sizeof(GUID));
	*(unsigned char*)&IID_IUniformResourceLocatorA = 0x80;
	memcpy(&IID_IPersistFile, &_CLSID_ShellLink, sizeof(GUID));
	IID_IPersistFile.Data1 = 0x0000010b;
	if (SUCCEEDED(CoCreateInstance(&_CLSID_InternetShortcut, NULL, CLSCTX_INPROC_SERVER, &IID_IUniformResourceLocatorA, (void**)&purl))) {
		purl->lpVtbl->SetURL(purl, "https://sourceforge.net/projects/imdisk-toolkit/", 0);
		if (SUCCEEDED(purl->lpVtbl->QueryInterface(purl, &IID_IPersistFile, (void**)&ppf))) {
			ppf->lpVtbl->Save(ppf, startmenu, TRUE);
			ppf->lpVtbl->Release(ppf);
		}
		purl->lpVtbl->Release(purl);
	}
	desk_lnk = IsDlgButtonChecked(hDlg, ID_CHECK5);

	// uninstall settings
	RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &h_key, NULL);
	RegSetValueEx(h_key, L"DisplayIcon", 0, REG_SZ, (void*)path, (wcslen(path) + 1) * sizeof(WCHAR));
	RegSetValueExA(h_key, "DisplayName", 0, REG_SZ, (void*)reg_disp_name_tk, sizeof reg_disp_name_tk);
	RegSetValueExA(h_key, "DisplayVersion", 0, REG_SZ, (void*)APP_VERSION, sizeof APP_VERSION);
	RegSetValueExA(h_key, "EstimatedSize", 0, REG_DWORD, (void*)&EstimatedSize, sizeof EstimatedSize);
	PathQuoteSpaces(wcscpy(cmd, path));
	wcscat(cmd, L" /u");
	RegSetValueEx(h_key, L"UninstallString", 0, REG_SZ, (void*)cmd, (wcslen(cmd) + 1) * sizeof(WCHAR));
	RegCloseKey(h_key);

	scman_handle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);

	// driver
	if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
		if ((svc_handle = OpenService(scman_handle, L"ImDisk", SERVICE_QUERY_CONFIG))) {
			reboot = TRUE;
			CloseServiceHandle(svc_handle);
		}
		if (is_arm64())
			wcscpy(cmd, L"cmd /c driver\\install_new.cmd ARM64");
		else
			wcscpy(cmd, L"rundll32 setupapi.dll,InstallHinfSection DefaultInstall 128 driver\\imdisk.inf");
		start_process(TRUE);
	}
	driver_ok = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDisk", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS;
	if (driver_ok) {
		SHGetFolderPath(NULL, CSIDL_SYSTEM, NULL, SHGFP_TYPE_CURRENT, cmd);
		DeleteFile(wcscat(cmd, L"\\imdisk.cpl.manifest"));
#ifdef _WIN64
		SHGetFolderPath(NULL, CSIDL_SYSTEMX86, NULL, SHGFP_TYPE_CURRENT, cmd);
		DeleteFile(wcscat(cmd, L"\\imdisk.cpl.manifest"));
#else
		FARPROC lpFunc;
		void *ptr;
		HMODULE hDLL = GetModuleHandleA("kernel32");
		if ((lpFunc = GetProcAddress(hDLL, "Wow64DisableWow64FsRedirection"))) lpFunc(&ptr);
		DeleteFile(cmd);
		if (lpFunc) GetProcAddress(hDLL, "Wow64RevertWow64FsRedirection")(ptr);
#endif
		wcscpy(cmd, L"reg copy HKLM\\SOFTWARE\\ImDisk\\DriverBackup HKLM\\SYSTEM\\CurrentControlSet\\Services\\ImDisk\\Parameters /f");
		start_process(TRUE);
		del_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk\\DriverBackup");
		qsc = (QUERY_SERVICE_CONFIG*)&cmd;
		for (i = 0; i < _countof(driver_svc_list); i++) {
			if ((svc_handle = OpenService(scman_handle, driver_svc_list[i], SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG | SERVICE_START))) {
				if (i < _countof(driver_svc_list) - 1) {
					if (QueryServiceConfig(svc_handle, qsc, 8192, &data_size) && qsc->dwStartType == SERVICE_AUTO_START)
						ChangeServiceConfig(svc_handle, SERVICE_NO_CHANGE, SERVICE_DEMAND_START, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
				} else
					StartService(svc_handle, 0, NULL);
				CloseServiceHandle(svc_handle);
			}
		}
		CheckDlgButton(hDlg, ID_CHECK1, BST_UNCHECKED);
		RegDeleteValueA(h_key, "DisplayName");
		RegCloseKey(h_key);
		wcscpy(startmenu_ptr, L"ImDisk Virtual Disk Driver.lnk");
		CopyFile(L"ImDisk Virtual Disk Driver.lnk", startmenu, FALSE);
		wcscpy(desk_ptr, L"ImDisk Virtual Disk Driver.lnk");
		if (desk_lnk) CopyFile(startmenu, desk, FALSE);
		else DeleteFile(desk);
	}

	// DiscUtils
	_snwprintf(startmenu_ptr, 99, L"%.94s.lnk", t[SHORTCUT_5]);
	wcscpy(desk_ptr, startmenu_ptr);
	if (IsDlgButtonChecked(hDlg, ID_CHECK2)) {
		data_size = sizeof net_release;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS &&
			RegQueryValueExA(h_key, "Release", NULL, NULL, (void*)&net_release, &data_size) == ERROR_SUCCESS && net_release >= 528040)
			RegCloseKey(h_key);
		else
			show_dotnet = !silent;

		fff = FindFirstFile(L"DiscUtils\\*", &entry);
		do {
			if (entry.cFileName[0] == '.') continue;
			_snwprintf(cmd, 99, L"DiscUtils\\%s", entry.cFileName);
			move(cmd);
		} while (FindNextFile(fff, &entry));
		FindClose(fff);

		move(L"MountImg.exe");
		shortcut(path, NULL);
		if (desk_lnk) CopyFile(startmenu, desk, FALSE);
		else DeleteFile(desk);

		// recreate ImDiskImg if needed
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
			max = -1;
			data_size = sizeof max;
			RegQueryValueEx(h_key, L"ImMaxReg", NULL, NULL, (void*)&max, &data_size);
			for (i = 0; i <= max; i++) {
				param_name_ptr = cmd + _snwprintf(cmd, _countof(cmd) - 1, L"Im%d", i);
				wcscpy(param_name_ptr, L"FileName");
				if (RegQueryValueEx(h_key, cmd, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
					wcscpy(param_name_ptr, L"Device");
					if (RegQueryValueEx(h_key, cmd, NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND) {
						PathQuoteSpaces(wcscpy(cmd, path));
						wcscat(cmd, L" /SVC");
						if ((svc_handle = OpenService(scman_handle, L"ImDiskImg", SERVICE_CHANGE_CONFIG | SERVICE_START))) {
							ChangeServiceConfig(svc_handle, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE, SERVICE_ERROR_NORMAL, cmd, NULL, NULL, L"ImDisk\0", NULL, NULL, NULL);
						} else
							svc_handle = CreateService(scman_handle, L"ImDiskImg", L"ImDisk Image File mounter", SERVICE_CHANGE_CONFIG | SERVICE_START, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
													   SERVICE_ERROR_NORMAL, cmd, NULL, NULL, L"ImDisk\0", NULL, NULL);
						if (svc_handle) {
							svc_description.lpDescription = L"Mounts image files at system startup.";
							ChangeServiceConfig2(svc_handle, SERVICE_CONFIG_DESCRIPTION, &svc_description);
							StartService(svc_handle, 0, NULL);
							CloseServiceHandle(svc_handle);
						} else if (silent < 2)
							MessageBox(hDlg, t[ERR_3], t[TITLE], MB_ICONERROR);
						break;
					}
				}
			}
			RegCloseKey(h_key);
		}
	} else {
		DeleteFile(startmenu);
		DeleteFile(desk);
		remove_discutils();
		del(L"MountImg.exe");
	}

	// RamDisk Configuration Tool
	_snwprintf(startmenu_ptr, 99, L"%.94s.lnk", t[SHORTCUT_4]);
	wcscpy(desk_ptr, startmenu_ptr);
	if (IsDlgButtonChecked(hDlg, ID_CHECK3)) {
#ifndef _WIN64
		BOOL is_wow64;
		IsWow64Process(GetCurrentProcess(), &is_wow64);
		MoveFileEx(is_wow64 ? L"RamDyn64.exe" : L"RamDyn32.exe", L"RamDyn.exe", 0);
		MoveFileEx(is_wow64 ? L"ImDiskTk-svc64.exe" : L"ImDiskTk-svc32.exe", L"ImDiskTk-svc.exe", 0);
#endif
		move(L"RamDyn.exe");
		move(L"ImDiskTk-svc.exe");

		move(L"RamDiskUI.exe");
		shortcut(path, NULL);
		if (desk_lnk) CopyFile(startmenu, desk, FALSE);
		else DeleteFile(desk);

		// recreate ramdisk services if needed
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
			data_size = sizeof RD_found;
			if (RegQueryValueEx(h_key, L"RDMountCurrent", NULL, NULL, (void*)&RD_found, &data_size) == ERROR_SUCCESS) {
				data_size = sizeof awealloc;
				if (RegQueryValueEx(h_key, L"Awealloc", NULL, NULL, (void*)&awealloc, &data_size) != ERROR_SUCCESS) awealloc = 0;
				data_size = sizeof dynamic;
				if (RegQueryValueEx(h_key, L"Dynamic", NULL, NULL, (void*)&dynamic, &data_size) != ERROR_SUCCESS) dynamic = 0;
				data_size = sizeof sync_flags;
				if (RegQueryValueEx(h_key, L"SyncFlags", NULL, NULL, (void*)&sync_flags, &data_size) != ERROR_SUCCESS) sync_flags = 0;
				data_size = sizeof image_file;
				if (RegQueryValueEx(h_key, L"ImageFile", NULL, NULL, (void*)&image_file, &data_size) != ERROR_SUCCESS) image_file[0] = 0;
				priv_req = awealloc & dynamic & RD_found;
				sync = sync_flags & RD_found && image_file[0];
				for (cmd[0] = '0'; cmd[0] <= 'Z'; cmd[0] == '9' ? cmd[0] = 'A' : cmd[0]++) {
					wcscpy(cmd + 1, L"_Awealloc");
					data_size = sizeof awealloc;
					if (RegQueryValueEx(h_key, cmd, NULL, NULL, (void*)&awealloc, &data_size) == ERROR_SUCCESS) {
						RD_found = TRUE;
						wcscpy(cmd + 2, L"Dynamic");
						data_size = sizeof dynamic;
						if (RegQueryValueEx(h_key, cmd, NULL, NULL, (void*)&dynamic, &data_size) != ERROR_SUCCESS) dynamic = 0;
						wcscpy(cmd + 2, L"SyncFlags");
						data_size = sizeof sync_flags;
						if (RegQueryValueEx(h_key, cmd, NULL, NULL, (void*)&sync_flags, &data_size) != ERROR_SUCCESS) sync_flags = 0;
						wcscpy(cmd + 2, L"ImageFile");
						data_size = sizeof image_file;
						if (RegQueryValueEx(h_key, cmd, NULL, NULL, (void*)&image_file, &data_size) != ERROR_SUCCESS) image_file[0] = 0;
						priv_req |= awealloc & dynamic;
						sync |= sync_flags & 1 && image_file[0];
					}
				}
				if (RD_found) {
					if (priv_req) {
						tok_priv.PrivilegeCount = 1;
						tok_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
						if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token) ||
							!LookupPrivilegeValue(NULL, privilege_name, &tok_priv.Privileges[0].Luid) ||
							!AdjustTokenPrivileges(token, FALSE, &tok_priv, 0, NULL, NULL) || GetLastError() != ERROR_SUCCESS) {
							CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, (SID*)sid, &sid_size);
							LsaOpenPolicy(NULL, &lsa_oa, POLICY_LOOKUP_NAMES, &lsa_h);
							LsaAddAccountRights(lsa_h, (SID*)sid, &lsa_str, 1);
							reboot = TRUE;
						}
					}
					PathQuoteSpaces(wcscpy(cmd, path));
					wcscat(cmd, L" SVC");
					if ((svc_handle = OpenService(scman_handle, L"ImDiskRD", SERVICE_CHANGE_CONFIG | SERVICE_START))) {
						ChangeServiceConfig(svc_handle, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE, SERVICE_ERROR_NORMAL, cmd, NULL, NULL, L"ImDisk\0", NULL, NULL, NULL);
					} else
						svc_handle = CreateService(scman_handle, L"ImDiskRD", L"ImDisk RamDisk starter", SERVICE_CHANGE_CONFIG | SERVICE_START, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
												   SERVICE_ERROR_NORMAL, cmd, NULL, NULL, L"ImDisk\0", NULL, NULL);
					if (svc_handle) {
						svc_description.lpDescription = L"Mounts a RamDisk at system startup.";
						ChangeServiceConfig2(svc_handle, SERVICE_CONFIG_DESCRIPTION, &svc_description);
						StartService(svc_handle, 0, NULL);
						CloseServiceHandle(svc_handle);
					} else if (silent < 2)
						MessageBox(hDlg, t[ERR_3], t[TITLE], MB_ICONERROR);

					if (sync) {
						wcscpy(path_name_ptr, L"ImDiskTk-svc.exe");
						if ((svc_handle = OpenService(scman_handle, L"ImDiskTk-svc", SERVICE_CHANGE_CONFIG | SERVICE_START))) {
							ChangeServiceConfig(svc_handle, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE, SERVICE_ERROR_NORMAL, path, NULL, NULL, L"ImDisk\0", NULL, NULL, NULL);
						} else
							svc_handle = CreateService(scman_handle, L"ImDiskTk-svc", L"ImDisk Toolkit helper service", SERVICE_CHANGE_CONFIG | SERVICE_START, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
													   SERVICE_ERROR_NORMAL, path, NULL, NULL, L"ImDisk\0", NULL, NULL);
						if (svc_handle) {
							svc_description.lpDescription = L"Service used for data synchronization at system shutdown.";
							ChangeServiceConfig2(svc_handle, SERVICE_CONFIG_DESCRIPTION, &svc_description);
							if (os_ver.dwMajorVersion >= 6) {
								svc_preshutdown_info.dwPreshutdownTimeout = 3600000;
								ChangeServiceConfig2(svc_handle, SERVICE_CONFIG_PRESHUTDOWN_INFO, &svc_preshutdown_info);
							}
							StartService(svc_handle, 0, NULL);
							CloseServiceHandle(svc_handle);
						} else if (silent < 2)
							MessageBox(hDlg, t[ERR_3], t[TITLE], MB_ICONERROR);
					}
				}
			}
			RegCloseKey(h_key);
		}
	} else {
		DeleteFile(startmenu);
		DeleteFile(desk);
		del(L"RamDiskUI.exe");
		del(L"RamDyn.exe");
	}

	CloseServiceHandle(scman_handle);

	if (driver_ok) {
		// context menus
		if (IsDlgButtonChecked(hDlg, ID_CHECK4)) {
			*path_name_ptr = 0;
			write_context_menu(path, FALSE);
		} else {
			del_command_key("*\\shell\\ImDiskMountFile");
			del_command_key("Drive\\shell\\ImDiskSaveImage");
			del_command_key("Drive\\shell\\ImDiskUnmount");
		}
	}

	if (path_prev[0] && wcscmp(install_path, path_prev) && (silent ||
		MessageBox(hDlg, t[PREV_TXT] ? t[PREV_TXT] : L"The previous installation is in another folder.\nDo you want to remove it?", t[TITLE], MB_YESNO | MB_ICONQUESTION) == IDYES)) {
		path_name_ptr = PathAddBackslash(wcscpy(path, path_prev));
		if (remove_discutils()) reboot = TRUE;
		for (i = 0; i < _countof(file_list); i++)
			if (!del(file_list[i]) && GetLastError() == ERROR_ACCESS_DENIED) {
				MoveFileEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
				reboot = TRUE;
			}
		path_name_ptr[-1] = 0;
		if (!RemoveDirectory(path)) {
			MoveFileEx(path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
			reboot = TRUE;
		}
	}

	if (show_dotnet) DialogBox(hinst, L"DOTNETBOX", hDlg, DotNetProc);

	if (reboot) {
		if (silent < 2) SetupPromptReboot(NULL, hDlg, FALSE);
	} else if (!show_dotnet && !silent) {
		wcscpy(cmd, t[FIN_1] ? t[FIN_1] : L"Installation finished.");
		if (os_ver.dwMajorVersion >= 6) wcscat(cmd, L"  ☺");
		MessageBox(hDlg, cmd, t[TITLE], MB_ICONINFORMATION);
	}

	EndDialog(hDlg, 0);
}


static void load_lang_install(HWND hDlg)
{
	_snwprintf(cmd, _countof(cmd) - 1, L"lang\\%s.txt", lang_file_list[n_lang]);
	load_lang(cmd);

	// set localized strings
	if (t[0]) {
		SetWindowText(hDlg, t[TITLE]);
		SetDlgItemText(hDlg, ID_TEXT1, t[TXT_1]);
		SetDlgItemText(hDlg, ID_TEXT10, t[TXT_3]);
		SetDlgItemText(hDlg, ID_GROUP1, t[COMP_0]);
		SetDlgItemText(hDlg, ID_CHECK1, t[COMP_1]);
		SetDlgItemText(hDlg, ID_CHECK2, t[COMP_2]);
		SetDlgItemText(hDlg, ID_CHECK3, t[COMP_3]);
		SetDlgItemText(hDlg, ID_GROUP2, t[OPT_0]);
		SetDlgItemText(hDlg, ID_CHECK4, t[OPT_1]);
		SetDlgItemText(hDlg, ID_CHECK5, t[OPT_2]);
		SetDlgItemText(hDlg, ID_CHECK6, t[OPT_3]);
		SetDlgItemText(hDlg, ID_TEXT11, t[LANG_TXT]);
		SetDlgItemText(hDlg, ID_TEXT2, t[DESC_0]);
		SetDlgItemText(hDlg, ID_PBUTTON2, t[CTRL_1]);
		SetDlgItemText(hDlg, IDOK, t[CTRL_2]);
		SetDlgItemText(hDlg, IDCANCEL, t[CTRL_3]);
	}
	_snwprintf(cmd, _countof(cmd) - 1, t[TXT_2] ? t[TXT_2] : L"This will install the ImDisk Toolkit (build %S).", APP_VERSION);
	SetDlgItemText(hDlg, ID_STATIC3, cmd);
}

static INT_PTR __stdcall InstallProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	BROWSEINFO bi;
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	RECT coord;
	HFONT hFont1, hFont2, hFont3;
	LOGFONTA font;
	void *v_buff1, *v_buff2;
	VS_FIXEDFILEINFO *v1, *v2;
	DWORD size_ver, data_size;
	HKEY h_key;
	LANGID prim_lang_id, full_lang_id;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			for (i = 0; i < _countof(lang_list); i++)
				SendDlgItemMessage(hDlg, ID_COMBO1, CB_ADDSTRING, 0, (LPARAM)lang_list[i]);

			// attempt to retrieve current language
			if (!lang_cmdline) {
				prim_lang_id = (full_lang_id = GetUserDefaultUILanguage()) & 0x3ff;
				switch (prim_lang_id) {
					case 0x07: n_lang = 1; break; // german
					case 0x0a: n_lang = 2; break; // spanish
					case 0x0c: n_lang = 3; break; // french
					case 0x10: n_lang = 4; break; // italian
					case 0x0e: n_lang = 5; break; // hungarian
					case 0x16: n_lang = 6; break; // brazilian-portuguese
					case 0x19: n_lang = 7; break; // russian
					case 0x0b: n_lang = 8; break; // finnish
					case 0x1d: n_lang = 9; break; // swedish
					case 0x1f: n_lang = 10; break; // turkish
					case 0x04: switch (full_lang_id) {
							case 0x7C04: case 0x0C04: case 0x1404: case 0x0404: n_lang = 12; break; // tchinese
							default: n_lang = 11; // schinese
						}
						break;
					case 0x12: n_lang = 13; break; // korean
				}
			}
			SendDlgItemMessage(hDlg, ID_COMBO1, CB_SETCURSEL, n_lang, 0);

			load_lang_install(hDlg);

			// check imdisk.exe version to see if update is needed
			CheckDlgButton(hDlg, ID_CHECK1, BST_CHECKED);
			if ((size_ver = GetFileVersionInfoSizeA("imdisk.exe", NULL))) {
				v_buff1 = VirtualAlloc(NULL, size_ver, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				GetFileVersionInfoA("imdisk.exe", 0, size_ver, v_buff1);
				VerQueryValueA(v_buff1, "\\", (void*)&v1, (UINT*)&size_ver);
				if ((size_ver = GetFileVersionInfoSizeA("driver\\cli\\i386\\imdisk.exe", NULL))) {
					v_buff2 = VirtualAlloc(NULL, size_ver, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
					GetFileVersionInfoA("driver\\cli\\i386\\imdisk.exe", 0, size_ver, v_buff2);
					VerQueryValueA(v_buff2, "\\", (void*)&v2, (UINT*)&size_ver);
					if (v2->dwFileVersionMS < v1->dwFileVersionMS || (v2->dwFileVersionMS == v1->dwFileVersionMS && v2->dwFileVersionLS <= v1->dwFileVersionLS))
						CheckDlgButton(hDlg, ID_CHECK1, BST_UNCHECKED);
					VirtualFree(v_buff2, 0, MEM_RELEASE);
				}
				VirtualFree(v_buff1, 0, MEM_RELEASE);
			}

			SendDlgItemMessage(hDlg, ID_EDIT1, EM_SETLIMITTEXT, MAX_PATH, 0);
			CheckDlgButton(hDlg, ID_CHECK5, BST_CHECKED);

			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
				// set the options according to the previous installation
				data_size = sizeof path;
				RegQueryValueEx(h_key, L"DisplayIcon", NULL, NULL, (void*)&path, &data_size);
				RegCloseKey(h_key);
				PathRemoveFileSpec(path);
				i = wcslen(path);
				path_name_ptr = PathAddBackslash(path);
				wcscpy(path_name_ptr, L"MountImg.exe");
				CheckDlgButton(hDlg, ID_CHECK2, PathFileExists(path));
				wcscpy(path_name_ptr, L"RamDiskUI.exe");
				CheckDlgButton(hDlg, ID_CHECK3, PathFileExists(path));
				path[i] = 0;
				wcscpy(path_prev, path);
#ifdef _WIN64
				WCHAR path_x86[MAX_PATH + 8];
				SHGetFolderPath(NULL, CSIDL_PROGRAM_FILESX86, NULL, SHGFP_TYPE_CURRENT, path_x86);
				wcscat(path_x86, L"\\ImDisk");
				if (!wcscmp(path, path_x86)) path[0] = 0;
#endif

				if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "Drive\\shell\\ImDiskSaveImage\\command", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
					CheckDlgButton(hDlg, ID_CHECK4, BST_CHECKED);
					RegCloseKey(h_key);
				}

				wcscpy(desk_ptr, L"ImDisk Virtual Disk Driver.lnk");
				wcscpy(desk_all_ptr, L"ImDisk Virtual Disk Driver.lnk");
				if (!PathFileExists(desk) && !PathFileExists(desk_all)) {
					_snwprintf(desk_ptr, 99, L"%.94s.lnk", t[SHORTCUT_4]);
					_snwprintf(desk_all_ptr, 99, L"%.94s.lnk", t[SHORTCUT_4]);
					if (!PathFileExists(desk) && !PathFileExists(desk_all)) {
						_snwprintf(desk_ptr, 99, L"%.94s.lnk", t[SHORTCUT_5]);
						_snwprintf(desk_all_ptr, 99, L"%.94s.lnk", t[SHORTCUT_5]);
						if (!PathFileExists(desk) && !PathFileExists(desk_all))
							CheckDlgButton(hDlg, ID_CHECK5, BST_UNCHECKED);
					}
				}

				CheckDlgButton(hDlg, ID_CHECK6, PathFileExists(startmenu_all));
			} else
				for (i = ID_CHECK4; i >= ID_CHECK2; i--)
					CheckDlgButton(hDlg, i, BST_CHECKED);

			if (!path[0]) {
				SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES, NULL, SHGFP_TYPE_CURRENT, path);
				wcscat(path, L"\\ImDisk");
			}
			SetDlgItemText(hDlg, ID_EDIT1, path_cmdline ? path_cmdline : path);

			if (opt_cmd_mountimg) CheckDlgButton(hDlg, ID_CHECK2, opt_cmd_mountimg - 1);
			if (opt_cmd_ramdiskui) CheckDlgButton(hDlg, ID_CHECK3, opt_cmd_ramdiskui - 1);
			if (opt_cmd_menus) CheckDlgButton(hDlg, ID_CHECK4, opt_cmd_menus - 1);
			if (opt_cmd_shortcutdesk) CheckDlgButton(hDlg, ID_CHECK5, opt_cmd_shortcutdesk - 1);
			if (opt_cmd_shortcutall) CheckDlgButton(hDlg, ID_CHECK6, opt_cmd_shortcutall - 1);

			if (silent) {
				install(hDlg);
				return TRUE;
			}

			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			hwnd_text1 = GetDlgItem(hDlg, ID_TEXT1);
			hwnd_static1 = GetDlgItem(hDlg, ID_STATIC1);
			hwnd_static2 = GetDlgItem(hDlg, ID_STATIC2);
			hwnd_static3 = GetDlgItem(hDlg, ID_STATIC3);

			icon_coord.left = 13;
			icon_coord.top = 7;
			icon_coord.right = icon_coord.bottom = 0;
			MapDialogRect(hDlg, &icon_coord);

			coord.top = 23; coord.left = 4;
			coord.bottom = 15; coord.right = 3;
			MapDialogRect(hDlg, &coord);
			ZeroMemory(&font, sizeof font);
			font.lfHeight = coord.top / 2;
			font.lfWidth = coord.left;
			font.lfWeight = FW_SEMIBOLD;
			font.lfCharSet = DEFAULT_CHARSET;
			strcpy(font.lfFaceName, "MS Shell Dlg");
			hFont1 = CreateFontIndirectA(&font);
			SendMessage(hwnd_text1, WM_SETFONT, (WPARAM)hFont1, 0);

			font.lfHeight = coord.bottom / 2;
			font.lfWidth = coord.right;
			font.lfWeight = FW_NORMAL;
			hFont2 = CreateFontIndirectA(&font);
			SendMessage(GetDlgItem(hDlg, ID_TEXT2), WM_SETFONT, (WPARAM)hFont2, 0);
			SendMessage(GetDlgItem(hDlg, ID_GROUP1), WM_SETFONT, (WPARAM)hFont2, 0);
			SendMessage(GetDlgItem(hDlg, ID_GROUP2), WM_SETFONT, (WPARAM)hFont2, 0);

			// ☺
			coord.top = 26; coord.left = 5;
			MapDialogRect(hDlg, &coord);
			font.lfHeight = coord.top / 2;
			font.lfWidth = coord.left;
			font.lfOutPrecision = OUT_OUTLINE_PRECIS;
			font.lfQuality = CLEARTYPE_QUALITY;
			strcpy(font.lfFaceName, "Arial Black");
			hFont3 = CreateFontIndirectA(&font);

			for (i = 0; i < _countof(hwnd_check); i++) {
				SendMessage(GetDlgItem(hDlg, ID_TEXT3 + i), WM_SETFONT, (WPARAM)hFont3, 0);
				hwnd_check[i] = GetDlgItem(hDlg, ID_CHECK1 + i);
			}

			hBrush = CreateSolidBrush(HEADER_BACKGROUND);
			SetCursor(LoadImage(NULL, MAKEINTRESOURCE(OCR_NORMAL), IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE | LR_SHARED));

			return TRUE;

		case WM_SETCURSOR:
			if (wParam == prev_wparam) return FALSE;
			for (i = 0; i < _countof(hwnd_check); i++)
				if (wParam == (WPARAM)hwnd_check[i]) {
					SetDlgItemText(hDlg, ID_TEXT3 + prev_mark, L"");
					SetDlgItemText(hDlg, ID_TEXT3 + i, L"☺");
					SetDlgItemText(hDlg, ID_TEXT2, t[DESC_1 + i]);
					prev_mark = i;
				}
			prev_wparam = wParam;
			return TRUE;

		case WM_CTLCOLORSTATIC:
			if ((HWND)lParam == hwnd_text1 || (HWND)lParam == hwnd_static1 || (HWND)lParam == hwnd_static2 || (HWND)lParam == hwnd_static3) {
				SetBkColor((HDC)wParam, HEADER_BACKGROUND);
				return (INT_PTR)hBrush;
			}
			return FALSE;

		case WM_COMMAND:
			if (LOWORD(wParam) == ID_PBUTTON1) {
				ZeroMemory(&bi, sizeof bi);
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = path;
				bi.lpszTitle = L"ImDisk - Setup";
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, path);
					SetDlgItemText(hDlg, ID_EDIT1, path);
				}
			}

			if (LOWORD(wParam) == ID_COMBO1 && HIWORD(wParam) == CBN_SELCHANGE) {
				n_lang = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
				SetDlgItemText(hDlg, ID_TEXT3 + prev_mark, L"");
				load_lang_install(hDlg);
			}

			if (LOWORD(wParam) == ID_PBUTTON2)
				DialogBox(hinst, L"CREDITSBOX", hDlg, CreditsProc);

			if (LOWORD(wParam) == IDOK)
				install(hDlg);

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}


static void set_uninstall_text(WCHAR *text)
{
	if (!silent_uninstall) SetDlgItemText(hwnd_uninst, ID_TEXT1, text);
}

static DWORD __stdcall uninstall(LPVOID lpParam)
{
	DWORD data_size;
	SC_HANDLE scman_handle, svc_handle;
	HKEY h_key, h_key2;
	WCHAR dir[MAX_PATH + 20];
	HMODULE h_cpl;
	FARPROC ImDiskGetDeviceListEx;
	ULONG *list;
	int i;

	SetCurrentDirectoryA("\\"); // required to delete installation directory

	// Toolkit services
	scman_handle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
	for (i = 0; i < _countof(tk_svc_list); i++) {
		_snwprintf(cmd, _countof(cmd) - 1, t[U_MSG_3], tk_svc_list[i]);
		set_uninstall_text(cmd);
		_snwprintf(cmd, _countof(cmd) - 1, L"net stop %s /y", tk_svc_list[i]);
		start_process(TRUE);
		svc_handle = OpenService(scman_handle, tk_svc_list[i], DELETE);
		DeleteService(svc_handle);
		CloseServiceHandle(svc_handle);
	}
	CloseServiceHandle(scman_handle);
	wcscpy(cmd, L"taskkill /f /im ImDiskTk-svc.exe");
	start_process(TRUE);

	// settings
	if (silent_uninstall || IsDlgButtonChecked(hwnd_uninst, ID_CHECK2))
		del_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk");
	else {
		wcscpy(cmd, L"reg copy HKLM\\SYSTEM\\CurrentControlSet\\Services\\ImDisk\\Parameters HKLM\\SOFTWARE\\ImDisk\\DriverBackup /f");
		start_process(TRUE);
	}
	if (silent_uninstall || IsDlgButtonChecked(hwnd_uninst, ID_CHECK3)) {
		RegOpenKeyExA(HKEY_USERS, ".DEFAULT\\Environment", 0, KEY_QUERY_VALUE, &h_key);
		data_size = sizeof dir;
		if (RegQueryValueEx(h_key, L"TMP", NULL, NULL, (void*)&dir, &data_size) == ERROR_SUCCESS) {
			RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &h_key2);
			RegSetValueEx(h_key2, L"TEMP", 0, REG_EXPAND_SZ, (void*)&dir, (wcslen(dir) + 1) * sizeof(WCHAR));
			RegSetValueEx(h_key2, L"TMP", 0, REG_EXPAND_SZ, (void*)&dir, (wcslen(dir) + 1) * sizeof(WCHAR));
			RegCloseKey(h_key2);
		}
		RegCloseKey(h_key);
		wcscpy(dir, L"%SystemRoot%\\TEMP");
		RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &h_key);
		RegSetValueEx(h_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&dir, (wcslen(dir) + 1) * sizeof(WCHAR));
		RegSetValueEx(h_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&dir, (wcslen(dir) + 1) * sizeof(WCHAR));
		RegCloseKey(h_key);
	}

	// driver
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDisk", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
		if (silent_uninstall || IsDlgButtonChecked(hwnd_uninst, ID_CHECK1)) {
			RegCloseKey(h_key);
			if ((h_cpl = LoadLibraryA("imdisk.cpl")) && (ImDiskGetDeviceListEx = GetProcAddress(h_cpl, "ImDiskGetDeviceListEx"))) {
				list = VirtualAlloc(NULL, 64002 * sizeof(ULONG), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
				if (ImDiskGetDeviceListEx(64002, list) && list[0]) {
					if (!silent_uninstall && MessageBox(hwnd_uninst, t[U_MSG_1], t[U_TITLE], MB_OKCANCEL | MB_ICONWARNING) == IDCANCEL) {
						EndDialog(hwnd_uninst, 0);
						return 0;
					}
					set_uninstall_text(t[U_MSG_2]);
					i = 1; do {
						_snwprintf(cmd, _countof(cmd) - 1, L"imdisk -D -u %u", list[i]);
						start_process(TRUE);
					} while (++i <= list[0]);
				}
				VirtualFree(list, 0, MEM_RELEASE);
			}
			for (i = 0; i < _countof(driver_svc_list); i++) {
				_snwprintf(cmd, _countof(cmd) - 1, t[U_MSG_3], driver_svc_list[i]);
				set_uninstall_text(cmd);
				_snwprintf(cmd, _countof(cmd) - 1, L"net stop %s /y", driver_svc_list[i]);
				start_process(TRUE);
			}
			wcscpy(cmd, L"taskkill /f /im imdsksvc.exe");
			start_process(TRUE);
			SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, dir);
			if (is_arm64())
				_snwprintf(cmd, _countof(cmd) - 1, L"%s\\system32\\uninstall_imdisk.cmd", dir);
			else
				_snwprintf(cmd, _countof(cmd) - 1, L"rundll32 setupapi.dll,InstallHinfSection DefaultUninstall %u %s\\inf\\imdisk.inf", silent_uninstall ? 128 : 132, dir);
			start_process(FALSE);
		} else {
			RegSetValueExA(h_key, "DisplayName", 0, REG_SZ, (void*)reg_disp_name_drv, sizeof reg_disp_name_drv);
			RegCloseKey(h_key);
			write_context_menu(L"", TRUE);
		}
	}

	set_uninstall_text(t[U_MSG_4]);

	// shortcuts
	dir[0] = 0;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS) {
		data_size = sizeof dir;
		RegQueryValueEx(h_key, L"DisplayIcon", NULL, NULL, (void*)&dir, &data_size);
		RegCloseKey(h_key);
		wcscpy(wcsrchr(dir, '\\') + 1, L"lang.txt");
		if (silent_uninstall) load_lang(dir);
	}
	startmenu_ptr = PathAddBackslash(startmenu);
	startmenu_all_ptr = PathAddBackslash(startmenu_all);
	for (i = SHORTCUT_1; i <= SHORTCUT_5 + 1; i++) {
		_snwprintf(startmenu_ptr, 99, i > SHORTCUT_5 ? L"ImDisk Virtual Disk Driver.lnk" : i == SHORTCUT_3 ? L"%.94s.url" : L"%.94s.lnk", t[i]);
		DeleteFile(startmenu);
		wcscpy(startmenu_all_ptr, startmenu_ptr);
		DeleteFile(startmenu_all);
		if (i >= SHORTCUT_4) {
			wcscpy(desk_ptr, startmenu_ptr);
			DeleteFile(desk);
			wcscpy(desk_all_ptr, startmenu_ptr);
			DeleteFile(desk_all);
		}
	}
	*(startmenu_ptr - 1) = 0;
	RemoveDirectory(startmenu);
	*(startmenu_all_ptr - 1) = 0;
	RemoveDirectory(startmenu_all);

	// files
	if (dir[0]) {
		wcscpy(path, dir);
		path_name_ptr = wcsrchr(path, '\\') + 1;
		remove_discutils();
		for (i = 0; i < _countof(file_list) - 1; i++)
			del(file_list[i]);
		wcscpy(path_name_ptr, L"config.exe");
		*(wcsrchr(dir, '\\')) = 0;
		_snwprintf(cmd, _countof(cmd) - 1, L"cmd /c \"for /l %%I in (0,0,1) do (del \"%s\"&rd \"%s\"&if not exist \"%s\" exit)\"", path, dir, path);
		del_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp");
		start_process(FALSE);
	}

	if (!silent_uninstall) EndDialog(hwnd_uninst, 0);
	return 0;
}


static INT_PTR __stdcall UninstallProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			// set localized strings
			if (t[0]) {
				SetWindowText(hDlg, t[U_TITLE]);
				SetDlgItemText(hDlg, ID_CHECK1, t[U_CTRL_1]);
				SetDlgItemText(hDlg, ID_CHECK2, t[U_CTRL_2]);
				SetDlgItemText(hDlg, ID_CHECK3, t[U_CTRL_3]);
				SetDlgItemText(hDlg, IDOK, t[U_CTRL_4]);
				SetDlgItemText(hDlg, IDCANCEL, t[U_CTRL_5]);
			}

			for (i = ID_CHECK1; i <= ID_CHECK3; i++)
				CheckDlgButton(hDlg, i, BST_CHECKED);

			hwnd_uninst = hDlg;

			return TRUE;

		case WM_COMMAND:
			if (process_uninst) return TRUE;

			if (LOWORD(wParam) == IDOK) {
				process_uninst = TRUE;
				for (i = ID_CHECK1; i <= ID_CHECK3; i++)
					ShowWindow(GetDlgItem(hDlg, i), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, ID_STATIC1), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDOK), SW_HIDE);
				ShowWindow(GetDlgItem(hDlg, IDCANCEL), SW_HIDE);
				CreateThread(NULL, 0, uninstall, NULL, 0, NULL);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}


static void write_file_reg_tmp(WCHAR *value)
{
	DWORD data_size, bytes_written;
	unsigned char *ptr;
	WCHAR txt[16];

	data_size = sizeof path;
	if (RegQueryValueEx(reg_key, value, NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS) {
		_snwprintf(txt, _countof(txt), L"\"%s\"=hex(2):", value);
		WriteFile(h_file, txt, wcslen(txt) * sizeof(WCHAR), &bytes_written, NULL);
		ptr = (unsigned char*)&path;
		while (data_size--) {
			_snwprintf(txt, _countof(txt), L"%02x%s", *ptr, data_size ? L"," : L"\r\n");
			WriteFile(h_file, txt, wcslen(txt) * sizeof(WCHAR), &bytes_written, NULL);
			ptr++;
		}
	}
}

static INT_PTR __stdcall SettingsProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	RECT ctrl1, ctrl2, ctrl3;
	HFONT font;
	DWORD data_size, bytes_written;
	DWORD show_explorer = 1, dlg_flags = 0, hidden_drives = 0;
	OPENFILENAME ofn = {sizeof ofn};
	BOOL disp_warn;
	HANDLE h;
	LARGE_INTEGER file_size;
	WCHAR *buf;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SetDlgItemText(hDlg, ID_TEXT1, version_str);
			EnableWindow(GetDlgItem(hDlg, ID_TEXT1), FALSE);
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			// set localized strings
			if (t[0]) {
				SetWindowText(hDlg, t[S_TITLE]);
				SetDlgItemText(hDlg, ID_STATIC1, t[S_CTRL_0]);
				SetDlgItemText(hDlg, ID_CHECK1, t[S_CTRL_1]);
				SetDlgItemText(hDlg, ID_CHECK2, t[S_CTRL_2]);
				SetDlgItemText(hDlg, ID_PBUTTON1, t[S_CTRL_3]);
				SetDlgItemText(hDlg, ID_STATIC2, t[S_CTRL_4]);
				SetDlgItemText(hDlg, ID_PBUTTON2, t[S_CTRL_5]);
				SetDlgItemText(hDlg, ID_CHECK3, t[S_CTRL_6]);
				SetDlgItemText(hDlg, IDOK, t[S_CTRL_7]);
				SetDlgItemText(hDlg, IDCANCEL, t[S_CTRL_8]);
			}

			// create list of letters
			GetWindowRect(GetDlgItem(hDlg, ID_CHECK_A), &ctrl1);
			GetWindowRect(GetDlgItem(hDlg, ID_CHECK_A + 1), &ctrl2);
			GetWindowRect(GetDlgItem(hDlg, ID_TEXT2), &ctrl3);
			ctrl2.left -= ctrl1.left;
			ctrl1.right -= ctrl1.left;
			ctrl1.bottom -= ctrl1.top;
			ctrl3.right -= ctrl3.left;
			ctrl3.bottom -= ctrl3.top;
			ScreenToClient(hDlg, (POINT*)&ctrl1);
			ScreenToClient(hDlg, (POINT*)&ctrl3);
			font = (HFONT)SendMessage(GetDlgItem(hDlg, ID_TEXT2), WM_GETFONT, 0, 0);
			cmd[1] = 0;
			for (i = 2; i < 26; i++) {
				CreateWindow(WC_BUTTON, NULL, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, ctrl1.left + i * ctrl2.left, ctrl1.top, ctrl1.right, ctrl1.bottom, hDlg, (HMENU)(size_t)(ID_CHECK_A + i), hinst, NULL);
				cmd[0] = 'A' + i;
				SendMessage(CreateWindow(WC_STATIC, cmd, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, ctrl3.left + i * ctrl2.left, ctrl3.top, ctrl3.right, ctrl3.bottom, hDlg, NULL, hinst, NULL), WM_SETFONT, (WPARAM)font, TRUE);
			}

			if (RegOpenKeyExA(HKEY_CLASSES_ROOT, "Drive\\shell\\ImDiskSaveImage\\command", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &reg_key) == ERROR_SUCCESS) {
				CheckDlgButton(hDlg, ID_CHECK1, BST_CHECKED);
				RegCloseKey(reg_key);
			}

			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &reg_key) == ERROR_SUCCESS) {
				data_size = sizeof show_explorer;
				RegQueryValueExA(reg_key, "ShowExplorer", NULL, NULL, (void*)&show_explorer, &data_size);
				data_size = sizeof dlg_flags;
				RegQueryValueExA(reg_key, "DlgFlags", NULL, NULL, (void*)&dlg_flags, &data_size);
				RegCloseKey(reg_key);
			}
			CheckDlgButton(hDlg, ID_CHECK2, !show_explorer);
			EnableWindow(GetDlgItem(hDlg, ID_PBUTTON1), dlg_flags);

			RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_WOW64_64KEY, NULL, &reg_key, NULL);
			data_size = sizeof hidden_drives;
			RegQueryValueExA(reg_key, "NoDrives", NULL, NULL, (void*)&hidden_drives, &data_size);			
			RegCloseKey(reg_key);
			hid_drive_ini = hidden_drives;
			for (i = 0; i < 26; i++) {
				if (hidden_drives & 1)
					CheckDlgButton(hDlg, ID_CHECK_A + i, BST_CHECKED);
				hidden_drives >>= 1;
			}

			return TRUE;

		case WM_COMMAND:
			for (i = 25; i >= 0; i--) {
				hidden_drives <<= 1;
				if (IsDlgButtonChecked(hDlg, ID_CHECK_A + i))
					hidden_drives++;
			}
			disp_warn = hidden_drives != hid_drive_ini;
			SetDlgItemText(hDlg, ID_TEXT1, disp_warn ? t[S_CTRL_9] : version_str);
			EnableWindow(GetDlgItem(hDlg, ID_TEXT1), disp_warn);

			if (LOWORD(wParam) == ID_PBUTTON1) {
				if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &reg_key) == ERROR_SUCCESS) {
					RegDeleteValueA(reg_key, "DlgFlags");
					RegCloseKey(reg_key);
				}
				EnableWindow(GetDlgItem(hDlg, ID_PBUTTON1), FALSE);
			}

			if (LOWORD(wParam) == ID_PBUTTON2) {
				path[0] = 0;
				ofn.hwndOwner = hDlg;
				ofn.lpstrFilter = L"Reg Files (*.reg)\0*.reg\0All Files (*.*)\0*.*\0";
				ofn.lpstrFile = path;
				ofn.nMaxFile = MAX_PATH;
				ofn.Flags = OFN_OVERWRITEPROMPT;
				ofn.lpstrDefExt = L"reg";
				if (GetSaveFileName(&ofn)) {
					DeleteFile(path);
					_snwprintf(cmd, _countof(cmd) - 1, L"reg export HKLM\\SOFTWARE\\ImDisk \"%s\"%s", path, os_ver.dwMajorVersion >= 6 ? L" /y" : L"");
					start_process(TRUE);
					if ((h_file = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE) {
						if (!GetLastError())
							WriteFile(h_file, L"\xFEFFWindows Registry Editor Version 5.00\r\n\r\n", 82, &bytes_written, NULL);

						i = 0; do _snwprintf(path_prev, _countof(path_prev), L"%s%d", path, i++); while (PathFileExists(path_prev));
						_snwprintf(cmd, _countof(cmd) - 1, L"reg export HKLM\\SYSTEM\\CurrentControlSet\\Services\\ImDisk\\Parameters \"%s\"%s", path_prev, os_ver.dwMajorVersion >= 6 ? L" /y" : L"");
						start_process(TRUE);
						if ((h = CreateFile(path_prev, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL)) != INVALID_HANDLE_VALUE) {
							GetFileSizeEx(h, &file_size);
							if (!file_size.HighPart) {
								buf = VirtualAlloc(NULL, file_size.LowPart, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
								ReadFile(h, buf, file_size.LowPart, &data_size, NULL);
								for (i = 0; i < data_size; i++)
									if (buf[i] == '[') break;
								WriteFile(h_file, &buf[i], file_size.LowPart - i * sizeof(WCHAR), &bytes_written, NULL);
								VirtualFree(buf, 0, MEM_RELEASE);
							}
							CloseHandle(h);
						}

						if (hidden_drives) {
							_snwprintf(cmd, _countof(cmd) - 1, L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer]\r\n\"NoDrives\"=dword:%08x\r\n\r\n", hidden_drives);
							WriteFile(h_file, cmd, wcslen(cmd) * sizeof(WCHAR), &bytes_written, NULL);
						}
						if (IsDlgButtonChecked(hDlg, ID_CHECK3)) {
							WriteFile(h_file, L"[HKEY_CURRENT_USER\\Environment]\r\n", 66, &bytes_written, NULL);
							RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_QUERY_VALUE, &reg_key);
							write_file_reg_tmp(L"TMP");
							write_file_reg_tmp(L"TEMP");
							RegCloseKey(reg_key);

							WriteFile(h_file, L"\r\n[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment]\r\n", 170, &bytes_written, NULL);
							RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE, &reg_key);
							write_file_reg_tmp(L"TMP");
							write_file_reg_tmp(L"TEMP");
							RegCloseKey(reg_key);
						}
						CloseHandle(h_file);
					}
				}
			}

			if (LOWORD(wParam) == IDOK) {
				if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
					// retrieve install path
					RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImDiskApp", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &reg_key);
					data_size = sizeof path;
					RegQueryValueEx(reg_key, L"DisplayIcon", NULL, NULL, (void*)&path, &data_size);
					RegCloseKey(reg_key);
					*(wcsrchr(path, '\\') + 1) = 0;

					write_context_menu(path, FALSE);
				} else {
					del_command_key("*\\shell\\ImDiskMountFile");
					del_command_key("Drive\\shell\\ImDiskSaveImage");
					del_command_key("Drive\\shell\\ImDiskUnmount");
				}

				if (IsDlgButtonChecked(hDlg, ID_CHECK2)) {
					if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &reg_key, NULL) == ERROR_SUCCESS) {
						show_explorer = 0;
						RegSetValueExA(reg_key, "ShowExplorer", 0, REG_DWORD, (void*)&show_explorer, sizeof show_explorer);
						RegCloseKey(reg_key);
					}
				} else {
					if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &reg_key) == ERROR_SUCCESS) {
						RegDeleteValueA(reg_key, "ShowExplorer");
						RegCloseKey(reg_key);
					}
				}

				RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &reg_key);
				if (hidden_drives)
					RegSetValueExA(reg_key, "NoDrives", 0, REG_DWORD, (void*)&hidden_drives, sizeof hidden_drives);
				else
					RegDeleteValueA(reg_key, "NoDrives");
				RegCloseKey(reg_key);

				EndDialog(hDlg, 0);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		default:
			return FALSE;
	}
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int i, argc;
	LPWSTR *argv;
	WCHAR *cmdline_ptr;
	SHELLEXECUTEINFO sei;

	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os_ver);

	hinst = GetModuleHandle(NULL);
	hIcon = LoadImage(hinst, MAKEINTRESOURCE(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);

	cmdline_ptr = GetCommandLine();
	argv = CommandLineToArgvW(cmdline_ptr, &argc);

	if (argc > 1) {
		if (!_wcsicmp(argv[1], L"/version")) {
			puts(APP_VERSION);
			ExitProcess(APP_NUMBER);
		}
		if (wcscmp(argv[1], L"/UAC") && wcscmp(argv[1], L"/u") && _wcsicmp(argv[1], L"/silentuninstall") && _wcsicmp(argv[1], L"/silent") && _wcsicmp(argv[1], L"/fullsilent") &&
			_wcsnicmp(argv[1], L"/installfolder:", 15) && _wcsnicmp(argv[1], L"/discutils:", 11) && _wcsnicmp(argv[1], L"/ramdiskui:", 11) && _wcsnicmp(argv[1], L"/menu_entries:", 14) &&
			_wcsnicmp(argv[1], L"/shortcuts_desktop:", 19) && _wcsnicmp(argv[1], L"/shortcuts_all:", 15) && _wcsnicmp(argv[1], L"/lang:", 6))
			goto syntax_help;
	}

	if (os_ver.dwMajorVersion >= 6) {
		if (argc <= 1 || wcscmp(argv[1], L"/UAC")) {
			_snwprintf(cmd, _countof(cmd) - 1, L"/UAC %s", cmdline_ptr);
			sei.cbSize = sizeof sei;
			sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
			sei.hwnd = NULL;
			sei.lpVerb = L"runas";
			sei.lpFile = argv[0];
			sei.lpParameters = cmd;
			sei.lpDirectory = NULL;
			sei.nShow = SW_SHOWDEFAULT;
			ShellExecuteEx(&sei);
			WaitForSingleObject(sei.hProcess, INFINITE);
			ExitProcess(0);
		}
		argc -= 2, argv += 2;
	}

	PathRemoveFileSpec(argv[0]);
	SetCurrentDirectory(argv[0]);
	SHGetFolderPath(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desk);
	desk_ptr = PathAddBackslash(desk);
	SHGetFolderPath(NULL, CSIDL_COMMON_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desk_all);
	desk_all_ptr = PathAddBackslash(desk_all);
	SHGetFolderPath(NULL, CSIDL_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, startmenu);
	wcscat(startmenu, L"\\ImDisk");
	SHGetFolderPath(NULL, CSIDL_COMMON_PROGRAMS, NULL, SHGFP_TYPE_CURRENT, startmenu_all);
	wcscat(startmenu_all, L"\\ImDisk");

	if (argc > 1 && !_wcsicmp(argv[1], L"/silentuninstall")) {
		silent_uninstall = TRUE;
		uninstall(NULL);
	} else if (PathFileExists(L"driver")) {
		while (--argc > 0) {
			argv++;
			if (!_wcsicmp(argv[0], L"/silent")) silent = 1;
			else if (!_wcsicmp(argv[0], L"/fullsilent")) silent = 2;
			else if (!_wcsnicmp(argv[0], L"/installfolder:", 15)) path_cmdline = &argv[0][15];
			else if (!_wcsnicmp(argv[0], L"/discutils:", 11)) opt_cmd_mountimg = argv[0][11] - '0' + 1;
			else if (!_wcsnicmp(argv[0], L"/ramdiskui:", 11)) opt_cmd_ramdiskui = argv[0][11] - '0' + 1;
			else if (!_wcsnicmp(argv[0], L"/menu_entries:", 14)) opt_cmd_menus = argv[0][14] - '0' + 1;
			else if (!_wcsnicmp(argv[0], L"/shortcuts_desktop:", 19)) opt_cmd_shortcutdesk = argv[0][19] - '0' + 1;
			else if (!_wcsnicmp(argv[0], L"/shortcuts_all:", 15)) opt_cmd_shortcutall = argv[0][15] - '0' + 1;
			else if (!_wcsnicmp(argv[0], L"/lang:", 6)) {
				for (i = 0; i < _countof(lang_file_list); i++)
					if (!_wcsicmp(&argv[0][6], lang_file_list[i])) {
						n_lang = i;
						lang_cmdline = TRUE;
					}
			} else {
syntax_help:
				MessageBoxA(NULL, "Switches:\n\n/silent\nSilent installation. Error messages and reboot prompt are still displayed.\n\n"
								  "/fullsilent\nSilent installation, without error message or prompt. No reboot will occur, even if files are in use.\n\n"
								  "/installfolder:\"path\"\nSet the installation folder.\n\n/lang:name\nBypass automatic language detection. 'name' is one of the available languages.\n\n"
								  "/OPTION:[0|1]\nForce an installation option to be selected or not.\nOPTION can be one of the followings: discutils, ramdiskui, menu_entries, shortcuts_desktop, shortcuts_all.\n\n"
								  "/silentuninstall\nSilent uninstallation. Driver (and therefore all existing virtual disk) and parameters are removed. This switch can also be passed to config.exe.\n\n"
								  "/version\nReturn the application version in the standard output and the exit code.",
								  "ImDisk - Setup", MB_ICONINFORMATION);
				ExitProcess(0);
			}
		}
		CoInitialize(NULL);
		DialogBox(hinst, L"INSTALLBOX", NULL, InstallProc);
	} else if (argc > 1 && !wcscmp(argv[1], L"/u")) {
		load_lang(L"lang.txt");
		DialogBox(hinst, L"UNINSTALLBOX", NULL, UninstallProc);
	} else {
		load_lang(L"lang.txt");
		DialogBox(hinst, L"SETTINGSBOX", NULL, SettingsProc);
	}

	ExitProcess(0);
}
