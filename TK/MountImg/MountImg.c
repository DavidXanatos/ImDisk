#define _WIN32_WINNT 0x0601
#define OEMRESOURCE
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <locale.h>
#include <winternl.h>
#include <intrin.h>
#include "resource.h"
#include "..\inc\imdisk.h"

static SERVICE_STATUS_HANDLE SvcStatusHandle;
static SERVICE_STATUS SvcStatus = {SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, SERVICE_ACCEPT_STOP, NO_ERROR, 0, 0, 600000};
static SC_HANDLE h_svc;

static HINSTANCE hinst;
static HICON hIcon;
static RECT icon_coord;
static HWND hDialog;
static OSVERSIONINFO os_ver;
static DWORD mask0 = 0, show_explorer = 1;
static HKEY registry_key;
static FARPROC ImDisk_GetDeviceListEx, ImDisk_ForceRemoveDevice, ImDisk_RemoveRegistrySettings, ImDisk_NotifyShellDriveLetter;

static WCHAR dev_list[] = {'h', 'c', 'f'};
static WCHAR ro_list[] = {'w', 'o'};
static WCHAR *ro_discutils_list[] = {L"", L" /readonly"};
static WCHAR *rm_list[] = {L"fix", L"rem"};
static WCHAR *boot_list[] = {L"", L" -P"};
static DWORD floppy_size[] = {160 << 10, 180 << 10, 320 << 10, 360 << 10, 640 << 10, 1200 << 10, 720 << 10, 820 << 10, 1440 << 10, 1680 << 10, 1722 << 10, 2880 << 10, 123264 << 10, 234752 << 10};

static WCHAR filename[MAX_PATH] = {}, mountdir[MAX_PATH] = {};
static WCHAR drive_list[27][4] = {}, drive[MAX_PATH + 2] = {};
static WCHAR module_name[MAX_PATH + 16], txt[1024] = {};

static BYTE init_ok = FALSE, net_installed = FALSE, partition_changed;
static UINT cmdline_dev_type = 0, dev_type, cmdline_partition = 1, partition;
static _Bool cmdline_partition_exist = FALSE, cmdline_mount = FALSE;
static BOOL mount_point = FALSE, readonly = FALSE, removable = FALSE, new_file, win_boot;
static long device_number;
static WCHAR cmdline_drive_letter = 0;

static int list_partition;
static __int64 list_size;
static WCHAR list_filesystem[MAX_PATH + 1], list_label[MAX_PATH + 1];
static ULONG list_device[64000];
static int lv_itemindex = 0;
static HANDLE mount_mutex;
static PROCESS_INFORMATION pi_discutilsdevio;

static TOOLINFO ti;
static HWND hTTip, h_listview;
static HWND hwnd_edit1, hwnd_combo2, hwnd_pbutton2, hwnd_pbutton3, hwnd_pbutton4, hwnd_check1, hwnd_check2, hwnd_check3, hwnd_rb3, hwnd_rb4, hwnd_rb5, hwnd_updown, hwnd_ok;
static COMBOBOXINFO combo1;
static WCHAR *lv_titles[] = {L"", L"Size", L"File System", L"Label"};
static LONG lv_width[] = {17, 38, 50, 73};


enum {
	TITLE,
	CTRL_1, CTRL_2, CTRL_3, CTRL_4, CTRL_5, CTRL_6, CTRL_7, CTRL_8, CTRL_9, CTRL_10, CTRL_11, CTRL_12, CTRL_13, CTRL_14, CTRL_15, CTRL_16, CTRL_17,
	LV_1, LV_2, LV_3,
	TTIP_1, TTIP_2, TTIP_3, TTIP_4, TTIP_5, TTIP_6, TTIP_7, TTIP_8,
	ERR_1, ERR_2, ERR_3, ERR_4, ERR_5, ERR_6,
	INV_0, INV_1, INV_2, INV_3, INV_4,
	CREA_0, CREA_1, CREA_2, CREA_3, CREA_4, CREA_5, CREA_6, CREA_7,
	UNIT_1, UNIT_2, UNIT_3, UNIT_4, UNIT_5, UNIT_6,
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
	if (!(current_str = wcsstr(buf, L"[MountImg]"))) return;
	wcstok(current_str, L"\r\n", &context);
	for (i = 0; i < NB_TXT; i++)
		t[i] = wcstok(NULL, L"\r\n", &context);
	size.LowPart /= sizeof(WCHAR);
	for (i = 0; i < size.LowPart; i++)
		if (buf[i] == '#') buf[i] = '\n';
	setlocale(LC_ALL, "");
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
			!(result = CreateProcessAsUser(lt.LinkedToken, NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)))
			result = CreateProcessAsUser(token, NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
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
	return (dw != 0);
}


static void wait_discutilsdevio()
{
	if (pi_discutilsdevio.hProcess == INVALID_HANDLE_VALUE) return;
	CloseHandle(pi_discutilsdevio.hThread);
	WaitForSingleObject(pi_discutilsdevio.hProcess, INFINITE);
	CloseHandle(pi_discutilsdevio.hProcess);
	pi_discutilsdevio.hProcess = INVALID_HANDLE_VALUE;
}

static long get_imdisk_unit()
{
	long i, j;

	if (!ImDisk_GetDeviceListEx(_countof(list_device), list_device)) return -1;
	i = j = 0;
	while (++j <= list_device[0])
		if (list_device[j] == i) { j = 0; i++; }
	return i;
}

static BOOL get_volume_param(int list_unit)
{
	WCHAR volume[24];
	HANDLE h;
	DWORD dw;
	BOOL fs_ok;

	_snwprintf(volume, _countof(volume), L"\\\\?\\ImDisk%d", list_unit);
	list_size = -1;
	h = CreateFile(volume, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &list_size, sizeof list_size, &dw, NULL);
	wcscat(volume, L"\\");
	list_filesystem[0] = list_label[0] = 0;
	fs_ok = GetVolumeInformation(volume, list_label, _countof(list_label), NULL, NULL, NULL, list_filesystem, _countof(list_filesystem));
	if (!DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dw, NULL) || !DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dw, NULL))
		ImDisk_ForceRemoveDevice(h, 0);
	CloseHandle(h);

	return fs_ok;
}

static BOOL imdisk_check()
{
	long list_unit;
	WCHAR cmdline[MAX_PATH + 80], txt_partition[16];
	BYTE retry = 0;
	BOOL fs_ok = FALSE;

	_snwprintf(txt_partition, _countof(txt_partition), list_partition > 1 ? L" -v %d" : L"", list_partition);
	do {
		if ((list_unit = get_imdisk_unit()) < 0) return FALSE;
		_snwprintf(cmdline, _countof(cmdline), L"imdisk -a -u %d -o %cd,ro,%s -f \"%s\"%s%s", list_unit, dev_list[dev_type], rm_list[removable], filename, retry ? L"" : L" -b auto", txt_partition);
		if (start_process(cmdline, TRUE)) break;
		fs_ok = get_volume_param(list_unit);
	} while (!fs_ok && ++retry < 2);

	return fs_ok;
}

static BOOL discutils_check()
{
	STARTUPINFO si = {sizeof si};
	DWORD ExitCode;
	long list_unit;
	WCHAR cmdline[MAX_PATH + 100], txt_partition[24];
	__int64 pipe, t;

	list_size = -1;
	pipe = _rdtsc();
	_snwprintf(txt_partition, _countof(txt_partition), list_partition != 1 ? L" /partition=%d" : L"", list_partition);
	_snwprintf(cmdline, _countof(cmdline), L"DiscUtils\\DiscUtilsDevio /name=ImDisk%I64x%s /filename=\"%s\" /readonly", pipe, txt_partition, filename);
	if (!CreateProcess(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS, NULL, NULL, &si, &pi_discutilsdevio)) return FALSE;
	if ((list_unit = get_imdisk_unit()) < 0) return FALSE;
	_snwprintf(cmdline, _countof(cmdline), L"imdisk -a -t proxy -u %d -o shm,%cd,ro,%s -f ImDisk%I64x", list_unit, dev_list[dev_type], rm_list[removable], pipe);
	t = _rdtsc();
	do {
		Sleep(0);
		// check if DiscUtilsDevio is still active
		GetExitCodeProcess(pi_discutilsdevio.hProcess, &ExitCode);
		if (ExitCode != STILL_ACTIVE) break;
		if (!start_process(cmdline, TRUE)) {
			get_volume_param(list_unit);
			break;
		}
	} while (_rdtsc() - t < 20000000000);
	return list_size != -1;
}

static DWORD __stdcall list_thread(LPVOID lpParam)
{
	LVITEM lv_item;
	BOOL volume_ok, discutils_pref = FALSE;
	WCHAR text[16];
	__int64 max_list_size = 0;
	int max_partition = 1;
	double d;
	int i;

	partition_changed = FALSE;
	lv_item.mask = LVIF_TEXT;
	lv_item.iItem = list_partition = 0;
	for (;;) {
		WaitForSingleObject(mount_mutex, INFINITE);
		if (list_partition != lv_item.iItem) break;
		if (++list_partition > 128) break;
		if (discutils_pref) {
			volume_ok = discutils_check();
			if (!volume_ok) volume_ok = imdisk_check();
		} else {
			volume_ok = imdisk_check();
			if (!volume_ok && net_installed) discutils_pref = volume_ok = discutils_check();
		}
		if (!volume_ok) {
			if (cmdline_partition_exist)
				cmdline_partition_exist = FALSE;
			else if (!partition_changed) {
				lv_itemindex = max_partition - 1;
				ListView_SetItemState(h_listview, lv_itemindex, LVIS_SELECTED, LVIS_SELECTED);
				ListView_EnsureVisible(h_listview, lv_itemindex, FALSE);
			}
			break;
		}
		lv_item.iSubItem = 0;
		lv_item.pszText = text;
		_snwprintf(text, _countof(text), L"%u", list_partition);
		if (list_partition - 1 != lv_item.iItem) break;
		ListView_InsertItem(h_listview, &lv_item);
		lv_item.iSubItem = 1;
		d = list_size;
		i = UNIT_1;
		while (d >= 999.5) d /= 1024, i++;
		_snwprintf(text, _countof(text) - 1, L"%.3g %s", d, t[i]);
		text[_countof(text) - 1] = 0;
		ListView_SetItem(h_listview, &lv_item);
		lv_item.iSubItem = 2;
		lv_item.pszText = list_filesystem;
		ListView_SetItem(h_listview, &lv_item);
		lv_item.iSubItem = 3;
		lv_item.pszText = list_label;
		ListView_SetItem(h_listview, &lv_item);
		lv_item.iItem++;
		if (list_size > max_list_size && list_filesystem[0]) {
			max_list_size = list_size;
			max_partition = list_partition;
		}
		wait_discutilsdevio();
		ReleaseMutex(mount_mutex);
	}
	wait_discutilsdevio();
	return 0;
}

static void file_check()
{
	WCHAR *ext;
	WIN32_FILE_ATTRIBUTE_DATA file_data;
	_Bool floppy_ok = FALSE;
	BOOL file_exist;
	int i;

	ext = PathFindExtension(filename);
	dev_type = !_wcsicmp(ext, L".iso") || !_wcsicmp(ext, L".nrg") || !_wcsicmp(ext, L".bin");
	CheckRadioButton(hDialog, ID_RB3, ID_RB5, ID_RB3 + dev_type);

	if ((file_exist = GetFileAttributesEx(filename, GetFileExInfoStandard, &file_data)) && !file_data.nFileSizeHigh)
		for (i = 0; i < _countof(floppy_size); i++)
			if (floppy_size[i] == file_data.nFileSizeLow) floppy_ok = TRUE;
	if (floppy_ok || !_wcsicmp(ext, L".vfd")) {
		dev_type = 2;
		CheckRadioButton(hDialog, ID_RB3, ID_RB5, ID_RB5);
		if (drive_list[0][0] == 'A' || drive_list[0][0] == 'B')
			SendMessage(hwnd_combo2, CB_SETCURSEL, 0, 0);
	}

	ListView_DeleteAllItems(h_listview);
	if (!cmdline_mount && file_exist && !(file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		CreateThread(NULL, 0, list_thread, NULL, 0, NULL);
}

static int get_reg_max()
{
	int max = -1;
	DWORD data_size = sizeof max;

	RegQueryValueEx(registry_key, L"ImMaxReg", NULL, NULL, (void*)&max, &data_size);
	return max;
}

static int reg_remove()
{
	WCHAR param_name[16], *param_name_ptr, reg_filename[MAX_PATH];
	DWORD data_size, device;
	int i, first_free, last_valid, max;

	first_free = last_valid = -1;
	max = get_reg_max();
	for (i = 0;; i++) {
		param_name_ptr = param_name + _snwprintf(param_name, _countof(param_name), L"Im%d", i);
		wcscpy(param_name_ptr, L"FileName");
		data_size = sizeof reg_filename;
		if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)reg_filename, &data_size) == ERROR_SUCCESS) {
			if (wcscmp(reg_filename, filename))
				last_valid = i;
			else {
				RegDeleteValue(registry_key, param_name);
				wcscpy(param_name_ptr, L"MountPoint");
				RegDeleteValue(registry_key, param_name);
				wcscpy(param_name_ptr, L"Param");
				RegDeleteValue(registry_key, param_name);
				wcscpy(param_name_ptr, L"Device");
				data_size = sizeof device;
				if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&device, &data_size) == ERROR_SUCCESS)
					ImDisk_RemoveRegistrySettings(device);
				RegDeleteValue(registry_key, param_name);
				if (first_free == -1) first_free = i;
			}
		} else {
			if (first_free == -1) first_free = i;
			if (i > max) break;
		}
	}
	RegSetValueEx(registry_key, L"ImMaxReg", 0, REG_DWORD, (void*)&last_valid, sizeof last_valid);

	return first_free;
}

static void reg_save()
{
	WCHAR param_name[16], *param_name_ptr;
	ULARGE_INTEGER param;
	int i, max;

	i = reg_remove();
	max = get_reg_max();
	if (i > max) RegSetValueEx(registry_key, L"ImMaxReg", 0, REG_DWORD, (void*)&i, sizeof i);
	param_name_ptr = param_name + _snwprintf(param_name, _countof(param_name), L"Im%d", i);
	wcscpy(param_name_ptr, L"FileName");
	RegSetValueEx(registry_key, param_name, 0, REG_SZ, (void*)filename, (wcslen(filename) + 1) * sizeof(WCHAR));
	wcscpy(param_name_ptr, L"MountPoint");
	RegSetValueEx(registry_key, param_name, 0, REG_SZ, (void*)drive, (wcslen(drive) + 1) * sizeof(WCHAR));
	param.HighPart = partition;
	param.LowPart = (dev_type << 16) + (removable << 1) + readonly;
	wcscpy(param_name_ptr, L"Param");
	RegSetValueEx(registry_key, param_name, 0, REG_QWORD, (void*)&param, sizeof param);
	if (device_number >= 0) {
		wcscpy(param_name_ptr, L"Device");
		RegSetValueEx(registry_key, param_name, 0, REG_DWORD, (void*)&device_number, sizeof device_number);
	}
}

static void disp_controls()
{
	ShowWindow(hwnd_combo2, !mount_point);
	ShowWindow(hwnd_edit1, mount_point);
	ShowWindow(hwnd_pbutton2, mount_point);
	EnableWindow(hwnd_ok, filename[0] && (!mount_point || mountdir[0]));
}

static DWORD __stdcall UnmountDrive(LPVOID lpParam)
{
	WCHAR cmd_line[MAX_PATH + 20];

	_snwprintf(cmd_line, _countof(cmd_line), L"ImDisk-Dlg RM \"%s\"", lpParam);
	start_process(cmd_line, TRUE);
	init_ok = TRUE;
	mount_point = IsDlgButtonChecked(hDialog, ID_RB2);
	disp_controls();
	EnableWindow(hwnd_pbutton3, TRUE);
	ListView_DeleteAllItems(h_listview);
	if (PathFileExists(filename) && !PathIsDirectory(filename))
		list_thread(NULL);
	return 0;
}

static void draw_icon(HWND hDlg)
{
	PAINTSTRUCT paint;

	DrawIcon(BeginPaint(hDlg, &paint), icon_coord.left, icon_coord.top, hIcon);
	EndPaint(hDlg, &paint);
}

static INT_PTR __stdcall CreateFile_Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HANDLE h;
	WCHAR txt_size[18];
	__int64 size;
	int i, unit;

	switch (Msg)
	{
		case WM_INITDIALOG:
			if (t[0]) {
				SetWindowText(hDlg, t[CREA_0]);
				SetDlgItemText(hDlg, ID_TEXT1, t[CREA_1]);
				SetDlgItemText(hDlg, ID_TEXT2, t[CREA_2]);
				SetDlgItemText(hDlg, ID_CHECK21, t[CREA_3]);
				SetDlgItemText(hDlg, IDOK, t[CREA_4]);
				SetDlgItemText(hDlg, IDCANCEL, t[CREA_5]);
				SetDlgItemText(hDlg, ID_RB21, t[UNIT_1]);
				SetDlgItemText(hDlg, ID_RB22, t[UNIT_2]);
				SetDlgItemText(hDlg, ID_RB23, t[UNIT_3]);
				SetDlgItemText(hDlg, ID_RB24, t[UNIT_4]);
			}
			hIcon = LoadImage(NULL, MAKEINTRESOURCE(OIC_QUES), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			icon_coord.left = 13;
			icon_coord.top = 11;
			icon_coord.right = 0;
			icon_coord.bottom = 0;
			MapDialogRect(hDlg, &icon_coord);

			CheckRadioButton(hDlg, ID_RB21, ID_RB24, ID_RB21);
			CheckDlgButton(hDlg, ID_CHECK21, !mount_point);

			return TRUE;

		case WM_PAINT:
			draw_icon(hDlg);
			return TRUE;

		case WM_COMMAND:
			unit = 0;
			for (i = 0; i < 4; i++)
				if (IsDlgButtonChecked(hDlg, ID_RB21 + i)) unit = 10 * i;

			GetDlgItemText(hDlg, ID_EDIT21, txt_size, _countof(txt_size));
			size = min(_wtoi64(txt_size), _I64_MAX >> unit) << unit;
			EnableWindow(GetDlgItem(hDlg, IDOK), size >= 65537);

			if (LOWORD(wParam) == IDOK) {
				if ((h = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
					MessageBox(hDlg, t[CREA_6], L"ImDisk", MB_ICONERROR);
					return TRUE;
				}
				SetFilePointerEx(h, (LARGE_INTEGER)size, NULL, FILE_BEGIN);
				if (!SetEndOfFile(h)) {
					CloseHandle(h);
					DeleteFile(filename);
					MessageBox(hDlg, t[CREA_7], L"ImDisk", MB_ICONERROR);
					return TRUE;
				}
				CloseHandle(h);
				new_file = TRUE;
				if (!(i = IsDlgButtonChecked(hDlg, ID_CHECK21))) file_check();
				EndDialog(hDlg, i);
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, FALSE);

			return TRUE;

		default:
			return FALSE;
	}
}

static DWORD __stdcall invalid_fs_unmount(LPVOID lpParam)
{
	WCHAR cmdline[MAX_PATH + 20];

	_snwprintf(cmdline, _countof(cmdline), L"ImDisk-Dlg RM \"%s\"", drive);
	start_process(cmdline, TRUE);
	EndDialog(lpParam, 0);
	return 0;
}

static INT_PTR __stdcall InvalidFS_Proc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			if (t[0]) {
				SetWindowText(hDlg, t[INV_0]);
				SetDlgItemText(hDlg, ID_TEXT1, t[INV_1]);
				SetDlgItemText(hDlg, ID_PBUTTON11, t[INV_2]);
				SetDlgItemText(hDlg, ID_PBUTTON12, t[INV_3]);
				SetDlgItemText(hDlg, ID_PBUTTON13, t[INV_4]);
			}
			hIcon = LoadImage(NULL, MAKEINTRESOURCE(OIC_WARNING), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			icon_coord.left = 14;
			icon_coord.top = 18;
			icon_coord.right = 0;
			icon_coord.bottom = 0;
			MapDialogRect(hDlg, &icon_coord);
			EnableWindow(GetDlgItem(hDlg, ID_PBUTTON12), !mount_point);
			if (!mount_point) SetTimer(hDlg, 1, 1000, NULL);
			MessageBeep(MB_ICONWARNING);
			return TRUE;

		case WM_PAINT:
			draw_icon(hDlg);
			return TRUE;

		case WM_TIMER:
			if (GetVolumeInformation(drive, NULL, 0, NULL, NULL, NULL, NULL, 0)) {
				EndDialog(hDlg, 0);
				EndDialog(hDialog, 0);
			}
			if (GetLastError() == ERROR_PATH_NOT_FOUND) {
				reg_remove();
				EndDialog(hDlg, 0);
			}
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == ID_PBUTTON11) {
				EnableWindow(GetDlgItem(hDlg, ID_PBUTTON11), FALSE);
				EnableWindow(GetDlgItem(hDlg, ID_PBUTTON12), FALSE);
				CreateThread(NULL, 0, invalid_fs_unmount, hDlg, 0, NULL);
				reg_remove();
			}

			if (LOWORD(wParam) == ID_PBUTTON12) {
				ShowWindow(hDlg, SW_HIDE);
				ShowWindow(hDialog, SW_HIDE);
				SHFormatDrive(hDialog, drive[0] - 'A', SHFMT_ID_DEFAULT, SHFMT_OPT_FULL);
				EndDialog(hDlg, 0);
				EndDialog(hDialog, 0);
			}

			if (LOWORD(wParam) == ID_PBUTTON13) {
				EndDialog(hDlg, 0);
				EndDialog(hDialog, 0);
			}

			return TRUE;

		default:
			return FALSE;
	}
}


static int Imdisk_Mount(BYTE no_check_fs)
{
	WCHAR cmdline[MAX_PATH * 2 + 80], txt_partition[16];
	BOOL fs_ok = FALSE;
	BYTE retry = !partition;

	_snwprintf(txt_partition, _countof(txt_partition), partition > 1 ? L" -v %d" : L"", partition);
	do {
		if ((device_number = get_imdisk_unit()) < 0) return 1;
		_snwprintf(cmdline, _countof(cmdline), L"imdisk -a -u %d -o %cd,ro,%s -f \"%s\"%s%s", device_number, dev_list[dev_type], rm_list[removable], filename, retry ? L"" : L" -b auto", txt_partition);
		if (start_process(cmdline, TRUE)) return 1;
		_snwprintf(cmdline, _countof(cmdline), L"\\\\?\\ImDisk%d\\", device_number);
		fs_ok = GetVolumeInformation(cmdline, NULL, 0, NULL, NULL, NULL, NULL, 0);
		_snwprintf(cmdline, _countof(cmdline), L"imdisk -D -u %d", device_number);
		start_process(cmdline, TRUE);
	} while (!fs_ok && ++retry < 2);
	if (fs_ok || no_check_fs) {
		if ((device_number = get_imdisk_unit()) < 0) return 1;
		_snwprintf(cmdline, _countof(cmdline), L"imdisk -a -u %d -m \"%s\" -o %cd,r%c,%s -f \"%s\"%s%s%s",
				 device_number, drive, dev_list[dev_type], ro_list[readonly], rm_list[removable], filename, retry ? L"" : L" -b auto", txt_partition, boot_list[win_boot]);
		return start_process(cmdline, TRUE);
	} else return 1;
}

static int DiscUtils_Mount()
{
	WCHAR cmdline1[MAX_PATH + 70], cmdline2[MAX_PATH + 70], txt_partition[24];
	WCHAR *cmdline_ptr[3] = {cmdline1, cmdline2, drive};
	HANDLE h;
	int error;
	__int64 pipe;

	pipe = _rdtsc();
	_snwprintf(txt_partition, _countof(txt_partition), partition != 1 ? L" /partition=%d" : L"", partition);
	_snwprintf(cmdline1, _countof(cmdline1), L"/name=ImDisk%I64x%s /filename=\"%s\"%s", pipe, txt_partition, filename, ro_discutils_list[readonly]);
	_snwprintf(cmdline2, _countof(cmdline2), L"-o shm,%cd,r%c,%s -f ImDisk%I64x", dev_list[dev_type], ro_list[readonly], rm_list[removable], pipe);
	h = CreateSemaphoreA(NULL, 0, 2, "Global\\MountImgSvcSema");
	StartService(h_svc, 3, (void*)cmdline_ptr);
	error = WaitForSingleObject(h, 15000) != WAIT_OBJECT_0 || WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
	CloseHandle(h);
	if (!error && !mount_point)
		ImDisk_NotifyShellDriveLetter(NULL, drive);

	return error;
}

static DWORD __stdcall Mount(LPVOID lpParam)
{
	WCHAR cmdline[MAX_PATH + 16];
	int i, error;

	WaitForSingleObject(mount_mutex, INFINITE);
	list_partition = -1;

	if (mount_point) wcscpy(drive, mountdir);

	error = Imdisk_Mount(new_file || !net_installed);
	if (error && !new_file && net_installed) {
		device_number = -1;
		error = DiscUtils_Mount();
	}

	if (error) {
		MessageBox(hDialog, t[ERR_1], L"ImDisk", MB_ICONERROR);
ready_again:
		EnableWindow(hwnd_pbutton3, TRUE);
		EnableWindow(hwnd_ok, TRUE);
		SetDlgItemText(hDialog, IDOK, L"OK");
		init_ok = TRUE;
		ReleaseMutex(mount_mutex);
		if (cmdline_mount) {
			cmdline_mount = FALSE;
			EnableWindow(hDialog, TRUE);
			SendMessage(hDialog, WM_NEXTDLGCTL, (WPARAM)combo1.hwndItem, TRUE);
			file_check();
		}
		return 0;
	}

	if (win_boot) reg_save();

	if (!new_file) {
		// check the new drive or mount point
		i = 0;
		if (mount_point) PathAddBackslash(drive);
		do {
			if (GetVolumeInformation(drive, NULL, 0, NULL, NULL, NULL, NULL, 0)) {
				if (cmdline_mount || !show_explorer) break;
				if (os_ver.dwMajorVersion < 6) {
					_snwprintf(cmdline, _countof(cmdline), L"explorer /n,%s", drive);
					start_process(cmdline, FALSE);
				} else
					ShellExecute(NULL, NULL, drive, NULL, NULL, SW_SHOWNORMAL);
				break;
			} else if (GetLastError() == ERROR_UNRECOGNIZED_VOLUME) {
				PathRemoveBackslash(drive);
				DialogBox(hinst, L"INVALID_FS", hDialog, InvalidFS_Proc);
				goto ready_again;
			}
			Sleep(100);
		} while (++i < 100);
	}

	EndDialog(hDialog, 0);

	return 0;
}

static INT_PTR __stdcall DlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	OPENFILENAME ofn = {sizeof ofn};
	BROWSEINFO bi = {};
	LPITEMIDLIST pid_folder; // PIDLIST_ABSOLUTE on MSDN
	WIN32_FIND_DATA FindFileData;
	HANDLE h;
	DWORD flags, mask, version, data_size;
	HWND h_updown;
	LVCOLUMN lv_col;
	RECT coord;
	HMODULE hDLL;
	FARPROC lpFunc;
	WCHAR c, param_name[24], *param_name_ptr;
	ULARGE_INTEGER param;
	BOOL is_imdisk;
	int i, n_drive, select, max;

	switch (Msg)
	{
		case WM_INITDIALOG:
			hDialog = hDlg;
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hinst, MAKEINTRESOURCE(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TITLE, t[TITLE]);
				SetDlgItemText(hDlg, ID_TEXT1, t[CTRL_1]);
				SetDlgItemText(hDlg, ID_RB1, t[CTRL_2]);
				SetDlgItemText(hDlg, ID_RB2, t[CTRL_3]);
				SetDlgItemText(hDlg, ID_PBUTTON3, t[CTRL_4]);
				SetDlgItemText(hDlg, ID_TEXT2, t[CTRL_5]);
				SetDlgItemText(hDlg, ID_RB3, t[CTRL_6]);
				SetDlgItemText(hDlg, ID_RB4, t[CTRL_7]);
				SetDlgItemText(hDlg, ID_RB5, t[CTRL_8]);
				SetDlgItemText(hDlg, ID_CHECK1, t[CTRL_9]);
				SetDlgItemText(hDlg, ID_CHECK2, t[CTRL_10]);
				SetDlgItemText(hDlg, ID_TEXT3, t[CTRL_11]);
				SetDlgItemText(hDlg, ID_CHECK3, t[CTRL_12]);
				SetDlgItemText(hDlg, ID_PBUTTON4, t[CTRL_13]);
				SetDlgItemText(hDlg, IDOK, t[CTRL_14]);
				SetDlgItemText(hDlg, IDCANCEL, t[CTRL_15]);
			}

			// initialize tooltips
			hTTip = CreateWindow(TOOLTIPS_CLASS, NULL, TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL); 
			ti.cbSize = sizeof(TOOLINFO);
			ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
			ti.hwnd = hDlg;
			ti.lpszText = LPSTR_TEXTCALLBACK;
			combo1.cbSize = sizeof(COMBOBOXINFO);
			ti.uId = (UINT_PTR)(hwnd_pbutton3 = GetDlgItem(hDlg, ID_PBUTTON3));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			GetComboBoxInfo(GetDlgItem(hDlg, ID_COMBO1), &combo1);
			ti.uId = (UINT_PTR)(combo1.hwndItem);
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_edit1 = GetDlgItem(hDlg, ID_EDIT1));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_check1 = GetDlgItem(hDlg, ID_CHECK1));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_check2 = GetDlgItem(hDlg, ID_CHECK2));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_rb3 = GetDlgItem(hDlg, ID_RB3));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_rb4 = GetDlgItem(hDlg, ID_RB4));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_rb5 = GetDlgItem(hDlg, ID_RB5));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_updown = GetDlgItem(hDlg, ID_UPDOWN));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			ti.uId = (UINT_PTR)(hwnd_check3 = GetDlgItem(hDlg, ID_CHECK3));
			SendMessage(hTTip, TTM_ADDTOOL, 0, (LPARAM)&ti);
			SendMessage(hTTip, TTM_SETMAXTIPWIDTH, 0, 1000);

			hwnd_combo2 = GetDlgItem(hDlg, ID_COMBO2);
			hwnd_pbutton2 = GetDlgItem(hDlg, ID_PBUTTON2);
			hwnd_pbutton4 = GetDlgItem(hDlg, ID_PBUTTON4);

			SendMessage(combo1.hwndCombo, CB_LIMITTEXT, _countof(filename) - 1, 0);
			// load file list
			max = get_reg_max();
			for (i = 0; i <= max; i++) {
				_snwprintf(param_name, _countof(param_name), L"Im%dFileName", i);
				data_size = MAX_PATH;
				if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)txt, &data_size) == ERROR_SUCCESS)
					SendMessage(combo1.hwndCombo, CB_ADDSTRING, 0, (LPARAM)txt);
			}
			SetDlgItemText(hDlg, ID_COMBO1, filename);

			CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB1 + mount_point);

			// set list of available drives
			n_drive = 0;
			select = -1;
			mask = mask0 | GetLogicalDrives();
			wcscpy(txt, L"\\\\.\\A:");
			for (c = 'A'; c <= 'Z'; c++) {
				h = CreateFile(txt, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
				is_imdisk = FALSE;
				if (!(mask & 1) || (is_imdisk = DeviceIoControl(h, IOCTL_IMDISK_QUERY_VERSION, NULL, 0, &version, sizeof version, &data_size, NULL))) {
					if (c == cmdline_drive_letter || (c >= 'C' && !is_imdisk && select == -1)) select = n_drive;
					drive_list[n_drive][0] = c;
					drive_list[n_drive][1] = ':';
					SendMessage(hwnd_combo2, CB_ADDSTRING, 0, (LPARAM)drive_list[n_drive++]);
				}
				CloseHandle(h);
				mask >>= 1;
				txt[4]++;
			}
			i = max(select, 0);
			SendMessage(hwnd_combo2, CB_SETCURSEL, i, 0);
			wcscpy(drive, drive_list[i]);

			SendMessage(hwnd_edit1, EM_SETLIMITTEXT, _countof(mountdir) - 1, 0);
			SetDlgItemText(hDlg, ID_EDIT1, mountdir);

			CheckDlgButton(hDlg, ID_CHECK1, readonly);
			CheckDlgButton(hDlg, ID_CHECK2, removable);

			// up-down control
			h_updown = CreateWindow(UPDOWN_CLASS, NULL, WS_CHILDWINDOW | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_HOTTRACK, 0, 0, 0, 0, hDlg, NULL, hinst, NULL);
			SendMessage(h_updown, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, ID_UPDOWN), 0);
			SendMessage(h_updown, UDM_SETRANGE, 0, MAKELPARAM(128, 0));
			SetDlgItemInt(hDlg, ID_UPDOWN, cmdline_partition, FALSE);
			partition = cmdline_partition;

			// list view
			h_listview = GetDlgItem(hDlg, ID_LISTVIEW);
			ListView_SetExtendedListViewStyle(h_listview, LVS_EX_FULLROWSELECT);
			lv_col.mask = LVCF_WIDTH | LVCF_TEXT;
			for (i = 0; i < _countof(lv_titles); i++) {
				coord.left = lv_width[i];
				MapDialogRect(hDlg, &coord);
				lv_col.cx = coord.left;
				lv_col.pszText = i && t[LV_1 + i - 1] ? t[LV_1 + i - 1] : lv_titles[i];
				lv_col.cchTextMax = (wcslen(lv_col.pszText) + 1) * sizeof(WCHAR);
				ListView_InsertColumn(h_listview, i, &lv_col);
			}

			hDLL = GetModuleHandleA("user32");
			if ((lpFunc = GetProcAddress(hDLL, "ChangeWindowMessageFilterEx"))) {
				lpFunc(hDlg, WM_DROPFILES, MSGFLT_ALLOW, NULL);
				lpFunc(hDlg, 0x0049, MSGFLT_ALLOW, NULL); // 0x0049 = WM_COPYGLOBALDATA
			} else if ((lpFunc = GetProcAddress(hDLL, "ChangeWindowMessageFilter"))) {
				lpFunc(WM_DROPFILES, MSGFLT_ADD);
				lpFunc(0x0049, MSGFLT_ADD);
			}

			file_check();
			if (cmdline_dev_type) {
				dev_type = cmdline_dev_type - 1;
				CheckRadioButton(hDlg, ID_RB3, ID_RB5, ID_RB3 + dev_type);
			}
			hwnd_ok = GetDlgItem(hDlg, IDOK);
			disp_controls();

			init_ok = TRUE;
			if (cmdline_mount)
				goto mount;
			else {
				EnableWindow(hDlg, TRUE);
				SetFocus(combo1.hwndItem);
			}
			return FALSE;

		case WM_NOTIFY:
			if (((NMHDR*)lParam)->code == TTN_GETDISPINFO) {
				SendMessage(hTTip, TTM_SETDELAYTIME, TTDT_AUTOPOP, ((NMHDR*)lParam)->idFrom == (UINT_PTR)combo1.hwndItem ? 25000 : 15000);
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_pbutton3)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_1];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)combo1.hwndItem)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_2];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_edit1)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_3];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_rb3 || ((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_rb4 || ((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_rb5)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_4];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_updown)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_5];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check1)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_6];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check2)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_7];
				if (((NMHDR*)lParam)->idFrom == (UINT_PTR)hwnd_check3)
					((NMTTDISPINFO*)lParam)->lpszText = t[TTIP_8];
			}
			if (((NMHDR*)lParam)->code == LVN_ITEMCHANGED && ((NMLISTVIEW*)lParam)->uNewState & LVIS_SELECTED) {
				partition_changed = TRUE;
				partition = ((NMLISTVIEW*)lParam)->iItem + 1;
				SetDlgItemInt(hDlg, ID_UPDOWN, partition, FALSE);
				lv_itemindex = ((NMLISTVIEW*)lParam)->iItem;
			}
			return TRUE;

		case WM_DROPFILES:
#ifdef _WIN64
			if (!DragQueryFile((HDROP)wParam, 0, txt, _countof(filename))) {
				// dragging from a 32-bit process gives invalid high order DWORD in wParam
				ULARGE_INTEGER ptr;
				ptr.QuadPart = (ULONGLONG)GlobalAlloc(GMEM_MOVEABLE, 0);
				GlobalFree((HGLOBAL)ptr.QuadPart);
				ptr.QuadPart -= 16;
				ptr.LowPart = (DWORD)wParam;
				wParam = (WPARAM)ptr.QuadPart;
				DragQueryFile((HDROP)wParam, 0, txt, _countof(filename));
			}
#else
			DragQueryFile((HDROP)wParam, 0, txt, _countof(filename));
#endif
			DragFinish((HDROP)wParam);
			if (PathIsDirectory(txt)) {
				SetDlgItemText(hDlg, ID_EDIT1, txt);
				CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB2);
				mount_point = TRUE;
			} else {
				SetDlgItemText(hDlg, ID_COMBO1, txt);
				wcscpy(filename, txt);
				file_check();
			}
			disp_controls();
			return TRUE;

		case WM_COMMAND:
			if (!init_ok) return FALSE;

			for (i = 0; i < 3; i++)
				if (IsDlgButtonChecked(hDlg, ID_RB3 + i)) dev_type = i;
			readonly = IsDlgButtonChecked(hDlg, ID_CHECK1);
			removable = IsDlgButtonChecked(hDlg, ID_CHECK2);

			if (LOWORD(wParam) == ID_COMBO1) {
				if (HIWORD(wParam) == CBN_EDITCHANGE) {
					GetDlgItemText(hDlg, ID_COMBO1, filename, _countof(filename));
					file_check();
				}
				if (HIWORD(wParam) == CBN_SELCHANGE) {
					SendMessage(combo1.hwndCombo, CB_GETLBTEXT, SendMessage(combo1.hwndCombo, CB_GETCURSEL, 0, 0), (LPARAM)filename);
					txt[0] = 0;
					max = get_reg_max();
					for (i = 0; i <= max; i++) {
						param_name_ptr = param_name + _snwprintf(param_name, _countof(param_name), L"Im%d", i);
						wcscpy(param_name_ptr, L"FileName");
						data_size = sizeof(txt) - sizeof(WCHAR);
						if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)txt, &data_size) == ERROR_SUCCESS && !wcscmp(txt, filename)) {
							wcscpy(param_name_ptr, L"MountPoint");
							data_size = sizeof txt;
							if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)txt, &data_size) == ERROR_SUCCESS) {
								if (txt[0] && txt[1] == ':' && !txt[2]) {
									mount_point = FALSE;
									SendMessage(hwnd_combo2, CB_SELECTSTRING, -1, (LPARAM)txt);
								} else {
									mount_point = TRUE;
									SetDlgItemText(hDlg, ID_EDIT1, txt);
								}
								CheckRadioButton(hDlg, ID_RB1, ID_RB2, ID_RB1 + mount_point);
							}
							wcscpy(param_name_ptr, L"Param");
							data_size = sizeof param;
							if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&param, &data_size) == ERROR_SUCCESS) {
								CheckDlgButton(hDlg, ID_CHECK1, readonly = param.LowPart & 1);
								CheckDlgButton(hDlg, ID_CHECK2, removable = (param.LowPart >> 1) & 1);
								CheckRadioButton(hDlg, ID_RB3, ID_RB5, ID_RB3 + (dev_type = param.LowPart >> 16));
								SetDlgItemInt(hDlg, ID_UPDOWN, partition = param.HighPart, FALSE);
							}
							break;
						}
					}
					ListView_DeleteAllItems(h_listview);
					if (PathFileExists(filename) && !PathIsDirectory(filename))
						CreateThread(NULL, 0, list_thread, NULL, 0, NULL);
				}
			}

			wcscpy(drive, drive_list[SendMessage(hwnd_combo2, CB_GETCURSEL, 0, 0)]);
			GetDlgItemText(hDlg, ID_EDIT1, mountdir, _countof(mountdir));
			mount_point = IsDlgButtonChecked(hDlg, ID_RB2);

			if (LOWORD(wParam) == ID_PBUTTON1) {
				ofn.hwndOwner = hDlg;
				ofn.lpstrFile = filename;
				ofn.nMaxFile = _countof(filename);
				ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
				GetOpenFileName(&ofn);
				if (filename[0]) {
					SetDlgItemText(hDlg, ID_COMBO1, filename);
					file_check();
				}
			}

			disp_controls();

			if (LOWORD(wParam) == ID_PBUTTON2) {
				bi.hwndOwner = hDlg;
				bi.pszDisplayName = mountdir;
				bi.lpszTitle = t[CTRL_17];
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
				pid_folder = SHBrowseForFolder(&bi);
				if (pid_folder) {
					SHGetPathFromIDList(pid_folder, mountdir);
					SetDlgItemText(hDlg, ID_EDIT1, mountdir);
				}
			}

			if (LOWORD(wParam) == ID_PBUTTON3) {
				init_ok = FALSE;
				EnableWindow(hwnd_pbutton3, FALSE);
				EnableWindow(hwnd_ok, FALSE);
				CreateThread(NULL, 0, UnmountDrive, mount_point ? mountdir : drive, 0, NULL);
				reg_remove();
				SendMessage(combo1.hwndCombo, CB_DELETESTRING, SendMessage(combo1.hwndCombo, CB_FINDSTRINGEXACT, -1, (LPARAM)filename), 0);
				SetDlgItemText(hDlg, ID_COMBO1, filename);
			}

			if (LOWORD(wParam) == ID_PBUTTON4) {
				_snwprintf(txt, _countof(txt) - 1, L"rundll32 imdisk.cpl,RunDLL_MountFile %s", filename);
				start_process(txt, FALSE);
				EndDialog(hDlg, 1);
			}

			if ((LOWORD(wParam) == ID_UPDOWN && HIWORD(wParam) == EN_CHANGE)) {
				partition_changed = TRUE;
				partition = GetDlgItemInt(hDlg, ID_UPDOWN, NULL, FALSE);
				if (partition - 1 != lv_itemindex) {
					lv_itemindex = partition - 1;
					ListView_SetItemState(h_listview, lv_itemindex, LVIS_SELECTED, LVIS_SELECTED);
					ListView_EnsureVisible(h_listview, lv_itemindex, FALSE);
				}
			}

			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 0);

			if (LOWORD(wParam) == IDOK) {
mount:
				win_boot = IsDlgButtonChecked(hDlg, ID_CHECK3);

				// save parameters
				RegSetValueEx(registry_key, L"MountPoint", 0, REG_DWORD, (void*)&mount_point, sizeof mount_point);
				RegSetValueEx(registry_key, L"MountDir", 0, REG_SZ, (void*)mountdir, (wcslen(mountdir) + 1) * sizeof(WCHAR));

				new_file = FALSE;
				if (!PathFileExists(filename) && !DialogBox(hinst, L"CREATE_FILE", hDlg, CreateFile_Proc))
					return TRUE;

				// check mount point
				if (mount_point) {
					GetFullPathName(mountdir, _countof(txt), txt, NULL);
					txt[3] = 0;
					if (!GetVolumeInformation(txt, NULL, 0, NULL, NULL, &flags, NULL, 0) || !(flags & FILE_SUPPORTS_REPARSE_POINTS)) {
						MessageBox(hDlg, t[ERR_2], L"ImDisk", MB_ICONERROR);
						return TRUE;
					}
					FindClose(h = FindFirstFile(mountdir, &FindFileData));
					if (h != INVALID_HANDLE_VALUE && FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT && FindFileData.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT) {
						if (MessageBox(hDlg, t[ERR_3], L"ImDisk", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDNO)
							return TRUE;
					} else if (!PathIsDirectoryEmpty(mountdir)) {
						MessageBox(hDlg, t[ERR_4], L"ImDisk", MB_ICONERROR);
						return TRUE;
					}
				} else
					if (GetLogicalDrives() & 1 << (drive[0] - 'A')) {
						MessageBox(hDlg, t[ERR_5], L"ImDisk", MB_ICONERROR);
						return TRUE;
					}

				init_ok = FALSE;
				EnableWindow(hwnd_pbutton3, FALSE);
				EnableWindow(hwnd_ok, FALSE);
				SetDlgItemText(hDlg, IDOK, t[CTRL_16]);
				if (cmdline_mount) {
					Mount(NULL);
					return FALSE;
				}
				else
					CreateThread(NULL, 0, Mount, NULL, 0, NULL);
			}

			return TRUE;

		case WM_CONTEXTMENU:
			if ((HWND)wParam == hwnd_pbutton4)
				ShellExecute(NULL, NULL, L"imdisk.cpl", NULL, NULL, SW_SHOWNORMAL);
			return TRUE;

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
	WCHAR cmd_line[MAX_PATH + 100];
	STARTUPINFO si = {sizeof si};
	PROCESS_INFORMATION pi;
	WCHAR txt_partition[24], param_name[24], *param_name_ptr, *cmd_line_ptr;
	ULARGE_INTEGER param;
	DWORD data_size, ExitCode;
	HANDLE h;
	int i, j, max, error;
	__int64 pipe;

	SvcStatusHandle = RegisterServiceCtrlHandlerEx(L"", HandlerEx, NULL);
	SetServiceStatus(SvcStatusHandle, &SvcStatus);

	if (dwArgc >= 4) {
		_snwprintf(cmd_line, _countof(cmd_line) - 1, L"DiscUtils\\DiscUtilsDevio %s", lpszArgv[1]);
		cmd_line[_countof(cmd_line) - 1] = 0;
		if (CreateProcess(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
			_snwprintf(cmd_line, _countof(cmd_line) - 1, L"imdisk -a -t proxy -m \"%s\" %s", lpszArgv[3], lpszArgv[2]);
			for (j = 0; j < 100; j++) {
				Sleep(100);
				GetExitCodeProcess(pi.hProcess, &ExitCode);
				if (ExitCode != STILL_ACTIVE) {
					error = TRUE;
					break;
				}
				if (!(error = start_process(cmd_line, TRUE))) break;
			}
		} else error = TRUE;

		h = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE, FALSE, "Global\\MountImgSvcSema");
		ReleaseSemaphore(h, 1 + error, NULL);
		CloseHandle(h);
	}
	else
	{
		wcscat(module_name, L" /NOTIF  :");
		cmd_line_ptr = module_name + wcslen(module_name) - 2;

		max = get_reg_max();
		for (i = 0; i <= max; i++) {
			param_name_ptr = param_name + _snwprintf(param_name, _countof(param_name), L"Im%d", i);
			wcscpy(param_name_ptr, L"FileName");
			data_size = sizeof(filename) - sizeof(WCHAR);
			if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)filename, &data_size) != ERROR_SUCCESS) continue;
			wcscpy(param_name_ptr, L"Device");
			if (RegQueryValueEx(registry_key, param_name, NULL, NULL, NULL, NULL) != ERROR_FILE_NOT_FOUND) continue;
			wcscpy(param_name_ptr, L"MountPoint");
			data_size = sizeof(drive) - sizeof(WCHAR);
			if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)drive, &data_size) != ERROR_SUCCESS) continue;
			wcscpy(param_name_ptr, L"Param");
			data_size = sizeof param;
			if (RegQueryValueEx(registry_key, param_name, NULL, NULL, (void*)&param, &data_size) != ERROR_SUCCESS) continue;

			pipe = _rdtsc();
			_snwprintf(txt_partition, _countof(txt_partition), param.HighPart != 1 ? L" /partition=%d" : L"", param.HighPart);
			_snwprintf(cmd_line, _countof(cmd_line), L"DiscUtils\\DiscUtilsDevio /name=ImDisk%I64x%s /filename=\"%s\"", pipe, txt_partition, filename);
			if (!CreateProcess(NULL, cmd_line, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) continue;
			_snwprintf(cmd_line, _countof(cmd_line), L"imdisk -a -t proxy -m \"%s\" -o shm,%cd,r%c,%s -f ImDisk%I64x", drive, dev_list[param.LowPart >> 16], ro_list[param.LowPart & 1], rm_list[(param.LowPart >> 1) & 1], pipe);
			for (j = 0; j < 100; j++) {
				Sleep(100);
				GetExitCodeProcess(pi.hProcess, &ExitCode);
				if (ExitCode != STILL_ACTIVE) {
					CloseHandle(pi.hProcess);
					CloseHandle(pi.hThread);
					error = TRUE;
					break;
				}
				if (!(error = start_process(cmd_line, TRUE))) break;
			}
			if (error) continue;

			if (drive[0] && drive[1] == ':' && !drive[2]) {
				*cmd_line_ptr = drive[0];
				start_process(module_name, 2);
			}
		}
	}

	SvcStatus.dwCurrentState = SERVICE_STOPPED;
	SetServiceStatus(SvcStatusHandle, &SvcStatus);
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int argc;
	LPWSTR *argv;
	WCHAR *cmdline_ptr, *exe_path, *opt;
	HMODULE h_cpl;
	HWND hwnd;
	HKEY h_key;
	SC_HANDLE h_scman;
	SERVICE_DESCRIPTION svc_description;
	WCHAR param_name[24], *param_name_ptr;
	_Bool svc_required = FALSE;
	DWORD data_size, net_release;
	int i, max;
	SERVICE_TABLE_ENTRY DispatchTable[] = {{L"", SvcMain}, {NULL, NULL}};

	pi_discutilsdevio.hProcess = INVALID_HANDLE_VALUE;

	os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&os_ver);

	cmdline_ptr = GetCommandLine();
	argv = CommandLineToArgvW(cmdline_ptr, &argc);

	if (argc > 2 && !wcscmp(argv[1], L"/NOTIF")) {
		if ((h_cpl = LoadLibraryA("imdisk.cpl")))
			GetProcAddress(h_cpl, "ImDiskNotifyShellDriveLetter")(NULL, argv[2]);
		ExitProcess(0);
	}

	if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &registry_key, NULL) != ERROR_SUCCESS)
		RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE, &registry_key);
	GetModuleFileName(NULL, module_name, MAX_PATH);
	PathQuoteSpaces(module_name);

	if (argc > 1 && !wcscmp(argv[1], L"/SVC")) {
		StartServiceCtrlDispatcher(DispatchTable);
		ExitProcess(0);
	}

	if ((os_ver.dwMajorVersion >= 6) && (argc <= 1 || wcscmp(argv[1], L"/UAC"))) {
		// send non-elevated drive list to the elevated process
		_snwprintf(txt, _countof(txt) - 1, L"/UAC %d %s", GetLogicalDrives(), cmdline_ptr);
		ShellExecute(NULL, L"runas", argv[0], txt, NULL, SW_SHOWDEFAULT);
		ExitProcess(0);
	}

	// get registry values
	data_size = sizeof mount_point;
	RegQueryValueEx(registry_key, L"MountPoint", NULL, NULL, (void*)&mount_point, &data_size);
	data_size = sizeof mountdir;
	RegQueryValueEx(registry_key, L"MountDir", NULL, NULL, (void*)mountdir, &data_size);
	data_size = sizeof show_explorer;
	RegQueryValueEx(registry_key, L"ShowExplorer", NULL, NULL, (void*)&show_explorer, &data_size);

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &h_key) == ERROR_SUCCESS &&
		RegQueryValueExA(h_key, "Release", NULL, NULL, (void*)&net_release, &data_size) == ERROR_SUCCESS && net_release >= 528040) {
		RegCloseKey(h_key);
		net_installed = TRUE;
	}

	cmdline_ptr = L"";
	exe_path = argv[0];

	while (--argc > 0) {
		argv++;
		if (argv[0][0] == '/') {
			opt = argv[0] + 1;
			if (!wcscmp(opt, L"UAC")) {
				if (argc < 3) ExitProcess(1);
				mask0 = _wtoi(argv[1]);
				argc -= 2;
				argv += 2;
				continue;
			}
			if (!_wcsnicmp(opt, L"MP=", 3)) {
				opt += 3;
				if (*opt && opt[1] == ':' && !opt[2])
					cmdline_drive_letter = *opt & ~0x20;
				else {
					mount_point = TRUE;
					wcsncpy(mountdir, opt, _countof(mountdir) - 1);
				}
				continue;
			}
			if (!_wcsicmp(opt, L"RO")) {
				readonly = TRUE;
				continue;
			}
			if (!_wcsicmp(opt, L"REM")) {
				removable = TRUE;
				continue;
			}
			if (!_wcsicmp(opt, L"HD")) {
				cmdline_dev_type = 1;
				continue;
			}
			if (!_wcsicmp(opt, L"CD")) {
				cmdline_dev_type = 2;
				continue;
			}
			if (!_wcsicmp(opt, L"FP")) {
				cmdline_dev_type = 3;
				continue;
			}
			if (!_wcsnicmp(opt, L"P=", 2)) {
				cmdline_partition = _wtoi(opt + 2);
				cmdline_partition_exist = TRUE;
				continue;
			}
			if (!_wcsicmp(opt, L"MOUNT")) {
				cmdline_mount = TRUE;
				continue;
			}
			if (!_wcsicmp(opt, L"H") || !wcscmp(opt, L"?")) {
				MessageBoxA(NULL, "MountImg.exe file_name [/MP=mount_point] [/RO] [/REM] [/HD | /CD | /FP] [/P=partition_number] [/MOUNT]", "ImDisk", MB_ICONINFORMATION);
				ExitProcess(0);
			}
		} else
			cmdline_ptr = argv[0];
	}

	wcsncpy(filename, cmdline_ptr, _countof(filename) - 1);

	hinst = GetModuleHandle(NULL);
	if (!(h_cpl = LoadLibraryA("imdisk.cpl")))
		MessageBoxA(NULL, "Warning: cannot find imdisk.cpl.", "ImDisk", MB_ICONWARNING);
	ImDisk_GetDeviceListEx = GetProcAddress(h_cpl, "ImDiskGetDeviceListEx");
	ImDisk_ForceRemoveDevice = GetProcAddress(h_cpl, "ImDiskForceRemoveDevice");
	ImDisk_RemoveRegistrySettings = GetProcAddress(h_cpl, "ImDiskRemoveRegistrySettings");
	ImDisk_NotifyShellDriveLetter = GetProcAddress(h_cpl, "ImDiskNotifyShellDriveLetter");

	// create service
	h_scman = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
	h_svc = OpenService(h_scman, L"ImDiskImg", SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP | DELETE);
	wcscat(module_name, L" /SVC");
	if (h_svc)
		ChangeServiceConfig(h_svc, SERVICE_WIN32_OWN_PROCESS, SERVICE_NO_CHANGE, SERVICE_ERROR_NORMAL, module_name, NULL, NULL, L"ImDisk\0", NULL, NULL, NULL);
	else {
		h_svc = CreateService(h_scman, L"ImDiskImg", L"ImDisk Image File mounter", SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_STOP | DELETE, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
							  module_name, NULL, NULL, L"ImDisk\0", NULL, NULL);
		if (h_svc) {
			svc_description.lpDescription = L"Mounts image files at system startup.";
			ChangeServiceConfig2(h_svc, SERVICE_CONFIG_DESCRIPTION, &svc_description);
		} else
			MessageBox(NULL, t[ERR_6], L"ImDisk", MB_ICONERROR);
	}

	mount_mutex = CreateMutex(NULL, FALSE, NULL);
	PathRemoveFileSpec(exe_path);
	SetCurrentDirectory(exe_path);
	load_lang();
	if (DialogBox(hinst, L"MOUNT_DLG", NULL, DlgProc) == 1) {
		// workaround: the window of the driver GUI sometimes disappears under other windows
		for (i = 0; i < 100; i++) {
			Sleep(50);
			if ((hwnd = FindWindow(NULL, L"Mount new virtual disk")) || (hwnd = FindWindow(NULL, L"Select partition in disk image"))) {
				SetForegroundWindow(hwnd);
				break;
			}
		}
	}

	// remove service if not required
	max = get_reg_max();
	for (i = 0; i <= max; i++) {
		param_name_ptr = param_name + _snwprintf(param_name, _countof(param_name), L"Im%d", i);
		wcscpy(param_name_ptr, L"FileName");
		if (RegQueryValueEx(registry_key, param_name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
			wcscpy(param_name_ptr, L"Device");
			if (RegQueryValueEx(registry_key, param_name, NULL, NULL, NULL, NULL) == ERROR_FILE_NOT_FOUND) {
				svc_required = TRUE;
				break;
			}
		}
	}
	if (!svc_required) DeleteService(h_svc);
	CloseServiceHandle(h_svc);
	CloseServiceHandle(h_scman);

	RegCloseKey(registry_key);
	WaitForSingleObject(mount_mutex, 10000);
	ExitProcess(0);
}
