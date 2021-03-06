/* arch/arm/mach-msm/lge/board-swift-misc.c
 * Copyright (C) 2009 LGE, Inc.
 * Author: miroslav_mm (c) myroslavmm@gmail.com
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/setup.h>
#include <asm/io.h>

#include <linux/types.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>

#include <mach/gpio.h>
#include <mach/vreg.h>
#include <mach/pmic.h>
#include <mach/msm_battery.h>
#include <mach/board.h>
#include <mach/msm_iomap.h>
#include <mach/rpc_server_handset.h>
#include <mach/board_lge.h>
#include <mach/msm_rpcrouter.h>

#include "board-swift.h"

static u32 swift_battery_capacity(u32 current_voltage);

static struct msm_psy_batt_pdata msm_psy_batt_data = {
	.voltage_min_design     = 3200,
	.voltage_max_design     = 4200,
	.avail_chg_sources      = AC_CHG | USB_CHG ,
	.batt_technology        = POWER_SUPPLY_TECHNOLOGY_LION,
	.calculate_capacity	= swift_battery_capacity,
};

static u32 swift_battery_capacity(u32 current_voltage) {

	u32 low_voltage   = msm_psy_batt_data.voltage_min_design;
	u32 high_voltage  = msm_psy_batt_data.voltage_max_design;
	u32 cap = 0 ;

	pr_info("Current Battary Voltage  = %d\n ",current_voltage);
	if (current_voltage >= 4190)
	  return 100;
	else if (current_voltage <= 3200)
	  return 0;
	else {
	    cap =  (current_voltage - low_voltage) * 100 / (high_voltage - low_voltage);
	    cap = cap +  (5 - cap % 5 );
	    return  cap;
	}
}

static struct platform_device msm_batt_device = {
	.name           = "msm-battery",
	.id         = -1,
	.dev.platform_data  = &msm_psy_batt_data,
};

/* ear sense driver */
static char *ear_state_string[] = {
	"0",
	"1",
};

enum {
	EAR_STATE_EJECT = 0,
	EAR_STATE_INJECT = 1,
};

enum {
	EAR_EJECT = 0,
	EAR_INJECT = 1,
};

struct rpc_snd_set_hook_mode_args {
	uint32_t mode;
	uint32_t cb_func;
	uint32_t client_data;
};

struct snd_set_hook_mode_msg {
	struct rpc_request_hdr hdr;
	struct rpc_snd_set_hook_mode_args args;
};

struct snd_set_hook_param_rep {
	struct rpc_reply_hdr hdr;
	uint32_t get_mode;
};

#define SND_SET_HOOK_MODE_PROC 75
#define RPC_SND_PROG 0x30000002
#define RPC_SND_VERS 0x00020001

static int swift_gpio_earsense_work_func(void) {

	int state;
	int gpio_value;
	struct snd_set_hook_param_rep hkrep;
	struct snd_set_hook_mode_msg hookmsg;
	int rc;

	struct msm_rpc_endpoint *ept = msm_rpc_connect_compatible(RPC_SND_PROG, RPC_SND_VERS, 0);
	if (IS_ERR(ept)) {
		rc = PTR_ERR(ept);
		ept = NULL;
		printk(KERN_ERR"failed to connect snd svc, error %d\n", rc);
	}
	
	hookmsg.args.cb_func = -1;
	hookmsg.args.client_data = 0;

	gpio_value = gpio_get_value(GPIO_EAR_SENSE);
	pr_info("%s: ear sense detected : %s\n", __func__,
			gpio_value?"injected":"ejected");
	if (gpio_value == EAR_EJECT) {
		state = EAR_STATE_EJECT;
		gpio_set_value(GPIO_HS_MIC_BIAS_EN, 0);
		hookmsg.args.mode = cpu_to_be32(0);
	} else {
		state = EAR_STATE_INJECT;
		gpio_set_value(GPIO_HS_MIC_BIAS_EN, 1);
		hookmsg.args.mode = cpu_to_be32(1);
	}

	if(ept) {
		rc = msm_rpc_call_reply(ept, SND_SET_HOOK_MODE_PROC, &hookmsg, sizeof(hookmsg),&hkrep, sizeof(hkrep), 5 * HZ);
		if (rc < 0){
			printk(KERN_ERR "%s:rpc err because of %d\n", __func__, rc);
		} else {
		    printk("send success\n");
		}
	 } else {
		printk(KERN_ERR "%s:ext_snd is NULL\n", __func__);
	 }
	 rc = msm_rpc_close(ept);
	 if (rc < 0)
		printk(KERN_ERR"msm_rpc_close failed\n");

	return state;
}

static char *swift_gpio_earsense_print_state(int state) {
	return ear_state_string[state];
}

static int swift_gpio_earsense_sysfs_store(const char *buf, size_t size) {

	int state;

	if (!strncmp(buf, "eject", size - 1))
		state = EAR_STATE_EJECT;
	else if (!strncmp(buf, "inject", size - 1))
		state = EAR_STATE_INJECT;
	else
		return -EINVAL;

	return state;
}

static unsigned swift_earsense_gpios[] = {
	GPIO_EAR_SENSE,
};

static struct lge_gpio_switch_platform_data swift_earsense_data = {
	.name = "h2w",
	.gpios = swift_earsense_gpios,
	.num_gpios = ARRAY_SIZE(swift_earsense_gpios),
	.irqflags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
	.wakeup_flag = 1,
	.work_func = swift_gpio_earsense_work_func,
	.print_state = swift_gpio_earsense_print_state,
	.sysfs_store = swift_gpio_earsense_sysfs_store,
};

static struct platform_device swift_earsense_device = {
	.name   = "lge-switch-gpio",
	.id = 1,
	.dev = {
		.platform_data = &swift_earsense_data,
	},
};

static struct platform_device msm_device_pmic_leds = {
        .name = "pmic-leds",
        .id = -1,
        .dev.platform_data = "button-backlight",
};


/* misc platform devices */
static struct platform_device *swift_misc_devices[] __initdata = {
	&msm_batt_device,
	&swift_earsense_device,
	&msm_device_pmic_leds,
};

/* main interface */
void __init lge_add_misc_devices(void) {

	platform_add_devices(swift_misc_devices, ARRAY_SIZE(swift_misc_devices));
}
