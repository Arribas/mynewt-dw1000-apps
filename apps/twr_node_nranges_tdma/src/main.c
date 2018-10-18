/**
 * Copyright (C) 2017-2018, Decawave Limited, All Rights Reserved
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_bsp.h"
#ifdef ARCH_sim
#include "mcu/mcu_sim.h"
#endif

#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_hal.h>
#include <dw1000/dw1000_phy.h>
#include <dw1000/dw1000_mac.h>
#include <dw1000/dw1000_rng.h>
#include <dw1000/dw1000_ftypes.h>
#include <tdma/dw1000_tdma.h>
#include <ccp/dw1000_ccp.h>

#if MYNEWT_VAL(DW1000_LWIP)
#include <dw1000/dw1000_lwip.h>
#endif
#if MYNEWT_VAL(DW1000_PAN)
#include <dw1000/dw1000_pan.h>
#endif
#if MYNEWT_VAL(N_RANGES_NPLUS_TWO_MSGS)
#include <nranges/dw1000_nranges.h>
dw1000_nranges_instance_t *nranges_instance = NULL;
#endif
#if MYNEWT_VAL(TIMESCALE)
#include <timescale/timescale.h> 
#endif
#include <clkcal/clkcal.h>

#define VERBOSE0
#define NSLOTS MYNEWT_VAL(TDMA_NSLOTS)
static uint16_t g_slot[NSLOTS] = {0};
#define VERBOSE0
static dw1000_rng_config_t rng_config = {
    .tx_holdoff_delay = 0x0300,         // Send Time delay in usec.
    .rx_timeout_period = 0x1,         // Receive response timeout in usec
    .tx_guard_delay = 0x0200
};

#if MYNEWT_VAL(DW1000_PAN)
static dw1000_pan_config_t pan_config = {
    .tx_holdoff_delay = 0x0C00,         // Send Time delay in usec.
    .rx_timeout_period = 0x8000         // Receive response timeout in usec.
};
#endif

static nrng_frame_t twr[] = {
    [0] = {
        .fctrl = FCNTL_IEEE_N_RANGES_16,                // frame control (0x8841 to indicate a data frame using 16-bit addressing).
        .PANID = 0xDECA,                // PAN ID (0xDECA)
        .code = DWT_TWR_INVALID
    },
    [1] = {
        .fctrl = FCNTL_IEEE_N_RANGES_16,                // frame control (0x8841 to indicate a data frame using 16-bit addressing).
        .PANID = 0xDECA,                // PAN ID (0xDECA)
        .code = DWT_TWR_INVALID
    },
    [2] = {
        .fctrl = FCNTL_IEEE_N_RANGES_16,                // frame control (0x8841 to indicate a data frame using 16-bit addressing).
        .PANID = 0xDECA,                 // PAN ID (0xDECA)
        .code = DWT_TWR_INVALID
    },
    [3] = {
        .fctrl = FCNTL_IEEE_N_RANGES_16,                // frame control (0x8841 to indicate a data frame using 16-bit addressing).
        .PANID = 0xDECA,                 // PAN ID (0xDECA)
        .code = DWT_TWR_INVALID,
    }
};

void print_frame(const char * name, nrng_frame_t *twr ){
    printf("%s{\n\tfctrl:0x%04X,\n", name, twr->fctrl);
    printf("\tseq_num:0x%02X,\n", twr->seq_num);
    printf("\tPANID:0x%04X,\n", twr->PANID);
    printf("\tdst_address:0x%04X,\n", twr->dst_address);
    printf("\tsrc_address:0x%04X,\n", twr->src_address);
    printf("\tcode:0x%04X,\n", twr->code);
    printf("\treception_timestamp:0x%08lX,\n", twr->reception_timestamp);
    printf("\ttransmission_timestamp:0x%08lX,\n", twr->transmission_timestamp);
    printf("\trequest_timestamp:0x%08lX,\n", twr->request_timestamp);
    printf("\tresponse_timestamp:0x%08lX\n}\n", twr->response_timestamp);
}

static void nrange_complete_cb(struct os_event *ev) {
    assert(ev != NULL);
    assert(ev->ev_arg != NULL);

    hal_gpio_toggle(LED_BLINK_PIN);
    dw1000_dev_instance_t * inst = (dw1000_dev_instance_t *)ev->ev_arg;
    dw1000_nranges_instance_t *nranges = nranges_instance;

    nrng_frame_t * previous_frame = nranges->frames[(nranges->idx-1)%(nranges->nframes/FRAMES_PER_RANGE)][FIRST_FRAME_IDX];
    nrng_frame_t * frame = nranges->frames[(nranges->idx)%(nranges->nframes/FRAMES_PER_RANGE)][SECOND_FRAME_IDX];

    if (inst->status.start_rx_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\": \"start_rx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    if (inst->status.start_tx_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\":\"start_tx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    if (inst->status.rx_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\":\"rx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    if (inst->status.rx_timeout_error)
        printf("{\"utime\": %lu,\"timer_ev_cb\":\"rx_timeout_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));


    if (frame->code == DWT_DS_TWR_NRNG_FINAL || frame->code == DWT_DS_TWR_NRNG_EXT_FINAL){
        previous_frame = previous_frame;
        uint32_t time_of_flight = (uint32_t) dw1000_nranges_twr_to_tof_frames(previous_frame, frame);
        float range = dw1000_rng_tof_to_meters(dw1000_nranges_twr_to_tof_frames(previous_frame, frame));
        float rssi = dw1000_get_rssi(inst);
        //print_frame("1st=", previous_frame);
        //print_frame("2nd=", frame);
        frame->code = DWT_DS_TWR_END;
            printf("{\"utime\": %lu,\"tof\": %lu,\"range\": %lu,\"res_req\": %lX,"
                   " \"rec_tra\": %lX, \"rssi\": %d}\n",
            os_cputime_ticks_to_usecs(os_cputime_get32()),
            time_of_flight,
            (uint32_t)(range * 1000),
            (frame->response_timestamp - frame->request_timestamp),
            (frame->transmission_timestamp - frame->reception_timestamp),
            (int)(rssi)
	    );
    }
}

/*! 
 * @fn superres_complete_cb(dw1000_dev_instance_t * inst)
 *
 * @brief This callback is in the interrupt context and is uses to schedule an pdoa_complete event on the default event queue.  
 * Processing should be kept to a minimum giving the context. All algorithms should be deferred to a thread on an event queue. 
 * In this example all postprocessing is performed in the pdoa_ev_cb.
 * input parameters
 * @param inst - dw1000_dev_instance_t * 
 *
 * output parameters
 *
 * returns none 
 */
/* The timer callout */
static struct os_callout slot_callout;

static void complete_cb(struct _dw1000_dev_instance_t * inst){
    if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return;
    }
    os_callout_init(&slot_callout, os_eventq_dflt_get(), nrange_complete_cb, inst);
    os_eventq_put(os_eventq_dflt_get(), &slot_callout.c_ev);
    return;
}


/*! 
 * @fn slot_timer_cb(struct os_event * ev)
 *
 * @brief In this example this timer callback is used to start_rx.
 *
 * input parameters
 * @param inst - struct os_event *  
 *
 * output parameters
 *
 * returns none 
 */

    
static void 
slot_timer_cb(struct os_event * ev){
    assert(ev);

    tdma_slot_t * slot = (tdma_slot_t *) ev->ev_arg;
    tdma_instance_t * tdma = slot->parent;
    dw1000_dev_instance_t * inst = tdma->parent;
    dw1000_ccp_instance_t * ccp = inst->ccp;
    uint16_t idx = slot->idx;

#if MYNEWT_VAL(CLOCK_CALIBRATION_ENABLED)
    clkcal_instance_t * clk = ccp->clkcal;
    uint64_t dx_time = (ccp->epoch + (uint64_t) roundf(clk->skew * (double)((idx * (uint64_t)tdma->period << 16)/tdma->nslots)));
#else
    uint64_t dx_time = (ccp->epoch + (uint64_t) ((idx * ((uint64_t)tdma->period << 16)/tdma->nslots)));
#endif
    //Note: Time is referenced to the Rmarker symbol, to it is necessary to advance the rxtime by the SHR_duration such that the preamble is received.
    dx_time = (dx_time - ((uint64_t)ceilf(dw1000_usecs_to_dwt_usecs(dw1000_phy_SHR_duration(&inst->attrib))) << 16)) & 0xFFFFFFFE00UL;

    dw1000_set_delay_start(inst, dx_time);
    uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_request_frame_t))
                            + rng_config.tx_holdoff_delay
                            + rng_config.tx_guard_delay;         // Remote side turn arroud time. 
    dw1000_set_rx_timeout(inst, timeout);

    dw1000_set_on_error_continue(inst, true);
    if(dw1000_start_rx(inst).start_rx_error){
         printf("{\"utime\": %lu,\"msg\": \"slot_timer_cb:start_rx_error\"}\n",os_cputime_ticks_to_usecs(os_cputime_get32()));
    }

#ifdef VERBOSE
    uint32_t utime = os_cputime_ticks_to_usecs(os_cputime_get32());
    printf("{\"utime\": %lu,\"slot\": %d,\"dx_time\": %llu}\n",utime, idx, dx_time);
#endif
}


#if MYNEWT_VAL(N_RANGES_NPLUS_TWO_MSGS)
void dw1000_nranges_pkg_init(void)
{
    dw1000_dev_instance_t * inst = hal_dw1000_inst(0);
    nranges_instance = dw1000_nranges_init(inst, DWT_NRNG_RESPONDER, sizeof(twr)/sizeof(nrng_frame_t), 0);
    dw1000_nrng_set_frames(inst, twr, sizeof(twr)/sizeof(nrng_frame_t));
}
#endif

int main(int argc, char **argv){
    int rc;

    sysinit();
    hal_gpio_init_out(LED_BLINK_PIN, 1);
    hal_gpio_init_out(LED_1, 1);
    hal_gpio_init_out(LED_3, 1);

    dw1000_dev_instance_t * inst = hal_dw1000_inst(0);

    inst->PANID = 0xDECA;
    inst->my_short_address = MYNEWT_VAL(DEVICE_ID);
    inst->slot_id = MYNEWT_VAL(SLOT_ID);
    inst->my_long_address = ((uint64_t) inst->device_id << 32) + inst->partID;

    dw1000_set_panid(inst,inst->PANID);
    dw1000_set_address16(inst,inst->my_short_address);
    dw1000_mac_init(inst, NULL);
    dw1000_rng_init(inst, &rng_config, 0);
    //dw1000_rng_set_frames(inst, twr, sizeof(twr)/sizeof(nrng_frame_t));
#if MYNEWT_VAL(DW1000_PAN)
    dw1000_pan_init(inst, &pan_config);
    dw1000_pan_start(inst, DWT_NONBLOCKING); // Don't block on the eventq_dflt
    while(inst->pan->status.valid != true){
        os_eventq_run(os_eventq_dflt_get());
        os_cputime_delay_usecs(5000);
    }
#endif
    printf("device_id = 0x%lX\n",inst->device_id);
    printf("PANID = 0x%X\n",inst->PANID);
    printf("DeviceID = 0x%X\n",inst->my_short_address);
    printf("partID = 0x%lX\n",inst->partID);
    printf("lotID = 0x%lX\n",inst->lotID);
    printf("xtal_trim = 0x%X\n",inst->xtal_trim);
    printf("frame_duration = %d usec\n",dw1000_phy_frame_duration(&inst->attrib, sizeof(twr_frame_final_t)));
    printf("holdoff = %d usec\n",(uint16_t)ceilf(dw1000_dwt_usecs_to_usecs(rng_config.tx_holdoff_delay))); 

    dw1000_rng_set_complete_cb(inst, complete_cb);

#if MYNEWT_VAL(DW1000_CCP_ENABLED)
    dw1000_ccp_init(inst, 2, MYNEWT_VAL(UUID_CCP_MASTER));
    if(inst->slot_id ==1)
        dw1000_ccp_start(inst, CCP_ROLE_MASTER);
    else
        dw1000_ccp_start(inst, CCP_ROLE_SLAVE);
    
#endif

    for (uint16_t i = 0; i < sizeof(g_slot)/sizeof(uint16_t); i++)
        g_slot[i] = i;

    tdma_init(inst, MYNEWT_VAL(TDMA_PERIOD), NSLOTS);
    for (uint16_t i = 1; i < NSLOTS; i++)
        tdma_assign_slot(inst->tdma, slot_timer_cb,  g_slot[i], &g_slot[i]);


    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    assert(0);
    return rc;
}

