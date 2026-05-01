@for /f %%I in ('wmic OS get LocalDateTime ^| find "."') do @set D=%%I
@set P=ImDiskTk%D:~0,8%
md %P%
copy /y /b install.bat %P%

del files\RamDyn.exe files\ImDiskTk-svc.exe ImDiskTk.zip ImDiskTk-x64.zip

copy /y /b RamDiskUI\RamDiskUI32.exe files\RamDiskUI.exe
copy /y /b RamDyn\RamDyn*.exe files
copy /y /b MountImg\MountImg32.exe files\MountImg.exe
copy /y /b ImDisk-Dlg\ImDisk-Dlg32.exe files\ImDisk-Dlg.exe
copy /y /b ImDiskTk-svc\ImDiskTk-svc*.exe files
copy /y /b install\config32.exe files\config.exe
makecab /d DiskDirectoryTemplate=%P% /f cab32.txt
"%ProgramW6432%\7-Zip\7z.exe" a ImDiskTk.zip %P% -mx=9

del files\RamDyn32.exe files\ImDiskTk-svc32.exe
ren files\RamDyn64.exe RamDyn.exe
ren files\ImDiskTk-svc64.exe ImDiskTk-svc.exe
copy /y /b RamDiskUI\RamDiskUI64.exe files\RamDiskUI.exe
copy /y /b MountImg\MountImg64.exe files\MountImg.exe
copy /y /b ImDisk-Dlg\ImDisk-Dlg64.exe files\ImDisk-Dlg.exe
copy /y /b install\config64.exe files\config.exe
makecab /d DiskDirectoryTemplate=%P% /f cab64.txt
"%ProgramW6432%\7-Zip\7z.exe" a ImDiskTk-x64.zip %P% -mx=9

rd /s /q %P%
pause