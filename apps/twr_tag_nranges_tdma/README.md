# Ranging with n nodes using 2n+2 messages along with TDMA slotting.

## Overview
This Demo uses 3 tags and 7 nodes and a clock master for demonstrating tdma based slotting and nranges based ranging.

### Building target for 7 nodes
```no-highlight
newt target create node
newt target set node app=apps/twr_node_nranges_tdma
newt target set node bsp=@mynewt-dw1000-core/hw/bsp/dwm1001
newt target amend node syscfg=DEVICE_ID=0x1001:SLOT_ID=1
newt run node 0
newt target amend node syscfg=DEVICE_ID=0x1002:SLOT_ID=2
newt run node 0
newt target amend node syscfg=DEVICE_ID=0x1003:SLOT_ID=3
newt run node 0
newt target amend node syscfg=DEVICE_ID=0x1004:SLOT_ID=4
newt run node 0
newt target amend node syscfg=DEVICE_ID=0x1005:SLOT_ID=5
newt run node 0
newt target amend node syscfg=DEVICE_ID=0x1006:SLOT_ID=6
newt run node 0
newt target amend node syscfg=DEVICE_ID=0x1007:SLOT_ID=7
newt run node 0
```
### Building target for 3 tags
```
newt target create tag
newt target set tag app=apps/twr_tag_nranges_tdma
newt target set tag bsp=@mynewt-dw1000-core/hw/bsp/dwm1001
newt target set tag build_profile=debug
newt target amend tag syscfg=N_NODES=7:NODE_START_SLOT_ID=1:NODE_END_SLOT_ID=7

newt target amend tag syscfg=DEVICE_ID=0x1111:SLOT_ID=1
newt run tag 0
newt target amend tag syscfg=DEVICE_ID=0x2222:SLOT_ID=2
newt run tag 0
newt target amend tag syscfg=DEVICE_ID=0x3333:SLOT_ID=3
newt run tag 0

```
### Building target for Clock master
```no-highlight
newt target create clock
newt target set clock app=apps/clock_master
newt target set clock bsp=@mynewt-dw1000-core/hw/bsp/dwm1001
newt target set clock build_profile=debug
newt run clock 0
```

The number of nodes to range with can be configured by setting the **N_NODES** on tag app during build time,
   (ex: for 3 nodes, use this command while building tag app **newt target amend tag syscfg=N_NODES=3** )

**NOTE:** To monitor the logs from the multiple tags in same PC, Do the following changes in the apps/twr_tag_tdma_nranges/syscfg.yml
```
    CONSOLE_RTT: 0
    CONSOLE_UART: 1

```
  Rebuild the app and run again.
  Use Any serial Console app with 1000000 baudrate on PC to monitor the Logs.
