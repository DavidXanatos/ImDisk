set Path_prev=%Path%
set Path=%~d0\mingw64\bin
:: gcc.exe RamDiskUI.c -S -o RamDiskUI.s -municode -mwindows -s -Os -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe RamDiskUI.c "%TEMP%\resource.o" -o RamDiskUI64.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lgcc -lmsvcrt -lntdll -ladvapi32 -lkernel32 -lshell32 -luser32 -lcomdlg32 -lgdi32 -lcomctl32 -lshlwapi -lwtsapi32^
 -Wl,--gc-sections,--nxcompat,--dynamicbase,--high-entropy-va -pie -e wWinMain
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause