# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.30

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
CMAKE_COMMAND = /opt/homebrew/Cellar/cmake/3.30.5/bin/cmake

# The command to remove a file.
RM = /opt/homebrew/Cellar/cmake/3.30.5/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/xiezhilong/Desktop/longkit

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/xiezhilong/Desktop/longkit/build

# Include any dependencies generated for this target.
include tests/CMakeFiles/test_webserver.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include tests/CMakeFiles/test_webserver.dir/compiler_depend.make

# Include the progress variables for this target.
include tests/CMakeFiles/test_webserver.dir/progress.make

# Include the compile flags for this target's objects.
include tests/CMakeFiles/test_webserver.dir/flags.make

tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.o: tests/CMakeFiles/test_webserver.dir/flags.make
tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.o: /Users/xiezhilong/Desktop/longkit/tests/test_webserver.cpp
tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.o: tests/CMakeFiles/test_webserver.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --progress-dir=/Users/xiezhilong/Desktop/longkit/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.o"
	cd /Users/xiezhilong/Desktop/longkit/build/tests && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.o -MF CMakeFiles/test_webserver.dir/test_webserver.cpp.o.d -o CMakeFiles/test_webserver.dir/test_webserver.cpp.o -c /Users/xiezhilong/Desktop/longkit/tests/test_webserver.cpp

tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Preprocessing CXX source to CMakeFiles/test_webserver.dir/test_webserver.cpp.i"
	cd /Users/xiezhilong/Desktop/longkit/build/tests && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /Users/xiezhilong/Desktop/longkit/tests/test_webserver.cpp > CMakeFiles/test_webserver.dir/test_webserver.cpp.i

tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green "Compiling CXX source to assembly CMakeFiles/test_webserver.dir/test_webserver.cpp.s"
	cd /Users/xiezhilong/Desktop/longkit/build/tests && /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /Users/xiezhilong/Desktop/longkit/tests/test_webserver.cpp -o CMakeFiles/test_webserver.dir/test_webserver.cpp.s

# Object files for target test_webserver
test_webserver_OBJECTS = \
"CMakeFiles/test_webserver.dir/test_webserver.cpp.o"

# External object files for target test_webserver
test_webserver_EXTERNAL_OBJECTS =

bin/test_webserver: tests/CMakeFiles/test_webserver.dir/test_webserver.cpp.o
bin/test_webserver: tests/CMakeFiles/test_webserver.dir/build.make
bin/test_webserver: lib/libLongKit.dylib
bin/test_webserver: tests/CMakeFiles/test_webserver.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color "--switch=$(COLOR)" --green --bold --progress-dir=/Users/xiezhilong/Desktop/longkit/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable ../bin/test_webserver"
	cd /Users/xiezhilong/Desktop/longkit/build/tests && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/test_webserver.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
tests/CMakeFiles/test_webserver.dir/build: bin/test_webserver
.PHONY : tests/CMakeFiles/test_webserver.dir/build

tests/CMakeFiles/test_webserver.dir/clean:
	cd /Users/xiezhilong/Desktop/longkit/build/tests && $(CMAKE_COMMAND) -P CMakeFiles/test_webserver.dir/cmake_clean.cmake
.PHONY : tests/CMakeFiles/test_webserver.dir/clean

tests/CMakeFiles/test_webserver.dir/depend:
	cd /Users/xiezhilong/Desktop/longkit/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/xiezhilong/Desktop/longkit /Users/xiezhilong/Desktop/longkit/tests /Users/xiezhilong/Desktop/longkit/build /Users/xiezhilong/Desktop/longkit/build/tests /Users/xiezhilong/Desktop/longkit/build/tests/CMakeFiles/test_webserver.dir/DependInfo.cmake "--color=$(COLOR)"
.PHONY : tests/CMakeFiles/test_webserver.dir/depend

