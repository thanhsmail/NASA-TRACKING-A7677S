# Danh sách nguồn của ứng dụng NASA Tracking.
#
# CMakeLists.txt gốc (của SDK) chỉ cần một dòng:
#     INCLUDE(${CMAKE_SOURCE_DIR}/app/app_sources.cmake)
# Khi nâng SDK, chép đè CMakeLists.txt mới rồi thêm lại dòng đó vào nhánh
# CONFIG_SIMCOM_PROTOCOL_V1, thay cho SET(app_src sc_application.c).
# Điểm vào của SDK (userSpaceEntry) nằm ở port/simcom_a7677s/port_entry.c.
#
#   app/                  code nghiệp vụ + driver ngoại vi (chỉ gọi hal/*.h)
#   hal/                  giao diện HAL (*.h)
#   port/simcom_a7677s/   cài đặt HAL cho SIMCom A7677S (nơi duy nhất gọi sAPI_*),
#                         board_pins.h (chân / kênh của module)

SET(NASA_APP_DIR  ${CMAKE_CURRENT_LIST_DIR})
SET(NASA_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
SET(NASA_PORT_DIR ${NASA_ROOT_DIR}/port/simcom_a7677s)

SET(app_src
    ${NASA_APP_DIR}/app_main.c
    ${NASA_APP_DIR}/app_indication.c
    ${NASA_APP_DIR}/drv_en25qh64a.c
    ${NASA_APP_DIR}/drv_lis3dsh.c
    ${NASA_APP_DIR}/app_nasa.c
    ${NASA_APP_DIR}/app_park_rules.c
    ${NASA_APP_DIR}/app_lxlt_rules.c
    ${NASA_APP_DIR}/app_network.c
    ${NASA_APP_DIR}/app_gps.c
    ${NASA_APP_DIR}/app_sms.c
    ${NASA_APP_DIR}/app_cmd.c
    ${NASA_APP_DIR}/app_com.c
    ${NASA_APP_DIR}/app_fota.c
    ${NASA_APP_DIR}/app_sim.c
    ${NASA_APP_DIR}/app_cfg.c
    ${NASA_APP_DIR}/app_backup.c
    ${NASA_APP_DIR}/app_utils.c
    # Phần phụ thuộc SDK: chỉ đổi các file này khi đổi chip / SDK
    ${NASA_PORT_DIR}/port_entry.c
    ${NASA_PORT_DIR}/port_log.c
    ${NASA_PORT_DIR}/port_os.c
    ${NASA_PORT_DIR}/port_periph.c
    ${NASA_PORT_DIR}/port_net.c
    ${NASA_PORT_DIR}/port_gnss.c
    ${NASA_PORT_DIR}/port_sms.c
    ${NASA_PORT_DIR}/port_fs_fota.c
)

# CMakeLists.txt của SDK cũ tự thêm cờ này khi có scfw_socket_errno.h; SDK 11/2024 bỏ.
# Giữ lại ở đây để mã errno socket như cũ (và không cảnh báo "EDEADLK redefined"...).
if (EXISTS ${CMAKE_SOURCE_DIR}/sc_lib/${SCMODULE}/inc/scfw_socket_errno.h)
    ADD_DEFINITIONS(-DUSE_LWIP_ERRNO)
endif()

# NASA_ROOT_DIR: cho #include "hal/hal_xxx.h"; hal/: cho #include "hal_xxx.h" trong port;
# NASA_PORT_DIR: cho #include "board_pins.h" (app_config.h) và "port_sdk.h"
INCLUDE_DIRECTORIES(
    ${NASA_APP_DIR}
    ${NASA_ROOT_DIR}
    ${NASA_ROOT_DIR}/hal
    ${NASA_PORT_DIR}
)
