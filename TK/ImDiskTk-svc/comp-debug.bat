\mingw64\bin\gcc.exe -mno-sse3 ImDiskTk-svc.c -S -o ImDiskTk-svc64.s -municode -s -Os -Wall

\mingw64\bin\windres.exe resource.rc "%TEMP%\resource.o"
\mingw64\bin\gcc.exe -mno-sse3 ImDiskTk-svc.c "%TEMP%\resource.o" -o ImDiskTk-svc64.exe -municode -s -Os -Wall^
 -lmsvcrt -lkernel32 -lntdll -lshlwapi

del "%TEMP%\resource.o"


\mingw32\bin\gcc.exe ImDiskTk-svc.c -S -o ImDiskTk-svc32.s -municode -s -Os -Wall

\mingw32\bin\windres.exe resource.rc "%TEMP%\resource.o"
\mingw32\bin\gcc.exe ImDiskTk-svc.c "%TEMP%\resource.o" -o ImDiskTk-svc32.exe -municode -s -Os -Wall^
 -lmsvcrt -lkernel32 -lntdll -lshlwapi

del "%TEMP%\resource.o"
pause