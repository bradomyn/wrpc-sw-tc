#include <stdio.h>

#include "hw/si570_if_wb.h"
#include "hw/softpll_regs.h"
#include "hw/memlayout.h"
#include "hw/pps_gen_regs.h"
#include "i2c.h"
#include "irq.h"

static volatile struct SPLL_WB *SPLL;
static volatile struct PPSG_WB *PPSG;

#include "spll_defs.h"
#include "ptp-noposix/softpll/spll_common.h"
#include "ptp-noposix/softpll/spll_debug.h"
#include "ptp-noposix/softpll/spll_main_bangbang.h"

static volatile struct SI570_WB *si570;

#define SI57X_ADDR 0x55

static int aux_scl(int state)
{
	int value = si570->GPSR & SI570_GPSR_SCL;
	if(state)
		si570->GPSR = SI570_GPSR_SCL;
	else
		si570->GPCR = SI570_GPCR_SCL;
	return value;
}

static int aux_sda(int state)
{
	int value = si570->GPSR & SI570_GPSR_SDA;
	if(state)
		si570->GPSR = SI570_GPSR_SDA;
	else
		si570->GPCR = SI570_GPCR_SDA;
	return value;
}

static const struct i2c_bitbang_interface i2c_aux_bb = { aux_scl, aux_sda };

void si57x_init()
{

	SPLL = (volatile struct SPLL_WB *) BASE_SOFTPLL;
 	si570 = (volatile struct SI570_WB *) BASE_AUX;
 	
 	printf("si570 @ 0x%x\n", si570);
 	mi2c_register_interface(WRPC_AUX_I2C, &i2c_aux_bb);
 	mi2c_scan(WRPC_AUX_I2C);
}

uint8_t si57x_read_reg(uint8_t reg_addr)
{
	uint8_t rv;
 	mi2c_start(WRPC_AUX_I2C);
 	mi2c_put_byte(WRPC_AUX_I2C, SI57X_ADDR << 1);
	mi2c_put_byte(WRPC_AUX_I2C, reg_addr);
 	mi2c_repeat_start(WRPC_AUX_I2C);
 	mi2c_put_byte(WRPC_AUX_I2C, (SI57X_ADDR << 1) | 1);
 	mi2c_get_byte(WRPC_AUX_I2C, &rv, 1);
 	mi2c_stop(WRPC_AUX_I2C);
 	return rv;
}

void si57x_write_reg(uint8_t reg_addr, uint8_t value)
{
 	mi2c_start(WRPC_AUX_I2C);
 	mi2c_put_byte(WRPC_AUX_I2C, SI57X_ADDR << 1);
	mi2c_put_byte(WRPC_AUX_I2C, reg_addr);
	mi2c_put_byte(WRPC_AUX_I2C, value);
 	mi2c_stop(WRPC_AUX_I2C);
}

uint64_t si57x_read_rfreq()
{
	return (((uint64_t) si57x_read_reg(12)) << 0) | 
		   (((uint64_t) si57x_read_reg(11)) << 8) | 
		   (((uint64_t) si57x_read_reg(10)) << 16) | 
		   (((uint64_t) si57x_read_reg(9)) << 24) | 
		   (((uint64_t) (si57x_read_reg(8) & 0x3f)) << 32);
}

int si57x_read_dividers(int *n1, int *hsdiv)
{
 	int r7, r8;
 	
 	r7 = si57x_read_reg(7);
 	r8 = si57x_read_reg(8);
 	*n1 = ((r7 & 0x1f) << 2) | ((r8 >> 6) & 0x3);
 	*hsdiv = (r7 >> 5) & 0x7;
 	
 	
}

int si57x_set_hsdiv(int hsdiv)
{
	si57x_write_reg(137, 0x10); //freeze DCO
	si57x_write_reg(7, (si57x_read_reg(7) & 0x1f) | (hsdiv << 5));
	si57x_write_reg(137, 0x0); //un-freeze DCO
}

uint8_t si57x_read_n1_lsb()
{
	return si57x_read_reg(8) >> 6;
}

void si57x_set_base_rfreq(uint64_t rfreq)
{
	si570->RFREQH = (uint32_t)(rfreq >> 32) | (si57x_read_n1_lsb() << 6);
	si570->RFREQL = (uint32_t)(rfreq & 0xffffffffULL);
}

void si57x_adjust_rfreq(int adjustment)
{
	SPLL->DAC_MAIN = SPLL_DAC_MAIN_VALUE_W(adjustment) | SPLL_DAC_MAIN_DAC_SEL_W(1);
}

volatile int count = 0;
volatile struct spll_main_bangbang_state auxpll;

void _irq_entry()
{
	volatile uint32_t trr;
	int src = -1, tag, i;

/* check if there are more tags in the FIFO */
	while(! (SPLL->TRR_CSR & SPLL_TRR_CSR_EMPTY))
	{
		trr = SPLL->TRR_R0;
		src = SPLL_TRR_R0_CHAN_ID_R(trr);
		tag = SPLL_TRR_R0_VALUE_R(trr);

/* execute the control algo */
		mpll_bangbang_update(&auxpll, tag, src);
		count++;
	}
	clear_irq();
}


void si57x_main()
{
	uint64_t rf;
	int hsdiv, n1;
 	volatile int dummy;
 	
 	printf("Si57x test start...\n");

	disable_irq();
 	
 	si57x_init();
 	si57x_write_reg(135, 0x80); // reset
 	timer_delay(300);
 	
 	rf=si57x_read_rfreq();
	printf("RF_H %08x RF_L %08x\n", (uint32_t)(rf >> 32), (uint32_t) rf);
	si57x_read_dividers(&n1, &hsdiv);
	printf("N1 = %d, HSDIV = %d\n", n1, hsdiv);

//	si57x_set_hsdiv(0);

	si57x_read_dividers(&n1, &hsdiv);
	printf("N1 = %d, HSDIV = %d\n", n1, hsdiv);
		
	si57x_set_base_rfreq(rf);
	timer_delay(10);


	n_chan_ref = SPLL_CSR_N_REF_R(SPLL->CSR);
	n_chan_out = SPLL_CSR_N_OUT_R(SPLL->CSR);
	
	printf("Ref channels: %d Out/BB channels: %d\n", n_chan_ref, n_chan_out);
	SPLL->DAC_HPLL = 32768;
	SPLL->DAC_MAIN = 32768;
	SPLL->CSR = 0 ;
	SPLL->OCER = 0;
	SPLL->RCER = 0;
	SPLL->ECCR = 0;
	SPLL->OCCR = 0;
	SPLL->DEGLITCH_THR = 1000;
    
    while(! (SPLL->TRR_CSR & SPLL_TRR_CSR_EMPTY)) dummy = SPLL->TRR_R0;

	SPLL->EIC_IER = 1;
//	SPLL->OCER |= 2;

	mpll_bangbang_init(&auxpll, 1, 0, 0);
	mpll_bangbang_start(&auxpll);
//mpll_bangbang_update( struct spll_main_bangbang_state *s, int tag, int source)
	printf("ch %d \n", auxpll.channel);

	enable_irq();
    
 	for(;;){
 	 printf("cnt %d\n", count);
 	 timer_delay(1000);
 	 }
}