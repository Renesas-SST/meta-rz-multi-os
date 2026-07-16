/**
 * @file    OpenAMP_RPMsg_cfg.h
 * @brief   OpenAMP configurations
 * @date    2020.10.27
 * @author  Copyright (c) 2020, eForce Co., Ltd. All rights reserved.
 * @license SPDX-License-Identifier: BSD-3-Clause
 *
 ****************************************************************************
 * @par     History
 *          - rev 1.0 (2020.10.27) Imada
 *            Initial version for RZ/G2.
 ****************************************************************************
 */

#ifndef OPENAMP_RPMSG_CFG_H_
#define OPENAMP_RPMSG_CFG_H_

// RPMSG config
#define APP_EPT_ADDR (0x0U)

// Memory region reserved between 0x43000000 - 0x437FFFFF for RPMSG
#define UC3_RPMSG_MEM_BASE (0x43000000U)
#define UC3_RPMSG_MEM_SIZE (0x00800000U)

#define VRING_SIZE (0x100000U)
#define VRING_SHM_SIZE (0x300000U)

#define CFG_RPMSG_SVCNO (0x2U)

// RPMSG channel #0
#define CFG_RPMSG_SVC_NAME0       "rpmsg-service-0"
#define CFG_VRING0_BASE0_PA       (0x43600000U)
#define CFG_VRING0_BASE0_VA       (CFG_VRING0_BASE0_PA + VA_OFFSET)
#define CFG_VRING1_BASE0_PA       (0x43650000U)
#define CFG_VRING1_BASE0_VA       (CFG_VRING1_BASE0_PA + VA_OFFSET)
#define CFG_VRING_SIZE0           (VRING_SIZE)
#define CFG_VRING_ALIGN0          (0x100U)
#define CFG_RPMSG_NUM_BUFS0       (512U)
#define CFG_VRING_SHM_BASE0_PA    (0x43700000U)
#define CFG_VRING_SHM_BASE0_VA    (CFG_VRING_SHM_BASE0_PA + VA_OFFSET)
#define CFG_VRING_SHM_SIZE0       (VRING_SHM_SIZE)
#define CFG_VRING_CTL_NAME0       "44000000.vring-ctl0-c1"
#define CFG_VRING_SHM_NAME0       "44200000.vring-shm0-c1"
#define VRING_NOTIFYID0           (0U)


// RPMSG channel #1
#define CFG_RPMSG_SVC_NAME1       "rpmsg-service-1"

#define CFG_VRING0_BASE1_PA       (0x43a00000U)
#define CFG_VRING0_BASE1_VA       (CFG_VRING0_BASE1_PA + VA_OFFSET)
#define CFG_VRING1_BASE1_PA       (0x43a50000U)
#define CFG_VRING1_BASE1_VA       (CFG_VRING1_BASE1_PA + VA_OFFSET)
#define CFG_VRING_SHM_BASE1_PA    (0x43b00000U)
#define CFG_VRING_SHM_BASE1_VA    (CFG_VRING_SHM_BASE1_PA + VA_OFFSET)
#define CFG_VRING_CTL_NAME1       "44100000.vring-ctl1-c1"
#define CFG_VRING_SHM_NAME1       "44500000.vring-shm1-c1"

#define CFG_VRING_SHM_SIZE1       (VRING_SHM_SIZE)
#define CFG_VRING_SIZE1           (VRING_SIZE)
#define CFG_VRING_ALIGN1          (0x100U)
#define CFG_RPMSG_NUM_BUFS1       (512U)
#define VRING_NOTIFYID1           (1U)

/* CM33 + CR8 core0 channel 0 */
#define CFG_VRING_CTL_NAME_C0_CH0   "43000000.vring-ctl0"     /* CM33  ch0 ctl */
#define CFG_VRING_SHM_NAME_C0_CH0   "43200000.vring-shm0"     /* CM33  ch0 shm */

/* CM33 + CR8 core0 channel 1 */
#define CFG_VRING_CTL_NAME_C0_CH1   "43100000.vring-ctl1"     /* CM33  ch1 ctl */
#define CFG_VRING_SHM_NAME_C0_CH1   "43500000.vring-shm1"     /* CM33  ch1 shm */

/* CR8_0 names */
#define CFG_VRING_CTL_NAME_C1_CH0  "43800000.vring-ctl0-c0"
#define CFG_VRING_SHM_NAME_C1_CH0  "43a00000.vring-shm0-c0"
#define CFG_VRING_CTL_NAME_C1_CH1  "43900000.vring-ctl1-c0"
#define CFG_VRING_SHM_NAME_C1_CH1  "43d00000.vring-shm1-c0"

#endif /* OPENAMP_RPMSG_CFG_H_ */
