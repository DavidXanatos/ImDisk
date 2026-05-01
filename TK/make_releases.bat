@for /f %%I in ('wmic OS get LocalDateTime ^| find "."') do @set D=%%I
@set P=ImDiskTk%D:~0,8%
md %P%
copy /y /b install.bat %P%

makecab /d DiskDirectoryTemplate=%P% /f cab64.txt
"%ProgramW6432%\7-Zip\7z.exe" a ImDiskTk64.zip %P% -mx=9

rd /s /q %P%
pause