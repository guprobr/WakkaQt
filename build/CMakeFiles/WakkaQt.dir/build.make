# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.28

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/guzpido/WakkaQt

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/guzpido/WakkaQt/build

# Include any dependencies generated for this target.
include CMakeFiles/WakkaQt.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/WakkaQt.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/WakkaQt.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/WakkaQt.dir/flags.make

qrc_resources.cpp: /home/guzpido/WakkaQt/resources.qrc
qrc_resources.cpp: /home/guzpido/WakkaQt/images/logo.jpg
qrc_resources.cpp: resources.qrc.depends
qrc_resources.cpp: /usr/lib/qt6/libexec/rcc
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Generating qrc_resources.cpp"
	/usr/lib/qt6/libexec/rcc --name resources --output /home/guzpido/WakkaQt/build/qrc_resources.cpp /home/guzpido/WakkaQt/resources.qrc

WakkaQt_autogen/timestamp: /usr/lib/qt6/libexec/moc
WakkaQt_autogen/timestamp: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --blue --bold --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Automatic MOC for target WakkaQt"
	/usr/bin/cmake -E cmake_autogen /home/guzpido/WakkaQt/build/CMakeFiles/WakkaQt_autogen.dir/AutogenInfo.json ""
	/usr/bin/cmake -E touch /home/guzpido/WakkaQt/build/WakkaQt_autogen/timestamp

CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o: WakkaQt_autogen/mocs_compilation.cpp
CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building CXX object CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o -MF CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o.d -o CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o -c /home/guzpido/WakkaQt/build/WakkaQt_autogen/mocs_compilation.cpp

CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/build/WakkaQt_autogen/mocs_compilation.cpp > CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.i

CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/build/WakkaQt_autogen/mocs_compilation.cpp -o CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.s

CMakeFiles/WakkaQt.dir/main.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/main.cpp.o: /home/guzpido/WakkaQt/main.cpp
CMakeFiles/WakkaQt.dir/main.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Building CXX object CMakeFiles/WakkaQt.dir/main.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/main.cpp.o -MF CMakeFiles/WakkaQt.dir/main.cpp.o.d -o CMakeFiles/WakkaQt.dir/main.cpp.o -c /home/guzpido/WakkaQt/main.cpp

CMakeFiles/WakkaQt.dir/main.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/main.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/main.cpp > CMakeFiles/WakkaQt.dir/main.cpp.i

CMakeFiles/WakkaQt.dir/main.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/main.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/main.cpp -o CMakeFiles/WakkaQt.dir/main.cpp.s

CMakeFiles/WakkaQt.dir/mainwindow.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/mainwindow.cpp.o: /home/guzpido/WakkaQt/mainwindow.cpp
CMakeFiles/WakkaQt.dir/mainwindow.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_5) "Building CXX object CMakeFiles/WakkaQt.dir/mainwindow.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/mainwindow.cpp.o -MF CMakeFiles/WakkaQt.dir/mainwindow.cpp.o.d -o CMakeFiles/WakkaQt.dir/mainwindow.cpp.o -c /home/guzpido/WakkaQt/mainwindow.cpp

CMakeFiles/WakkaQt.dir/mainwindow.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/mainwindow.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/mainwindow.cpp > CMakeFiles/WakkaQt.dir/mainwindow.cpp.i

CMakeFiles/WakkaQt.dir/mainwindow.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/mainwindow.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/mainwindow.cpp -o CMakeFiles/WakkaQt.dir/mainwindow.cpp.s

CMakeFiles/WakkaQt.dir/sndwidget.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/sndwidget.cpp.o: /home/guzpido/WakkaQt/sndwidget.cpp
CMakeFiles/WakkaQt.dir/sndwidget.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_6) "Building CXX object CMakeFiles/WakkaQt.dir/sndwidget.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/sndwidget.cpp.o -MF CMakeFiles/WakkaQt.dir/sndwidget.cpp.o.d -o CMakeFiles/WakkaQt.dir/sndwidget.cpp.o -c /home/guzpido/WakkaQt/sndwidget.cpp

CMakeFiles/WakkaQt.dir/sndwidget.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/sndwidget.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/sndwidget.cpp > CMakeFiles/WakkaQt.dir/sndwidget.cpp.i

CMakeFiles/WakkaQt.dir/sndwidget.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/sndwidget.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/sndwidget.cpp -o CMakeFiles/WakkaQt.dir/sndwidget.cpp.s

CMakeFiles/WakkaQt.dir/previewdialog.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/previewdialog.cpp.o: /home/guzpido/WakkaQt/previewdialog.cpp
CMakeFiles/WakkaQt.dir/previewdialog.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_7) "Building CXX object CMakeFiles/WakkaQt.dir/previewdialog.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/previewdialog.cpp.o -MF CMakeFiles/WakkaQt.dir/previewdialog.cpp.o.d -o CMakeFiles/WakkaQt.dir/previewdialog.cpp.o -c /home/guzpido/WakkaQt/previewdialog.cpp

CMakeFiles/WakkaQt.dir/previewdialog.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/previewdialog.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/previewdialog.cpp > CMakeFiles/WakkaQt.dir/previewdialog.cpp.i

CMakeFiles/WakkaQt.dir/previewdialog.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/previewdialog.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/previewdialog.cpp -o CMakeFiles/WakkaQt.dir/previewdialog.cpp.s

CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o: /home/guzpido/WakkaQt/previewwebcam.cpp
CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_8) "Building CXX object CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o -MF CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o.d -o CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o -c /home/guzpido/WakkaQt/previewwebcam.cpp

CMakeFiles/WakkaQt.dir/previewwebcam.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/previewwebcam.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/previewwebcam.cpp > CMakeFiles/WakkaQt.dir/previewwebcam.cpp.i

CMakeFiles/WakkaQt.dir/previewwebcam.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/previewwebcam.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/previewwebcam.cpp -o CMakeFiles/WakkaQt.dir/previewwebcam.cpp.s

CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o: CMakeFiles/WakkaQt.dir/flags.make
CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o: qrc_resources.cpp
CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o: CMakeFiles/WakkaQt.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_9) "Building CXX object CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o -MF CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o.d -o CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o -c /home/guzpido/WakkaQt/build/qrc_resources.cpp

CMakeFiles/WakkaQt.dir/qrc_resources.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/WakkaQt.dir/qrc_resources.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/guzpido/WakkaQt/build/qrc_resources.cpp > CMakeFiles/WakkaQt.dir/qrc_resources.cpp.i

CMakeFiles/WakkaQt.dir/qrc_resources.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/WakkaQt.dir/qrc_resources.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/guzpido/WakkaQt/build/qrc_resources.cpp -o CMakeFiles/WakkaQt.dir/qrc_resources.cpp.s

# Object files for target WakkaQt
WakkaQt_OBJECTS = \
"CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o" \
"CMakeFiles/WakkaQt.dir/main.cpp.o" \
"CMakeFiles/WakkaQt.dir/mainwindow.cpp.o" \
"CMakeFiles/WakkaQt.dir/sndwidget.cpp.o" \
"CMakeFiles/WakkaQt.dir/previewdialog.cpp.o" \
"CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o" \
"CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o"

# External object files for target WakkaQt
WakkaQt_EXTERNAL_OBJECTS =

WakkaQt: CMakeFiles/WakkaQt.dir/WakkaQt_autogen/mocs_compilation.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/main.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/mainwindow.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/sndwidget.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/previewdialog.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/previewwebcam.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/qrc_resources.cpp.o
WakkaQt: CMakeFiles/WakkaQt.dir/build.make
WakkaQt: /usr/lib/x86_64-linux-gnu/libQt6MultimediaWidgets.so.6.4.2
WakkaQt: /usr/lib/x86_64-linux-gnu/libQt6Multimedia.so.6.4.2
WakkaQt: /usr/lib/x86_64-linux-gnu/libQt6Network.so.6.4.2
WakkaQt: /usr/lib/x86_64-linux-gnu/libQt6Widgets.so.6.4.2
WakkaQt: /usr/lib/x86_64-linux-gnu/libQt6Gui.so.6.4.2
WakkaQt: /usr/lib/x86_64-linux-gnu/libQt6Core.so.6.4.2
WakkaQt: /usr/lib/x86_64-linux-gnu/libGLX.so
WakkaQt: /usr/lib/x86_64-linux-gnu/libOpenGL.so
WakkaQt: CMakeFiles/WakkaQt.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/home/guzpido/WakkaQt/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_10) "Linking CXX executable WakkaQt"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/WakkaQt.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/WakkaQt.dir/build: WakkaQt
.PHONY : CMakeFiles/WakkaQt.dir/build

CMakeFiles/WakkaQt.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/WakkaQt.dir/cmake_clean.cmake
.PHONY : CMakeFiles/WakkaQt.dir/clean

CMakeFiles/WakkaQt.dir/depend: WakkaQt_autogen/timestamp
CMakeFiles/WakkaQt.dir/depend: qrc_resources.cpp
	cd /home/guzpido/WakkaQt/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/guzpido/WakkaQt /home/guzpido/WakkaQt /home/guzpido/WakkaQt/build /home/guzpido/WakkaQt/build /home/guzpido/WakkaQt/build/CMakeFiles/WakkaQt.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : CMakeFiles/WakkaQt.dir/depend

