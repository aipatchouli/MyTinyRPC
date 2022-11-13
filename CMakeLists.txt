cmake_minimum_required (VERSION 3.22.2)
project (MyTinyRPC VERSION 0.1.0 LANGUAGES C CXX) 

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 编译时从添加的路径寻找头文件
aux_source_directory(${PROJECT_SOURCE_DIR}/include INCLUDE_DIR)
include_directories(${INCLUDE_DIR})
include_directories(/usr/lib64)
include_directories(/usr/local/lib)

# 添加源文件
aux_source_directory(${PROJECT_SOURCE_DIR}/src DIR_SRCS)
add_executable (MyTinyRPCServer ${PROJECT_SOURCE_DIR}/src/server.cxx)
add_executable (MyTinyRPCClient ${PROJECT_SOURCE_DIR}/src/client.cxx)

add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY)


#编译器相关设置
set(CMAKE_BUILD_TYPE DEBUGE)
SET(CMAKE_CXX_COMPILER "clang++")
add_compile_options(-g -lboost_system -O2 -std=c++17 -v)