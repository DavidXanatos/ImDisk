set Path_prev=%Path%
set Path=%~d0\mingw32\bin
:: gcc.exe ImDisk-Dlg.c -S -o ImDisk-Dlg.s -municode -mwindows -s -Os -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe ImDisk-Dlg.c "%TEMP%\resource.o" -o ImDisk-Dlg32.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lgcc -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lshlwapi -lcomdlg32^
 -Wl,--gc-sections,--nxcompat,--dynamicbase -pie -e _wWinMain@16
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause