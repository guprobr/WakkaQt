# toolchain-w64.cmake

# Specify the cross-compilation toolchain
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10)

# MinGW-w64 compilers
set(CMAKE_C_COMPILER /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)

# Qt for Windows (adjust the path to your installation)
set(Qt6_DIR "/qt6/windows/lib/cmake/Qt6")

# Other settings
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 /qt6/windows)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)