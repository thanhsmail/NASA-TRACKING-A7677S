cmake_minimum_required(VERSION 3.10)
project(HostUnitTests C)

set(CMAKE_C_STANDARD 99)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../hal
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

set(COMMON_MOCK_SRCS
    mocks/mock_hal_os.c
    mocks/mock_hal_gnss.c
    mocks/mock_hal_net.c
    mocks/mock_hal_gpio.c
    ../app_utils.c
)

# Test Park Rules
add_executable(test_park_rules
    unit/test_park_rules.c
    ../app_park_rules.c
    ${COMMON_MOCK_SRCS}
)

# Test LXLT Rules
add_executable(test_lxlt_rules
    unit/test_lxlt_rules.c
    ../app_lxlt_rules.c
    ../app_gps.c
    ${COMMON_MOCK_SRCS}
)

enable_testing()
add_test(NAME TestParkRules COMMAND test_park_rules)
add_test(NAME TestLxltRules COMMAND test_lxlt_rules)
