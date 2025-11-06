setlocal
cd /d %~dp0
if not exist build (
    mkdir build
)
cd build
cmake ..
cmake --build .
endlocal