#define _WIN32_WINNT 0x0600
#define OEMRESOURCE
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <winternl.h>
#include <ntstatus.h>
#include <ntdef.h>
#include <aclapi.h>
#include <ntsecapi.h>
#include <intrin.h>
#include "resource.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdisktk.h"

typedef union {struct {unsigned char filesystem, compress, pad[2];}; unsigned long l;} FS_DATA;

static SERVICE_STATUS_HANDLE SvcStatusHandle;
static SERVICE_STATUS SvcStatus = {SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, SERVICE_ACCEPT_STOP, NO_ERROR, 0, 0, 3600000};
static SC_HANDLE h_scman, h_svc;

static OSVERSIONINFO os_ver;
static HINSTANCE hinst;
static HICON hIcon, hIcon_warn;
static _Bool init1_ok = FALSE, init2_ok = FALSE, init3_ok = FALSE, apply_ok = TRUE;
static HKEY registry_key;
static FARPROC ImDisk_OpenDeviceByMountPoint, ImDisk_NotifyShellDriveLetter;
static SECURITY_ATTRIBUTES sa;

static UINT min_size[4][9] = {{2761, 2752, 2754, 2757, 2761, 2785, 2849, 2977, 3009},
							  {65, 65, 65, 65, 65, 65, 65, 65, 65}, // 20 KB for FAT, but minimal volume size for driver 2.0.8 is 65537
							  {36860, 36860, 69623, 135150, 266204, 528312, 1052528, 2100960, 4197824},
							  {181, 181, 181, 181, 181, 208, 288, 448, 768}};
static UINT max_size[2][9] = {{4194003, 33039, 65802, 131329, 262383, 524491, 1048707, 2097139, 4194003},
							  {33555455, 2125824, 4214784, 8392705, 16744451, 33456135, 33555455, 33555455, 33555455}};
static char unit_list[] = {'K', 'M', 'G'};
static WCHAR *filesystem_list[] = {L"NTFS", L"FAT", L"FAT32", L"exFAT"};
static char *compress_list[] = {"", "/c "};
static char *cluster_list[] = {"", "/a:512", "/a:1024", "/a:2048", "/a:4096", "/a:8192", "/a:16k", "/a:32k", "/a:64k"};
static char *quickf_list[] = {"", "/q "};
static char *awe_list[] = {"", "-o awe "};
static char *fileawe_list[] = {"-t vm", "-o awe"};
static WCHAR *param_list[] = {L"DriveSize", L"Unit", L"Dynamic", L"FileSystem", L"TempFolder", L"Cluster", L"Label", L"QuickFormat", L"Awealloc",
							  L"DynMethod", L"CleanRatio", L"CleanTimer", L"MaxActivity", L"BlockSize", L"RDMountPoint", L"AddParam", L"ImageFile"};
static WCHAR *param_list_sync[] = {L"ImageFile", L"SyncFlags", L"SyncExcluded", L"RDMountPoint", L"VolumeID"};
static FS_DATA fsys = {};
static DWORD drive_size = 64, unit = 1, dynamic = FALSE, wanted_drive = 'R', win_boot = TRUE, temp_folder = TRUE;
static DWORD cluster = 0, quick_format = FALSE, awealloc = FALSE, use_mount_point = 0, mount_current = FALSE, sync_flags = 0x6, volume_id;
static DWORD dyn_method = 0, clean_ratio = 10, clean_timer = 10, max_activity = 10, block_size = 20;
static WCHAR label[33], mount_point[MAX_PATH] = {}, image_file[MAX_PATH + 1] = {}, run_object[MAX_PATH] = {}, add_param[255] = {}, sync_excluded[500];
static DWORD reg_dynamic, reg_win_boot, reg_awealloc, reg_use_MP, reg_image_file, reg_sync_flags;
static BOOL mount_file, mount_dir;
static WCHAR drive_list[26][4] = {}, drive_select[3];
static UINT drive_default;
static DWORD mask0, show_explorer = 1;

static WCHAR svc_cmd_line[MAX_PATH + 16], hlp_svc_path[MAX_PATH + 4], key_name[16];
static HWND hwnd[4];
static BOOL item_enable;
static HWND hwnd_edit1, hwnd_edit2, hwnd_edit3, hwnd_edit4, hwnd_edit5, hwnd_edit6, hwnd_check1, hwnd_check2, hwnd_check3, hwnd_check4, hwnd_check5, hwnd_check6, hwnd_check7, hwnd_check8;
static HWND hwnd_combo2, hwnd_combo3, hwnd_combo5, hwnd_pbutton2, hwnd_pbutton3, hwnd_pbutton7, hwnd_edit11, hwnd_edit12, hwnd_edit13, hwnd_edit14;
static COMBOBOXINFO combo4;

static RECT circle = {13, 208, 18, 213}, icon_coord = {};
static COLORREF color;
static HWND hTTip;
static WCHAR TTip_txt[200] = {};
static _Bool ttip_on_disabled_ctrl;


enum {
	TITLE, PS_OK, PS_EXIT,
	TAB1_0, TAB1_1, TAB1_2, TAB1_3, TAB1_4, TAB1_5, TAB1_6, TAB1_7, TAB1_8, TAB1_9, TAB1_10, TAB1_11,
	TTIP1_1, TTIP1_2, TTIP1_3, TTIP1_4, TTIP1_5, TTIP1_6, TTIP1_7,
	TAB2_0, TAB2_1, TAB2_2, TAB2_3, TAB2_4, TAB2_5, TAB2_6, TAB2_7, TAB2_8, TAB2_9, TAB2_10,
	CLUST_0, CLUST_1, CLUST_2, CLUST_3, CLUST_4, CLUST_5, CLUST_6, CLUST_7, CLUST_8,
	TTIP2_1, TTIP2_2, TTIP2_3, TTIP2_4, TTIP2_5, TTIP2_6, TTIP2_7, TTIP2_8, TTIP2_9, TTIP2_10,
	TAB3_0, TAB3_1, TAB3_2, TAB3_3, TAB3_4, TAB3_5, TAB3_6,
	TTIP3_1, TTIP3_2, TTIP3_3, TTIP3_4,
	DYNAWE_1, DYNAWE_2, DYNAWE_3,
	MSG_0, MSG_1, MSG_2, MSG_3, MSG_4, MSG_5, MSG_6, MSG_7, MSG_8, MSG_9, MSG_10, MSG_11, MSG_12, MSG_13, MSG_14, MSG_15, MSG_16, MSG_17, MSG_18, MSG_19, MSG_20, MSG_21, MSG_22, MSG_23, MSG_24, MSG_25, MSG_26,
	TEMP_1, TEMP_2, TEMP_3, TEMP_4, TEMP_5, TEMP_6, TEMP_7, TEMP_8, TEMP_9, TEMP_10, TEMP_11,
	DYN_1, DYN_2, DYN_3, DYN_4, DYN_5, DYN_6, DYN_7, DYN_8, DYN_9, DYN_10, DYN_11, DYN_12, DYN_13, DYN_14,
	DYNTT_1, DYNTT_2, DYNTT_3, DYNTT_4, DYNTT_5,
	WARN_1, WARN_2, WARN_3, WARN_4,
	NB_TXT
};
static WCHAR *t[NB_TXT] = {};

static void load_lang()
{
	HANDLE h;
	LARGE_INTEGER size;
	WCHAR *buf, *current_str, *context;
	DWORD dw;
	int i;

	if ((h = CreateFile(L"lang.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
		MessageBoxA(NULL, "Warning: cannot find lang.txt.", "ImDisk", MB_ICONERROR);
		return;
	}
	GetFileSizeEx(h, &size);
	if (size.QuadPart > 1 << 17) {
		CloseHandle(h);
		return;
	}
	buf = VirtualAlloc(NULL, size.QuadPart + sizeof(WCHAR), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	ReadFile(h, buf, size.LowPart, &dw, NULL);
	CloseHandle(h);
	if (!(current_str = wcsstr(buf, L"[RamDiskUI]"))) return;
	wcstok(current_str, L"\r\n", &context);
	for (i = 0; i < NB_TXT; i++)
		t[i] = wcstok(NULL, L"\r\n", &context);
	size.LowPart /= sizeof(WCHAR);
	for (i = 0; i < size.LowPart; i++)
		if (buf[i] == '#') buf[i] = '\n';
}


static DWORD start_process(WCHAR *cmd, BYTE flag)
{
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	TOKEN_LINKED_TOKEN lt = {};
	HANDLE token;
	DWORD dw;
	BOOL result;

	if (flag == 2 && (dw = WTSGetActiveConsoleSessionId()) != -1 && WTSQueryUserToken(dw, &token)) {
		if (!GetTokenInformation(token, TokenLinkedToken, &lt, sizeof lt, &dw) ||
			!(result = CreateProcessAsUser(lt.LinkedToken, NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)))
			result = CreateProcessAsUser(token, NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
		CloseHandle(token);
		CloseHandle(lt.LinkedToken);
	} else
		result = CreateProcess(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	dw = 1;
	if (result) {
		if (flag) {
			WaitForSingleObject(pi.hProcess, INFINITE);
			GetExitCodeProcess(pi.hProcess, &dw);
		}
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	return dw;
}

static void reg_set_dword(WCHAR *name, DWORD *value)
{
	RegSetValueEx(registry_key, name, 0, REG_DWORD, (void*)value, sizeof(DWORD));
}

static long reg_query_dword(WCHAR *name, DWORD *value)
{
	DWORD data_size = sizeof(DWORD);

	return RegQueryValueEx(registry_key, name, NULL, NULL, (void*)value, &data_size);
}

static long param_reg_query_dword(WCHAR *name, DWORD *value)
{
	wcscpy(&key_name[2], name);
	return reg_query_dword(key_name, value);
}

static BOOL is_faststartup_enabled()
{
	HKEY reg_key;
	DWORD value = 0, data_size = sizeof value;

	reg_query_dword(L"DlgFlags", &value);
	if (value & FLAG_FASTSTARTUP) return FALSE;
	if (!PathFileExists(L"C:\\hiberfil.sys") && GetLastError() == ERROR_FILE_NOT_FOUND) return FALSE;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power", 0, KEY_QUERY_VALUE, &reg_key) != ERROR_SUCCESS) return FALSE;
	value = 0;
	RegQueryValueEx(reg_key, L"HiberbootEnabled", NULL, NULL, (void*)&value, &data_size);
	RegCloseKey(reg_key);
	return value;
}

static BOOL is_trim_enabled()
{
	HKEY reg_key;
	DWORD value = 1, data_size = sizeof value;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\FileSystem", 0, KEY_QUERY_VALUE, &reg_key) != ERROR_SUCCESS) return FALSE;
	RegQueryValueEx(reg_key, L"DisableDeleteNotification", NULL, NULL, (void*)&value, &data_size);
	RegCloseKey(reg_key);
	return !value;
}

static void circ_draw(HWND h_wnd)
{
	HDC hdc;
	PAINTSTRUCT paint;
	HBRUSH brush;

	hdc = BeginPaint(h_wnd, &paint);
	brush = CreateSolidBrush(color);
	SelectObject(hdc, brush);
	Ellipse(hdc, circle.left, circle.top, circle.right, circle.bottom);
	DeleteObject(brush);
	EndPaint(h_wnd, &paint);
}

static void notif(COLORREF c, WCHAR *text)
{
	int i;

	color = c;
	for (i = 1; i <= 3; i++) {
		RedrawWindow(hwnd[i], &circle, NULL, RDW_INVALIDATE);
		SetDlgItemText(hwnd[i], ID_TEXT1, text);
	}
}

static HWND add_tooltip(TOOLINFO *ti)
{
	HWND hwnd_ctrl;

	ti->cbSize = sizeof(TOOLINFO);
	hwnd_ctrl = ti->uId == ID_COMBO4 ? combo4.hwndItem : GetDlgItem(ti->hwnd, ti->uId);
	ti->lpszText = LPSTR_TEXTCALLBACK;

	if (ttip_on_disabled_ctrl) {
		ti->uFlags = TTF_SUBCLASS;
		GetWindowRect(hwnd_ctrl, &ti->rect);
		ScreenToClient(ti->hwnd, (LPPOINT)&ti->rect);
		ScreenToClient(ti->hwnd, ((LPPOINT)&ti->rect) + 1);
		SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)ti);
	}

	ti->uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	ti->uId = (UINT_PTR)hwnd_ctrl;
	SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)ti);

	return hwnd_ctrl;
}

static void remove_reg_param(WCHAR item_to_remove)
{
	WCHAR param_name[16];
	int i;

	param_name[0] = item_to_remove;
	param_name[1] = '_';
	for (i = 0; i < _countof(param_list); i++) {
		wcscpy(&param_name[2], param_list[i]);
		RegDeleteValue(registry_key, param_name);
	}
	for (i = 0; i < _countof(param_list_sync); i++) {
		wcscpy(&param_name[2], param_list_sync[i]);
		RegDeleteValue(registry_key, param_name);
	}
}

static void copy_list_param(WCHAR *param_name, BOOL copy_sync)
{
	WCHAR **list = copy_sync ? param_list_sync : param_list;
	char data[600];
	DWORD data_size, type;
	int i, j;

	j = copy_sync ? _countof(param_list_sync) : _countof(param_list);
	for (i = 0; i < j; i++) {
		wcscpy(&param_name[2], list[i]);
		data_size = sizeof data;
		RegQueryValueEx(registry_key, list[i], NULL, &type, (void*)&data, &data_size);
		RegSetValueEx(registry_key, param_name, 0, type, (void*)&data, data_size);
	}
}

static BOOL is_MP_imdisk_device()
{
	HANDLE h;

	if (!ImDisk_OpenDeviceByMountPoint) return FALSE;
	CloseHandle(h = (HANDLE)ImDisk_OpenDeviceByMountPoint(mount_point, GENERIC_READ));
	return h != INVALID_HANDLE_VALUE;
}

__declspec(noreturn) static void configure_services_and_exit()
{
	BOOL RD_found, sync_found;
	SERVICE_DESCRIPTION svc_description;
	SERVICE_PRESHUTDOWN_INFO svc_preshutdown_info;
	DWORD dw;

	RD_found = mount_current;
	sync_found = reg_sync_flags & 1 && reg_image_file;
	key_name[1] = '_';
	for (key_name[0] = '0'; key_name[0] <= 'Z'; key_name[0] == '9' ? key_name[0] = 'A' : key_name[0]++) {
		if (param_reg_query_dword(L"Awealloc", &dw) == ERROR_SUCCESS) RD_found = TRUE;
		if (param_reg_query_dword(L"SyncFlags", &dw) == ERROR_SUCCESS) sync_found = TRUE;
	}

	if (!RD_found) DeleteService(h_svc);
	CloseServiceHandle(h_svc);

	h_svc = OpenService(h_scman, L"ImDiskTk-svc", SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP | DELETE);
	if (sync_found) {
		if (h_svc)
			ChangeServiceConfig(h_svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE, SERVICE_ERROR_NORMAL, hlp_svc_path, NULL, NULL, L"ImDisk\0", NULL, NULL, NULL);
		else {
			h_svc = CreateService(h_scman, L"ImDiskTk-svc", L"ImDisk Toolkit helper service", SERVICE_CHANGE_CONFIG | SERVICE_START, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
								  hlp_svc_path, NULL, NULL, L"ImDisk\0", NULL, NULL);
			if (h_svc) {
				svc_description.lpDescription = L"Service used for data synchronization at system shutdown.";
				ChangeServiceConfig2(h_svc, SERVICE_CONFIG_DESCRIPTION, &svc_description);
				if (os_ver.dwMajorVersion >= 6) {
					svc_preshutdown_info.dwPreshutdownTimeout = 3600000;
					ChangeServiceConfig2(h_svc, SERVICE_CONFIG_PRESHUTDOWN_INFO, &svc_preshutdown_info);
				}
			} else
				MessageBox(NULL, t[MSG_23], L"ImDisk", MB_ICONERROR);
		}
		StartService(h_svc, 0, NULL);
	} else {
		ControlService(h_svc, SERVICE_CONTROL_STOP, &SvcStatus);
		DeleteService(h_svc);
	}
	CloseServiceHandle(h_svc);
	CloseServiceHandle(h_scman);

	ExitProcess(0);
}

static void load_mount_point()
{
	DWORD data_size;
	WCHAR param_name[16], path[MAX_PATH];

	SendDlgItemMessage(hwnd[2], ID_COMBO4, CB_RESETCONTENT, 0, 0);
	param_name[1] = '_';
	wcscpy(&param_name[2], L"RDMountPoint");
	for (param_name[0] = '0'; param_name[0] <= '9'; param_name[0]++) {
		data_size = sizeof path;
		if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
			SendDlgItemMessage(hwnd[2], ID_COMBO4, CB_ADDSTRING, 0, (LPARAM)path);
	}
	SetDlgItemText(hwnd[2], ID_COMBO4, mount_point);
	EnableWindow(hwnd_pbutton7, is_MP_imdisk_device());
}

static void remove_mount_point()
{
	DWORD data_size;
	WCHAR cmd_line[MAX_PATH + 20];
	WCHAR param_name[16];

	_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -D -m \"%s\"", mount_point);
	start_process(cmd_line, TRUE);
	param_name[1] = '_';
	wcscpy(&param_name[2], L"RDMountPoint");
	for (param_name[0] = '0'; param_name[0] <= '9'; param_name[0]++) {
		data_size = sizeof cmd_line;
		if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&cmd_line, &data_size) == ERROR_SUCCESS && !wcscmp(cmd_line, mount_point))
			remove_reg_param(param_name[0]);
	}
}

static void update_unmount_button()
{
	HANDLE h;
	DWORD version, data_size;
	WCHAR text[8];

	_snwprintf(text, _countof(text), L"\\\\.\\%s", drive_select);
	h = CreateFile(text, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	EnableWindow(hwnd_pbutton3, DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &version, sizeof version, &data_size, NULL));
	CloseHandle(h);
}

static DWORD __stdcall UnmountDrive(LPVOID lpParam)
{
	WCHAR cmd_line[24];

	notif(RGB(255, 255, 0), t[MSG_25]);
	_snwprintf(cmd_line, _countof(cmd_line), L"ImDisk-Dlg RM %s", drive_select);
	start_process(cmd_line, TRUE);
	if (!PathFileExists(drive_select)) {
		EnableWindow(hwnd_pbutton3, FALSE);
		SetFocus(hwnd[1]);
		if (drive_select[0] == wanted_drive) {
			mount_current = FALSE;
			reg_set_dword(L"RDMountCurrent", &mount_current);
		}
		remove_reg_param(drive_select[0]);
	}
	init1_ok = apply_ok = TRUE;
	notif(RGB(0, 255, 0), t[MSG_24]);
	return 0;
}

static DWORD __stdcall UnmountMP(LPVOID lpParam)
{
	notif(RGB(255, 255, 0), t[MSG_25]);
	remove_mount_point();
	load_mount_point();
	SetFocus(hwnd[2]);
	init2_ok = apply_ok = TRUE;
	notif(RGB(0, 255, 0), t[MSG_24]);
	return 0;
}

static BOOL SeLockMemoryPrivilege_required()
{
	WCHAR privilege_name[] = L"SeLockMemoryPrivilege";
	HANDLE token = INVALID_HANDLE_VALUE;
	TOKEN_PRIVILEGES tok_priv;
	LSA_HANDLE lsa_h = INVALID_HANDLE_VALUE;
	LSA_OBJECT_ATTRIBUTES lsa_oa = {};
	unsigned char sid[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof sid;
	LSA_UNICODE_STRING lsa_str = {sizeof privilege_name - sizeof(WCHAR), sizeof privilege_name, privilege_name};

	if (!dynamic || !awealloc) return FALSE;
	tok_priv.PrivilegeCount = 1;
	tok_priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token) &&
		LookupPrivilegeValue(NULL, privilege_name, &tok_priv.Privileges[0].Luid) &&
		AdjustTokenPrivileges(token, FALSE, &tok_priv, 0, NULL, NULL) && GetLastError() == ERROR_SUCCESS) {
		CloseHandle(token);
		return FALSE;
	}
	CloseHandle(token);
	if (MessageBox(hwnd[0], t[DYNAWE_1], L"ImDisk", MB_OKCANCEL | MB_ICONWARNING) == IDOK) {
		if (CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, (SID*)sid, &sid_size) &&
			LsaOpenPolicy(NULL, &lsa_oa, POLICY_LOOKUP_NAMES, &lsa_h) == STATUS_SUCCESS &&
			LsaAddAccountRights(lsa_h, (SID*)sid, &lsa_str, 1) == STATUS_SUCCESS) {
			if (MessageBox(hwnd[0], t[DYNAWE_2], L"ImDisk", MB_YESNO | MB_ICONWARNING) == IDYES)
				ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OPERATINGSYSTEM | SHTDN_REASON_MINOR_RECONFIG | SHTDN_REASON_FLAG_PLANNED);
		} else {
			MessageBox(hwnd[0], t[DYNAWE_3], L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), t[DYNAWE_3]);
		}
		LsaClose(lsa_h);
	}
	return TRUE;
}

static DWORD __stdcall SyncThread(LPVOID lpParam)
{
	WCHAR cmd_line[] = L"ImDiskTk-svc SYNC";

	notif(RGB(255, 255, 0), t[MSG_26]);
	start_process(cmd_line, TRUE);
	init1_ok = init2_ok = init3_ok = apply_ok = TRUE;
	notif(RGB(0, 255, 0), t[MSG_24]);
	return 0;
}

static INT_PTR __stdcall VarProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HKEY reg_key;
	DWORD data_size;
	HANDLE h;
	WIN32_FIND_DATA FindFileData;
	WCHAR path[MAX_PATH + 8];
	struct {REPARSE_DATA_BUFFER rdb; WCHAR path[2 * MAX_PATH + 2];} reparse_buf;
	UNICODE_STRING nt_path;
	WCHAR *error_msg;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_RB1, t[TEMP_1]);
				SetDlgItemText(hDlg, ID_TEXT1, t[TEMP_2]);
				SetDlgItemText(hDlg, ID_TEXT2, t[TEMP_3]);
				SetDlgItemText(hDlg, ID_PBUTTON1, t[TEMP_4]);
				SetDlgItemText(hDlg, ID_RB2, t[TEMP_5]);
				SetDlgItemText(hDlg, ID_TEXT3, t[TEMP_6]);
				SetDlgItemText(hDlg, ID_TEXT4, t[TEMP_7]);
				SetDlgItemText(hDlg, ID_PBUTTON2, t[TEMP_8]);
				SetDlgItemText(hDlg, IDOK, t[TEMP_9]);
				SetDlgItemText(hDlg, IDCANCEL, t[TEMP_10]);
			}

			for (i = ID_EDIT1; i <= ID_EDIT5; i++)
				SendDlgItemMessage(hDlg, i, EM_SETLIMITTEXT, MAX_PATH, 0);

			path[_countof(path) - 1] = 0;
			RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_QUERY_VALUE, &reg_key);
			data_size = sizeof path;
			if (RegQueryValueEx(reg_key, L"TMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
				SetDlgItemText(hDlg, ID_EDIT1, path);
			RegCloseKey(reg_key);

			FindClose(h = FindFirstFile(path, &FindFileData));
			if (h != INVALID_HANDLE_VALUE && FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && FindFileData.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
				CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB2);
				SetDlgItemText(hDlg, ID_EDIT4, path);
				h = CreateFile(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
				ZeroMemory(&reparse_buf, sizeof reparse_buf);
				DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, &reparse_buf, sizeof reparse_buf, &data_size, NULL);
				CloseHandle(h);
				SetDlgItemText(hDlg, ID_EDIT5, reparse_buf.rdb.MountPointReparseBuffer.PathBuffer + (reparse_buf.rdb.MountPointReparseBuffer.PrintNameOffset >> 1));
			} else {
				CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB1);
				SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);
				wcscat(path, L"\\Temp");
				SetDlgItemText(hDlg, ID_EDIT4, path);
				_snwprintf(path, _countof(path) - 1, L"%s\\Temp", drive_select);
				SetDlgItemText(hDlg, ID_EDIT5, path);
			}

			RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_QUERY_VALUE, &reg_key);
			data_size = sizeof path;
			if (RegQueryValueEx(reg_key, L"TMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
				SetDlgItemText(hDlg, ID_EDIT2, path);
			RegCloseKey(reg_key);

			_snwprintf(path, _countof(path) - 1, L"%s\\Temp", drive_select);
			SetDlgItemText(hDlg, ID_EDIT3, path);

			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == ID_PBUTTON1) {
				GetDlgItemText(hDlg, ID_EDIT3, path, _countof(path));
				SetDlgItemText(hDlg, ID_EDIT1, path);
				SetDlgItemText(hDlg, ID_EDIT2, path);
			}

			if (LOWORD(wParam) == ID_PBUTTON2) {
				RegOpenKeyExA(HKEY_USERS, ".DEFAULT\\Environment", 0, KEY_QUERY_VALUE, &reg_key);
				data_size = sizeof path;
				if (RegQueryValueEx(reg_key, L"TMP", NULL, NULL, (void*)&path, &data_size) == ERROR_SUCCESS)
					SetDlgItemText(hDlg, ID_EDIT1, path);
				RegCloseKey(reg_key);
				SetDlgItemText(hDlg, ID_EDIT2, L"%SystemRoot%\\TEMP");
				CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB1);
			}

			if (LOWORD(wParam) == IDOK) {
				if (IsDlgButtonChecked(hDlg, ID_RB1)) {
					RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &reg_key);
					GetDlgItemText(hDlg, ID_EDIT1, path, _countof(path));
					RegSetValueEx(reg_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(WCHAR));
					RegSetValueEx(reg_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(WCHAR));
					RegCloseKey(reg_key);
					RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &reg_key);
					GetDlgItemText(hDlg, ID_EDIT2, path, _countof(path));
					RegSetValueEx(reg_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(WCHAR));
					RegSetValueEx(reg_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&path, (wcslen(path) + 1) * sizeof(WCHAR));
					RegCloseKey(reg_key);
				} else {
					GetDlgItemText(hDlg, ID_EDIT4, path, _countof(path));
					CreateDirectory(path, NULL);
					h = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
					GetDlgItemText(hDlg, ID_EDIT5, path, _countof(path));
					RtlDosPathNameToNtPathName_U(path, &nt_path, NULL, NULL);
					i = wcslen(path) << 1;
					ZeroMemory(&reparse_buf, sizeof reparse_buf);
					reparse_buf.rdb.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
					reparse_buf.rdb.ReparseDataLength = sizeof(reparse_buf.rdb.MountPointReparseBuffer) + nt_path.Length + i + 2;
					reparse_buf.rdb.MountPointReparseBuffer.SubstituteNameLength = nt_path.Length;
					reparse_buf.rdb.MountPointReparseBuffer.PrintNameOffset = nt_path.Length + 2;
					reparse_buf.rdb.MountPointReparseBuffer.PrintNameLength = i;
					memcpy(reparse_buf.rdb.MountPointReparseBuffer.PathBuffer, nt_path.Buffer, nt_path.Length);
					memcpy(reparse_buf.rdb.MountPointReparseBuffer.PathBuffer + (nt_path.Length >> 1) + 1, path, i);
					if (DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, &reparse_buf, 20 + nt_path.Length + i, NULL, 0, &data_size, NULL)) {
						RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_SET_VALUE, &reg_key);
						GetDlgItemText(hDlg, ID_EDIT4, path, _countof(path));
						i = (wcslen(path) + 1) * sizeof(WCHAR);
						RegSetValueEx(reg_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&path, i);
						RegSetValueEx(reg_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&path, i);
						RegCloseKey(reg_key);
						RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0, KEY_SET_VALUE, &reg_key);
						RegSetValueEx(reg_key, L"TEMP", 0, REG_EXPAND_SZ, (void*)&path, i);
						RegSetValueEx(reg_key, L"TMP", 0, REG_EXPAND_SZ, (void*)&path, i);
						RegCloseKey(reg_key);
					} else {
						FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(), 0, (LPTSTR)&error_msg, 0, NULL);
						_snwprintf(reparse_buf.path, _countof(reparse_buf.path) - 1, L"%s\n%s", t[TEMP_11] ? t[TEMP_11] : L"Error: cannot create junction.", error_msg);
						reparse_buf.path[_countof(reparse_buf.path) - 1] = 0;
						MessageBox(hDlg, reparse_buf.path, L"ImDisk", MB_ICONERROR);
						LocalFree(error_msg);
					}
					CloseHandle(h);
					LocalFree(nt_path.Buffer);
				}

				EndDialog(hDlg, 0);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			return TRUE;

		case WM_ENDSESSION:
			configure_services_and_exit();

		default:
			return FALSE;
	}
}

static INT_PTR __stdcall DynProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	TOOLINFO ti;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TEXT6, t[DYN_1]);
				SetDlgItemText(hDlg, ID_TITLE, t[DYN_5]);
				SetDlgItemText(hDlg, ID_TEXT7, t[DYN_6]);
				SetDlgItemText(hDlg, ID_TEXT1, t[DYN_7]);
				SetDlgItemText(hDlg, ID_TEXT2, t[DYN_8]);
				SetDlgItemText(hDlg, ID_TEXT3, t[DYN_9]);
				SetDlgItemText(hDlg, ID_TEXT4, t[DYN_10]);
				SetDlgItemText(hDlg, ID_TEXT5, t[DYN_11]);
				SetDlgItemText(hDlg, ID_TEXT8, t[DYN_12]);
				SetDlgItemText(hDlg, IDOK, t[DYN_13]);
				SetDlgItemText(hDlg, IDCANCEL, t[DYN_14]);
			}

			// add tooltips
			ttip_on_disabled_ctrl = FALSE;
			ti.hwnd = hDlg;
			ti.uId = ID_COMBO5;
			hwnd_combo5 = add_tooltip(&ti);
			ti.uId = ID_EDIT11;
			hwnd_edit11 = add_tooltip(&ti);
			ti.uId = ID_EDIT12;
			hwnd_edit12 = add_tooltip(&ti);
			ti.uId = ID_EDIT13;
			hwnd_edit13 = add_tooltip(&ti);
			ti.uId = ID_EDIT14;
			hwnd_edit14 = add_tooltip(&ti);

			// initialize controls
			for (i = DYN_2; i <= DYN_4; i++)
				SendMessage(hwnd_combo5, CB_ADDSTRING, 0, (LPARAM)t[i]);
			SendMessage(hwnd_combo5, CB_SETCURSEL, dyn_method, 0);
			SetDlgItemInt(hDlg, ID_EDIT11, block_size, FALSE);
			SetDlgItemInt(hDlg, ID_EDIT12, clean_ratio, FALSE);
			SetDlgItemInt(hDlg, ID_EDIT13, clean_timer, FALSE);
			SetDlgItemInt(hDlg, ID_EDIT14, max_activity, FALSE);

			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_combo5)
					((NMTTDISPINFO*)lParam)->lpszText = t[DYNTT_1];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit11)
					((NMTTDISPINFO*)lParam)->lpszText = t[DYNTT_2];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit12)
					((NMTTDISPINFO*)lParam)->lpszText = t[DYNTT_3];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit13)
					((NMTTDISPINFO*)lParam)->lpszText = t[DYNTT_4];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit14)
					((NMTTDISPINFO*)lParam)->lpszText = t[DYNTT_5];
			}
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDOK:
					dyn_method = SendMessage(hwnd_combo5, CB_GETCURSEL, 0, 0);
					clean_ratio = GetDlgItemInt(hDlg, ID_EDIT12, NULL, FALSE);
					clean_timer = GetDlgItemInt(hDlg, ID_EDIT13, NULL, FALSE);
					max_activity = GetDlgItemInt(hDlg, ID_EDIT14, NULL, FALSE);
					block_size = GetDlgItemInt(hDlg, ID_EDIT11, NULL, FALSE);
					if (block_size < 12) block_size = 12;
					if (block_size > 30) block_size = 30;

				case IDCANCEL:
					ti.cbSize = sizeof(TOOLINFO);
					ti.hwnd = hDlg;
					ti.uId = (UINT_PTR)hwnd_edit14;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&ti);
					ti.uId = (UINT_PTR)hwnd_edit13;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&ti);
					ti.uId = (UINT_PTR)hwnd_edit12;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&ti);
					ti.uId = (UINT_PTR)hwnd_edit11;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&ti);
					ti.uId = (UINT_PTR)hwnd_combo5;
					SendMessage(hTTip, TTM_DELTOOL, 0, (LPARAM)&ti);
					EndDialog(hDlg, 0);
			}
			return TRUE;

		case WM_ENDSESSION:
			configure_services_and_exit();

		default:
			return FALSE;
	}
}

static INT_PTR __stdcall WarnProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT paint;
	DWORD flags;
	WCHAR cmd_line[] = L"control /name Microsoft.PowerOptions /page pageGlobalSettings";

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TEXT1, t[WARN_1]);
				SetDlgItemText(hDlg, ID_PBUTTON1, t[WARN_2]);
				SetDlgItemText(hDlg, ID_CHECK1, t[WARN_3]);
				SetDlgItemText(hDlg, ID_PBUTTON2, t[WARN_4]);
			}

			icon_coord.left = 14;
			icon_coord.top = 18;
			MapDialogRect(hDlg, &icon_coord);
			SetFocus(GetDlgItem(hDlg, ID_PBUTTON2));
			MessageBeep(MB_ICONWARNING);
			return FALSE;

		case WM_PAINT:
			DrawIcon(BeginPaint(hDlg, &paint), icon_coord.left, icon_coord.top, hIcon_warn);
			EndPaint(hDlg, &paint);
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case ID_PBUTTON1:
					start_process(cmd_line, FALSE);

				case ID_PBUTTON2:
					if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
						flags = 0;
						reg_query_dword(L"DlgFlags", &flags);
						flags |= FLAG_FASTSTARTUP;
						reg_set_dword(L"DlgFlags", &flags);
					}

				case IDCANCEL:
					EndDialog(hDlg, 0);
			}
			return TRUE;

		case WM_ENDSESSION:
			configure_services_and_exit();

		default:
			return FALSE;
	}
}

static DWORD __stdcall ApplyParameters(LPVOID lpParam)
{
	SHELLEXECUTEINFO ShExInf = {sizeof ShExInf};
	HANDLE h;
	DWORD dw, version, attrib;
	WCHAR cmd_line[2 * MAX_PATH + 400];
	WCHAR label_tmp[34];
	WCHAR drive[MAX_PATH + 1];
	WCHAR param_name[16];
	UINT size_kb;
	WCHAR *current_MP;
	WCHAR *svc_arg[2];
	int i;

	notif(RGB(255, 255, 0), t[MSG_1]);

	// check size parameter
	size_kb = min(drive_size, UINT_MAX >> (unit * 10)) << (unit * 10);
	if (!mount_file) {
		if (size_kb < min_size[fsys.filesystem][cluster]) {
			_snwprintf(cmd_line, _countof(cmd_line), t[MSG_2], min_size[fsys.filesystem][cluster]);
size_error:
			MessageBox(hwnd[0], cmd_line, L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), t[MSG_4]);
			apply_ok = TRUE;
			return 0;
		}
		if ((fsys.filesystem == 1 || fsys.filesystem == 2) && size_kb > max_size[fsys.filesystem - 1][cluster]) {
			_snwprintf(cmd_line, _countof(cmd_line), t[MSG_3], max_size[fsys.filesystem - 1][cluster] >> (unit * 10), unit_list[unit]);
			goto size_error;
		}
	}

	if (is_faststartup_enabled())
		DialogBox(hinst, L"WARN_DLG", hwnd[0], WarnProc);

	// check the folder of mount point or the drive letter and unmount it
	if (use_mount_point) {
		attrib = GetFileAttributes(mount_point);
		if (attrib == INVALID_FILE_ATTRIBUTES || ((attrib & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == FILE_ATTRIBUTE_DIRECTORY && !PathIsDirectoryEmpty(mount_point))) {
			MessageBox(hwnd[0], t[MSG_5], L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), t[MSG_5]);
			apply_ok = TRUE;
			return 0;
		}
		if (is_MP_imdisk_device() && MessageBox(hwnd[0], t[MSG_6], L"ImDisk", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
			notif(RGB(0, 255, 0), t[MSG_0]);
			apply_ok = TRUE;
			return 0;
		}
		notif(RGB(255, 255, 0), t[MSG_7]);
		remove_mount_point();
	} else {
		_snwprintf(drive, _countof(drive), L"\\\\.\\%s", drive_select);
		h = CreateFile(drive, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &version, sizeof version, &dw, NULL)) {
			if (MessageBox(hwnd[0], t[MSG_8], L"ImDisk", MB_OKCANCEL | MB_ICONWARNING) != IDOK) {
				CloseHandle(h);
				notif(RGB(0, 255, 0), t[MSG_0]);
				apply_ok = TRUE;
				return 0;
			} else {
				notif(RGB(255, 255, 0), t[MSG_9]);
				_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -D -m %s", drive_select);
				start_process(cmd_line, TRUE);
			}
		}
		CloseHandle(h);
	}

	// check if the drive is now free
	if (!use_mount_point && (GetLogicalDrives() & 1 << (drive_select[0] - 'A'))) {
		MessageBox(hwnd[0], t[MSG_10], L"ImDisk", MB_ICONERROR);
		notif(RGB(255, 0, 0), t[MSG_10]);
		apply_ok = TRUE;
		return 0;
	}

	current_MP = use_mount_point ? mount_point : drive_select;

	// build the ramdisk
	notif(RGB(255, 255, 0), t[MSG_11]);
	if (dynamic) {
		if (mount_file) i = _snwprintf(cmd_line, MAX_PATH, L"\"%s\" ", image_file);
		else i = _snwprintf(cmd_line, 21, L"%I64u ", (ULONGLONG)drive_size << (unit * 10));
		if (dyn_method == 1 || (!dyn_method && !fsys.filesystem && is_trim_enabled())) i += _snwprintf(&cmd_line[i], 4, L"-1 ");
		else i += _snwprintf(&cmd_line[i], 34, L"%u %u %u ", clean_ratio, clean_timer, max_activity);
		_snwprintf(&cmd_line[i], 280, L"%u %u \"%s\"", awealloc, block_size, add_param);
		svc_arg[0] = current_MP;
		svc_arg[1] = cmd_line;
		h = CreateEventA(NULL, FALSE, FALSE, "Global\\RamDynSvcEvent");
		StartService(h_svc, 2, (void*)svc_arg);
		WaitForSingleObject(h, 3600000);
		CloseHandle(h);
		if (!use_mount_point && (GetLogicalDrives() & (1 << (drive_select[0] - 'A'))))
			ImDisk_NotifyShellDriveLetter(NULL, current_MP);
	} else if (mount_file) {
		_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -a %S -m \"%s\" %s -f \"%s\"", fileawe_list[awealloc], current_MP, add_param, image_file);
		start_process(cmd_line, TRUE);
	} else {
		_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -a -m \"%s\" %S%s -s %d%C", current_MP, awe_list[awealloc], add_param, drive_size, unit_list[unit]);
		start_process(cmd_line, TRUE);
	}

	update_unmount_button();

	// format
	if (!mount_file) {
		wcscpy(drive, drive_select);
		if (use_mount_point) {
			((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer[0] = 0;
			h = CreateFile(mount_point, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
			DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, cmd_line, sizeof cmd_line, &dw, NULL);
			CloseHandle(h);
			if (wcsncmp(((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer, L"\\Device\\ImDisk", 14)) {
				MessageBox(hwnd[0], t[MSG_12], L"ImDisk", MB_ICONERROR);
				notif(RGB(255, 0, 0), t[MSG_12]);
				apply_ok = TRUE;
				return 0;
			}
			PathRemoveBackslash(((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer);
			if (!(dw = 0x3ffffff ^ GetLogicalDrives())) goto err_cannot_mount;
			drive[0] = _bit_scan_reverse(dw) + 'A';
			DefineDosDevice(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, drive, ((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer);
		}

		if (!PathFileExists(drive) && GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
			_snwprintf(cmd_line, _countof(cmd_line), L"format.com %s /fs:%s %S%S%S /y", drive, filesystem_list[fsys.filesystem], compress_list[fsys.l == 0x100], quickf_list[quick_format | dynamic], cluster_list[cluster]);
			start_process(cmd_line, TRUE);
			wcscpy(label_tmp, label);
			if (fsys.filesystem) label_tmp[11] = 0;
			drive[2] = L'\\';
			drive[3] = 0;
			SetVolumeLabel(drive, label_tmp);
			drive[2] = 0;
		}

		if (use_mount_point)
			DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive, NULL);
	}

	// check whether the drive is mounted and contains a valid file system
	_snwprintf(drive, _countof(drive), L"%s\\", current_MP);
	if (!GetVolumeInformation(drive, NULL, 0, &volume_id, NULL, NULL, NULL, 0)) {
		if (GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
			MessageBox(hwnd[0], mount_file ? t[MSG_13] : t[MSG_14], L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), mount_file ? t[MSG_13] : t[MSG_14]);
		} else {
err_cannot_mount:
			MessageBox(hwnd[0], t[MSG_15], L"ImDisk", MB_ICONERROR);
			notif(RGB(255, 0, 0), t[MSG_15]);
		}
		apply_ok = TRUE;
		return 0;
	}

	// create temp folder
	if (!mount_file && temp_folder) {
		_snwprintf(cmd_line, _countof(cmd_line), L"%s\\Temp", current_MP);
		CreateDirectory(cmd_line, &sa);
	}

	// copy files
	if (mount_dir) {
		notif(RGB(255, 255, 0), t[MSG_16]);
		PathAddBackslash(image_file);
		_snwprintf(cmd_line, _countof(cmd_line), L"xcopy \"%s*\" \"%s\" /e /c /q /h /k /y", image_file, current_MP);
		if (!fsys.filesystem) wcscat(cmd_line, L" /x");
		if (os_ver.dwMajorVersion >= 6) wcscat(cmd_line, L" /b");
		if (start_process(cmd_line, TRUE))
			MessageBox(hwnd[0], t[MSG_17], L"ImDisk", MB_ICONERROR);
		else
			sync_flags |= 8;
		if ((sync_flags & 3) == 3 && image_file[0]) {
			_snwprintf(cmd_line, _countof(cmd_line), L"attrib -a \"%s\\*\" /s", current_MP);
			if (os_ver.dwMajorVersion >= 6) wcscat(cmd_line, L" /l");
			start_process(cmd_line, TRUE);
		}
	}
	if (mount_file) sync_flags |= 8;

	// run user command
	if (run_object[0]) {
		notif(RGB(255, 255, 0), t[MSG_18]);
		svc_arg[0] = run_object;
		for (i = 0;; i++) {
			if (StartService(h_svc, 1, (void*)svc_arg)) break;
			if (i == 39 || GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
				MessageBox(hwnd[0], t[MSG_19], L"ImDisk", MB_ICONERROR);
				break;
			}
			Sleep(100);
		}
	}

	// show the mounted drive
	if (!use_mount_point && show_explorer) {
		notif(RGB(255, 255, 0), t[MSG_20]);
		ShExInf.fMask = SEE_MASK_INVOKEIDLIST;
		ShExInf.lpVerb = L"properties";
		ShExInf.lpFile = drive_select;
		ShExInf.nShow = SW_SHOWNORMAL;
		ShellExecuteEx(&ShExInf);
	}

	// save parameters
	notif(RGB(255, 255, 0), t[MSG_21]);
	param_name[0] = wanted_drive;
	param_name[1] = '_';
	if (mount_current)
		copy_list_param(param_name, FALSE);
	if (reg_sync_flags & 1 && reg_image_file && !reg_use_MP)
		copy_list_param(param_name, TRUE);

	wanted_drive = drive_select[0];
	reg_dynamic = dynamic;
	reg_win_boot = win_boot;
	reg_awealloc = awealloc;
	reg_use_MP = use_mount_point;
	mount_current = win_boot & !use_mount_point;
	reg_sync_flags = sync_flags;
	PathRemoveBackslash(image_file);
	reg_image_file = image_file[0];
	reg_set_dword(L"DriveSize", &drive_size);
	reg_set_dword(L"Unit", &unit);
	reg_set_dword(L"Dynamic", &dynamic);
	reg_set_dword(L"WantedDrive", &wanted_drive);
	reg_set_dword(L"FileSystem", (DWORD*)&fsys);
	reg_set_dword(L"WinBoot", &win_boot);
	reg_set_dword(L"TempFolder", &temp_folder);
	reg_set_dword(L"Cluster", &cluster);
	RegSetValueEx(registry_key, L"Label", 0, REG_SZ, (void*)&label, (wcslen(label) + 1) * sizeof(WCHAR));
	reg_set_dword(L"QuickFormat", &quick_format);
	reg_set_dword(L"Awealloc", &awealloc);
	reg_set_dword(L"DynMethod", &dyn_method);
	reg_set_dword(L"CleanRatio", &clean_ratio);
	reg_set_dword(L"CleanTimer", &clean_timer);
	reg_set_dword(L"MaxActivity", &max_activity);
	reg_set_dword(L"BlockSize", &block_size);
	reg_set_dword(L"RDUseMP", &use_mount_point);
	RegSetValueEx(registry_key, L"RDMountPoint", 0, REG_SZ, (void*)&mount_point, (wcslen(mount_point) + 1) * sizeof(WCHAR));
	RegSetValueEx(registry_key, L"RunObject", 0, REG_SZ, (void*)&run_object, (wcslen(run_object) + 1) * sizeof(WCHAR));
	RegSetValueEx(registry_key, L"AddParam", 0, REG_SZ, (void*)&add_param, (wcslen(add_param) + 1) * sizeof(WCHAR));
	RegSetValueEx(registry_key, L"ImageFile", 0, REG_SZ, (void*)&image_file, (wcslen(image_file) + 1) * sizeof(WCHAR));
	reg_set_dword(L"SyncFlags", &sync_flags);
	RegSetValueEx(registry_key, L"SyncExcluded", 0, REG_SZ, (void*)&sync_excluded, (wcslen(sync_excluded) + 1) * sizeof(WCHAR));
	reg_set_dword(L"VolumeID", &volume_id);
	reg_set_dword(L"RDMountCurrent", &mount_current);

	if (!use_mount_point)
		// remove previous parameters in case of redefining of an existing ramdisk
		remove_reg_param(drive_select[0]);
	else {
		param_name[1] = '_';
		wcscpy(&param_name[2], L"RDMountPoint");
		for (param_name[0] = '0'; param_name[0] <= '9'; param_name[0]++)
			if (reg_query_dword(param_name, &dw) == ERROR_FILE_NOT_FOUND) break;
		if (param_name[0] > '9' && (win_boot || (sync_flags & 1 && image_file[0])))
			MessageBox(hwnd[0], t[MSG_22], L"ImDisk", MB_ICONWARNING);
		else {
			if (win_boot)
				copy_list_param(param_name, FALSE);
			if (sync_flags & 1 && image_file[0])
				copy_list_param(param_name, TRUE);
		}
		load_mount_point();
	}

	notif(RGB(0, 255, 0), t[MSG_24]);

	apply_ok = TRUE;
	return 0;
}

static INT_PTR __stdcall Tab1Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HKEY reg_key;
	HANDLE h;
	DWORD mask, data_size, version;
	WCHAR text[8];
	BOOL Translated;
	TOOLINFO ti;
	int n_drive, i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hwnd[1] = hDlg;
			hwnd[0] = GetParent(hDlg);

			// add tooltips
			ttip_on_disabled_ctrl = TRUE;
			ti.hwnd = hDlg;
			ti.uId = ID_EDIT1;
			hwnd_edit1 = add_tooltip(&ti);
			ti.uId = ID_CHECK1;
			hwnd_check1 = add_tooltip(&ti);
			ti.uId = ID_COMBO2;
			hwnd_combo2 = add_tooltip(&ti);
			ti.uId = ID_PBUTTON3;
			hwnd_pbutton3 = add_tooltip(&ti);
			ti.uId = ID_CHECK2;
			hwnd_check2 = add_tooltip(&ti);
			ti.uId = ID_CHECK3;
			hwnd_check3 = add_tooltip(&ti);

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hwnd[0], IDOK, t[PS_OK]);
				SetDlgItemText(hwnd[0], IDCANCEL, t[PS_EXIT]);

				SetDlgItemText(hDlg, ID_TITLE, t[TITLE]);
				SetDlgItemText(hDlg, ID_TEXT3, t[TAB1_1]);
				SetDlgItemText(hDlg, ID_RB1, t[TAB1_2]);
				SetDlgItemText(hDlg, ID_RB2, t[TAB1_3]);
				SetDlgItemText(hDlg, ID_RB3, t[TAB1_4]);
				SetDlgItemText(hDlg, ID_CHECK1, t[TAB1_5]);
				SetDlgItemText(hDlg, ID_TEXT4, t[TAB1_6]);
				SetDlgItemText(hDlg, ID_PBUTTON3, t[TAB1_7]);
				SetDlgItemText(hDlg, ID_TEXT5, t[TAB1_8]);
				SetDlgItemText(hDlg, ID_CHECK2, t[TAB1_9]);
				SetDlgItemText(hDlg, ID_CHECK3, t[TAB1_10]);
				SetDlgItemText(hDlg, ID_PBUTTON4, t[TAB1_11]);
				SetDlgItemText(hDlg, ID_TEXT1, t[MSG_0]);
			}

			// initialize controls
			SetDlgItemInt(hDlg, ID_EDIT1, drive_size, FALSE);
			CheckRadioButton(hDlg, ID_RB1, ID_RB3, ID_RB1 + unit);
			CheckDlgButton(hDlg, ID_CHECK1, dynamic);
			CheckDlgButton(hDlg, ID_CHECK2, win_boot);
			CheckDlgButton(hDlg, ID_CHECK3, temp_folder);

			// set list of available drives
			drive_default = n_drive = 0;
			mask = mask0 | GetLogicalDrives();
			wcscpy(text, L"\\\\.\\A:");
			for (i = 'A'; i <= 'Z'; i++) {
				h = CreateFile(text, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				if (!(mask & 1) || DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &version, sizeof version, &data_size, NULL)) {
					if (i == wanted_drive) drive_default = n_drive;
					drive_list[n_drive][0] = i;
					drive_list[n_drive][1] = ':';
					SendDlgItemMessage(hDlg, ID_COMBO1, CB_ADDSTRING, 0, (LPARAM)drive_list[n_drive++]);
				}
				CloseHandle(h);
				mask >>= 1;
				text[4]++;
			}
			SendDlgItemMessage(hDlg, ID_COMBO1, CB_SETCURSEL, drive_default, 0);

			for (i = 0; i < 3; i++)
				SendMessage(hwnd_combo2, CB_ADDSTRING, 0, (LPARAM)filesystem_list[i]);
			if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\exfat", 0, KEY_QUERY_VALUE, &reg_key) == ERROR_SUCCESS) {
				SendMessage(hwnd_combo2, CB_ADDSTRING, 0, (LPARAM)filesystem_list[3]);
				RegCloseKey(reg_key);
			}
			SendMessage(hwnd_combo2, CB_SETCURSEL, fsys.filesystem, 0);

			item_enable = !mount_file;

			// set notification circle
			MapDialogRect(hDlg, &circle);
			color = RGB(0, 255, 0);

			init1_ok = TRUE;
			return TRUE;

		case WM_PAINT:
			circ_draw(hDlg);
			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit1 || ((NMHDR*)lParam)->idFrom == ID_EDIT1) {
					_snwprintf(TTip_txt, 99, t[TTIP1_1], (min_size[fsys.filesystem][cluster] + (1 << (unit * 10)) - 1) >> (unit * 10), t[TAB1_2 + unit]);
					TTip_txt[99] = 0;
					if (fsys.filesystem == 1 || fsys.filesystem == 2) {
						TTip_txt[i = wcslen(TTip_txt)] = '\n';
						_snwprintf(&TTip_txt[i + 1], 99, t[TTIP1_2], max_size[fsys.filesystem - 1][cluster] >> (unit * 10), t[TAB1_2 + unit]);
					}
					((NMTTDISPINFO*)lParam)->lpszText = TTip_txt;
				}
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check1)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP1_3];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_pbutton3 || ((NMHDR*)lParam)->idFrom == ID_PBUTTON3)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP1_4];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_combo2 || ((NMHDR*)lParam)->idFrom == ID_COMBO2)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP1_5];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check2)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP1_6];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check3 || ((NMHDR*)lParam)->idFrom == ID_CHECK3)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP1_7];
			}

			if (((NMHDR*)lParam)->code == PSN_APPLY) {
				SetWindowLongPtr(hDlg, DWLP_MSGRESULT, use_mount_point ? PSNRET_INVALID_NOCHANGEPAGE : PSNRET_INVALID); // prevent PropertySheet to be closed with the OK button
				if (apply_ok) {
					apply_ok = FALSE;
					CreateThread(NULL, 0, ApplyParameters, NULL, 0, NULL);
				}
			}

			if (((NMHDR*)lParam)->code != PSN_SETACTIVE) return TRUE;
			wParam = 0;

		case WM_COMMAND:
			if (!init1_ok) return FALSE;

			// update parameters
			drive_size = GetDlgItemInt(hDlg, ID_EDIT1, &Translated, FALSE);
			if (!Translated) drive_size = UINT_MAX;
			for (i = 0; i < 3; i++)
				if (IsDlgButtonChecked(hDlg, ID_RB1 + i)) unit = i;
			dynamic = IsDlgButtonChecked(hDlg, ID_CHECK1);
			wcscpy(drive_select, drive_list[SendDlgItemMessage(hDlg, ID_COMBO1, CB_GETCURSEL, 0, 0)]);
			fsys.filesystem = SendMessage(hwnd_combo2, CB_GETCURSEL, 0, 0);
			win_boot = IsDlgButtonChecked(hDlg, ID_CHECK2);
			temp_folder = IsDlgButtonChecked(hDlg, ID_CHECK3);

			// manage controls activation
			EnableWindow(GetDlgItem(hDlg, ID_TEXT3), item_enable);
			EnableWindow(hwnd_edit1, item_enable);
			EnableWindow(GetDlgItem(hDlg, ID_RB1), item_enable);
			EnableWindow(GetDlgItem(hDlg, ID_RB2), item_enable);
			EnableWindow(GetDlgItem(hDlg, ID_RB3), item_enable);
			EnableWindow(GetDlgItem(hDlg, ID_TEXT5), item_enable);
			EnableWindow(hwnd_combo2, item_enable);
			EnableWindow(GetDlgItem(hDlg, ID_CHECK3), item_enable);
			update_unmount_button();

			if (LOWORD(wParam) == ID_CHECK1 && SeLockMemoryPrivilege_required())
				CheckDlgButton(hDlg, ID_CHECK1, dynamic = FALSE);

			if (LOWORD(wParam) == ID_PBUTTON3) {
				init1_ok = apply_ok = FALSE;
				CreateThread(NULL, 0, UnmountDrive, NULL, 0, NULL);
			}

			if (LOWORD(wParam) == ID_PBUTTON4)
				DialogBox(hinst, L"VAR_DLG", hwnd[0], VarProc);

			return TRUE;

		case WM_ENDSESSION:
			configure_services_and_exit();

		default:
			return FALSE;
	}
}

static INT_PTR __stdcall Tab2Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	OPENFILENAME ofn = {sizeof ofn};
	BROWSEINFO bi;
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	WCHAR text[4 * MAX_PATH + 100], sys_dir[MAX_PATH], temp_str[MAX_PATH + 10];;
	TOOLINFO ti;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hwnd[2] = hDlg;

			// add tooltips
			ttip_on_disabled_ctrl = TRUE;
			ti.hwnd = hDlg;
			ti.uId = ID_COMBO3;
			hwnd_combo3 = add_tooltip(&ti);
			ti.uId = ID_EDIT2;
			hwnd_edit2 = add_tooltip(&ti);
			ti.uId = ID_CHECK4;
			hwnd_check4 = add_tooltip(&ti);
			ti.uId = ID_CHECK5;
			hwnd_check5 = add_tooltip(&ti);
			ti.uId = ID_CHECK6;
			hwnd_check6 = add_tooltip(&ti);
			ti.uId = ID_CHECK7;
			hwnd_check7 = add_tooltip(&ti);
			ti.uId = ID_PBUTTON7;
			hwnd_pbutton7 = add_tooltip(&ti);

			combo4.cbSize = sizeof(COMBOBOXINFO);
			GetComboBoxInfo(GetDlgItem(hDlg, ID_COMBO4), &combo4);
			ti.uId = ID_COMBO4;
			add_tooltip(&ti);

			ti.uId = ID_EDIT6;
			hwnd_edit6 = add_tooltip(&ti);
			ti.uId = ID_EDIT4;
			hwnd_edit4 = add_tooltip(&ti);

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TITLE, t[TITLE]);
				SetDlgItemText(hDlg, ID_TEXT6, t[TAB2_1]);
				SetDlgItemText(hDlg, ID_TEXT7, t[TAB2_2]);
				SetDlgItemText(hDlg, ID_CHECK4, t[TAB2_3]);
				SetDlgItemText(hDlg, ID_CHECK5, t[TAB2_4]);
				SetDlgItemText(hDlg, ID_CHECK6, t[TAB2_5]);
				SetDlgItemText(hDlg, ID_PBUTTON9, t[TAB2_6]);
				SetDlgItemText(hDlg, ID_CHECK7, t[TAB2_7]);
				SetDlgItemText(hDlg, ID_PBUTTON7, t[TAB2_8]);
				SetDlgItemText(hDlg, ID_TEXT9, t[TAB2_9]);
				SetDlgItemText(hDlg, ID_TEXT10, t[TAB2_10]);
				SetDlgItemText(hDlg, ID_TEXT1, t[MSG_0]);
				for (i = CLUST_0; i <= CLUST_8; i++)
					SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)t[i]);
			} else {
				SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)L"Default");
				SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)L"0.5 KB");
				for (i = 1; i <= 64; i <<= 1) {
					_snwprintf(text, _countof(text), L"%d KB", i);
					SendMessage(hwnd_combo3, CB_ADDSTRING, 0, (LPARAM)text);
				}
			}

			// initialize controls
			SendMessage(hwnd_combo3, CB_SETCURSEL, cluster, 0);

			SetDlgItemText(hDlg, ID_EDIT2, label);

			CheckDlgButton(hDlg, ID_CHECK4, quick_format);
			CheckDlgButton(hDlg, ID_CHECK5, fsys.compress);
			CheckDlgButton(hDlg, ID_CHECK6, awealloc);

			CheckDlgButton(hDlg, ID_CHECK7, use_mount_point);
			SendMessage(combo4.hwndCombo, CB_LIMITTEXT, _countof(mount_point) - 1, 0);
			load_mount_point();

			SendMessage(hwnd_edit6, EM_SETLIMITTEXT, _countof(run_object) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT6, run_object);
			SendMessage(hwnd_edit4, EM_SETLIMITTEXT, _countof(add_param) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT4, add_param);

			init2_ok = TRUE;
			return TRUE;

		case WM_PAINT:
			circ_draw(hDlg);
			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_combo3 || ((NMHDR*)lParam)->idFrom == ID_COMBO3)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_1];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit2 || ((NMHDR*)lParam)->idFrom == ID_EDIT2)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_2];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check4 || ((NMHDR*)lParam)->idFrom == ID_CHECK4)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_3];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check5 || ((NMHDR*)lParam)->idFrom == ID_CHECK5)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_4];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check6)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_5];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check7)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_6];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_pbutton7 || ((NMHDR*)lParam)->idFrom == ID_PBUTTON7)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_7];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)combo4.hwndItem)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_8];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit6)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_9];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit4)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP2_10];
			}
			if (((NMHDR*)lParam)->code != PSN_SETACTIVE) return TRUE;
			wParam = 0;

		case WM_COMMAND:
			if (!init2_ok) return FALSE;

			// update parameters
			cluster = SendMessage(hwnd_combo3, CB_GETCURSEL, 0, 0);
			SendMessage(hwnd_edit2, EM_SETLIMITTEXT, fsys.filesystem ? 11 : 32, 0);
			if (fsys.filesystem && wcslen(label) > 11) {
				label[11] = 0;
				SetDlgItemText(hDlg, ID_EDIT2, label);
			} else
				GetDlgItemText(hDlg, ID_EDIT2, label, 33);
			quick_format = IsDlgButtonChecked(hDlg, ID_CHECK4);
			fsys.compress = IsDlgButtonChecked(hDlg, ID_CHECK5);
			awealloc = IsDlgButtonChecked(hDlg, ID_CHECK6);
			use_mount_point = IsDlgButtonChecked(hDlg, ID_CHECK7);
			GetDlgItemText(hDlg, ID_COMBO4, mount_point, _countof(mount_point));
			GetDlgItemText(hDlg, ID_EDIT6, run_object, _countof(run_object));
			GetDlgItemText(hDlg, ID_EDIT4, add_param, _countof(add_param));

			// manage controls activation
			EnableWindow(GetDlgItem(hDlg, ID_TEXT6), item_enable);
			EnableWindow(hwnd_combo3, item_enable);
			EnableWindow(GetDlgItem(hDlg, ID_TEXT7), item_enable);
			EnableWindow(hwnd_edit2, item_enable);
			if (LOWORD(wParam) == ID_COMBO4) {
				if (HIWORD(wParam) == CBN_SELCHANGE)
					SendMessage(combo4.hwndCombo, CB_GETLBTEXT, SendMessage(combo4.hwndCombo, CB_GETCURSEL, 0, 0), (LPARAM)mount_point);
				EnableWindow(hwnd_pbutton7, is_MP_imdisk_device());
			}
			EnableWindow(hwnd_check4, item_enable & !dynamic);
			EnableWindow(hwnd_check5, item_enable & !fsys.filesystem);
			EnableWindow(GetDlgItem(hDlg, ID_PBUTTON9), dynamic);

			if (LOWORD(wParam) == ID_CHECK6 && SeLockMemoryPrivilege_required())
				CheckDlgButton(hDlg, ID_CHECK6, awealloc = FALSE);

			if (LOWORD(wParam) == ID_PBUTTON5) {
				ofn.hwndOwner = hDlg;
				ofn.lpstrFile = run_object;
				ofn.nMaxFile = _countof(run_object);
				ofn.Flags = OFN_PATHMUSTEXIST;
				GetOpenFileName(&ofn);
				if (run_object[0])
					SetDlgItemText(hDlg, ID_EDIT6, run_object);
			}

			if (LOWORD(wParam) == ID_PBUTTON2) {
				GetSystemDirectory(sys_dir, _countof(sys_dir));
				GetTempPath(_countof(temp_str), temp_str);
				wcscat(temp_str, L"ImDisk");
				_snwprintf(text, _countof(text), L"cmd /c \"%s\\imdisk 2>\"%s\"&notepad \"%s\"&del \"%s\"\"", sys_dir, temp_str, temp_str, temp_str);
				start_process(text, FALSE);
			}

			if (LOWORD(wParam) == ID_PBUTTON7) {
				init2_ok = apply_ok = FALSE;
				CreateThread(NULL, 0, UnmountMP, NULL, 0, NULL);
			}

			if (LOWORD(wParam) == ID_PBUTTON8) {
				ZeroMemory(&bi, sizeof bi);
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = mount_point;
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, mount_point);
					SetDlgItemText(hDlg, ID_COMBO4, mount_point);
				}
				else
					GetDlgItemText(hDlg, ID_COMBO4, mount_point, _countof(mount_point));
			}

			if (LOWORD(wParam) == ID_PBUTTON9)
				DialogBox(hinst, L"DYN_DLG", hwnd[0], DynProc);

			return TRUE;

		case WM_ENDSESSION:
			configure_services_and_exit();

		default:
			return FALSE;
	}
}

static INT_PTR __stdcall Tab3Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	BROWSEINFO bi;
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	TOOLINFO ti;
	DWORD attrib;
	BOOL sync_active;
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hwnd[3] = hDlg;

			// add tooltips
			ttip_on_disabled_ctrl = TRUE;
			ti.hwnd = hDlg;
			ti.uId = ID_EDIT3;
			hwnd_edit3 = add_tooltip(&ti);
			ti.uId = ID_CHECK8;
			hwnd_check8 = add_tooltip(&ti);
			ti.uId = ID_EDIT5;
			hwnd_edit5 = add_tooltip(&ti);
			ti.uId = ID_PBUTTON2;
			hwnd_pbutton2 = add_tooltip(&ti);

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TITLE, t[TITLE]);
				SetDlgItemText(hDlg, ID_TEXT2, t[TAB3_1]);
				SetDlgItemText(hDlg, ID_CHECK8, t[TAB3_2]);
				SetDlgItemText(hDlg, ID_CHECK9, t[TAB3_3]);
				SetDlgItemText(hDlg, ID_CHECK10, t[TAB3_4]);
				SetDlgItemText(hDlg, ID_TEXT8, t[TAB3_5]);
				SetDlgItemText(hDlg, ID_PBUTTON2, t[TAB3_6]);
				SetDlgItemText(hDlg, ID_TEXT1, t[MSG_0]);
			}

			// initialize controls
			SendMessage(hwnd_edit3, EM_SETLIMITTEXT, _countof(image_file) - 2, 0);
			SetDlgItemText(hDlg, ID_EDIT3, image_file);

			for (i = 0; i < 3; i++)
				CheckDlgButton(hDlg, ID_CHECK8 + i, (sync_flags >> i) & 1);

			SendMessage(hwnd_edit5, EM_SETLIMITTEXT, _countof(sync_excluded) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT5, sync_excluded);

			init3_ok = TRUE;
			return TRUE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit3)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP3_1];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check8 || ((NMHDR*)lParam)->idFrom == ID_CHECK8)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP3_2];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit5 || ((NMHDR*)lParam)->idFrom == ID_EDIT5)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP3_3];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_pbutton2 || ((NMHDR*)lParam)->idFrom == ID_PBUTTON2)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP3_4];
			}
			if (((NMHDR*)lParam)->code != PSN_SETACTIVE) return TRUE;
			wParam = 0;

		case WM_COMMAND:
			if (!init3_ok) return FALSE;

			if ((LOWORD(wParam) == ID_EDIT3 && HIWORD(wParam) == EN_CHANGE)) {
				GetDlgItemText(hDlg, ID_EDIT3, image_file, _countof(image_file) - 1);
				attrib = GetFileAttributes(image_file);
				mount_dir = attrib != INVALID_FILE_ATTRIBUTES && attrib & FILE_ATTRIBUTE_DIRECTORY;
				mount_file = !(attrib & FILE_ATTRIBUTE_DIRECTORY);
				item_enable = !mount_file;
			}
			EnableWindow(hwnd_check8, image_file[0]);

			sync_flags = IsDlgButtonChecked(hDlg, ID_CHECK8);
			if (LOWORD(wParam) == ID_CHECK8 && sync_flags && is_faststartup_enabled())
				DialogBox(hinst, L"WARN_DLG", hwnd[0], WarnProc);

			sync_active = sync_flags && image_file[0];
			EnableWindow(GetDlgItem(hDlg, ID_CHECK9), sync_active);
			EnableWindow(GetDlgItem(hDlg, ID_CHECK10), sync_active);
			EnableWindow(GetDlgItem(hDlg, ID_TEXT8), sync_active);
			EnableWindow(hwnd_edit5, sync_active);
			EnableWindow(hwnd_pbutton2, sync_active);
			sync_flags += (IsDlgButtonChecked(hDlg, ID_CHECK9) << 1) + (IsDlgButtonChecked(hDlg, ID_CHECK10) << 2);

			if (LOWORD(wParam) == ID_PBUTTON1) {
				ZeroMemory(&bi, sizeof bi);
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = image_file;
				bi.ulFlags = BIF_BROWSEINCLUDEFILES | BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, image_file);
					SetDlgItemText(hDlg, ID_EDIT3, image_file);
				}
				else
					GetDlgItemText(hDlg, ID_EDIT3, image_file, _countof(image_file) - 1);
			}

			if ((LOWORD(wParam) == ID_EDIT5 && HIWORD(wParam) == EN_CHANGE))
				GetDlgItemText(hDlg, ID_EDIT5, sync_excluded, _countof(sync_excluded));

			if (LOWORD(wParam) == ID_PBUTTON2) {
				init1_ok = init2_ok = init3_ok = apply_ok = FALSE;
				CreateThread(NULL, 0, SyncThread, NULL, 0, NULL);
			}

			return TRUE;

		case WM_PAINT:
			circ_draw(hDlg);
			return TRUE;

		case WM_ENDSESSION:
			configure_services_and_exit();

		default:
			return FALSE;
	}
}


static DWORD __stdcall HandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
	switch (dwControl)
	{
		case SERVICE_CONTROL_STOP:
			SvcStatus.dwCurrentState = SERVICE_STOPPED;
			SetServiceStatus(SvcStatusHandle, &SvcStatus);
			ExitProcess(0);

		case SERVICE_CONTROL_INTERROGATE:
			return NO_ERROR;

		default:
			return ERROR_CALL_NOT_IMPLEMENTED;
	}
}

static void __stdcall SvcMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
	WCHAR cmd_line[2 * MAX_PATH + 400];
	WCHAR drive[4];
	DWORD exit_code, drive_mask, id, attrib, data_size;
	FS_DATA reg_fsys;
	DWORD reg_drive_size, reg_unit, reg_dynamic, reg_wanted_drive, reg_temp_folder, reg_cluster, reg_quick_format;
	DWORD reg_dyn_method, reg_clean_ratio, reg_clean_timer, reg_max_activity, reg_block_size;
	WCHAR reg_label[33], reg_mount_point[MAX_PATH], reg_image_file[MAX_PATH + 1], reg_run_object[MAX_PATH], reg_add_param[255];
	WCHAR *current_MP, *key_name_ptr, *cmd_line_ptr;
	BOOL reg_mount_file, reg_mount_dir, trim_ok;
	HANDLE h;
	SHELLEXECUTEINFO ShExInf = {sizeof ShExInf};
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	int i;

	SvcStatusHandle = RegisterServiceCtrlHandlerEx(L"", HandlerEx, NULL);
	SetServiceStatus(SvcStatusHandle, &SvcStatus);

	switch (dwArgc) {
		case 3:
			_snwprintf(cmd_line, _countof(cmd_line) - 1, L"RamDyn \"%s\" %s", lpszArgv[1], lpszArgv[2]);
			cmd_line[_countof(cmd_line) - 1] = 0;
			if (CreateProcess(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
				for (;;) {
					Sleep(100);
					GetExitCodeProcess(pi.hProcess, &exit_code);
					if (exit_code != STILL_ACTIVE) break;
					if ((h = (HANDLE)ImDisk_OpenDeviceByMountPoint(lpszArgv[1], 0)) != INVALID_HANDLE_VALUE) {
						CloseHandle(h);
						break;
					}
				}
				CloseHandle(pi.hProcess);
				CloseHandle(pi.hThread);
			}
			h = OpenEventA(EVENT_MODIFY_STATE, FALSE, "Global\\RamDynSvcEvent");
			SetEvent(h);
			CloseHandle(h);
			break;

		case 2:
			ShExInf.fMask = SEE_MASK_NOASYNC;
			ShExInf.lpVerb = NULL;
			ShExInf.lpFile = lpszArgv[1];
			ShExInf.nShow = SW_SHOWNORMAL;
			ShellExecuteEx(&ShExInf);
			break;

		default:
			reg_unit = 1;
			reg_dynamic = FALSE;
			reg_fsys.l = 0;
			reg_temp_folder = TRUE;
			reg_cluster = 0;
			wcscpy(reg_label, L"RamDisk");
			reg_quick_format = FALSE;
			reg_awealloc = FALSE;
			reg_dyn_method = 0;
			reg_clean_ratio = 10;
			reg_clean_timer = 10;
			reg_max_activity = 10;
			reg_block_size = 20;
			reg_mount_point[0] = 0;
			reg_image_file[0] = 0;
			reg_run_object[0] = 0;
			reg_add_param[0] = 0;
			trim_ok = is_trim_enabled();

			wcscat(svc_cmd_line, L" NOTIF  :");
			cmd_line_ptr = svc_cmd_line + wcslen(svc_cmd_line) - 2;

			key_name[1] = '_';
			for (key_name[0] = '0'; key_name[0] <= 'Z'; key_name[0] == '9' ? key_name[0] = 'A' : key_name[0]++) {
				if (param_reg_query_dword(L"DriveSize", &reg_drive_size) == ERROR_SUCCESS) {
					reg_wanted_drive = key_name[0];
					param_reg_query_dword(L"Unit", &reg_unit);
					param_reg_query_dword(L"Dynamic", &reg_dynamic);
					param_reg_query_dword(L"FileSystem", (DWORD*)&reg_fsys);
					param_reg_query_dword(L"TempFolder", &reg_temp_folder);
					param_reg_query_dword(L"Cluster", &reg_cluster);
					wcscpy(&key_name[2], L"Label");
					data_size = sizeof reg_label;
					RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_label, &data_size);
					param_reg_query_dword(L"QuickFormat", &reg_quick_format);
					param_reg_query_dword(L"Awealloc", &reg_awealloc);
					param_reg_query_dword(L"DynMethod", &reg_dyn_method);
					param_reg_query_dword(L"CleanRatio", &reg_clean_ratio);
					param_reg_query_dword(L"CleanTimer", &reg_clean_timer);
					param_reg_query_dword(L"MaxActivity", &reg_max_activity);
					param_reg_query_dword(L"BlockSize", &reg_block_size);
					wcscpy(&key_name[2], L"RDMountPoint");
					data_size = sizeof reg_mount_point;
					RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_mount_point, &data_size);
					wcscpy(&key_name[2], L"ImageFile");
					data_size = sizeof reg_image_file;
					RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_image_file, &data_size);
					wcscpy(&key_name[2], L"RunObject");
					data_size = sizeof reg_run_object;
					RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_run_object, &data_size);
					wcscpy(&key_name[2], L"AddParam");
					data_size = sizeof reg_add_param;
					RegQueryValueEx(registry_key, key_name, NULL, NULL, (void*)&reg_add_param, &data_size);
					param_reg_query_dword(L"SyncFlags", &reg_sync_flags);
					key_name_ptr = key_name;
				} else if (key_name[0] == wanted_drive && mount_current) {
					reg_drive_size = drive_size;
					reg_unit = unit;
					reg_dynamic = dynamic;
					reg_wanted_drive = wanted_drive;
					reg_fsys.l = fsys.l;
					reg_temp_folder = temp_folder;
					reg_cluster = cluster;
					wcscpy(reg_label, label);
					reg_quick_format = quick_format;
					reg_awealloc = awealloc;
					reg_dyn_method = dyn_method;
					reg_clean_ratio = clean_ratio;
					reg_clean_timer = clean_timer;
					reg_max_activity = max_activity;
					reg_block_size = block_size;
					wcscpy(reg_image_file, image_file);
					wcscpy(reg_run_object, run_object);
					wcscpy(reg_add_param, add_param);
					reg_sync_flags = sync_flags;
					key_name_ptr = &key_name[2];
					wcscpy(&key_name[2], L"SyncFlags");
				} else continue;

				// prevent data from being deleted in source if xcopy has not finished
				reg_sync_flags &= ~8;
				reg_set_dword(key_name_ptr, &reg_sync_flags);

				attrib = GetFileAttributes(reg_image_file);
				reg_mount_dir = attrib != INVALID_FILE_ATTRIBUTES && attrib & FILE_ATTRIBUTE_DIRECTORY;
				reg_mount_file = !(attrib & FILE_ATTRIBUTE_DIRECTORY);

				drive[1] = L':';
				drive[2] = 0;
				if (key_name[0] <= '9')
					current_MP = reg_mount_point;
				else {
					drive[0] = reg_wanted_drive;
					current_MP = drive;
					if (PathFileExists(drive) || GetLastError() != ERROR_PATH_NOT_FOUND) continue;
				}

				if (reg_dynamic) {
					i = _snwprintf(cmd_line, MAX_PATH + 11, L"RamDyn \"%s\" ", current_MP);
					if (reg_mount_file) i += _snwprintf(&cmd_line[i], MAX_PATH, L"\"%s\" ", reg_image_file);
					else i += _snwprintf(&cmd_line[i], 21, L"%I64u ", (ULONGLONG)reg_drive_size << (reg_unit * 10));
					if (reg_dyn_method == 1 || (!reg_dyn_method && !reg_fsys.filesystem && trim_ok)) i += _snwprintf(&cmd_line[i], 4, L"-1 ");
					else i += _snwprintf(&cmd_line[i], 34, L"%u %u %u ", reg_clean_ratio, reg_clean_timer, reg_max_activity);
					_snwprintf(&cmd_line[i], 280, L"%u %u \"%s\"", reg_awealloc, reg_block_size, reg_add_param);
					if (CreateProcess(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
						for (;;) {
							Sleep(100);
							GetExitCodeProcess(pi.hProcess, &data_size);
							if (data_size != STILL_ACTIVE) break;
							if ((h = (HANDLE)ImDisk_OpenDeviceByMountPoint(current_MP, 0)) != INVALID_HANDLE_VALUE) {
								CloseHandle(h);
								break;
							}
						}
						CloseHandle(pi.hProcess);
						CloseHandle(pi.hThread);
					}
					if (reg_mount_file) {
						reg_sync_flags |= 8;
						goto data_ok;
					}
				} else if (reg_mount_file) {
					_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -a %S -m \"%s\" %s -f \"%s\"", fileawe_list[reg_awealloc], current_MP, reg_add_param, reg_image_file);
					start_process(cmd_line, TRUE);
					reg_sync_flags |= 8;
					goto data_ok;
				} else {
					_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -a -m \"%s\" %S%s -s %d%C", current_MP, awe_list[reg_awealloc], reg_add_param, reg_drive_size, unit_list[reg_unit]);
					start_process(cmd_line, TRUE);
				}

				if (key_name[0] <= '9') {
					((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer[0] = 0;
					h = CreateFile(reg_mount_point, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
					DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, cmd_line, sizeof cmd_line, &data_size, NULL);
					CloseHandle(h);
					if (wcsncmp(((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer, L"\\Device\\ImDisk", 14)) continue;
					PathRemoveBackslash(((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer);
					if (!(drive_mask = 0x3ffffff ^ GetLogicalDrives())) continue;
					drive[0] = _bit_scan_reverse(drive_mask) + 'A';
					DefineDosDevice(DDD_RAW_TARGET_PATH | DDD_NO_BROADCAST_SYSTEM, drive, ((REPARSE_DATA_BUFFER*)cmd_line)->MountPointReparseBuffer.PathBuffer);
				}

				if (!PathFileExists(drive) && GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
					_snwprintf(cmd_line, _countof(cmd_line), L"format.com %s /fs:%s %S%S%S /y", drive, filesystem_list[reg_fsys.filesystem], compress_list[reg_fsys.l == 0x100],
							 quickf_list[reg_quick_format | reg_dynamic], cluster_list[reg_cluster]);
					start_process(cmd_line, TRUE);
					if (reg_fsys.filesystem) reg_label[11] = 0;
					drive[2] = '\\';
					drive[3] = 0;
					SetVolumeLabel(drive, reg_label);
					GetVolumeInformation(drive, NULL, 0, &id, NULL, NULL, NULL, 0);
					wcscpy(&key_name[2], L"VolumeID");
					reg_set_dword(key_name_ptr, &id);
					drive[2] = 0;
				} else {
					if (key_name[0] <= '9')
						DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive, NULL);
					continue;
				}

				if (key_name[0] <= '9')
					DefineDosDevice(DDD_REMOVE_DEFINITION | DDD_NO_BROADCAST_SYSTEM, drive, NULL);

				if (reg_temp_folder) {
					_snwprintf(cmd_line, _countof(cmd_line), L"%s\\Temp", current_MP);
					CreateDirectory(cmd_line, &sa);
				}

				if (reg_mount_dir) {
					PathAddBackslash(reg_image_file);
					_snwprintf(cmd_line, _countof(cmd_line), L"xcopy \"%s*\" \"%s\" /e /c /q /h /k /y", reg_image_file, current_MP);
					if (!reg_fsys.filesystem) wcscat(cmd_line, L" /x");
					if (os_ver.dwMajorVersion >= 6) wcscat(cmd_line, L" /b");
					if (!start_process(cmd_line, TRUE))
						reg_sync_flags |= 8;
					if ((reg_sync_flags & 3) == 3 && reg_image_file[0]) {
						_snwprintf(cmd_line, _countof(cmd_line), L"attrib -a \"%s\\*\" /s", current_MP);
						if (os_ver.dwMajorVersion >= 6) wcscat(cmd_line, L" /l");
						start_process(cmd_line, FALSE);
					}
				}

data_ok:
				if (reg_sync_flags & 8) {
					wcscpy(&key_name[2], L"SyncFlags");
					reg_set_dword(key_name_ptr, &reg_sync_flags);
				}

				if (reg_run_object[0]) {
					ShExInf.fMask = SEE_MASK_NOASYNC;
					ShExInf.lpVerb = NULL;
					ShExInf.lpFile = reg_run_object;
					ShExInf.nShow = SW_SHOWNORMAL;
					ShellExecuteEx(&ShExInf);
				}

				// drive letter notification
				if (key_name[0] >= 'A') {
					*cmd_line_ptr = key_name[0];
					start_process(svc_cmd_line, 2);
				}
			}
	}

	SvcStatus.dwCurrentState = SERVICE_STOPPED;
	SetServiceStatus(SvcStatusHandle, &SvcStatus);
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	PROPSHEETPAGE psp[3];
	PROPSHEETHEADER psh = {sizeof psh};
	HMODULE h_cpl;
	unsigned char sid[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof sid;
	PACL pACL = NULL;
	EXPLICIT_ACCESS ea = {GENERIC_ALL, SET_ACCESS, SUB_CONTAINERS_AND_OBJECTS_INHERIT, {NULL, NO_MULTIPLE_TRUSTEE, TRUSTEE_IS_SID, TRUSTEE_IS_WELL_KNOWN_GROUP, (LPTSTR)sid}};
	SECURITY_DESCRIPTOR sd;
	SERVICE_DESCRIPTION svc_description;
	DWORD data_size, dw;
	int argc;
	WCHAR **argv;
	SERVICE_TABLE_ENTRY DispatchTable[] = {{L"", SvcMain}, {NULL, NULL}};

	argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if (argc > 2 && !wcscmp(argv[1], L"NOTIF")) {
		if ((h_cpl = LoadLibraryA("imdisk.cpl")))
			GetProcAddress(h_cpl, "ImDiskNotifyShellDriveLetter")(NULL, argv[2]);
		ExitProcess(0);
	}

	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os_ver);

	// load default values
	wcscpy(label, L"RamDisk");
	wcscpy(sync_excluded, L"Temp\r\nSystem Volume Information");

	// get registry values
	if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &registry_key, NULL) != ERROR_SUCCESS)
		RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE, &registry_key);
	reg_query_dword(L"DriveSize", &drive_size);
	reg_query_dword(L"Unit", &unit);
	reg_query_dword(L"Dynamic", &dynamic);
	reg_query_dword(L"WantedDrive", &wanted_drive);
	reg_query_dword(L"FileSystem", (DWORD*)&fsys);
	if (reg_query_dword(L"WinBoot", &win_boot) == ERROR_SUCCESS) mount_current = win_boot;
	reg_query_dword(L"TempFolder", &temp_folder);
	reg_query_dword(L"Cluster", &cluster);
	data_size = sizeof label;
	RegQueryValueEx(registry_key, L"Label", NULL, NULL, (void*)&label, &data_size);
	reg_query_dword(L"QuickFormat", &quick_format);
	reg_query_dword(L"Awealloc", &awealloc);
	reg_query_dword(L"DynMethod", &dyn_method);
	reg_query_dword(L"CleanRatio", &clean_ratio);
	reg_query_dword(L"CleanTimer", &clean_timer);
	reg_query_dword(L"MaxActivity", &max_activity);
	reg_query_dword(L"BlockSize", &block_size);
	reg_query_dword(L"RDUseMP", &use_mount_point);
	data_size = sizeof mount_point;
	RegQueryValueEx(registry_key, L"RDMountPoint", NULL, NULL, (void*)&mount_point, &data_size);
	data_size = sizeof run_object;
	RegQueryValueEx(registry_key, L"RunObject", NULL, NULL, (void*)&run_object, &data_size);
	data_size = sizeof add_param;
	RegQueryValueEx(registry_key, L"AddParam", NULL, NULL, (void*)&add_param, &data_size);
	data_size = sizeof image_file;
	RegQueryValueEx(registry_key, L"ImageFile", NULL, NULL, (void*)&image_file, &data_size);
	reg_query_dword(L"SyncFlags", &sync_flags);
	data_size = sizeof sync_excluded;
	RegQueryValueEx(registry_key, L"SyncExcluded", NULL, NULL, (void*)&sync_excluded, &data_size);
	reg_query_dword(L"RDMountCurrent", &mount_current);
	reg_query_dword(L"ShowExplorer", &show_explorer);

	reg_dynamic = dynamic;
	reg_win_boot = win_boot;
	reg_awealloc = awealloc;
	reg_use_MP = use_mount_point;
	reg_image_file = image_file[0];
	reg_sync_flags = sync_flags;
	dw = GetFileAttributes(image_file);
	mount_dir = dw != INVALID_FILE_ATTRIBUTES && dw & FILE_ATTRIBUTE_DIRECTORY;
	mount_file = !(dw & FILE_ATTRIBUTE_DIRECTORY);

	// access rights of Temp folder
	CreateWellKnownSid(WinWorldSid, NULL, (SID*)sid, &sid_size);
	SetEntriesInAcl(1, &ea, NULL, &pACL);
	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, TRUE, pACL, FALSE);
	sa.nLength = sizeof sa;
	sa.lpSecurityDescriptor = &sd;
	sa.bInheritHandle = FALSE;

	h_cpl = LoadLibraryA("imdisk.cpl");
	if (!h_cpl && argc > 1 && (!wcscmp(argv[1], L"SVC") || !wcscmp(argv[1], L"UAC")))
		WTSSendMessageA(WTS_CURRENT_SERVER_HANDLE, WTSGetActiveConsoleSessionId(), "ImDisk", 14, "Warning: cannot find imdisk.cpl.", 66, MB_OK | MB_ICONWARNING, 0, &data_size, FALSE);
	ImDisk_OpenDeviceByMountPoint = GetProcAddress(h_cpl, "ImDiskOpenDeviceByMountPoint");
	ImDisk_NotifyShellDriveLetter = GetProcAddress(h_cpl, "ImDiskNotifyShellDriveLetter");

	GetModuleFileName(NULL, svc_cmd_line, MAX_PATH);
	wcscpy(hlp_svc_path, svc_cmd_line);
	PathQuoteSpaces(svc_cmd_line);

	if (argc > 1 && !wcscmp(argv[1], L"SVC")) {
		StartServiceCtrlDispatcher(DispatchTable);
		ExitProcess(0);
	}

	if ((os_ver.dwMajorVersion >= 6) && (argc <= 1 || wcscmp(argv[1], L"UAC"))) {
		// send non-elevated drive list to the elevated process
		_snwprintf(add_param, _countof(add_param), L"UAC %d", GetLogicalDrives());
		ShellExecute(NULL, L"runas", argv[0], add_param, NULL, SW_SHOWDEFAULT);
		ExitProcess(0);
	}

	mask0 = os_ver.dwMajorVersion < 6 ? 0 : _wtoi(argv[2]);

	hinst = GetModuleHandle(NULL);
	hIcon = LoadImage(hinst, MAKEINTRESOURCE(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
	hIcon_warn = LoadImage(NULL, MAKEINTRESOURCE(OIC_WARNING), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);

	wcscat(svc_cmd_line, L" SVC");
	PathRemoveFileSpec(hlp_svc_path);
	PathAddBackslash(hlp_svc_path);
	SetCurrentDirectory(hlp_svc_path);
	wcscat(hlp_svc_path, L"ImDiskTk-svc.exe");
	load_lang();

	h_scman = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
	if ((h_svc = OpenService(h_scman, L"ImDiskRD", SERVICE_CHANGE_CONFIG | SERVICE_START | DELETE)))
		ChangeServiceConfig(h_svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE, SERVICE_ERROR_NORMAL, svc_cmd_line, NULL, NULL, L"ImDisk\0", NULL, NULL, NULL);
	else {
		h_svc = CreateService(h_scman, L"ImDiskRD", L"ImDisk RamDisk starter", SERVICE_CHANGE_CONFIG | SERVICE_START | DELETE, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
							  svc_cmd_line, NULL, NULL, L"ImDisk\0", NULL, NULL);
		if (h_svc) {
			svc_description.lpDescription = L"Mounts a RamDisk at system startup.";
			ChangeServiceConfig2(h_svc, SERVICE_CONFIG_DESCRIPTION, &svc_description);
		} else
			MessageBox(NULL, t[MSG_23], L"ImDisk", MB_ICONERROR);
	}

	// set up the property sheet
	ZeroMemory(psp, sizeof psp);

	psp[0].dwSize = sizeof(PROPSHEETPAGE);
	psp[0].dwFlags = PSP_USETITLE | PSP_PREMATURE;
	psp[0].hInstance = hinst;
	psp[0].pszTemplate = L"TAB1";
	psp[0].pfnDlgProc = Tab1Proc;
	psp[0].pszTitle = t[TAB1_0] ? t[TAB1_0] : L"Basic";

	psp[1].dwSize = sizeof(PROPSHEETPAGE);
	psp[1].dwFlags = PSP_USETITLE | PSP_PREMATURE;
	psp[1].hInstance = hinst;
	psp[1].pszTemplate = L"TAB2";
	psp[1].pfnDlgProc = Tab2Proc;
	psp[1].pszTitle = t[TAB2_0] ? t[TAB2_0] : L"Advanced";

	psp[2].dwSize = sizeof(PROPSHEETPAGE);
	psp[2].dwFlags = PSP_USETITLE | PSP_PREMATURE;
	psp[2].hInstance = hinst;
	psp[2].pszTemplate = L"TAB3";
	psp[2].pfnDlgProc = Tab3Proc;
	psp[2].pszTitle = t[TAB3_0] ? t[TAB3_0] : L"Data";

	psh.dwFlags = PSH_NOAPPLYNOW | PSH_PROPSHEETPAGE | PSH_NOCONTEXTHELP | PSH_USEHICON;
	psh.hIcon = hIcon;
	psh.pszCaption = L"ImDisk";
	psh.nPages = 3;
	psh.ppsp = (LPCPROPSHEETPAGE)&psp;

	// initialize tooltips
	hTTip = CreateWindow(TOOLTIPS_CLASS, NULL, TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL); 
	SendMessage(hTTip, TTM_SETMAXTIPWIDTH, 0, 1000);
	SendMessage(hTTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 20000);

	PropertySheet(&psh);

	configure_services_and_exit();
}
