/**
 * @file mock_hal_os.h
 * @brief Mock HAL OS for Host Unit Testing
 */
#ifndef MOCK_HAL_OS_H
#define MOCK_HAL_OS_H

#include "hal/hal_os.h"

void Mock_HAL_OS_SetTick(uint32_t tick);
void Mock_HAL_OS_AdvanceTicks(uint32_t count);

#endif /* MOCK_HAL_OS_H */
