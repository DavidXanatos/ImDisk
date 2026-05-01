#define SERVICE_CONTROL_PRESHUTDOWN 0x0000000F
#define SERVICE_ACCEPT_PRESHUTDOWN 0x00000100
#include <windows.h>
#include <wtsapi32.h>
#include <winternl.h>
#include <ntstatus.h>
#include <intrin.h>
#include "..\inc\imdisk.h"

#define DEF_BUFFER_SIZE (1 << 20)

static HANDLE stop_event = NULL;
static SERVICE_STATUS_HANDLE SvcStatusHandle;
static SERVICE_STATUS SvcStatus = {SERVICE_WIN32_OWN_PROCESS, SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN, NO_ERROR, 0, 0, 3600000};

static WCHAR *path_source, *path_dest, *base_source, *base_dest;
static WCHAR *excluded_list[(500 + 1) / 3 + 1] = {};
static DWORD flags;
static HANDLE h_source, h_dest;
static void *buf;


static void copy_file(BOOL is_directory)
{
	DWORD bytes_read, bytes_written, attrib;
	void *read_context = NULL, *write_context = NULL;
	FILE_BASIC_INFORMATION fbi;
	IO_STATUS_BLOCK iosb;
	_Bool attrib_ok;

	if ((h_source = CreateFile(path_source, GENERIC_READ | ACCESS_SYSTEM_SECURITY, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL)) != INVALID_HANDLE_VALUE) {
		attrib = GetFileAttributes(path_dest);
		if (attrib & FILE_ATTRIBUTE_READONLY) SetFileAttributes(path_dest, FILE_ATTRIBUTE_NORMAL);
		if ((h_dest = CreateFile(path_dest, GENERIC_WRITE | WRITE_OWNER | WRITE_DAC | ACCESS_SYSTEM_SECURITY, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, is_directory ? OPEN_EXISTING : CREATE_ALWAYS,
			FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL)) != INVALID_HANDLE_VALUE) {
			attrib_ok = NtQueryInformationFile(h_source, &iosb, &fbi, sizeof fbi, FileBasicInformation) == STATUS_SUCCESS;
			do {
				if (!BackupRead(h_source, buf, DEF_BUFFER_SIZE, &bytes_read, FALSE, TRUE, &read_context) || !bytes_read) break;
			} while (BackupWrite(h_dest, buf, bytes_read, &bytes_written, FALSE, TRUE, &write_context));
			BackupRead(h_source, NULL, 0, NULL, TRUE, FALSE, &read_context);
			BackupWrite(h_dest, NULL, 0, NULL, TRUE, FALSE, &read_context);
			if (attrib_ok) NtSetInformationFile(h_dest, &iosb, &fbi, sizeof fbi, FileBasicInformation);
			CloseHandle(h_dest);
			SvcStatus.dwCheckPoint++;
			SetServiceStatus(SvcStatusHandle, &SvcStatus);
		}
		else if (attrib != INVALID_FILE_ATTRIBUTES && attrib & FILE_ATTRIBUTE_READONLY) SetFileAttributes(path_dest, attrib);
		CloseHandle(h_source);
	}
}

static void scan_dir_copy(int index_source, int index_dest)
{
	WIN32_FIND_DATA entry;
	HANDLE fff;
	DWORD attrib;
	int name_size, i;

	*(ULONG*)(path_source + index_source) = '*';
	if ((fff = FindFirstFile(path_source, &entry)) == INVALID_HANDLE_VALUE) return;
	do {
		name_size = lstrlenW(entry.cFileName) + 1;
		if (index_source + name_size > 32765 || index_dest + name_size > 32765) continue;
		memcpy(&path_source[index_source], entry.cFileName, name_size * sizeof(WCHAR));
		memcpy(&path_dest[index_dest], entry.cFileName, name_size * sizeof(WCHAR));
		if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (entry.cFileName[0] == '.' && (!entry.cFileName[1] || (entry.cFileName[1] == '.' && !entry.cFileName[2]))) continue;
			i = 0;
			while (excluded_list[i] && lstrcmpiW(base_source, excluded_list[i])) i++;
			if (excluded_list[i]) continue;
			switch (attrib = GetFileAttributes(path_dest)) {
				default:
					if (attrib & FILE_ATTRIBUTE_DIRECTORY) {
						if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
							RemoveDirectory(path_dest);
							CreateDirectory(path_dest, NULL);
							copy_file(TRUE);
						}
						break;
					}
					if (attrib & FILE_ATTRIBUTE_READONLY) SetFileAttributes(path_dest, FILE_ATTRIBUTE_NORMAL);
					DeleteFile(path_dest);
				case INVALID_FILE_ATTRIBUTES:
					CreateDirectory(path_dest, NULL);
					copy_file(TRUE);
			}
			if (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
			*(ULONG*)(path_source + index_source + name_size - 1) = '\\';
			*(ULONG*)(path_dest + index_dest + name_size - 1) = '\\';
			scan_dir_copy(index_source + name_size, index_dest + name_size);
		}
		else if (!(flags & 2) || entry.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE)
			copy_file(FALSE);
	} while (FindNextFile(fff, &entry));
	FindClose(fff);
}

static void scan_dir_delete(int index_source, int index_dest, BOOL del_recurs)
{
	WIN32_FIND_DATA entry;
	HANDLE fff;
	DWORD source_attrib;
	_Bool source_exist;
	int name_size, i;

	*(ULONG*)(path_dest + index_dest) = '*';
	if ((fff = FindFirstFile(path_dest, &entry)) == INVALID_HANDLE_VALUE) return;
	do {
		name_size = lstrlenW(entry.cFileName) + 1;
		if (index_source + name_size > 32765 || index_dest + name_size > 32765) continue;
		memcpy(&path_source[index_source], entry.cFileName, name_size * sizeof(WCHAR));
		memcpy(&path_dest[index_dest], entry.cFileName, name_size * sizeof(WCHAR));
		source_attrib = GetFileAttributes(path_source);
		source_exist = source_attrib != INVALID_FILE_ATTRIBUTES || (GetLastError() | 1) != ERROR_PATH_NOT_FOUND;
		if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (entry.cFileName[0] == '.' && (!entry.cFileName[1] || (entry.cFileName[1] == '.' && !entry.cFileName[2]))) continue;
			i = 0;
			while (excluded_list[i] && lstrcmpiW(base_dest, excluded_list[i])) i++;
			if (excluded_list[i]) continue;
			if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
				*(ULONG*)(path_source + index_source + name_size - 1) = '\\';
				*(ULONG*)(path_dest + index_dest + name_size - 1) = '\\';
				scan_dir_delete(index_source + name_size, index_dest + name_size, del_recurs || (source_exist && source_attrib & FILE_ATTRIBUTE_REPARSE_POINT));
			}
			if (del_recurs || !source_exist || !(source_attrib & FILE_ATTRIBUTE_DIRECTORY) || (!(source_attrib & FILE_ATTRIBUTE_REPARSE_POINT) && entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
				path_dest[index_dest + name_size - 1] = 0;
				if (entry.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributes(path_dest, FILE_ATTRIBUTE_NORMAL);
				RemoveDirectory(path_dest);
			}
		}
		else if (del_recurs || !source_exist) {
			if (entry.dwFileAttributes & FILE_ATTRIBUTE_READONLY) SetFileAttributes(path_dest, FILE_ATTRIBUTE_NORMAL);
			DeleteFile(path_dest);
		}
	} while (FindNextFile(fff, &entry));
	FindClose(fff);
}


static DWORD __stdcall save_ramdisk(LPVOID lpParam)
{
	HMODULE h_cpl;
	FARPROC ImDiskSetAPIFlags, ImDiskCreateDevice, ImDiskRemoveDevice;
	DISK_GEOMETRY dg = {};
	HKEY reg_key;
	DWORD wanted_drive = 'R', drive_mask, data_size, attrib;
	DWORD reg_volume_id, volume_id;
	_Bool dest_is_dir;
	WCHAR *key_name_ptr, key_name[16], temp_letter[4];
	WCHAR mount_point[MAX_PATH + 1], image_file[MAX_PATH + 1], *sync_excluded;
	HANDLE token = INVALID_HANDLE_VALUE;
	struct {DWORD PrivilegeCount; LUID_AND_ATTRIBUTES Privileges[2];} tok_priv;
	int i, j;

	tok_priv.PrivilegeCount = 2;
	tok_priv.Privileges[0].Attributes = tok_priv.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;
	OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token);
	LookupPrivilegeValueA(NULL, "SeBackupPrivilege", &tok_priv.Privileges[0].Luid);
	LookupPrivilegeValueA(NULL, "SeRestorePrivilege", &tok_priv.Privileges[1].Luid);
	AdjustTokenPrivileges(token, FALSE, (TOKEN_PRIVILEGES*)&tok_priv, 0, NULL, NULL);
	CloseHandle(token);

	h_cpl = LoadLibraryA("imdisk.cpl");
	ImDiskSetAPIFlags = GetProcAddress(h_cpl, "ImDiskSetAPIFlags");
	ImDiskCreateDevice = GetProcAddress(h_cpl, "ImDiskCreateDevice");
	ImDiskRemoveDevice = GetProcAddress(h_cpl, "ImDiskRemoveDevice");

	if (!(buf = VirtualAlloc(NULL, DEF_BUFFER_SIZE + 2 * 32768 * sizeof(WCHAR) + 501 * sizeof(WCHAR), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE))) goto stop;
	path_source = buf + DEF_BUFFER_SIZE;
	path_dest = path_source + 32768;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ImDisk", 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &reg_key) != ERROR_SUCCESS) goto stop;
	data_size = sizeof wanted_drive;
	RegQueryValueEx(reg_key, L"WantedDrive", NULL, NULL, (void*)&wanted_drive, &data_size);

	key_name[1] = '_';
	for (key_name[0] = '0'; key_name[0] <= 'Z'; key_name[0] == '9' ? key_name[0] = 'A' : key_name[0]++) {
		key_name_ptr = key_name[0] == wanted_drive ? &key_name[2] : key_name;
		lstrcpyW(&key_name[2], L"SyncFlags");
		data_size = sizeof flags;
		if (!(RegQueryValueEx(reg_key, key_name_ptr, NULL, NULL, (void*)&flags, &data_size) == ERROR_SUCCESS && flags & 1)) continue;
		lstrcpyW(&key_name[2], L"ImageFile");
		data_size = sizeof image_file;
		if (!(RegQueryValueEx(reg_key, key_name_ptr, NULL, NULL, (void*)&image_file, &data_size) == ERROR_SUCCESS && image_file[0])) continue;
		image_file[_countof(image_file) - 2] = 0;
		lstrcpyW(&key_name[2], L"VolumeID");
		data_size = sizeof reg_volume_id;
		if (RegQueryValueEx(reg_key, key_name_ptr, NULL, NULL, (void*)&reg_volume_id, &data_size) != ERROR_SUCCESS) continue;

		// source
		if (key_name[0] <= '9') {
			lstrcpyW(&key_name[2], L"RDMountPoint");
			mount_point[0] = 0;
			data_size = sizeof mount_point;
			RegQueryValueEx(reg_key, key_name, NULL, NULL, (void*)mount_point, &data_size);
			if (!mount_point[0]) continue;
			mount_point[_countof(mount_point) - 2] = 0;
		} else {
			mount_point[0] = key_name[0];
			mount_point[1] = ':';
			mount_point[2] = 0;
		}
		if (mount_point[lstrlenW(mount_point) - 1] != '\\') lstrcatW(mount_point, L"\\");
		if (!GetVolumeInformation(mount_point, NULL, 0, &volume_id, NULL, NULL, NULL, 0) || volume_id != reg_volume_id) continue;

		// destination
		attrib = GetFileAttributes(image_file);
		if ((dest_is_dir = attrib != INVALID_FILE_ATTRIBUTES && attrib & FILE_ATTRIBUTE_DIRECTORY)) {
			if (image_file[lstrlenW(image_file) - 1] != '\\') lstrcatW(image_file, L"\\");
		} else {
			if (!(drive_mask = 0x3ffffff ^ GetLogicalDrives())) continue;
			temp_letter[0] = _bit_scan_reverse(drive_mask) + 'A';
			temp_letter[1] = ':';
			temp_letter[2] = 0;
			if (!ImDiskSetAPIFlags || !ImDiskCreateDevice || !ImDiskRemoveDevice) continue;
			ImDiskSetAPIFlags((ULONGLONG)(IMDISK_API_NO_BROADCAST_NOTIFY | IMDISK_API_FORCE_DISMOUNT));
			if (!ImDiskCreateDevice(NULL, &dg, NULL, IMDISK_TYPE_FILE | IMDISK_DEVICE_TYPE_HD, image_file, FALSE, temp_letter)) continue;
			temp_letter[2] = '\\';
			temp_letter[3] = 0;
		}

		// excluded folders
		sync_excluded = path_dest + 32768;
		*sync_excluded = 0;
		lstrcpyW(&key_name[2], L"SyncExcluded");
		data_size = 499 * sizeof(WCHAR);
		RegQueryValueEx(reg_key, key_name_ptr, NULL, NULL, (void*)sync_excluded, &data_size);
		for (i = 0;; i++) {
			while (*(ULONG*)sync_excluded == '\r' + ('\n' << 16)) sync_excluded += 2;
			if (!*sync_excluded) break;
			excluded_list[i] = sync_excluded;
			do sync_excluded++; while (*sync_excluded && *(ULONG*)sync_excluded != '\r' + ('\n' << 16));
			*sync_excluded = 0;
			sync_excluded += 2;
		}

		lstrcpyW(path_source, L"\\\\?\\");
		lstrcpyW(&path_source[4], mount_point);
		i = lstrlenW(path_source);
		base_source = path_source + i;

		lstrcpyW(path_dest, L"\\\\?\\");
		lstrcpyW(dest_is_dir && image_file[0] == '\\' && image_file[1] == '\\' ? path_dest : &path_dest[4], dest_is_dir ? image_file : temp_letter);
		j = lstrlenW(path_dest);
		base_dest = path_dest + j;

		if ((flags & 0x0c) == 0x0c) scan_dir_delete(i, j, FALSE);
		scan_dir_copy(i, j);

		if (!dest_is_dir) ImDiskRemoveDevice(NULL, 0, temp_letter);
	}

	i = GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, NULL, NULL, buf, (DEF_BUFFER_SIZE / sizeof(WCHAR)) / 2);
	*(ULONG*)(buf + (i - 1) * sizeof(WCHAR)) = ' ';
	i += GetTimeFormat(LOCALE_USER_DEFAULT, 0, NULL, NULL, (WCHAR*)buf + i, (DEF_BUFFER_SIZE / sizeof(WCHAR)) / 2);
	RegSetValueEx(reg_key, L"LastCompletedSync", 0, REG_SZ, buf, i * sizeof(WCHAR));

stop:
	SetEvent(stop_event);
	return 0;
}


static DWORD __stdcall HandlerEx(DWORD dwControl, DWORD dwEventType, LPVOID lpEventData, LPVOID lpContext)
{
	switch (dwControl)
	{
		case SERVICE_CONTROL_PRESHUTDOWN:
		case SERVICE_CONTROL_SHUTDOWN:
			SvcStatus.dwCurrentState = SERVICE_STOP_PENDING;
			SetServiceStatus(SvcStatusHandle, &SvcStatus);
			CreateThread(NULL, 0, save_ramdisk, NULL, 0, NULL);
			return NO_ERROR;

		case SERVICE_CONTROL_STOP:
			SetEvent(stop_event);

		case SERVICE_CONTROL_INTERROGATE:
			return NO_ERROR;

		default:
			return ERROR_CALL_NOT_IMPLEMENTED;
	}
}


static void __stdcall SvcMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
	SvcStatusHandle = RegisterServiceCtrlHandlerEx(L"", HandlerEx, NULL);
	SetServiceStatus(SvcStatusHandle, &SvcStatus);
	WaitForSingleObject(stop_event, INFINITE);
	SvcStatus.dwCurrentState = SERVICE_STOPPED;
	SetServiceStatus(SvcStatusHandle, &SvcStatus);
}


int __stdcall wWinMain(HINSTANCE hinstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
	SERVICE_TABLE_ENTRY DispatchTable[] = {{L"", SvcMain}, {NULL, NULL}};
	OSVERSIONINFO os_ver;
	HKEY reg_key;
	DWORD data_size;
	char timeout[20], *str_ptr;
	WCHAR *command_line;
	int i = 0;

	command_line = GetCommandLine();
	command_line += lstrlenW(command_line);
	if (!lstrcmpW(command_line - 4, L"SYNC"))
		save_ramdisk(NULL);
	else {
		os_ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&os_ver);
		if (os_ver.dwMajorVersion < 6) {
			RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &reg_key, NULL);
			timeout[0] = 0;
			data_size = sizeof timeout;
			RegQueryValueExA(reg_key, "WaitToKillServiceTimeout", NULL, NULL, (void*)timeout, &data_size);
			str_ptr = timeout;
			while (*str_ptr) { i *= 10; i += *str_ptr - '0'; str_ptr++; }
			if (i < 600000) RegSetValueExA(reg_key, "WaitToKillServiceTimeout", 0, REG_SZ, (void*)"600000", 7);
			RegCloseKey(reg_key);
			SvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
		}
		stop_event = CreateEvent(NULL, FALSE, FALSE, NULL);
		StartServiceCtrlDispatcher(DispatchTable);
	}

	ExitProcess(0);
}
