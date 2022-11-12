cmake_minimum_required (VERSION 3.22.2)
project (MyTinyRPC VERSION 0.1.0 LANGUAGES C CXX) 

# Enable C++20
set (CMAKE_CXX_STANDARD 20)
set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

if(DEBUG)
  add_compile_options (-g -O0 -Wall -Wextra -fstandalone-debug -fcolor-diagnostics -std=c++20)
endif()

if(RELEASE)
  add_compile_options (-O3 -Wall -Wextra -std=c++20)
endif()

# 编译时从添加的路径寻找头文件
aux_source_directory(${PROJECT_SOURCE_DIR}/include INCLUDE_DIR)
include_directories(${INCLUDE_DIR})

# 添加源文件
aux_source_directory(${PROJECT_SOURCE_DIR}/src DIR_SRCS)
add_executable (MyTinyRPC ${DIR_SRCS})