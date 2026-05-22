cmake_minimum_required(VERSION 3.16)
project(power_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(power_analyzer_lib
    lld/ModbusRtuSerial.cpp
    lld/IEM3250LLD.cpp
    lld/FLOG_LLD.cpp
    lld/BufferedLogBackend.cpp
    hld/MeterHLD.cpp
    hld/LogHLD.cpp
    controller/Controller.cpp
)

target_link_libraries(power_analyzer_lib PRIVATE pthread gpiod)

add_executable(power_analyzer main.cpp)
target_link_libraries(power_analyzer PRIVATE power_analyzer_lib)
