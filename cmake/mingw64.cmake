# MSYS2 MinGW-w64 gcc producing native Windows x64 binaries.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(MINGW_ROOT "C:/msys64/mingw64" CACHE PATH "MSYS2 MinGW-w64 prefix")
set(CMAKE_C_COMPILER "${MINGW_ROOT}/bin/gcc.exe")
set(CMAKE_RC_COMPILER "${MINGW_ROOT}/bin/windres.exe")
