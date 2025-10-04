cmake_minimum_required(VERSION 3.15)
project(FPSI_Protocol VERSION 1.0.0)

add_definitions(-DENABLE_BOOST)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -pthread -fPIC")

# 定义库路径变量
set(VOLEPSI_ROOT /home/luck/Nolen/crx1/preInstall/volepsi/out/install/linux)
set(LIB_ROOT /home/luck/Nolen/crx1/preLibrary)
set(SEAL_ROOT /home/luck/xzy/FHE/SEAL)

# 包含目录 - 按正确顺序
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${VOLEPSI_ROOT}/include
    ${VOLEPSI_ROOT}/include/cryptoTools
    ${VOLEPSI_ROOT}/include/libOTe
    ${VOLEPSI_ROOT}/include/coproto
    ${VOLEPSI_ROOT}/include/macoro
    /home/luck/xzy/OKVS/okvs/bandokvs
    /home/luck/ck/AsiaCCS25/modified/volepsi
    ${SEAL_ROOT}/native/src
    ${SEAL_ROOT}/build/native/src
)

# 库目录
link_directories(
    ${LIB_ROOT}/lib
    ${SEAL_ROOT}/build/lib
)

# 查找必需的库
find_library(BANDOKVS_LIB bandokvs PATHS ${LIB_ROOT}/lib REQUIRED)
find_library(LIBOTE_LIB libOTe PATHS ${LIB_ROOT}/lib REQUIRED)
find_library(CRYPTOTOOLS_LIB cryptoTools PATHS ${LIB_ROOT}/lib REQUIRED)
find_library(COPROTO_LIB coproto PATHS ${LIB_ROOT}/lib REQUIRED)
find_library(MACORO_LIB macoro PATHS ${LIB_ROOT}/lib REQUIRED)
find_library(VOLEPSI_LIB volePSI PATHS ${LIB_ROOT}/lib REQUIRED)

# 工具库
add_library(fpsi_utils STATIC
    utils.cpp
    elsh.cpp
)

target_link_libraries(fpsi_utils
    ${BANDOKVS_LIB}
    ${LIBOTE_LIB}
    ${CRYPTOTOOLS_LIB}
    ${COPROTO_LIB}
    ${MACORO_LIB}
    ${VOLEPSI_LIB}
    ${SEAL_ROOT}/build/lib/libseal-4.1.a
    ${SEAL_ROOT}/build/lib/libzstd.a
    ${SEAL_ROOT}/build/lib/libz.a
    pthread
    dl
    rt
)

# Sender 可执行文件
add_executable(fpsi_sender fpsi_sender.cpp)
target_link_libraries(fpsi_sender 
    fpsi_utils
    ${CRYPTOTOOLS_LIB}
    ${COPROTO_LIB}
    pthread
)

# Receiver 可执行文件
add_executable(fpsi_receiver fpsi_receiver.cpp)
target_link_libraries(fpsi_receiver 
    fpsi_utils
    ${CRYPTOTOOLS_LIB}
    ${COPROTO_LIB}
    pthread
)

# 打印调试信息
message(STATUS "CRYPTOTOOLS_LIB: ${CRYPTOTOOLS_LIB}")
message(STATUS "COPROTO_LIB: ${COPROTO_LIB}")

# 安装规则
install(TARGETS fpsi_sender fpsi_receiver
    RUNTIME DESTINATION bin
)