set Path_prev=%Path%
set Path=%~d0\mingw64\bin
:: gcc.exe config.c -S -o config64.s -municode -mwindows -s -Os -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe config.c "%TEMP%\resource.o" -o config64.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lcomdlg32 -lgdi32 -lshlwapi -lversion -lsetupapi -lole32^
 -Wl,--gc-sections,--nxcompat,--dynamicbase,--high-entropy-va -pie -e wWinMain
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause