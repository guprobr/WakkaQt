cmake_minimum_required(VERSION 3.16)

# Project name
project(WakkaQt VERSION 1.0 LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)  # Enable automatic handling of Qt resource files

# Force static linking
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(BUILD_SHARED_LIBS OFF)  # Disable shared libraries by default

# Find the required Qt modules
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
    MultimediaWidgets
)

if (WIN32)
    message(STATUS "Detected Windows (WIN32)")

    # GStreamer include and library directories
    set(GSTREAMER_INCLUDE_DIRS 
        "C:/gstreamer/1.0/mingw_x86_64/include/gstreamer-1.0"
        "C:/gstreamer/1.0/mingw_x86_64/include/glib-2.0"
        "C:/gstreamer/1.0/mingw_x86_64/include/"
        "C:/gstreamer/1.0/mingw_x86_64/lib/glib-2.0/include")
    
    set(GSTREAMER_LIBRARY_DIRS "C:/gstreamer/1.0/mingw_x86_64/lib")

    # Libraries to link against (use static libraries)
    set(GSTREAMER_LIBRARIES
        ${GSTREAMER_LIBRARY_DIRS}/libgstreamer-1.0.dll.a
        ${GSTREAMER_LIBRARY_DIRS}/libgobject-2.0.dll.a
        ${GSTREAMER_LIBRARY_DIRS}/libglib-2.0.dll.a
        ${GSTREAMER_LIBRARY_DIRS}/libgmodule-2.0.dll.a
        ${GSTREAMER_LIBRARY_DIRS}/libgthread-2.0.dll.a
    )

    # MinGW runtime (static)
    set(MINGW_RUNTIME "-static-libgcc -static-libstdc++ -static")
    
    # Include directories and link directories
    include_directories(${GSTREAMER_INCLUDE_DIRS})
    link_directories(${GSTREAMER_LIBRARY_DIRS})

else()
    # For Linux (or Unix-like systems)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
    include_directories(${GSTREAMER_INCLUDE_DIRS})
    link_directories(${GSTREAMER_LIBRARY_DIRS})
endif()

message(STATUS "GStreamer include directories: ${GSTREAMER_INCLUDE_DIRS}")

# Add the resource file
qt_add_resources(resources resources.qrc)
message(STATUS "Resources added: ${resources}")

# Add the executable
add_executable(${PROJECT_NAME}
    main.cpp
    mainwindow.cpp
    sndwidget.cpp
    previewdialog.cpp
    mainwindow.h
    sndwidget.h
    previewdialog.h
    ${resources}
)

# Link libraries in the correct order
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    ${GSTREAMER_LIBRARIES}
)

# Include headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GSTREAMER_INCLUDE_DIRS}
)

# Set the necessary linker flags for MinGW
if(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE ${MINGW_RUNTIME})
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "${MINGW_RUNTIME} -static")
endif()
