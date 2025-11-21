@echo off
setlocal
cd /d %~dp0

:: 1. Editorのビルド
echo Building Editor...
cd editor
call npm run build
if %errorlevel% neq 0 (
    echo Error: Failed to build editor.
    exit /b %errorlevel%
)
cd ..

:: 2. HTMLヘッダーの生成
echo Generating C Header...
python html_to_c_header.py
if %errorlevel% neq 0 (
    echo Error: Failed to generate header.
    exit /b %errorlevel%
)

:: 3. ファームウェアのビルド (既存の処理)
echo Building Firmware...
if not exist build (
    mkdir build
)
cd build
cmake ..
cmake --build .

endlocal