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
#include <dw1000/dw1000_ftypes.h>
#include <tdma/tdma.h>
#include <ccp/ccp.h>
#include <nranges/nranges.h>
#if MYNEWT_VAL(TIMESCALE)
#include <timescale/timescale.h> 
#endif
#include <clkcal/clkcal.h>

//#define VERBOSE 1
#define NSLOTS MYNEWT_VAL(TDMA_NSLOTS)
static uint16_t g_slot[NSLOTS] = {0};
#define VERBOSE0

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
    dw1000_nrng_instance_t *nranges = inst->nrng;

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
        uint32_t time_of_flight = (uint32_t) dw1000_nrng_twr_to_tof_frames(previous_frame, frame);
        float range = dw1000_nrng_tof_to_meters(dw1000_nrng_twr_to_tof_frames(previous_frame, frame));
        float rssi = dw1000_get_rssi(inst);
        //print_frame("1st=", previous_frame);
        //print_frame("2nd=", frame);
        frame->code = DWT_DS_TWR_NRNG_END;
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
static bool complete_cb(dw1000_dev_instance_t * inst, dw1000_mac_interface_t * cbs){
    if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    os_callout_init(&slot_callout, os_eventq_dflt_get(), nrange_complete_cb, inst);
    os_eventq_put(os_eventq_dflt_get(), &slot_callout.c_ev);
    return true;
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
                            + inst->nrng->config.tx_holdoff_delay
                            + inst->nrng->config.tx_guard_delay;         // Remote side turn arroud time. 
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

int main(int argc, char **argv){
    int rc;

    sysinit();
    hal_gpio_init_out(LED_BLINK_PIN, 1);
    hal_gpio_init_out(LED_1, 1);
    hal_gpio_init_out(LED_3, 1);

    dw1000_dev_instance_t * inst = hal_dw1000_inst(0);

    dw1000_mac_interface_t cbs = (dw1000_mac_interface_t){
        .id =  DW1000_APP0,
        .complete_cb = complete_cb
    };
    dw1000_mac_append_interface(inst, &cbs);

    printf("device_id = 0x%lX\n",inst->device_id);
    printf("PANID = 0x%X\n",inst->PANID);
    printf("DeviceID = 0x%X\n",inst->my_short_address);
    printf("partID = 0x%lX\n",inst->partID);
    printf("lotID = 0x%lX\n",inst->lotID);
    printf("xtal_trim = 0x%X\n",inst->xtal_trim);
    inst->slot_id = MYNEWT_VAL(SLOT_ID);
#if MYNEWT_VAL(CCP_ENABLED)
    if(inst->slot_id ==1)
        dw1000_ccp_start(inst, CCP_ROLE_MASTER);
    else
        dw1000_ccp_start(inst, CCP_ROLE_SLAVE);
#endif

    for (uint16_t i = 0; i < sizeof(g_slot)/sizeof(uint16_t); i++)
        g_slot[i] = i;

    for (uint16_t i = 1; i < NSLOTS; i++)
        tdma_assign_slot(inst->tdma, slot_timer_cb,  g_slot[i], &g_slot[i]);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    assert(0);
    return rc;
}

