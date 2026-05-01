set Path_prev=%Path%
set Path=%~d0\mingw32\bin

windres.exe resource.rc "%TEMP%\resource.o"
gcc.exe ImDisk-Dlg.c "%TEMP%\resource.o" -o ImDisk-Dlg.exe -municode -s -Os -Wall -lshlwapi -lcomdlg32
del "%TEMP%\resource.o"
set Path=%Path_prev%
pause