set Path_prev=%Path%
set Path=%~d0\mingw32\bin
:: gcc.exe config.c -S -o config32.s -municode -mwindows -s -Os -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe config.c "%TEMP%\resource.o" -o config32.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lcomdlg32 -lgdi32 -lshlwapi -lversion -lsetupapi -lole32^
 -Wl,--gc-sections,--nxcompat,--dynamicbase -pie -e _wWinMain@16
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause