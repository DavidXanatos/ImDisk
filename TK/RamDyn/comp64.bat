set Path_prev=%Path%
set Path=%~d0\mingw64\bin
:: gcc.exe RamDyn.c -S -o RamDyn.s -municode -s -O3 -minline-all-stringops -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe RamDyn.c "%TEMP%\resource.o" -o RamDyn64.exe -municode -mwindows -s -O3 -minline-all-stringops -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lmsvcrt -lkernel32 -lntdll -luser32 -ladvapi32 -lwtsapi32 -Wl,--gc-sections,--nxcompat,--dynamicbase,--high-entropy-va -pie -e wWinMain
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause