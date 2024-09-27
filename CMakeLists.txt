cmake_minimum_required(VERSION 3.16)

# Project name
project(WakkaQt VERSION 1.0 LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Find the required Qt modules
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
    MultimediaWidgets
)

# Add the resource file
qt_add_resources(resources resources.qrc)
message(STATUS "Resources added: ${resources}")

# Add the executable
add_executable(${PROJECT_NAME}
    main.cpp
    mainwindow.cpp
    sndwidget.cpp
    previewdialog.cpp
    audioamplifier.cpp
    mainwindow.h
    sndwidget.h
    previewdialog.h
    audioamplifier.h
    ${resources}
)

# Link libraries in the correct order
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    ${MINGW_RUNTIME_LIB}
)

# Include headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)