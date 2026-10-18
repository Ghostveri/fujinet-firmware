cmake_minimum_required(VERSION 3.16)

# Also usable alone, without configuring the complete PC firmware.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ESPHTTPClientTests LANGUAGES C CXX)
    enable_testing()
endif()

get_filename_component(FIRMWARE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ESP_HTTP_CLIENT_IDF_PATH "$ENV{IDF_PATH}" CACHE PATH "Existing ESP-IDF source tree")
set(IDF_COMPONENTS "${ESP_HTTP_CLIENT_IDF_PATH}/components")
if(NOT EXISTS "${IDF_COMPONENTS}/http_parser/http_parser.c")
    message(FATAL_ERROR "Set ESP_HTTP_CLIENT_IDF_PATH to ESP-IDF with components/http_parser")
endif()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdkconfig.h" "// Plain HTTP host test configuration.\n")

add_executable(esp_http_client_tests
    ClientTests.cpp
    ${FIRMWARE_ROOT}/lib/fn_esp_http_client/fn_esp_http_client.cpp
    ${FIRMWARE_ROOT}/lib/fn_esp_http_client/fn_http_header.cpp
    ${FIRMWARE_ROOT}/lib/fn_esp_http_client/fn_http_utils.cpp
    ${IDF_COMPONENTS}/http_parser/http_parser.c
)
target_compile_features(esp_http_client_tests PRIVATE cxx_std_20)
target_include_directories(esp_http_client_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IDF_COMPONENTS}/http_parser
    ${IDF_COMPONENTS}/tcp_transport/include
    ${FIRMWARE_ROOT}/tests/esp_stubs
    ${FIRMWARE_ROOT}/lib/fn_esp_http_client
    ${FIRMWARE_ROOT}/components_pc
)
target_compile_options(esp_http_client_tests PRIVATE
    "SHELL:-include stdlib.h" "SHELL:-include esp_system.h")
target_compile_definitions(esp_http_client_tests PRIVATE ESP_ERR_INVALID_STATE=0x103)
add_test(NAME esp_http_client_tests COMMAND esp_http_client_tests)
set_tests_properties(esp_http_client_tests PROPERTIES TIMEOUT 10)
