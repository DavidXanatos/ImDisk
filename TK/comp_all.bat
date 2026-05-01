@for /f %%I in ('wmic OS get LocalDateTime ^| find "."') do @set D=%%I
echo #define APP_VERSION "%D:~0,8%">inc\build.h
echo #define APP_NUMBER %D:~0,8%>>inc\build.h
cd ImDisk-Dlg
call comp32.bat -
call comp64.bat -
cd ..\ImDiskTk-svc
call comp32.bat -
call comp64.bat -
cd ..\install
call comp32.bat -
call comp64.bat -
cd ..\MountImg
call comp32.bat -
call comp64.bat -
cd ..\RamDiskUI
call comp32.bat -
call comp64.bat -
cd ..\RamDyn
call comp32.bat -
call comp64.bat -
cd ..
@pause