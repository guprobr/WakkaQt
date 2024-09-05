cmake_minimum_required(VERSION 3.16)

# Project name
project(WakkaQt VERSION 1.0 LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)  # Enable automatic handling of Qt resource files

# Find the required Qt modules
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
    MultimediaWidgets
)

# Find and configure GStreamer
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
include_directories(${GSTREAMER_INCLUDE_DIRS})
link_directories(${GSTREAMER_LIBRARY_DIRS})

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
    previewwebcam.cpp
    mainwindow.h
    sndwidget.h
    previewdialog.h
    previewwebcam.h
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
