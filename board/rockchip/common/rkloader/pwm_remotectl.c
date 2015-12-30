/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 * Peter, Software Engineering, <superpeter.cai@gmail.com>.
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <linux/input.h>
#include <asm/io.h>
#include <asm/arch/rkplat.h>


/* PWM0 registers  */
#define PWM_REG_CNTR                    0x00  /* Counter Register */
#define PWM_REG_HPR                     0x04  /* Period Register */
#define PWM_REG_LPR                     0x08  /* Duty Cycle Register */
#define PWM_REG_CTRL                    0x0c  /* Control Register */
#define PWM_REG_INTSTS                  0x10  /* Interrupt Status Refister */
#define PWM_REG_INT_EN                  0x14  /* Interrupt Enable Refister */


/*REG_CTRL bits definitions*/
#define PWM_ENABLE                      (1 << 0)
#define PWM_DISABLE                     (0 << 0)

/*operation mode*/
#define PWM_MODE_ONESHOT                (0x00 << 1)
#define PWM_MODE_CONTINUMOUS            (0x01 << 1)
#define PWM_MODE_CAPTURE                (0x02 << 1)

/*duty cycle output polarity*/
#define PWM_DUTY_POSTIVE                (0x01 << 3)
#define PWM_DUTY_NEGATIVE               (0x00 << 3)

/*incative state output polarity*/
#define PWM_INACTIVE_POSTIVE            (0x01 << 4)
#define PWM_INACTIVE_NEGATIVE           (0x00 << 4)

/*clock source select*/
#define PWM_CLK_SCALE                   (1 << 9)
#define PWM_CLK_NON_SCALE               (0 << 9)

#define PWM_CH0_INT                     (1 << 0)
#define PWM_CH1_INT                     (1 << 1)
#define PWM_CH2_INT                     (1 << 2)
#define PWM_CH3_INT                     (1 << 3)

#define PWM_CH0_POL                     (1 << 8)
#define PWM_CH1_POL                     (1 << 9)
#define PWM_CH2_POL                     (1 << 10)
#define PWM_CH3_POL                     (1 << 11)

#define PWM_CH0_INT_ENABLE              (1 << 0)
#define PWM_CH0_INT_DISABLE             (0 << 0)

#define PWM_CH1_INT_ENABLE              (1 << 0)
#define PWM_CH1_INT_DISABLE             (0 << 1)

#define PWM_CH2_INT_ENABLE              (1 << 2)
#define PWM_CH2_INT_DISABLE             (0 << 2)

#define PWM_CH3_INT_ENABLE              (1 << 3)
#define PWM_CH3_INT_DISABLE             (0 << 3)

/*prescale factor*/
#define PWMCR_MIN_PRESCALE              0x00
#define PWMCR_MAX_PRESCALE              0x07

#define PWMDCR_MIN_DUTY	       	        0x0001
#define PWMDCR_MAX_DUTY                 0xFFFF

#define PWMPCR_MIN_PERIOD               0x0001
#define PWMPCR_MAX_PERIOD               0xFFFF

#define PWMPCR_MIN_PERIOD               0x0001
#define PWMPCR_MAX_PERIOD               0xFFFF


int g_ir_keycode = 0;
int g_ir_flag_signal = 0;


typedef enum _RMC_STATE {
	RMC_IDLE,
	RMC_PRELOAD,
	RMC_USERCODE,
	RMC_GETDATA,
	RMC_SEQUENCE
} eRMC_STATE;


struct rkxx_remotectl_suspend_data {
	int suspend_flag;
	int cnt;
	long scanTime[50];
};

struct rkxx_remote_key_table {
	int scanCode;
	int keyCode;		
};

struct rkxx_remotectl_button {	
	int usercode;
	int nbuttons;
	struct rkxx_remote_key_table *key_table;
};

struct rkxx_remotectl_drvdata {
	int state;
	int nbuttons;
	int result;
	unsigned long pre_time;
	unsigned long cur_time;
	long int pre_sec;
	long int cur_sec;
	long period;
	int scandata;
	int count;
	int keybdNum;
	int keycode;
	int press;
	int pre_press;
	unsigned int  base;
	struct rkxx_remotectl_suspend_data remotectl_suspend_data;
};


/********************************************************************
**                            ?궨??                                *
********************************************************************/
#define RK_PWM_TIME_PRE_MIN      19   /*4500*/
#define RK_PWM_TIME_PRE_MAX      30   /*5500*/           /*PreLoad 4.5+0.56 = 5.06ms*/

#define RK_PWM_TIME_BIT0_MIN     1  /*Bit0  1.125ms*/
#define RK_PWM_TIME_BIT0_MAX     5

#define RK_PWM_TIME_BIT1_MIN     7  /*Bit1  2.25ms*/
#define RK_PWM_TIME_BIT1_MAX     11

#define RK_PWM_TIME_RPT_MIN      0x215   /*101000*/
#define RK_PWM_TIME_RPT_MAX      0x235   /*103000*/         /*Repeat  105-2.81=102.19ms*/  //110-9-2.25-0.56=98.19ms

#define RK_PWM_TIME_SEQ1_MIN     2   /*2650*/
#define RK_PWM_TIME_SEQ1_MAX     0x20   /*3000*/           /*sequence  2.25+0.56=2.81ms*/ //11.25ms

#define RK_PWM_TIME_SEQ2_MIN     0xEE   /*101000*/
#define RK_PWM_TIME_SEQ2_MAX     0x120   /*103000*/         /*Repeat  105-2.81=102.19ms*/  //110-9-2.25-0.56=98.19ms


static struct rkxx_remotectl_drvdata data = {0};
static struct rkxx_remotectl_drvdata *ddata = NULL;


static struct rkxx_remote_key_table remote_key_table_geekbox_ff00[] = {
	{0xeb, KEY_POWER}, 
	{0xec, KEY_MENU},
	{0xfc, KEY_UP},
	{0xfd, KEY_LEFT},
	{0xf1, KEY_REPLY},
	{0xe5, KEY_RIGHT},
	{0xf8, KEY_DOWN},
	{0xa7, KEY_VOLUMEDOWN},
	{0xa3, 388},
	{0xf4, KEY_VOLUMEUP},
	{0xfe, KEY_BACK},
	{0xb7, KEY_HOME},
};


static struct rkxx_remotectl_button remotectl_button[] = {
	{
		.usercode = 0xff00,
		.nbuttons =  29,
		.key_table = &remote_key_table_geekbox_ff00[0],
	},
};

static int remotectl_keybdNum_lookup(struct rkxx_remotectl_drvdata *ddata)
{	
	int i;	

	for (i = 0; i < sizeof(remotectl_button)/sizeof(struct rkxx_remotectl_button); i++){		
		if (remotectl_button[i].usercode == (ddata->scandata&0xFFFF)){			
			ddata->keybdNum = i;
			return 1;
		}
	}

	return 0;
}

static int remotectl_keycode_lookup(struct rkxx_remotectl_drvdata *ddata)
{	
	int i;	
	unsigned char keyData = (unsigned char)((ddata->scandata >> 8) & 0xff);

	for (i = 0; i < remotectl_button[ddata->keybdNum].nbuttons; i++){
		if (remotectl_button[ddata->keybdNum].key_table[i].scanCode == keyData){			
			ddata->keycode = remotectl_button[ddata->keybdNum].key_table[i].keyCode;
			return 1;
		}
	}

	return 0;
}

int remotectl_do_something_readtime(void)
{
	int val;

	val = readl(ddata->base + PWM_REG_INTSTS);
	if (val&PWM_CH3_INT) {
		if (val & PWM_CH3_POL) {
			val = readl(ddata->base + PWM_REG_HPR);
			ddata->period = val;
			writel(PWM_CH3_INT, ddata->base + PWM_REG_INTSTS);
			//printf("hpr=0x%x \n", val);
			return 1;
		} else {
			val = readl(ddata->base + PWM_REG_LPR);
			writel(PWM_CH3_INT, ddata->base + PWM_REG_INTSTS);
			//printf("lpr=0x%x \n", val);
		}
	}

	return 0;
}


int remotectl_do_something(void)
{
	if(remotectl_do_something_readtime() == 0)
	{
		return 0;
	}

	switch (ddata->state) {
		case RMC_IDLE: {
			;
			break;
		}

		case RMC_PRELOAD: {
			g_ir_flag_signal = 1;
			if ((RK_PWM_TIME_PRE_MIN < ddata->period) && 
					(ddata->period < RK_PWM_TIME_PRE_MAX)){
			
				ddata->scandata = 0;
				ddata->count = 0;
				ddata->state = RMC_USERCODE;
			} else {
				ddata->state = RMC_PRELOAD;
			}

			break;
		}

		case RMC_USERCODE: {
			g_ir_flag_signal = 1;
			if ((RK_PWM_TIME_BIT1_MIN < ddata->period) && (ddata->period < RK_PWM_TIME_BIT1_MAX))
				ddata->scandata |= (0x01 << ddata->count);
			ddata->count++;
			if (ddata->count == 0x10) {//16 bit user code
				printf("remote usercode1 = 0x%x\n",((ddata->scandata)&0xFFFF));
				if (remotectl_keybdNum_lookup(ddata)) {
					ddata->state = RMC_GETDATA;
					ddata->scandata = 0;
					ddata->count = 0;
				} else {
					ddata->state = RMC_PRELOAD;
				}
			}

			break;
		}

		case RMC_GETDATA: {
			g_ir_flag_signal = 1;
			if ((RK_PWM_TIME_BIT1_MIN < ddata->period) && (ddata->period < RK_PWM_TIME_BIT1_MAX))
				ddata->scandata |= (0x01<<ddata->count);
			ddata->count++;
			if (ddata->count < 0x10)
				return 0;
			printf("RMC_GETDATA=%x\n", (ddata->scandata>>8));
			if ((ddata->scandata&0x0ff) ==
					((~ddata->scandata >> 8) & 0x0ff)) {
				if (remotectl_keycode_lookup(ddata)) {
					ddata->press = 1;
					ddata->state = RMC_PRELOAD;
					g_ir_keycode = ddata->keycode;
					printf("g_ir_keycode = [%d] \n", g_ir_keycode);
				} else {
					ddata->state = RMC_PRELOAD;
				}
			} else {
				ddata->state = RMC_PRELOAD;
			}

			break;
		}
	
		default:
			break;
	}

	return 0;
}



static int rk_pwm_remotectl_hw_init(struct rkxx_remotectl_drvdata *ddata)
{
	int val;

	val = readl(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_DISABLE;
	writel(val, ddata->base + PWM_REG_CTRL);
	
	val = readl(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFF9) | PWM_MODE_CAPTURE;
	writel(val, ddata->base + PWM_REG_CTRL);
	
	val = readl(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFF008DFF) | 0x00646200;
	writel(val, ddata->base + PWM_REG_CTRL);
	
	val = readl(ddata->base + PWM_REG_INT_EN);
	val = (val & 0xFFFFFFF7) | PWM_CH3_INT_ENABLE;
	writel(val, ddata->base + PWM_REG_INT_EN);
	
	val = readl(ddata->base + PWM_REG_CTRL);
	val = (val & 0xFFFFFFFE) | PWM_ENABLE;
	writel(val, ddata->base + PWM_REG_CTRL);
	return 0;
}

#if defined(CONFIG_RKCHIP_RK3036) || defined(CONFIG_RKCHIP_RK3126) || defined(CONFIG_RKCHIP_RK3128)
	#define PWM_REMOTE_BASE		RKIO_PWM_PHYS
#elif defined(CONFIG_RKCHIP_RK3288) || defined(CONFIG_RKCHIP_RK3368)
	#define PWM_REMOTE_BASE		RKIO_RK_PWM_PHYS
#else
	#error "PLS config rk chip for pwm remote base."
#endif

void remotectlInitInDriver(void)
{
	printf("remotectl v0.1\n");
	ddata = &data;
	ddata->state = RMC_PRELOAD;
	ddata->base = PWM_REMOTE_BASE + 0x30; // PWM3

	rk_pwm_remotectl_hw_init(ddata);
}

