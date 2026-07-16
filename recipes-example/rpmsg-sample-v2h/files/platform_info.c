/*
 * Copyright (c) 2014, Mentor Graphics Corporation
 * All rights reserved.
 * Copyright (c) 2017 Xilinx, Inc.
 * Copyright (c) 2020, eForce Co., Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**************************************************************************
 * FILE NAME
 *
 *       platform_info.c
 *
 * DESCRIPTION
 *
 *       Platform-specific data + APIs for OpenAMP on RZ/V2H multi-OS.
 *
 *       Supports three remote targets sharing a single mailbox unit
 *       (10480000.mbox-uio) via the MHU-B peripheral:
 *
 *         UIO_RECEIVER1 (mbx_id=1) -> CM33
 *         UIO_RECEIVER2 (mbx_id=2) -> CR8 core0
 *         UIO_RECEIVER3 (mbx_id=3) -> CR8 core1
 *
 *       Vring memory map:
 *         CM33:
 *           rsctbl @ 0x42f00000, mhu-shm @ 0x42f02000
 *           ch0 ctl 0x43000000, ch0 shm 0x43200000
 *           ch1 ctl 0x43100000, ch1 shm 0x43500000
 *
 *         CR8 core0:
 *           rsctbl @ 0x42f02000, mhu-shm @ 0x42f03000
 *           ch0 ctl 0x43800000, ch0 shm 0x43a00000
 *           ch1 ctl 0x43900000, ch1 shm 0x43d00000
 *
 *         CR8 core1:
 *           rsctbl @ 0x42f04000, mhu-shm @ 0x42f05000
 *           ch0 ctl 0x44000000, ch0 shm 0x44200000
 *           ch1 ctl 0x44100000, ch1 shm 0x44500000
 *
 **************************************************************************/

#include <metal/atomic.h>
#include <metal/assert.h>
#include <metal/device.h>
#include <metal/irq.h>
#include <metal/utilities.h>
#include <openamp/rpmsg_virtio.h>
#include "platform_info.h"
#include "rsc_table.h"
#ifdef __linux__
#include <sched.h>
#include <stddef.h>
#include <pthread.h>
#else /* uC3 */
#include <metal/sys.h>
#endif

#ifdef __linux__
#define _rproc_wait()  (sched_yield())
#endif

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     (_a > _b) ? _b : _a; })

/* Variables */
/* IPI dedicated task */
#ifndef __linux__ /* uC3 */
static ID ipi_tsk_id[CFG_RPMSG_SVCNO] = {0};
#endif

/** flag SIGINT or SIGTERM have been received */
extern int force_stop;

/** to stop execution until received an interrupt.  */
extern pthread_mutex_t mutex;
extern pthread_cond_t cond[MBX_CH_NUM];
extern pthread_key_t thkey;
extern int tindex;

struct mbx_channel chn_info[MBX_CH_NUM] = {
    {0, 0, 0                  },  /* UIO MBX (unused) */
    {5, 8, INT_MHU_RSP_CH8_NS },  /* CM33      -> msg=5, rsp=8 */
    {3, 6, INT_MHU_RSP_CH6_NS },  /* CR8 core0 -> msg=3, rsp=6 */
    {4, 7, INT_MHU_RSP_CH7_NS },  /* CR8 core1 -> msg=4, rsp=7 */
};

/** initialized on first send in rz_proc_notify() */
struct mhu_send_type send_type_info[MBX_CH_NUM] = {
    {MHU_SEND_TYPE_MSG},
};

/* IPI(MBX) information */
struct ipi_info ipi[UIO_MAX] = {
    {
        MBX_DEV_NAME, DEV_BUS_NAME,
        NULL, NULL,
        0, 0,
        {0, 0}, 0,
#ifdef __linux__
        ATOMIC_FLAG_INIT, 0,
#else
        {E_ID, E_ID},
#endif
    },
    {
        "receiver@10480100", DEV_BUS_NAME,
        NULL, NULL,
        0, 0,
        {0, 0}, 0,
#ifdef __linux__
        ATOMIC_FLAG_INIT, 0,
#else
        {E_ID, E_ID},
#endif
    },
    {
        "receiver@104800c0", DEV_BUS_NAME,
        NULL, NULL,
        0, 0,
        {0, 0}, 0,
#ifdef __linux__
        ATOMIC_FLAG_INIT, 0,
#else
        {E_ID, E_ID},
#endif
    },
    {
        "receiver@104800e0", DEV_BUS_NAME,
        NULL, NULL,
        0, 0,
        {0, 0}, 0,
#ifdef __linux__
        ATOMIC_FLAG_INIT, 0,
#else
        {E_ID, E_ID},
#endif
    },
};

/*
 * vring information — channel 0 slots.
 *
 *   vrinfo[0] = CM33:
 *               rsc  @ 0x42f00000
 *               ctl0 @ 0x43000000
 *               shm0 @ 0x43200000
 *
 *   vrinfo[1] = CR8 core0:
 *               rsc  @ 0x42f02000
 *               ctl0 @ 0x43800000
 *               shm0 @ 0x43a00000
 *
 *   vrinfo[2] = CR8 core1:
 *               rsc  @ 0x42f04000
 *               ctl0 @ 0x44000000
 *               shm0 @ 0x44200000
 */
struct vring_info vrinfo[3] = {
    {
        { /* rsc */
            CFG_RSCTBL_DEV_NAME,        /* "42f00000.rsctbl" */
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* ctl ch0 */
            CFG_VRING_CTL_NAME_C0_CH0,  /* "43000000.vring-ctl0" */
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* shm ch0 */
            CFG_VRING_SHM_NAME_C0_CH0,  /* "43200000.vring-shm0" */
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
    },
    {
        { /* rsc */
            "42f02000.rsctbl",
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* ctl ch0 */
            CFG_VRING_CTL_NAME_C1_CH0,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* shm ch0 */
            CFG_VRING_SHM_NAME_C1_CH0,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
    },
    {
        { /* rsc */
            "42f04000.rsctbl",
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* ctl ch0 */
            CFG_VRING_CTL_NAME0,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* shm ch0 */
            CFG_VRING_SHM_NAME0,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
    }
};

/*
 * Channel-1 slots (separate physical regions).
 *
 *   vrinfo_ch1[0] = CM33 ch1:
 *                   ctl1 @ 0x43100000
 *                   shm1 @ 0x43500000
 *
 *   vrinfo_ch1[1] = CR8 core0 ch1:
 *                   ctl1 @ 0x43900000
 *                   shm1 @ 0x43d00000
 *
 *   vrinfo_ch1[2] = CR8 core1 ch1:
 *                   ctl1 @ 0x44100000
 *                   shm1 @ 0x44500000
 *
 * Only ctl and shm are populated (rsc is unused for ch1 the
 * resource table is shared with ch0 in each rsctbl block).
 */
struct vring_info vrinfo_ch1[3] = {
    {   /* [0] CM33 ch1 */
        { /* rsc unused for ch1 */
            NULL, NULL, NULL, NULL, NULL,
        },
        { /* ctl ch1 */
            CFG_VRING_CTL_NAME_C0_CH1,  /* "43100000.vring-ctl1" */
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* shm ch1 */
            CFG_VRING_SHM_NAME_C0_CH1,  /* "43500000.vring-shm1" */
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
    },
    {   /* [1] CR8 core0 ch1 */
        { /* rsc unused for ch1 */
            NULL, NULL, NULL, NULL, NULL,
        },
        { /* ctl ch1 */
            CFG_VRING_CTL_NAME_C1_CH1,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* shm ch1 */
            CFG_VRING_SHM_NAME_C1_CH1,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
    },
    {   /* [2] CR8 core1 ch1 COMPLETE */
        { /* rsc unused for ch1 */
            NULL, NULL, NULL, NULL, NULL,
        },
        { /* ctl ch1 */
            CFG_VRING_CTL_NAME1,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
        { /* shm ch1 */
            CFG_VRING_SHM_NAME1,
            DEV_BUS_NAME, NULL, NULL, NULL,
        },
    }
};

/* shared memory (MHU shmem) information one per remote */
struct shm_info shm[3] = {
    {   
        SHM_DEV_NAME_C0,
        DEV_BUS_NAME,
        NULL, NULL, NULL,
    },
    {
        SHM_DEV_NAME_CR8_0,
        DEV_BUS_NAME,
        NULL, NULL, NULL,
    },
    {
        SHM_DEV_NAME_C1,
        DEV_BUS_NAME,
        NULL, NULL, NULL,
    },
};

/* processor operations at RZ/V2H. Defines notify + rproc mgmt ops. */
extern struct remoteproc_ops rz_proc_ops;

/* RPMsg virtio shared buffer pool */
static __thread struct rpmsg_virtio_shm_pool shpool;

#ifndef __linux__ /* uC3 */
static void start_ipi_task(void *platform);
#endif

#ifdef __linux__
static inline void virtio_clear_status(void *rsc_table)
{
    struct remote_resource_table *rt = (struct remote_resource_table *)rsc_table;
    rt->rpmsg_vdev.status = 0x0;
    return;
}
#endif

static struct remoteproc *
platform_create_proc(int proc_index, int rsc_index, int mbx_index)
{
    void *rsc_table;
    unsigned int rsc_size;
    int ret = 0;
    struct remoteproc *rproc_inst;
    struct remoteproc_priv *rproc_priv;
#ifdef __linux__
    metal_phys_addr_t pa = 0;
    metal_phys_addr_t rsc_base = 0;                 /* MUST init to 0 */
    struct metal_io_region *rsc_io = NULL;
#endif

    (void)rsc_index;

    /* Allocate and initialize remoteproc_priv */
    rproc_priv = metal_allocate_memory(sizeof(struct remoteproc_priv));
    if (!rproc_priv)
        return NULL;
    memset(rproc_priv, 0, sizeof(*rproc_priv));
    rproc_priv->notify_id  = (unsigned int)proc_index;
    rproc_priv->channel    = (unsigned int)proc_index;
    rproc_priv->mbx_chn_id = mbx_index;

    /* Allocate remoteproc */
    rproc_inst = metal_allocate_memory(sizeof(struct remoteproc));
    if (!rproc_inst)
        goto err1;
    memset(rproc_inst, 0, sizeof(*rproc_inst));

    /* remoteproc initialization */
    if (!remoteproc_init(rproc_inst, &rz_proc_ops, rproc_priv)) {
        LPRINTF("remoteproc_init failed for mbx_index=%d", mbx_index);
        goto err2;
    }

    /* Verify per-target rsctbl UIO region is available */
#ifdef __linux__
    {
        int rsc_slot = (mbx_index == UIO_RECEIVER1) ? 0 :
                       (mbx_index == UIO_RECEIVER2) ? 1 : 2;
        if (!rproc_priv->vr_info) {
            LPRINTF("ERROR: vr_info not initialized for mbx_index=%d", mbx_index);
            goto err2;
        }
        if (!rproc_priv->vr_info[rsc_slot].io) {
            LPRINTF("rsctbl not available for mbx_index=%d (core not kicked?)",
                    mbx_index);
            goto err2;
        }
    }
#endif

#ifdef __linux__
    rsc_size = sizeof(struct remote_resource_table);

    /* STEP 1: Select the per-target rsctbl base address FIRST. */
    switch (mbx_index) {
    case UIO_RECEIVER1:
        rsc_base = 0x42F00000UL;   /* CM33   */
        break;
    case UIO_RECEIVER2:
        rsc_base = 0x42F02000UL;   /* CR8_0  */
        break;
    case UIO_RECEIVER3:
        rsc_base = 0x42F04000UL;   /* CR8_1  */
        break;
    default:
        LPRINTF("Invalid mbx_index=%d", mbx_index);
        goto err2;
    }

    /* Safety net: rsc_base MUST be non-zero before proceeding. */
    if (rsc_base == 0) {
        LPRINTF("ERROR: rsc_base not set for mbx_index=%d", mbx_index);
        goto err2;
    }

    /* STEP 2: Compute the per-channel entry offset.
     * The CR8 firmware (src/rsc_table.c) publishes TWO entries
     * back-to-back: resources[0]=ch0, resources[1]=ch1.
     * proc_index (0 or 1) selects which entry to read. */
    pa = rsc_base + (metal_phys_addr_t)proc_index * rsc_size;

    LPRINTF("Mapping rsctbl for mbx=%d ch=%d -> pa=0x%lx (base=0x%lx, entry=%d, rsc_size=0x%x)",
            mbx_index, proc_index,
            (unsigned long)pa, (unsigned long)rsc_base,
            proc_index, rsc_size);

    /* STEP 3: Mmap it and capture the io region. */
    rsc_table = remoteproc_mmap(rproc_inst, &pa, NULL, rsc_size, 0, &rsc_io);
    if (!rsc_table) {
        LPRINTF("Failed to map the resource table for mbx_index=%d ch=%d",
                mbx_index, proc_index);
        goto err2;
    }

    /* Fallback: resolve io region explicitly if not returned. */
    if (!rsc_io)
        rsc_io = remoteproc_get_io_with_pa(rproc_inst, pa);
    if (!rsc_io) {
        LPRINTF("ERROR: could not resolve rsc_io for pa=0x%lx",
                (unsigned long)pa);
        goto err2;
    }

    /* Attach io region so OpenAMP's remoteproc_create_virtio() can use it. */
    rproc_inst->rsc_io = rsc_io;

    LPRINTF("rsctbl mbx=%d ch=%d pa=0x%lx rsc_io=%p rsc_table=%p",
            mbx_index, proc_index,
            (unsigned long)pa, rsc_io, rsc_table);
#else /* uC3 / FreeRTOS */
    rsc_table = get_resource_table(rsc_index, &rsc_size);
    rproc_inst->rsc_io = rproc_priv->vr_info[VRING_RSC].io;
#endif

    /* Parse the resource table */
    ret = remoteproc_set_rsc_table(rproc_inst, rsc_table, rsc_size);
    if (ret) {
        LPRINTF("Failed to initialize remoteproc for mbx_index=%d ch=%d: %d",
                mbx_index, proc_index, ret);
        goto err2;
    }

    /* Debug dump */
#ifdef __linux__
    {
        struct remote_resource_table *rt =
            (struct remote_resource_table *)rproc_inst->rsc_table;
        LPRINTF("RT ver=%u num=%u", rt->version, rt->num);
        LPRINTF("  vdev: notifyid=%u status=0x%x num_vrings=%u",
                rt->rpmsg_vdev.notifyid, rt->rpmsg_vdev.status,
                rt->rpmsg_vdev.num_of_vrings);
        LPRINTF("  vring0: da=0x%llx align=%u num=%u",
                (unsigned long long)rt->rpmsg_vring0.da,
                rt->rpmsg_vring0.align, rt->rpmsg_vring0.num);
        LPRINTF("  vring1: da=0x%llx align=%u num=%u",
                (unsigned long long)rt->rpmsg_vring1.da,
                rt->rpmsg_vring1.align, rt->rpmsg_vring1.num);
    }
#endif

    LPRINTF("Initialize remoteproc successfully.");
    return rproc_inst;

err2:
    if (rproc_inst->ops)
        (void)remoteproc_remove(rproc_inst);
    metal_free_memory(rproc_inst);
err1:
    metal_free_memory(rproc_priv);
    return NULL;
}

/*
 * shm_slot_for_target - return the vr_info[] index of the RPMsg shared-memory
 *                       buffer region for a given (mbx_id, channel) pair.
 *
 *   vr_info layout (set by init_memory_device):
 *     [0]  42f00000  CM33 rsctbl
 *     [1]  42f02000  CR8 core0 rsctbl
 *     [2]  43000000  CM33 ch0 ctl
 *     [3]  43200000  CM33 ch0 shm   ← CM33 ch0
 *     [4]  43800000  CR8 core0 ch0 ctl
 *     [5]  43a00000  CR8 core0 ch0 shm  ← CR8 core0 ch0
 *     [6]  43100000  CM33 ch1 ctl
 *     [7]  43500000  CM33 ch1 shm   ← CM33 ch1
 *     [8]  43900000  CR8 core0 ch1 ctl
 *     [9]  43d00000  CR8 core0 ch1 shm  ← CR8 core0 ch1
 *    [10]  42f01000  CM33 mhu-shm
 *    [11]  42f03000  CR8 core0 mhu-shm
 *    [12]  42f04000  CR8 core1 rsctbl
 *    [13]  44000000  CR8 core1 ch0 ctl
 *    [14]  44200000  CR8 core1 ch0 shm  ← CR8 core1 ch0
 *    [15]  42f05000  CR8 core1 mhu-shm
 *    [16]  44100000  CR8 core1 ch1 ctl
 *    [17]  44500000  CR8 core1 ch1 shm  ← CR8 core1 ch1
 */
static int shm_slot_for_target(int mbx_id, int channel)
{
    if (mbx_id == UIO_RECEIVER1) {
        return (channel == 0) ? 3 : 7;   /* CM33 ch0=3, ch1=7 */
    } else if (mbx_id == UIO_RECEIVER2) {
        return (channel == 0) ? 5 : 9;   /* CR8 core0 ch0=5, ch1=9 */
    } else { /* UIO_RECEIVER3 */
        return (channel == 0) ? 14 : 17; /* CR8 core1 ch0=14, ch1=17 */
    }
}

struct rpmsg_device *
platform_create_rpmsg_vdev(struct remoteproc *rproc, unsigned int vdev_index,
                           unsigned int role,
                           void (*rst_cb)(struct virtio_device *vdev),
                           rpmsg_ns_bind_cb ns_bind_cb)
{
    struct remoteproc_priv *prproc;
    struct rpmsg_virtio_device *rpmsg_vdev;
    struct virtio_device *vdev = NULL;
    struct metal_io_region *shbuf_io;
    metal_phys_addr_t pa;
#ifdef __linux__
    void *shbuf;
    size_t len;
#endif
    int ret;
    int shm_slot;

    prproc = rproc->priv;

    rpmsg_vdev = metal_allocate_memory(sizeof(*rpmsg_vdev));
    if (!rpmsg_vdev)
        return NULL;
    memset(rpmsg_vdev, 0, sizeof(*rpmsg_vdev));

    LPRINTF("creating remoteproc virtio sample app");
    vdev = remoteproc_create_virtio(rproc, (int)vdev_index, role, rst_cb);
    if (!vdev) {
        LPRINTF("failed remoteproc_create_virtio");
        goto err;
    }

    /* FIX: Add NULL check for vr_info before using it */
    if (!prproc || !prproc->vr_info) {
        LPRINTF("ERROR: prproc->vr_info is NULL");
        goto err;
    }

    shm_slot = shm_slot_for_target((int)prproc->mbx_chn_id, (int)prproc->channel);
    LPRINTF("shm_slot=%d for mbx_id=%u channel=%u", shm_slot, prproc->mbx_chn_id, prproc->channel);

    /* FIX: Add bounds checking for shm_slot */
    if (shm_slot < 0 || shm_slot >= VRING_MAX) {
        LPRINTF("ERROR: shm_slot %d out of bounds (max=%d)", shm_slot, VRING_MAX);
        goto err;
    }

    /*
     * Resolve the correct SHM slot for this target+channel combination.
     * prproc->notify_id  = channel (0 or 1)
     * prproc->mbx_chn_id = mailbox target (UIO_RECEIVER1/2/3)
     */
    if (!prproc->vr_info[shm_slot].io || !prproc->vr_info[shm_slot].mem) {
        LPRINTF("shm region for slot %d not available (core not kicked?)", shm_slot);
        goto err;
    }

    pa = metal_io_phys(prproc->vr_info[shm_slot].io, 0x0U);

    shbuf_io = remoteproc_get_io_with_pa(rproc, pa);
    if (!shbuf_io) {
        LPRINTF("failed remoteproc_get_io_with_pa");
        goto err;
    }

    /* Only RPMsg virtio master needs to initialize the shared buffers pool */
#ifdef __linux__
    LPRINTF("initializing rpmsg shared buffer pool");
    shbuf = metal_io_phys_to_virt(shbuf_io, pa);
    len = prproc->vr_info[shm_slot].mem->size;
    rpmsg_virtio_init_shm_pool(&shpool, shbuf, len);
#endif

    LPRINTF("initializing rpmsg vdev");
    /* RPMsg virtio slave can set shared buffers pool argument to NULL */
    ret = rpmsg_init_vdev(rpmsg_vdev, vdev, ns_bind_cb, shbuf_io, &shpool);
    if (ret) {
        LPRINTF("failed rpmsg_init_vdev");
        goto err;
    }

#ifndef __linux__ /* uC3 */
    start_ipi_task(rproc);
#endif

    return rpmsg_virtio_get_rpmsg_device(rpmsg_vdev);

err:
#ifdef __linux__
    if (rproc->rsc_table)
        virtio_clear_status(rproc->rsc_table);
#endif
    if (vdev)
        remoteproc_remove_virtio(rproc, vdev);
    metal_free_memory(rpmsg_vdev);
    return NULL;
}

/**
 * @fn thread_specific_ipi
 * @brief Get the UIO in use by a thread
 * @return ipi_info
 */
static struct ipi_info *thread_specific_ipi(void)
{
    int *idx = pthread_getspecific(thkey);
    struct ipi_info *pipi;

    if (!idx)
        return NULL;

    if ((*idx) < UIO_MAX) {
        pipi = &ipi[*idx];
    } else {
        pipi = NULL;
    }
    return pipi;
}

int platform_poll(struct remoteproc *rproc)
{
#ifdef __linux__
    unsigned int flags;
    int *idx = pthread_getspecific(thkey);
    struct ipi_info *pipi;

    pipi = thread_specific_ipi();
    if (!pipi) goto error_return;

    while (!force_stop) {
        flags = metal_irq_save_disable();
        if (!(atomic_flag_test_and_set(&pipi->sync))) {
            metal_irq_restore_enable(flags);
            remoteproc_get_notification(rproc, RSC_NOTIFY_ID_ANY);
            break;
        }
        metal_irq_restore_enable(flags);
        pthread_mutex_lock(&mutex);
        pthread_cond_wait(&cond[*idx], &mutex);
        pthread_mutex_unlock(&mutex);
    }
#else /* uC3 */
    (void) rproc;
#endif
error_return:
    return 0;
}

int platform_init(unsigned long proc_id, unsigned long rsc_id,
                  unsigned long mbx_id, struct remoteproc **platform)
{
    struct remoteproc *rproc;

    if (!platform) {
        LPRINTF("Failed to initialize platform,"
                "NULL pointer to store platform data.");
        return -EINVAL;
    }

    if ((proc_id >= RPVDEV_MAX_NUM) || (rsc_id >= RPVDEV_MAX_NUM)) {
        LPRINTF("Invalid rproc number specified.");
        return -EINVAL;
    }

    LPRINTF("proc_id:%ld rsc_id:%ld mbx_id:%ld", proc_id, rsc_id, mbx_id);
    rproc = platform_create_proc((int)proc_id, (int)rsc_id, (int)mbx_id);
    if (!rproc) {
        LPRINTF("Failed to create remoteproc device.");
        return -EINVAL;
    }
    *platform = rproc;

    return 0;
}

void platform_release_rpmsg_vdev(struct remoteproc *rproc,
                                 struct rpmsg_device *rpdev)
{
    struct rpmsg_virtio_device *rpmsg_vdev;

#ifdef __linux__
    if (rproc->rsc_table)
        virtio_clear_status(rproc->rsc_table);
#endif

    rpmsg_vdev = metal_container_of(rpdev, struct rpmsg_virtio_device, rdev);
    rpmsg_deinit_vdev(rpmsg_vdev);
    remoteproc_remove_virtio(rproc, rpmsg_vdev->vdev);
    metal_free_memory(rpmsg_vdev);
}

void platform_cleanup(struct remoteproc *rproc)
{
    if (rproc) {
        (void)remoteproc_remove(rproc);
        /* Release the private area */
        if (rproc->priv) {
            metal_free_memory(rproc->priv);
            rproc->priv = NULL;
        }
        metal_free_memory(rproc);
    }
}

#ifndef __linux__ /* uC3 */
static void IpiTaskId(VP_INT exinf)
{
    struct remoteproc *rproc = exinf;
    struct remoteproc_priv *prproc = rproc->priv;
    int ret;

    while (1) {
        wai_sem(ipi[UIO_MBX].ipi_sem_id[prproc->notify_id]);

        /* Ignore an incoming interrupt if the virtio layer of a target
         * remoteproc is not yet initialized */
        if (metal_list_is_empty(&rproc->vdevs)) {
            continue;
        }

        ret = remoteproc_get_notification(rproc, RSC_NOTIFY_ID_ANY);
        if (ret) {
            LPRINTF("remoteproc_get_notification() failed with %d", ret);
        }
    }
}

static void start_ipi_task(void *platform)
{
    struct remoteproc *rproc = platform;
    struct remoteproc_priv *prproc = rproc->priv;
    const T_CSEM csem_ipi = { TA_TFIFO, 0, 1 };

    ipi[UIO_MBX].ipi_sem_id[prproc->notify_id] = acre_sem((T_CSEM*)&csem_ipi);

    T_CTSK ctsk_ipitask;
    ctsk_ipitask.tskatr  = (TA_HLNG | TA_FPU | TA_ACT);
    ctsk_ipitask.name    = "metal_ipi_tsk";
    ctsk_ipitask.itskpri = 2;
    ctsk_ipitask.stksz   = 0x400U;
    ctsk_ipitask.stk     = NULL;
    ctsk_ipitask.task    = (FP)IpiTaskId;
    ctsk_ipitask.exinf   = (VP_INT)platform;
    ipi_tsk_id[prproc->notify_id] = acre_tsk((T_CTSK*)&ctsk_ipitask);

    if (ipi_tsk_id[prproc->notify_id] <= 0) {
        LPRINTF("Failed to register an interrupt service routine.");
    }
}
#endif
