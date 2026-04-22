cmake_minimum_required(VERSION 3.16)
project(power_analyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Platform-specifieke serial implementatie
if(WIN32)
    set(SERIAL_SRC lld/ModbusRtuSerialStub.cpp)
else()
    set(SERIAL_SRC lld/ModbusRtuSerial.cpp)
endif()

add_library(power_analyzer_lib
    ${SERIAL_SRC}
    lld/IEM3250LLD.cpp
    lld/FLOG_LLD.cpp
    lld/BufferedLogBackend.cpp
    hld/MeterHLD.cpp
    hld/LogHLD.cpp
    controller/Controller.cpp
)

if(WIN32)
    target_link_libraries(power_analyzer_lib PRIVATE ws2_32)
else()
    target_link_libraries(power_analyzer_lib PRIVATE pthread)
endif()

add_executable(power_analyzer main.cpp)
target_link_libraries(power_analyzer PRIVATE power_analyzer_lib)
