set Path_prev=%Path%
set Path=%~d0\mingw64\bin
:: gcc.exe ImDiskTk-svc.c -S -o ImDiskTk-svc.s -municode -mwindows -s -Os -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe ImDiskTk-svc.c "%TEMP%\resource.o" -o ImDiskTk-svc64.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lkernel32 -lntdll -ladvapi32 -Wl,--gc-sections,--nxcompat,--dynamicbase,--high-entropy-va -pie -e wWinMain
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause