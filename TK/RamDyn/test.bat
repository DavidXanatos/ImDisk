@echo off
net session >nul 2>nul || (
echo CreateObject("Shell.Application"^).NameSpace("%CD%"^).ParseName("%~nx0"^).InvokeVerb("runas"^)>"%TEMP%\UAC.vbs"
"%TEMP%\UAC.vbs"
del "%TEMP%\UAC.vbs"
exit /b
)
cd /d "%~dp0"
@echo on

RamDyn64 X: 4194304 -1 0 20 ""
:: RamDyn64 Z: 4194304 10 10 10 0 20 "-p \"/fs:NTFS /q /y\""
:: RamDyn64 Z: 4194304 10 10 10 0 20 "-p \"/fs:NTFS /a:512 /q /y\""
:: RamDyn64 Z: D:\test 10 10 10 0 20 ""