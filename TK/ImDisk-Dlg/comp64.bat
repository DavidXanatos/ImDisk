set Path_prev=%Path%
set Path=%~d0\mingw64\bin
:: gcc.exe ImDisk-Dlg.c -S -o ImDisk-Dlg.s -municode -mwindows -s -Os -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe ImDisk-Dlg.c "%TEMP%\resource.o" -o ImDisk-Dlg64.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lshlwapi -lcomdlg32^
 -Wl,--gc-sections,--nxcompat,--dynamicbase,--high-entropy-va -pie -e wWinMain
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause