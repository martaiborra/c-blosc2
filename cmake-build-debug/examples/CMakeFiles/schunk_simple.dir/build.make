# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.16

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
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
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/martaiborra/c-blosc2

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/martaiborra/c-blosc2/cmake-build-debug

# Include any dependencies generated for this target.
include examples/CMakeFiles/schunk_simple.dir/depend.make

# Include the progress variables for this target.
include examples/CMakeFiles/schunk_simple.dir/progress.make

# Include the compile flags for this target's objects.
include examples/CMakeFiles/schunk_simple.dir/flags.make

examples/CMakeFiles/schunk_simple.dir/schunk_simple.c.o: examples/CMakeFiles/schunk_simple.dir/flags.make
examples/CMakeFiles/schunk_simple.dir/schunk_simple.c.o: ../examples/schunk_simple.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/martaiborra/c-blosc2/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object examples/CMakeFiles/schunk_simple.dir/schunk_simple.c.o"
	cd /home/martaiborra/c-blosc2/cmake-build-debug/examples && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/schunk_simple.dir/schunk_simple.c.o   -c /home/martaiborra/c-blosc2/examples/schunk_simple.c

examples/CMakeFiles/schunk_simple.dir/schunk_simple.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/schunk_simple.dir/schunk_simple.c.i"
	cd /home/martaiborra/c-blosc2/cmake-build-debug/examples && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/martaiborra/c-blosc2/examples/schunk_simple.c > CMakeFiles/schunk_simple.dir/schunk_simple.c.i

examples/CMakeFiles/schunk_simple.dir/schunk_simple.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/schunk_simple.dir/schunk_simple.c.s"
	cd /home/martaiborra/c-blosc2/cmake-build-debug/examples && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/martaiborra/c-blosc2/examples/schunk_simple.c -o CMakeFiles/schunk_simple.dir/schunk_simple.c.s

# Object files for target schunk_simple
schunk_simple_OBJECTS = \
"CMakeFiles/schunk_simple.dir/schunk_simple.c.o"

# External object files for target schunk_simple
schunk_simple_EXTERNAL_OBJECTS =

examples/schunk_simple: examples/CMakeFiles/schunk_simple.dir/schunk_simple.c.o
examples/schunk_simple: examples/CMakeFiles/schunk_simple.dir/build.make
examples/schunk_simple: blosc/libblosc2.so.2.0.0
examples/schunk_simple: examples/CMakeFiles/schunk_simple.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/martaiborra/c-blosc2/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable schunk_simple"
	cd /home/martaiborra/c-blosc2/cmake-build-debug/examples && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/schunk_simple.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
examples/CMakeFiles/schunk_simple.dir/build: examples/schunk_simple

.PHONY : examples/CMakeFiles/schunk_simple.dir/build

examples/CMakeFiles/schunk_simple.dir/clean:
	cd /home/martaiborra/c-blosc2/cmake-build-debug/examples && $(CMAKE_COMMAND) -P CMakeFiles/schunk_simple.dir/cmake_clean.cmake
.PHONY : examples/CMakeFiles/schunk_simple.dir/clean

examples/CMakeFiles/schunk_simple.dir/depend:
	cd /home/martaiborra/c-blosc2/cmake-build-debug && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/martaiborra/c-blosc2 /home/martaiborra/c-blosc2/examples /home/martaiborra/c-blosc2/cmake-build-debug /home/martaiborra/c-blosc2/cmake-build-debug/examples /home/martaiborra/c-blosc2/cmake-build-debug/examples/CMakeFiles/schunk_simple.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : examples/CMakeFiles/schunk_simple.dir/depend
