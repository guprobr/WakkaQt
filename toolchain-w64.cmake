# Specify the cross-compilation toolchain
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10)

# MinGW-w64 compilers
set(CMAKE_C_COMPILER /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)

# Use Linux native Qt tools during cross-compilation
set(CMAKE_HOST_MOC /usr/bin/moc)       # Path to native Linux moc
set(CMAKE_HOST_UIC /usr/bin/uic)       # Path to native Linux uic
set(CMAKE_HOST_RCC /usr/bin/rcc)       # Path to native Linux rcc

# Qt for Windows (adjust the path to your installation)
set(Qt6_DIR "/home/guzpido/Desktop/Qt/lib/cmake/Qt6")

# CMake find settings
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 /home/guzpido/Desktop/Qt/)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
