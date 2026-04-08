cmake_minimum_required(VERSION 3.16)

# Project name
project(WakkaQt VERSION 2.0.0 LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Set compiler flags for aggressive optimization
if(WINDOWS)  
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Ox /fp:fast /fp:except- /Ob2 /arch:AVX2")
else() 
    set(CMAKE_CXX_FLAGS "-O3 -ffast-math -fno-strict-aliasing -ftree-vectorize -ftree-loop-vectorize -funroll-loops -march=native -mtune=native")    
endif()

# Find the required Qt modules
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
    MultimediaWidgets
    Concurrent
)

# --- FFTW (Windows / MinGW) ---
if(WIN32)
  # Prefer Program Files (x86), fall back to Program Files
  set(_PF "$ENV{ProgramFiles\(x86\)}")
  if(NOT _PF)
    set(_PF "$ENV{ProgramFiles}")
  endif()
  file(TO_CMAKE_PATH "${_PF}" _PF)
  set(FFTW_ROOT "${_PF}/fftw")

  # Headers
  find_path(FFTW3_INCLUDE_DIR
    NAMES fftw3.h
    HINTS "${FFTW_ROOT}/include")

  # Libraries (double precision: fftw_* symbols)
  find_library(FFTW3_LIBRARIES
    NAMES fftw3-3 fftw3 libfftw3-3 libfftw3
    HINTS "${FFTW_ROOT}/lib")

  # (optional) threads and single-precision — only link if you use them
  find_library(FFTW3_THREADS_LIBRARIES
    NAMES fftw3_threads-3 fftw3_threads libfftw3_threads-3 libfftw3_threads
    HINTS "${FFTW_ROOT}/lib")
  find_library(FFTW3F_LIBRARIES         # for fftwf_* symbols
    NAMES fftw3f-3 fftw3f libfftw3f-3 libfftw3f
    HINTS "${FFTW_ROOT}/lib")

  if(NOT FFTW3_INCLUDE_DIR OR NOT FFTW3_LIBRARIES)
    message(FATAL_ERROR "FFTW not found under ${FFTW_ROOT}. Check install paths.")
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(FFTW3 REQUIRED fftw3)
endif()

# --- FFmpeg native libraries ---
if(WIN32)
  set(_FF "$ENV{ProgramFiles\(x86\)}/ffmpeg")
  if(NOT EXISTS "${_FF}")
    set(_FF "$ENV{ProgramFiles}/ffmpeg")
  endif()
  file(TO_CMAKE_PATH "${_FF}" FFMPEG_ROOT)

  find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
    HINTS "${FFMPEG_ROOT}/include")
  find_library(AVFORMAT_LIB   NAMES avformat   HINTS "${FFMPEG_ROOT}/lib")
  find_library(AVCODEC_LIB    NAMES avcodec    HINTS "${FFMPEG_ROOT}/lib")
  find_library(AVFILTER_LIB   NAMES avfilter   HINTS "${FFMPEG_ROOT}/lib")
  find_library(AVUTIL_LIB     NAMES avutil     HINTS "${FFMPEG_ROOT}/lib")
  find_library(SWRESAMPLE_LIB NAMES swresample HINTS "${FFMPEG_ROOT}/lib")
  find_library(SWSCALE_LIB    NAMES swscale    HINTS "${FFMPEG_ROOT}/lib")

  if(NOT FFMPEG_INCLUDE_DIR OR NOT AVFORMAT_LIB)
    message(WARNING "FFmpeg dev headers not found under ${FFMPEG_ROOT}. Native FFmpeg integration disabled.")
    set(FFMPEG_FOUND FALSE)
  else()
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_LIBRARIES ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVFILTER_LIB}
                         ${AVUTIL_LIB} ${SWRESAMPLE_LIB} ${SWSCALE_LIB})
  endif()
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(AVFORMAT  libavformat)
  pkg_check_modules(AVCODEC   libavcodec)
  pkg_check_modules(AVFILTER  libavfilter)
  pkg_check_modules(AVUTIL    libavutil)
  pkg_check_modules(SWRESAMPLE libswresample)
  pkg_check_modules(SWSCALE   libswscale)

  if(AVFORMAT_FOUND AND AVCODEC_FOUND AND AVFILTER_FOUND AND AVUTIL_FOUND
     AND SWRESAMPLE_FOUND AND SWSCALE_FOUND)
    set(FFMPEG_FOUND TRUE)
    set(FFMPEG_INCLUDE_DIR ${AVFORMAT_INCLUDE_DIRS})
    set(FFMPEG_LIBRARIES
        ${AVFORMAT_LIBRARIES} ${AVCODEC_LIBRARIES} ${AVFILTER_LIBRARIES}
        ${AVUTIL_LIBRARIES} ${SWRESAMPLE_LIBRARIES} ${SWSCALE_LIBRARIES})
    set(FFMPEG_CFLAGS
        ${AVFORMAT_CFLAGS_OTHER} ${AVCODEC_CFLAGS_OTHER})
    message(STATUS "FFmpeg native libraries found — native integration enabled")
  else()
    set(FFMPEG_FOUND FALSE)
    message(WARNING "FFmpeg dev libraries not found (install libavformat-dev etc.) — native integration disabled")
  endif()
endif()

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
    mainwindowDevicesMgr.cpp
    mainwindowPlaybackMgr.cpp
    mainwindowRecorderMgr.cpp
    mainwindowRenderMgr.cpp
    mainwindowLibraryMgr.cpp
    complexes.cpp
    sndwidget.cpp
    pitchmonitorwidget.cpp
    previewdialog.cpp
    audioamplifier.cpp
    audiorecorder.cpp
    audiovizmediaplayer.cpp
    audiovisualizerwidget.cpp
    vocalenhancer.cpp
    DownloadDialog.cpp
    sessionmanager.cpp
    librarydialog.cpp
    ffmpegnative.cpp
    mainwindow.h
    complexes.h
    sndwidget.h
    pitchmonitorwidget.h
    previewdialog.h
    audioamplifier.h
    audiorecorder.h
    audiovizmediaplayer.h
    audiovisualizerwidget.h
    vocalenhancer.h
    DownloadDialog.h
    sessionmanager.h
    librarydialog.h
    ffmpegnative.h
    ${resources}
)

# Link libraries
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
    ${FFTW3_INCLUDE_DIR}
)

# FFmpeg native integration (optional — graceful fallback to QProcess if absent)
if(FFMPEG_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WAKKAQT_FFMPEG_NATIVE=1)
    target_link_libraries(${PROJECT_NAME} ${FFMPEG_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIR})
    if(DEFINED FFMPEG_CFLAGS)
        target_compile_options(${PROJECT_NAME} PRIVATE ${FFMPEG_CFLAGS})
    endif()
endif()

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

if(UNIX AND NOT APPLE)
    # icon dir
    set(ICON_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/256x256/apps")
    # Install icon
    install(FILES images/icon.png DESTINATION ${ICON_INSTALL_DIR} RENAME WakkaQt.png)
    # Install .desktop file
    install(FILES WakkaQt.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
endif()