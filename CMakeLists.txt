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
    Concurrent
)
find_package(PkgConfig REQUIRED)

# Add FFTW3 library
pkg_check_modules(FFTW3 REQUIRED fftw3)

if(UNIX AND NOT APPLE)
    pkg_check_modules(GLIB2 REQUIRED glib-2.0)
    include_directories(${GLIB2_INCLUDE_DIRS})
    link_directories(${GLIB2_LIBRARY_DIRS})
endif()

# Add the resource file
qt_add_resources(resources resources.qrc)
message(STATUS "Resources added: ${resources}")

# Add the executable
add_executable(${PROJECT_NAME}
    main.cpp
    mainwindow.cpp
    complexes.cpp
    sndwidget.cpp
    previewdialog.cpp
    audioamplifier.cpp
    audiorecorder.cpp
    audiovizmediaplayer.cpp
    audiovisualizerwidget.cpp
    vocalenhancer.cpp
    mainwindow.h
    complexes.h
    sndwidget.h
    previewdialog.h
    audioamplifier.h
    audiorecorder.h
    audiovizmediaplayer.h
    audiovisualizerwidget.h
    vocalenhancer.h
    ${resources}
)

# Link libraries in the correct order
target_link_libraries(${PROJECT_NAME}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
    ${GLIB2_LIBRARIES}
    ${FFTW3_LIBRARIES}
)

# Include headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DQT_NO_DEBUG_OUTPUT")
    if(WIN32)
        # Add the WIN32 flag to remove the console window in Windows
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()
message (STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if(UNIX AND NOT APPLE)  
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION /usr/bin                 # Linux: install to /usr/bin
    )
elseif(WIN32) 
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin  # Windows: install to bin under CMAKE_INSTALL_PREFIX
    )
elseif(APPLE)  
    install(TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION /Applications             # macOS: install to /Applications (for app bundles)
    )
endif()