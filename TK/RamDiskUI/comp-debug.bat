set Path_prev=%Path%
set Path=%~d0\mingw64\bin

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe RamDiskUI.c "%TEMP%\resource.o" -o RamDiskUI.exe^
 -lntdll -lgdi32 -lcomctl32 -lcomdlg32 -lshlwapi -lwtsapi32 -municode -s -Os -Wall -D_CRTBLD
del "%TEMP%\resource.o"
set Path=%Path_prev%
pause