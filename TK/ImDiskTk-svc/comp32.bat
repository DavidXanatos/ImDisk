set Path_prev=%Path%
set Path=%~d0\mingw32\bin

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe ImDiskTk-svc.c "%TEMP%\resource.o" -o ImDiskTk-svc32.exe -municode -mwindows -s -Os -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lkernel32 -lntdll -ladvapi32 -Wl,--gc-sections,--nxcompat,--dynamicbase -pie -e _wWinMain@16
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause