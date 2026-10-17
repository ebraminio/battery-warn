@echo off
setlocal

set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%VCVARS%" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
call "%VCVARS%" >nul || exit /b 1

rc /nologo battery-warn.rc || exit /b 1

cl battery-warn.cc toast.cc battery-warn.res /FeBatteryWarn.exe ^
   /std:c++17 /O1 /GS- /Zl /Gy /EHs-c- /GR- ^
   /link /NODEFAULTLIB /SUBSYSTEM:WINDOWS /ENTRY:start ^
   /OPT:REF /OPT:ICF /FIXED /MERGE:.rdata=.text ^
   kernel32.lib user32.lib shell32.lib advapi32.lib ^
   ole32.lib oleaut32.lib uuid.lib
if errorlevel 1 exit /b 1

endlocal
