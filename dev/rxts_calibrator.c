/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#include <stdio.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>
#include <wrc.h>

#include "board.h"
#include "trace.h"
#include "syscon.h"
#include "endpoint.h"
#include "softpll_ng.h"
#include "wrc_ptp.h"
#include "eeprom.h"
#include "ptpd_netif.h"

/* New calibrator for the transition phase value. A major pain in the ass for
   the folks who frequently rebuild their gatewares. The idea is described
   below:
   - lock the PLL to the master
   - scan the whole phase shifter range
   - at each scanning step, generate a fake RX timestamp.
   - check if the rising edge counter is ahead of the falling edge counter
     (added a special bit for it in the TSU).
   - determine phases at which positive/negative transitions occur
   - transition phase value is in the middle between the rising and falling
     edges.
   
   This calibration procedure is fast enough to be run on slave nodes whenever
   the link goes up. For master mode, the core must be run at least once as a
   slave to calibrate itself and store the current transition phase value in
   the EEPROM.
*/

/* how finely we scan the phase shift range to determine where we have the bit
 * flip */
#define CAL_SCAN_STEP 100

/* deglitcher threshold (to remove 1->0->1 flip bit glitches that might occur
   due to jitter) */
#define CAL_DEGLITCH_THRESHOLD 5

/* we scan at least one clock period to look for rising->falling edge transition
   plus some headroom */
#define CAL_SCAN_RANGE (REF_CLOCK_PERIOD_PS + \
		(3 * CAL_DEGLITCH_THRESHOLD * CAL_SCAN_STEP))

#define TD_WAIT_INACTIVE	0
#define TD_GOT_TRANSITION	1
#define TD_DONE			2

/* state of transition detector */
struct trans_detect_state {
	int prev_val;
	int sample_count;
	int state;
	int trans_phase;
};

/* finds the transition in the value of flip_bit and returns phase associated
   with it. If no transition phase has been found yet, returns 0. Non-zero
   polarity means we are looking for positive transitions, 0 - negative
   transitions */
static int lookup_transition(struct trans_detect_state *state, int flip_bit,
			     int phase, int polarity)
{
	if (polarity)
		polarity = 1;

	switch (state->state) {
	case TD_WAIT_INACTIVE:
		/* first, wait until we have at least CAL_DEGLITCH_THRESHOLD of
		   inactive state samples */
		if (flip_bit != polarity)
			state->sample_count++;
		else
			state->sample_count = 0;

		if (state->sample_count >= CAL_DEGLITCH_THRESHOLD) {
			state->state = TD_GOT_TRANSITION;
			state->sample_count = 0;
		}

		break;

	case TD_GOT_TRANSITION:
		if (flip_bit != polarity)
			state->sample_count = 0;
		else {
			state->sample_count++;
			if (state->sample_count >= CAL_DEGLITCH_THRESHOLD) {
				state->state = TD_DONE;
				state->trans_phase =
				    phase -
				    CAL_DEGLITCH_THRESHOLD * CAL_SCAN_STEP;
			}
		}
		break;

	case TD_DONE:
		return 1;
		break;
	}
	return 0;
}

static struct trans_detect_state det_rising, det_falling;
static int cal_cur_phase;

/* Starts RX timestamper calibration process state machine. Invoked by
   ptpnetif's check lock function when the PLL has already locked, to avoid
   complicating the API of ptp-noposix/ppsi. */

void rxts_calibration_start()
{
	cal_cur_phase = 0;
	det_rising.prev_val = det_falling.prev_val = -1;
	det_rising.state = det_falling.state = TD_WAIT_INACTIVE;
	det_rising.sample_count = 0;
	det_falling.sample_count = 0;
	det_rising.trans_phase = 0;
	det_falling.trans_phase = 0;
	spll_set_phase_shift(0, 0);
}

/* Updates RX timestamper state machine. Non-zero return value means that
   calibration is done. */
int rxts_calibration_update(uint32_t *t24p_value)
{
	int32_t ttrans = 0;

	if (spll_shifter_busy(0))
		return 0;

	/* generate a fake RX timestamp and check if falling edge counter is
	   ahead of rising edge counter */
	int flip = ep_timestamper_cal_pulse();

	/* look for transitions (with deglitching) */
	lookup_transition(&det_rising, flip, cal_cur_phase, 1);
	lookup_transition(&det_falling, flip, cal_cur_phase, 0);

	if (cal_cur_phase >= CAL_SCAN_RANGE) {
		if (det_rising.state != TD_DONE || det_falling.state != TD_DONE) 
		{
			TRACE_DEV("RXTS calibration error.\n");
			return -1;
		}

		/* normalize */
		while (det_falling.trans_phase >= REF_CLOCK_PERIOD_PS)
			det_falling.trans_phase -= REF_CLOCK_PERIOD_PS;
		while (det_rising.trans_phase >= REF_CLOCK_PERIOD_PS)
			det_rising.trans_phase -= REF_CLOCK_PERIOD_PS;

		/* Use falling edge as second sample of rising edge */
		if (det_falling.trans_phase > det_rising.trans_phase)
			ttrans = det_falling.trans_phase - REF_CLOCK_PERIOD_PS/2;
		else if(det_falling.trans_phase < det_rising.trans_phase)
			ttrans = det_falling.trans_phase + REF_CLOCK_PERIOD_PS/2;
		ttrans += det_rising.trans_phase;
		ttrans /= 2;

		/*normalize ttrans*/
		if(ttrans < 0) ttrans += REF_CLOCK_PERIOD_PS;
		if(ttrans >= REF_CLOCK_PERIOD_PS) ttrans -= REF_CLOCK_PERIOD_PS;


		printf("RXTS calibration: R@%dps, F@%dps, transition@%dps\n",
			  det_rising.trans_phase, det_falling.trans_phase,
			  ttrans);

		*t24p_value = (uint32_t)ttrans;
		return 1;
	}

	cal_cur_phase += CAL_SCAN_STEP;

	spll_set_phase_shift(0, cal_cur_phase);

	return 0;
}

/* legacy function for 'calibration force' command */
int measure_t24p(uint32_t *value)
{
	int rv;
	pp_printf("Waiting for link...\n");
	while (!ep_link_up(NULL))
		timer_delay(100);

	spll_init(SPLL_MODE_SLAVE, 0, 1);
	pp_printf("Locking PLL...\n");
	while (!spll_check_lock(0))
		timer_delay(100);
	pp_printf("\n");

	pp_printf("Calibrating RX timestamper...\n");
	rxts_calibration_start();

	while (!(rv = rxts_calibration_update(value))) ;
	return rv;
}

/*SoftPLL must be locked prior calling this function*/
static int calib_t24p_slave(uint32_t *value)
{
	int rv;

	rxts_calibration_start();
	while (!(rv = rxts_calibration_update(value))) ;

	if (rv < 0) {
		pp_printf("Could not calibrate t24p, trying to read from EEPROM\n");
		if(eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR, value, 0) < 0) {
			pp_printf("Something went wrong while writing EEPROM\n");
			return -1;
		}

	}
	else {
		pp_printf("t24p value is %d ps, storing to EEPROM\n", *value);
		if(eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR, value, 1) < 0) {
			pp_printf("Something went wrong while writing EEPROM\n");
			return -1;
		}
	}

	return 0;
}

static int calib_t24p_master(uint32_t *value)
{
	int rv;

	rv = eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR, value, 0);
	if(rv < 0)
		pp_printf("Something went wrong while reading from EEPROM: %d\n", rv);
	else
		pp_printf("t24p read from EEPROM: %d ps\n", *value);

	return rv;
}

int calib_t24p(int mode, uint32_t *value)
{
	int ret;

	if (mode == WRC_MODE_SLAVE)
		ret = calib_t24p_slave(value);
	else
		ret = calib_t24p_master(value);

	//update phtrans value in socket struct
	ptpd_netif_set_phase_transition(*value);
	return ret;
}

/*
* Tom's timestamp linearity testing code begins here 
*
* Not for release, to stay in the t24_fix_tom branch forever
*/


/* sets the number of taps of the RX timestamp  strobe delay line. 
 Simulates a synthesis-to-synthesis delay variation over the whole range of t24p value */
static void set_delay_taps(int taps)
{
	int i;
	
	gpio_out(GPIO_PIN_SR_EN, 0);
	gpio_out(GPIO_PIN_SR_RST, 1);
	gpio_out(GPIO_PIN_SR_RST, 0);
	
	for(i=0;i<64;i++)
  {
    gpio_out(GPIO_PIN_SR_D, i==taps ? 0 : 1);
		gpio_out(GPIO_PIN_SR_EN, 1);
		gpio_out(GPIO_PIN_SR_EN, 0);
	}
}

static wr_socket_t *cal_socket;
static wr_sockaddr_t cal_addr;

static wr_timestamp_t ts_sub(wr_timestamp_t a, wr_timestamp_t b)
{
  wr_timestamp_t c;

  c.sec = 0;
  c.nsec = 0;

  c.phase = a.phase - b.phase;

  while(c.phase < 0)
  {
    c.phase+=1000;
    c.nsec--;
  }

  while(c.phase > 1000)
  {
    c.phase-=1000;
    c.nsec++;
  }


  c.nsec += a.nsec - b.nsec;

  while(c.nsec < 0)
  {
    c.nsec += 1000000000L;
    c.sec--;
  }
  
  while(c.nsec > 1000000000L)
  {
    c.nsec -= 1000000000L;
    c.sec++;
  }

  c.sec += a.sec - b.sec;

  return c;
}

struct cal_packet_t {
  int seq;
  int is_followup;
  wr_timestamp_t ts;
};

/* receives a packet with a maximum timeout of (tmo) milliseconds */
int rx_with_timeout(wr_socket_t *sock, void *buf, int size, int tmo, wr_timestamp_t *ts)
{
	wr_sockaddr_t addr;
	int tics = timer_get_tics(), n;
	do {
		update_rx_queues();
		n =	ptpd_netif_recvfrom(sock, &addr, buf, size, ts);
		if(timer_get_tics() > tics + tmo)
			return -1;
	} while(n <= 0);
	return 0;
}


static int seq= 0;

/* executes a single PTP-like clock offset measurement, t1 = master TX timestamp, t2 = slave RX timestamp  */
static void measure_offset(wr_timestamp_t *delta, wr_timestamp_t *t1, wr_timestamp_t *t2)
{
	wr_timestamp_t ts1;
	wr_sockaddr_t addr;
	uint8_t buf[256];
	struct cal_packet_t *cal = (struct cal_packet_t *)buf;
	
  uint8_t mcast[] = {0x01, 0x1b, 0x19, 0 , 0, 0};

	do {
		timer_delay(7);
	  addr.family = PTPD_SOCK_RAW_ETHERNET;
	  addr.ethertype = 0x88f7;
	  memcpy(addr.mac, mcast, 6);

		cal->seq = seq;
		cal->is_followup = 0;

	  /* send a request to the master */
		ptpd_netif_sendto(cal_socket,&addr, cal, sizeof(struct cal_packet_t), NULL);

		/* receive the pseudo-SYNC message, take its timestamp as t2 */
		if(rx_with_timeout(cal_socket, cal, sizeof(buf), 100, &ts1) < 0)
			continue;
		if (cal->is_followup)
			continue;

		/* receive the pseudo-FOLLOWUP message containing t1 of the previous SYNC msg */
		if(rx_with_timeout(cal_socket, cal, sizeof(buf), 100, NULL) < 0)
			continue;

		if(seq != cal->seq)
		{
			seq++;
			continue;
		}
		
		seq++;
			
		*t1 = cal->ts;
		*t2 = ts1;

		/* compute the offset */
		*delta = ts_sub(ts1, cal->ts);
		timer_delay(1);
	} while(0);
}

/* measures the clock offset for a given phase setpoint */
static int measure_offset_for_phase(int phase, wr_timestamp_t *delta, wr_timestamp_t *t1, wr_timestamp_t *t2)
{
			int rphase;
			
			spll_set_phase_shift(0, phase);
			
			while (spll_shifter_busy(0));
			
			/* make sure the phase tracker returns same phase value as the setpoint (avaraging
			   time constants are differnet for the MPLL shifter and ptrackers) */
			do {
	  		spll_read_ptracker(0, &rphase, NULL);            
			} while( abs(rphase - (phase % 8000)) > 10);

			/* do the measurement */
			measure_offset(delta, t1, t2);
			return 0;
}


/* checks for jumps in RX timestamps for a given value of t24p parameter.
   the error value is added to t24p to simualte the calibration error.  */
static int check_jumps(int tp, int error, int taps)
{
	int phase, rphase;
	int max_diff = 0;
	wr_timestamp_t delta0, delta, t1, t2;
  
  delta0.sec = delta0.nsec = delta0.phase = 0;
  t1=t2= delta ;
  
  /* tell the timestamping functions to use our value of t24p */
  ptpd_netif_set_phase_transition(tp+error);
  printf("MEAS TAPS %d T24P %d OFFSET %d\n", taps, tp, error);

	/* sweep a bunch of full clock cycles */
  for(phase = 0; phase < 30000; phase += 100)
  {
    wr_timestamp_t d_delta;
    measure_offset_for_phase(phase, &delta, &t1, &t2);
		spll_read_ptracker(0, &rphase, NULL);            

		/* check if the offsets follow the phase setpoint. If the difference (minus the initial value)
		   is larger than certain threshold, we have a jump */
    if(phase == 0)
      delta0 = delta;
    d_delta = ts_sub(delta, delta0);
    int64_t d_delta_ps = (int64_t)d_delta.sec * 1000000000000LL + (int64_t)d_delta.nsec * 1000LL + d_delta.phase;
    int diff = abs(d_delta_ps - phase);
    if(diff > max_diff)
     	max_diff = diff;
    /* dump some logging info */
    printf("PHASE %d RPHASE %d TSO %d T1 %d %d %d T2 %d %d %d %d %d DIFF %d\n", phase, rphase, (int)d_delta_ps, (int)t1.sec, t1.nsec, t1.phase, (int)t2.sec, t2.nsec, t2.phase, t2.raw_ahead, t2.raw_nsec, diff );
  }
 	printf("RESULT %s\n", max_diff < 100 ? "OK" : "FAIL");
 	return (max_diff >= 100);
}

/* Checks for timestamper jumps by scanning whole range of phase shifts
   and whole range of t24p values. */

void calib_t24p_full_sweep()
{
	int taps, value;
		
  uint8_t mcast[] = {0x01, 0x1b, 0x19, 0 , 0, 0};

  cal_addr.family = PTPD_SOCK_RAW_ETHERNET;
  cal_addr.ethertype = 0x88f7;
  memcpy(cal_addr.mac, mcast, 6);

  cal_socket = ptpd_netif_create_socket(PTPD_SOCK_RAW_ETHERNET, 0, &cal_addr);

  spll_init(SPLL_MODE_SLAVE, 0, 1);
	spll_enable_ptracker(0, 1);

	for(taps = 0; taps < 64; taps++)
	{
		set_delay_taps(taps);
	
		timer_delay(100);

		rxts_calibration_start();
		while(rxts_calibration_update((uint32_t *)&value) <= 0);
		ptpd_netif_set_phase_transition(value);

		check_jumps( value, 0, taps );				
	}		
}

