set Path_prev=%Path%
set Path=%~d0\mingw32\bin
windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe MountImg.c "%TEMP%\resource.o" -o MountImg32.exe -municode -s -Os -Wall -D_CRTBLD^
 -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lcomdlg32 -lshlwapi -lwtsapi32^
 -Wl,--gc-sections,--nxcompat -Wl,--dynamicbase -pie -e _wWinMain@16
del "%TEMP%\resource.o"

set Path=%~d0\mingw64\bin
windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe MountImg.c "%TEMP%\resource.o" -o MountImg64.exe -municode -s -Os -Wall -D_CRTBLD^
 -nostdlib -lmsvcrt -lkernel32 -lshell32 -luser32 -ladvapi32 -lcomdlg32 -lshlwapi -lwtsapi32^
 -Wl,--gc-sections,--nxcompat -Wl,--dynamicbase -pie -e wWinMain
del "%TEMP%\resource.o"
set Path=%Path_prev%

pause