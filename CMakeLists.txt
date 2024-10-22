# CMake 最低版本要求
cmake_minimum_required(VERSION 3.5)
# 项目名称
project(LongKit)
# 显示项目的一些基本信息
message(STATUS "0voice PROJECT_BINARY_DIR       " ${PROJECT_BINARY_DIR})
message(STATUS "0voice PROJECT_SOURCE_DIR       " ${PROJECT_SOURCE_DIR})
message(STATUS "0voice CMAKE_CURRENT_SOURCE_DIR " ${CMAKE_CURRENT_SOURCE_DIR})

# 设置库文件路径
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)
# 设置可执行程序路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

# 设置子目录
set(SUB_DIR_LIST "app" "protocol" "network" "poller" "thread" "util")

# 安装目录
set(INSTALL_PATH_LIB lib)
set(INSTALL_PATH_INCLUDE include)

message(STATUS "0voice INSTALL_PATH_LIB     " ${INSTALL_PATH_LIB})
message(STATUS "0voice INSTALL_PATH_INCLUDE " ${INSTALL_PATH_INCLUDE})

# 遍历所有的文件，并加载到 SRC_LIST 变量
foreach(SUB_DIR ${SUB_DIR_LIST})
    aux_source_directory(src/${SUB_DIR} SRC_LIST)
    install(DIRECTORY src/${SUB_DIR} DESTINATION ${INSTALL_PATH_INCLUDE} FILES_MATCHING PATTERN "*.h")
endforeach()

# 手动包含 spdlog 和 fmt 的头文件和库文件路径
include_directories("/opt/homebrew/Cellar/spdlog/1.14.1_1/include")
include_directories("/opt/homebrew/Cellar/fmt/11.0.2/include")
message(STATUS "包含 spdlog 头文件路径: /opt/homebrew/Cellar/spdlog/1.14.1_1/include")
message(STATUS "包含 fmt 头文件路径: /opt/homebrew/Cellar/fmt/11.0.2/include")

# 链接 spdlog 和 fmt 库
link_directories("/opt/homebrew/Cellar/spdlog/1.14.1_1/lib")
link_directories("/opt/homebrew/Cellar/fmt/11.0.2/lib")
message(STATUS "链接 spdlog 库路径: /opt/homebrew/Cellar/spdlog/1.14.1_1/lib")
message(STATUS "链接 fmt 库路径: /opt/homebrew/Cellar/fmt/11.0.2/lib")

# 设置要链接的库
set(LINK_LIB_LIST spdlog fmt)

# 打印即将链接的库文件
message(STATUS "0voice 将链接依赖库:${LINK_LIB_LIST}")

# 引用头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# 使能 C++17 支持
set(CMAKE_CXX_STANDARD 17)
# 支持 debug
add_compile_options(-g)
# 忽略弃用警告
add_compile_options(-Wno-deprecated-declarations)

# 打印项目名称
message(STATUS "0voice CMAKE_PROJECT_NAME: ${CMAKE_PROJECT_NAME}")

# 生成动态库 LongKit_shared
add_library(LongKit_shared SHARED ${SRC_LIST})

# 链接库到 LongKit_shared
target_link_libraries(LongKit_shared PRIVATE ${LINK_LIB_LIST})

# 设置输出库的名称为 LongKit
set_target_properties(LongKit_shared PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}")

# 确保 spdlog 和 fmt 库被正确找到
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# 如果找到了 spdlog 和 fmt，则链接它们
if(spdlog_FOUND AND fmt_FOUND)
  target_link_libraries(LongKit_shared PRIVATE spdlog::spdlog fmt::fmt)
else()
  message(FATAL_ERROR "spdlog or fmt not found. Please install them or check your CMake configuration.")
endif()

# 安装动态库
install(TARGETS LongKit_shared ARCHIVE DESTINATION ${INSTALL_PATH_LIB} LIBRARY DESTINATION ${INSTALL_PATH_LIB})

# 测试程序，添加子目录以便编译测试代码
add_subdirectory(tests)

# 如果需要编译静态库，取消注释以下代码
# add_library(${CMAKE_PROJECT_NAME}_static STATIC ${SRC_LIST})
# 设置静态库输出名称
# set_target_properties(${CMAKE_PROJECT_NAME}_static PROPERTIES OUTPUT_NAME "${CMAKE_PROJECT_NAME}")
# 安装静态库
# install(TARGETS ${CMAKE_PROJECT_NAME}_static ARCHIVE DESTINATION ${INSTALL_PATH_LIB})
