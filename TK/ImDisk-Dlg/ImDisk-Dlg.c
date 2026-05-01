#define OEMRESOURCE
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <ntsecapi.h>
#include <dbt.h>
#include "resource.h"
#include "..\inc\imdisk.h"
#include "..\inc\imdisktk.h"

#define DEF_BUFFER_SIZE (1 << 20)

static HINSTANCE hinst;
static HICON hIcon;
static HWND hwnd_status, save_hdlg;
static HKEY registry_key;
static HMODULE h_cpl;
static RECT icon_coord = {};
static DWORD flag_to_set = 0, flags = 0;
static BOOL no_reg_write = FALSE;
static volatile BOOL stop_lock = FALSE;
static LONGLONG img_offset, offset_no_mbr;
static BOOL write_mbr, cdrom = FALSE;
static struct {IMDISK_CREATE_DATA icd; WCHAR buff[MAX_PATH + 15];} create_data = {};
static WCHAR *mount_point, *file_name = create_data.icd.FileName;
static volatile unsigned int percentage_done = 0;
static HANDLE h;


enum {
	TXT_1, TXT_2, TXT_3,
	RM_CTRL_1, RM_CTRL_2, RM_CTRL_3, RM_CTRL_4, RM_CTRL_5, RM_CTRL_6,
	MSG_1, MSG_2, MSG_3, MSG_4, MSG_5, MSG_6, MSG_7, MSG_8,
	RM_ERR_1, RM_ERR_2, RM_ERR_3, RM_ERR_4,
	CP_CTRL_1, CP_CTRL_2, CP_CTRL_3, CP_CTRL_4, CP_CTRL_5, CP_CTRL_6,
	F_TYPE_1, F_TYPE_2, F_TYPE_3,
	CP_MSG_1, CP_MSG_2, CP_MSG_3, CP_MSG_4,
	CP_ERR_1, CP_ERR_2, CP_ERR_3, CP_ERR_4, CP_ERR_5, CP_ERR_6, CP_ERR_7, CP_ERR_8, CP_ERR_9,
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

	if ((h = CreateFile(L"lang.txt", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) return;
	GetFileSizeEx(h, &size);
	if (size.QuadPart > 1 << 17) {
		CloseHandle(h);
		return;
	}
	buf = VirtualAlloc(NULL, size.QuadPart + sizeof(WCHAR), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	ReadFile(h, buf, size.LowPart, &dw, NULL);
	CloseHandle(h);
	if (!(current_str = wcsstr(buf, L"[ImDisk-Dlg]"))) return;
	wcstok(current_str, L"\r\n", &context);
	for (i = 0; i < NB_TXT; i++)
		t[i] = wcstok(NULL, L"\r\n", &context);
	size.LowPart /= sizeof(WCHAR);
	for (i = 0; i < size.LowPart; i++)
		if (buf[i] == '#') buf[i] = '\n';
}


static void error(HWND hDlg, WCHAR *txt)
{
	WCHAR *ptr, buf[512];

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK, NULL, GetLastError(), 0, (LPTSTR)&ptr, 0, NULL);
	_snwprintf(buf, _countof(buf), L"%.200s\n\n(%.300s)", txt, ptr);
	MessageBox(hDlg, buf, L"ImDisk", MB_ICONERROR);
}


static INT_PTR __stdcall StatusProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			hwnd_status = GetDlgItem(hDlg, ID_TEXT1);
			return TRUE;

		default:
			return FALSE;
	}
}

static void draw_icon(HWND hDlg)
{
	PAINTSTRUCT paint;

	DrawIcon(BeginPaint(hDlg, &paint), icon_coord.left, icon_coord.top, hIcon);
	EndPaint(hDlg, &paint);
}

static INT_PTR __stdcall WarnProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hinst, MAKEINTRESOURCE(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TEXT1, t[flag_to_set == FLAG_ACCESS ? TXT_1 : TXT_2]);
				SetDlgItemText(hDlg, ID_CHECK1, t[RM_CTRL_1]);
				SetDlgItemText(hDlg, IDOK, t[RM_CTRL_3]);
				SetDlgItemText(hDlg, IDCANCEL, t[RM_CTRL_4]);
			} else
				SetDlgItemTextA(hDlg, ID_TEXT1, flag_to_set == FLAG_ACCESS ?
								"Warning: you don't have enough rights to properly dismount this volume.\nThe write buffers might not be flushed and therefore there is a risk of data loss.\n\nDo you still want to continue?" :
								"The volume is in use by another process and cannot be locked.\n\nIf all your data are saved, you can safely continue and the volume will be properly dismounted.\n\nDo you want to continue?");

			ShowWindow(GetDlgItem(hDlg, ID_CHECK1), !no_reg_write);

			hIcon = LoadImage(NULL, MAKEINTRESOURCE(OIC_WARNING), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			icon_coord.left = 14;
			icon_coord.top = 18;
			MapDialogRect(hDlg, &icon_coord);
			SetFocus(GetDlgItem(hDlg, IDCANCEL));
			MessageBeep(MB_ICONWARNING);
			return FALSE;

		case WM_PAINT:
			draw_icon(hDlg);
			return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK) {
				if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
					flags |= flag_to_set;
					RegSetValueExA(registry_key, "DlgFlags", 0, REG_DWORD, (void*)&flags, sizeof(DWORD));
				}
				EndDialog(hDlg, 0);
			}
			if (LOWORD(wParam) == IDCANCEL)
				EndDialog(hDlg, 1);
			return TRUE;

		default:
			return FALSE;
	}
}

static INT_PTR __stdcall ImgModProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hinst, MAKEINTRESOURCE(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TEXT1, t[TXT_3]);
				SetDlgItemText(hDlg, ID_CHECK1, t[RM_CTRL_2]);
				SetDlgItemText(hDlg, IDCANCEL, t[RM_CTRL_4]);
				SetDlgItemText(hDlg, IDYES, t[RM_CTRL_5]);
				SetDlgItemText(hDlg, IDNO, t[RM_CTRL_6]);
			} else
				SetDlgItemTextA(hDlg, ID_TEXT1, "The virtual disk has been modified.\n\nDo you want to save it as an image file before removing it?");

			ShowWindow(GetDlgItem(hDlg, ID_CHECK1), !no_reg_write);

			hIcon = LoadImage(NULL, MAKEINTRESOURCE(OIC_NOTE), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
			icon_coord.left = 14;
			icon_coord.top = 17;
			MapDialogRect(hDlg, &icon_coord);
			SetFocus(GetDlgItem(hDlg, IDYES));
			return FALSE;

		case WM_PAINT:
			draw_icon(hDlg);
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDYES: case IDNO:
					if (IsDlgButtonChecked(hDlg, ID_CHECK1)) {
						flags |= FLAG_IMG_MOD;
						RegSetValueExA(registry_key, "DlgFlags", 0, REG_DWORD, (void*)&flags, sizeof(DWORD));
					}
				case IDCANCEL:
					EndDialog(hDlg, LOWORD(wParam));
			}
			return TRUE;

		default:
			return FALSE;
	}
}


static DWORD __stdcall write_img(LPVOID lpParam)
{
	HANDLE h_file;
	LARGE_INTEGER file_size;
	DWORD data_size, current_size, partial_sector, new_file = FALSE;
	LONGLONG init_size, vol_size = create_data.icd.DiskGeometry.Cylinders.QuadPart;
	void *buf;
	PARTITION_INFORMATION partition_info;
	IMDISK_SET_DEVICE_FLAGS device_flags;
	WCHAR text[300];

	if ((h_file = CreateFile(file_name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
		error(save_hdlg, t[CP_ERR_3] ? t[CP_ERR_3] : L"Cannot open image file.");
		goto err_save;
	}
	new_file = !GetLastError();
	if (!(buf = VirtualAlloc(NULL, DEF_BUFFER_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) {
		error(save_hdlg, t[CP_ERR_4] ? t[CP_ERR_4] : L"Cannot allocate memory.");
		goto err_save;
	}

	if (write_mbr) {
		partition_info.StartingOffset.QuadPart = img_offset;
		partition_info.PartitionLength.QuadPart = vol_size;
		partition_info.PartitionType = 0x06;
		partition_info.BootIndicator = TRUE;
		if (!GetProcAddress(h_cpl, "ImDiskBuildMBR")(&create_data.icd.DiskGeometry, &partition_info, 1, buf, 512)) {
			error(save_hdlg, t[CP_ERR_5] ? t[CP_ERR_5] : L"Error creating MBR.");
			goto err_save;
		}
		if (!(WriteFile(h_file, buf, 512, &data_size, NULL) && data_size == 512)) {
			error(save_hdlg, t[CP_ERR_6] ? t[CP_ERR_6] : L"Error writing MBR.");
			goto err_save;
		}
	}

	if (!SetFilePointerEx(h_file, (LARGE_INTEGER)img_offset, NULL, FILE_BEGIN)) {
		error(save_hdlg, t[CP_ERR_7] ? t[CP_ERR_7] : L"Error setting file pointer.");
		goto err_save;
	}

	DeviceIoControl(h, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &data_size, NULL);
	init_size = vol_size;

	while (vol_size > 0) {
		current_size = min(vol_size, DEF_BUFFER_SIZE);
		partial_sector = current_size % create_data.icd.DiskGeometry.BytesPerSector;
		current_size -= partial_sector;
		if (!(ReadFile(h, buf, current_size, &data_size, NULL) && data_size == current_size)) {
			error(save_hdlg, t[CP_ERR_8] ? t[CP_ERR_8] : L"Read error.");
			goto err_save;
		}
		ZeroMemory(buf + current_size, partial_sector);
		current_size += partial_sector;
		if (!(WriteFile(h_file, buf, current_size, &data_size, NULL) && data_size == current_size)) {
			error(save_hdlg, t[CP_ERR_9] ? t[CP_ERR_9] : L"Write error.");
			goto err_save;
		}
		vol_size -= current_size;
		percentage_done = (init_size - vol_size) * 100 / init_size;
	}

	CloseHandle(h_file);
	device_flags.FlagsToChange = IMDISK_IMAGE_MODIFIED;
	device_flags.FlagValues = 0;
	DeviceIoControl(h, IOCTL_IMDISK_SET_DEVICE_FLAGS, &device_flags, sizeof device_flags, NULL, 0, &data_size, NULL);

	_snwprintf(text, _countof(text) - 1, t[CP_MSG_4] ? t[CP_MSG_4] : L"Volume %s successfully saved into %s.", mount_point, file_name);
	text[_countof(text) - 1] = 0;
	MessageBox(save_hdlg, text, L"ImDisk", MB_ICONINFORMATION);
	EndDialog(save_hdlg, 0);
	return 0;

err_save:
	if (new_file && GetFileSizeEx(h_file, &file_size) && !file_size.QuadPart) DeleteFile(file_name);
	CloseHandle(h_file);
	EndDialog(save_hdlg, 1);
	return 0;
}

static DWORD __stdcall lock_attempt(LPVOID lpParam)
{
	DWORD data_size;
	WCHAR text[300];

	_snwprintf(text, _countof(text) - 1, t[CP_MSG_2] ? t[CP_MSG_2] : L"%s is in use by another process and cannot be locked.\nTherefore, it can be modified while copying.", mount_point);
	text[_countof(text) - 1] = 0;
	SetDlgItemText(save_hdlg, ID_TEXT3, text);
	_snwprintf(text, _countof(text) - 1, t[CP_MSG_1] ? t[CP_MSG_1] : L"%s locked and ready.", mount_point);

	do {
		if (DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &data_size, NULL)) {
			DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &data_size, NULL);
			SetDlgItemText(save_hdlg, ID_TEXT3, text);
			break;
		}
		Sleep(500);
	} while (!stop_lock);

	return 0;
}

static INT_PTR __stdcall SaveProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	OPENFILENAME ofn;
	WCHAR text[300];
	int i;

	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadImage(hinst, MAKEINTRESOURCE(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

			// set localized strings
			if (t[0]) {
				SetDlgItemText(hDlg, ID_TITLE, t[CP_CTRL_1]);
				SetDlgItemText(hDlg, ID_TEXT1, t[CP_CTRL_2]);
				SetDlgItemText(hDlg, ID_TEXT2, t[CP_CTRL_3]);
				SetDlgItemText(hDlg, ID_CHECK1, t[CP_CTRL_4]);
				SetDlgItemText(hDlg, IDOK, t[CP_CTRL_5]);
				SetDlgItemText(hDlg, IDCANCEL, t[CP_CTRL_6]);
			}

			save_hdlg = hDlg;
			CreateThread(NULL, 0, lock_attempt, NULL, 0, NULL);

			SendMessage(GetDlgItem(hDlg, ID_EDIT1), EM_SETLIMITTEXT, MAX_PATH - 1, 0);
			if (IMDISK_TYPE(create_data.icd.Flags) != IMDISK_TYPE_VM) create_data.icd.FileNameLength = 0;
			create_data.icd.FileName[create_data.icd.FileNameLength / sizeof(WCHAR)] = 0;
			GetProcAddress(h_cpl, "ImDiskNativePathToWin32")(&file_name);
			SetDlgItemText(hDlg, ID_EDIT1, file_name);

			_i64tow(img_offset = create_data.icd.ImageOffset.QuadPart, text, 10);
			SetDlgItemText(hDlg, ID_EDIT2, text);
			EnableWindow(GetDlgItem(hDlg, ID_CHECK1), !cdrom);

			return TRUE;

		case WM_TIMER:
			_snwprintf(text, _countof(text) - 1, t[CP_MSG_3] ? t[CP_MSG_3] : L"Save of %s in progress... %u%%", mount_point, percentage_done);
			text[_countof(text) - 1] = 0;
			SetDlgItemText(hDlg, ID_TEXT3, text);
			return TRUE;

		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case ID_EDIT1:
					if (HIWORD(wParam) == EN_CHANGE) {
						GetDlgItemText(hDlg, ID_EDIT1, file_name, MAX_PATH);
						EnableWindow(GetDlgItem(hDlg, IDOK), file_name[0]);
					}
					break;

				case ID_PBUTTON1:
					ZeroMemory(&ofn, sizeof ofn);
					ofn.lStructSize = sizeof ofn;
					ofn.hwndOwner = hDlg;
					ofn.lpstrFile = file_name;
					ofn.nMaxFile = MAX_PATH;
					ofn.Flags = OFN_OVERWRITEPROMPT;
					_snwprintf(text, _countof(text) - 1, L"%s (*.img)\1*.img\1%s (*.*)\1*.*\1", t[F_TYPE_1] ? t[F_TYPE_1] : L"Image Files", t[F_TYPE_3] ? t[F_TYPE_3] : L"All Files");
					text[_countof(text) - 1] = 0;
					ofn.lpstrDefExt = L"img";
					if (cdrom) {
						_snwprintf(text, _countof(text) - 1, L"%s (*.iso)\1*.iso\1%s (*.*)\1*.*\1", t[F_TYPE_2] ? t[F_TYPE_2] : L"ISO Images", t[F_TYPE_3] ? t[F_TYPE_3] : L"All Files");
						text[_countof(text) - 1] = 0;
						ofn.lpstrDefExt = L"iso";
					}
					for (i = wcslen(text); i >= 0; i--) if (text[i] == 1) text[i] = 0;
					ofn.lpstrFilter = text;
					GetSaveFileName(&ofn);
					SetDlgItemText(hDlg, ID_EDIT1, file_name);
					break;

				case ID_CHECK1:
					if ((write_mbr = IsDlgButtonChecked(hDlg, ID_CHECK1))) {
						offset_no_mbr = img_offset;
						img_offset = (LONGLONG)create_data.icd.DiskGeometry.BytesPerSector * create_data.icd.DiskGeometry.SectorsPerTrack;
					} else
						img_offset = offset_no_mbr;
					_i64tow(img_offset, text, 10);
					SetDlgItemText(hDlg, ID_EDIT2, text);
					EnableWindow(GetDlgItem(hDlg, ID_TEXT2), !write_mbr);
					EnableWindow(GetDlgItem(hDlg, ID_EDIT2), !write_mbr);
					break;

				case IDOK:
					stop_lock = TRUE;
					GetDlgItemText(hDlg, ID_EDIT2, text, _countof(text));
					img_offset = _wtoi64(text);
					EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
					EnableWindow(GetDlgItem(hDlg, ID_EDIT1), FALSE);
					EnableWindow(GetDlgItem(hDlg, ID_PBUTTON1), FALSE);
					EnableWindow(GetDlgItem(hDlg, ID_EDIT2), FALSE);
					EnableWindow(GetDlgItem(hDlg, ID_CHECK1), FALSE);
					SetTimer(hDlg, 1, 200, NULL);
					CreateThread(NULL, 0, write_img, NULL, 0, NULL);
					break;

				case IDCANCEL:
					EndDialog(hDlg, 1);
			}

			return TRUE;

		default:
			return FALSE;
	}
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	int argc;
	WCHAR **argv, *command_line;
	DWORD dw, access_list[] = {GENERIC_READ | GENERIC_WRITE, GENERIC_READ, FILE_READ_ATTRIBUTES, 0};
	DWORD_PTR dwp;
	DEV_BROADCAST_VOLUME dbv = {sizeof(DEV_BROADCAST_VOLUME), DBT_DEVTYP_VOLUME};
	int n_access;
	_Bool is_ramdisk;
	FARPROC ImDiskOpenDeviceByMountPoint;
	WCHAR txt[MAX_PATH + 1];

	command_line = GetCommandLine();
	argv = CommandLineToArgvW(command_line, &argc);
	if (argc < 3 || !argv[2][0]) ExitProcess(1);

	if (!(h_cpl = LoadLibraryA("imdisk.cpl"))) {
		error(NULL, L"Error: cannot find imdisk.cpl. Please reinstall the driver.");
		ExitProcess(1);
	}
	ImDiskOpenDeviceByMountPoint = GetProcAddress(h_cpl, "ImDiskOpenDeviceByMountPoint");

	hinst = GetModuleHandle(NULL);
	PathRemoveFileSpec(argv[0]);
	SetCurrentDirectory(argv[0]);
	load_lang();
	mount_point = argv[2];
	if (!wcscmp(&mount_point[1], L":\\")) mount_point[2] = 0;

	if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS | KEY_WOW64_64KEY, NULL, &registry_key, NULL) != ERROR_SUCCESS) {
		RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_WOW64_64KEY, NULL, &registry_key, NULL);
		no_reg_write = TRUE;
	}
	dw = sizeof(DWORD);
	RegQueryValueExA(registry_key, "DlgFlags", NULL, NULL, (void*)&flags, &dw);


	if (!wcscmp(argv[1], L"RM")) {
		CreateDialog(hinst, L"STATUS_DLG", NULL, StatusProc);
		SetWindowText(hwnd_status, t[MSG_1]);
		for (n_access = 0; n_access < _countof(access_list); n_access++)
			if ((h = (HANDLE)ImDiskOpenDeviceByMountPoint(mount_point, access_list[n_access])) != INVALID_HANDLE_VALUE) break;
		if (h == INVALID_HANDLE_VALUE) {
			error(hwnd_status, t[RM_ERR_1] ? t[RM_ERR_1] : L"Cannot open device.");
			ExitProcess(1);
		}
		if (!DeviceIoControl(h, IOCTL_IMDISK_QUERY_DEVICE, NULL, 0, &create_data, sizeof create_data, &dw, NULL)) {
			_snwprintf(txt, _countof(txt) - 1, t[RM_ERR_2] ? t[RM_ERR_2] : L"%s is not an ImDisk virtual disk.", mount_point);
			txt[_countof(txt) - 1] = 0;
			error(hwnd_status, txt);
			ExitProcess(1);
		}
		is_ramdisk = IMDISK_IS_MEMORY_DRIVE(create_data.icd.Flags) || !wcsncmp(file_name, L"\\BaseNamedObjects\\Global\\RamDyn", 31);
		if (n_access >= 2) {
			flag_to_set = FLAG_ACCESS;
			if (!(flags & FLAG_ACCESS) && !IMDISK_READONLY(create_data.icd.Flags) && !is_ramdisk)
				if (DialogBox(hinst, L"WARN_DLG", NULL, WarnProc)) ExitProcess(0);
		}

		SetWindowText(hwnd_status, t[MSG_2]);
		if (mount_point[1] == ':' && !mount_point[2]) {
			dbv.dbcv_unitmask = 1 << (mount_point[0] - 'A');
			SendMessageTimeout(HWND_BROADCAST, WM_DEVICECHANGE, DBT_DEVICEREMOVEPENDING, (LPARAM)&dbv, SMTO_BLOCK | SMTO_ABORTIFHUNG, 4000, &dwp);
		}

		SetWindowText(hwnd_status, t[MSG_3]);
		FlushFileBuffers(h);

		SetWindowText(hwnd_status, t[MSG_4]);
		if (!DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dw, NULL) && !(flags & FLAG_LOCK) && !flag_to_set) {
			flag_to_set = FLAG_LOCK;
			if (DialogBox(hinst, L"WARN_DLG", NULL, WarnProc)) ExitProcess(0);
		}

		SetWindowText(hwnd_status, t[MSG_5]);
		DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dw, NULL);

		DeviceIoControl(h, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dw, NULL);
		if (!(flags & FLAG_IMG_MOD) && is_ramdisk && create_data.icd.Flags & IMDISK_IMAGE_MODIFIED)
			switch (DialogBox(hinst, L"IMGMOD_DLG", NULL, ImgModProc)) {
				case IDCANCEL:
					ExitProcess(0);
				case IDYES:
					cdrom = IMDISK_DEVICE_TYPE(create_data.icd.Flags) == IMDISK_DEVICE_TYPE_CD;
					SetWindowText(hwnd_status, t[MSG_6]);
					if (DialogBox(hinst, L"SAVE_DLG", NULL, SaveProc)) ExitProcess(0);
			}

		SetWindowText(hwnd_status, t[MSG_7]);
		if (!DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &dw, NULL) && !GetProcAddress(h_cpl, "ImDiskForceRemoveDevice")(h, 0)) {
			error(hwnd_status, t[RM_ERR_3] ? t[RM_ERR_3] : L"Cannot remove device.");
			ExitProcess(1);
		}
		CloseHandle(h);

		SetWindowText(hwnd_status, t[MSG_8]);
		if (!GetProcAddress(h_cpl, "ImDiskRemoveMountPoint")(mount_point)) {
			error(hwnd_status, t[RM_ERR_4] ? t[RM_ERR_4] : L"Cannot remove mount point.");
			ExitProcess(1);
		}
	}


	if (!wcscmp(argv[1], L"CP")) {
		_snwprintf(txt, _countof(txt) - 1, L"%s\\", mount_point);
		txt[_countof(txt) - 1] = 0;
		switch (GetDriveType(txt)) {
			case DRIVE_REMOTE:
				error(NULL, t[CP_ERR_1] ? t[CP_ERR_1] : L"Unsupported drive type.");
				ExitProcess(1);
			case DRIVE_CDROM:
				cdrom = TRUE;
		}
		if ((h = (HANDLE)ImDiskOpenDeviceByMountPoint(mount_point, GENERIC_READ)) == INVALID_HANDLE_VALUE) {
			error(NULL, t[RM_ERR_1] ? t[RM_ERR_1] : L"Cannot open device.");
			ExitProcess(1);
		}
		if (!DeviceIoControl(h, IOCTL_IMDISK_QUERY_DEVICE, NULL, 0, &create_data, sizeof create_data, &dw, NULL))
			if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY, NULL, 0, &create_data.icd.DiskGeometry, sizeof(DISK_GEOMETRY), &dw, NULL) ||
				!GetProcAddress(h_cpl, "ImDiskGetVolumeSize")(h, &create_data.icd.DiskGeometry.Cylinders.QuadPart)) {
				error(NULL, t[CP_ERR_2] ? t[CP_ERR_2] : L"Error retrieving volume properties.");
				ExitProcess(1);
			}
		DialogBox(hinst, L"SAVE_DLG", NULL, SaveProc);
	}

	ExitProcess(0);
}
