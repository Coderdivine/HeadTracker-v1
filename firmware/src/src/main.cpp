 /* Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <string.h>
#include <power/power.h>
#include <nrfx_rtc.h>
#include <usb/usb_device.h>
#include <drivers/uart.h>
#include "nano33ble.h"
#include "io.h"

#ifdef DISABLE_SLEEP_STATES

static int disable_ds_1(const struct device *dev)
{
	ARG_UNUSED(dev);

    // Disable all power states
	pm_ctrl_disable_state(PM_STATE_SOFT_OFF);
    pm_ctrl_disable_state(PM_STATE_SUSPEND_TO_RAM);
    pm_ctrl_disable_state(PM_STATE_SUSPEND_TO_IDLE);
    pm_ctrl_disable_state(PM_STATE_SUSPEND_TO_DISK);
    pm_ctrl_disable_state(PM_STATE_RUNTIME_IDLE);
    pm_ctrl_disable_state(PM_STATE_STANDBY);

	return 0;
}
SYS_INIT(disable_ds_1, PRE_KERNEL_2, 0);

#endif

extern "C" void main(void)
{
    start(); // Call Our C++ Main
}

static int board_internal_sensors_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	io_init();

	NRF_PWM_Type * PWM[] = {
		NRF_PWM0, NRF_PWM1, NRF_PWM2, NRF_PWM3
	};

	for (unsigned int i = 0; i < (ARRAY_SIZE(PWM)); i++) {
		PWM[i]->ENABLE = 0;
		PWM[i]->PSEL.OUT[0] = 0xFFFFFFFFUL;
	}

	return 0;
}


SYS_INIT(board_internal_sensors_init, PRE_KERNEL_1, 32);
