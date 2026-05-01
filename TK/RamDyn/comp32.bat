set Path_prev=%Path%
set Path=%~d0\mingw32\bin
:: gcc.exe RamDyn.c -S -o RamDyn.s -municode -s -O3 -minline-all-stringops -Wall -D_CRTBLD

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe RamDyn.c "%TEMP%\resource.o" -o RamDyn32.exe -municode -mwindows -s -O3 -minline-all-stringops -Wall -D_CRTBLD -fno-ident^
 -nostdlib -lgcc -lmsvcrt -lkernel32 -lntdll -luser32 -ladvapi32 -lwtsapi32 -Wl,--gc-sections,--nxcompat,--dynamicbase,--large-address-aware -pie -e _wWinMain@16
del "%TEMP%\resource.o"
set Path=%Path_prev%
@if "%1"=="" pause