@echo off
SETLOCAL ENABLEDELAYEDEXPANSION
cd /D "%~dp0"

: Execute with FASTBuild as the working directory so we can use its BFF files without modification
cd "thirdparty\fastbuild\Code"

..\..\..\.bin\fastbuild\x86_64-pc-windows\FBuild.exe -config "..\..\..\fbuild.bff" %*
