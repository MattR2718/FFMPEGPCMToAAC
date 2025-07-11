# CMakeList.txt : CMake project for FFMPEGPCMToAAC, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("FFMPEGPCMToAAC")

# Load FFMPEG
find_package(FFMPEG REQUIRED)

# Add source to this project's executable.
add_executable (FFMPEGPCMToAAC "FFMPEGPCMToAAC.cpp" "FFMPEGPCMToAAC.h" "AudioEncoder.h" "AudioInput.h")

target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET FFMPEGPCMToAAC PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
