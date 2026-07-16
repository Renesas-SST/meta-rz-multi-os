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
 *       rz_rproc.c
 *
 * DESCRIPTION
 *
 *       This file defines RZ/G2 CA5X/CR7 remoteproc implementation.
 *
 * @par  History
 *       - rev 1.0 (2020.10.27) Imada
 *         Initial version.
 *
 **************************************************************************/

#include <pthread.h>
#include <metal/atomic.h>
#include <metal/assert.h>
#include <metal/device.h>
#include <metal/irq.h>
#include <metal/list.h>
#include <metal/utilities.h>
#include <openamp/rpmsg_virtio.h>
#include "platform_info.h"

#define MAX_READ_WAIT 6000 * 1000

extern struct ipi_info ipi[UIO_MAX];
extern struct shm_info shm[3];
extern struct mbx_channel chn_info[MBX_CH_NUM];
extern struct mhu_send_type send_type_info[MBX_CH_NUM];

/** to stop execution until received an interrupt. */
extern pthread_mutex_t mutex;
extern pthread_cond_t cond[MBX_CH_NUM];

/** thread specific key */
extern pthread_key_t thkey;

/** for judgement whether thread is in operation. */
extern bool valid_thread[MBX_CH_NUM];

/** to aboid second initialization of the common resource */
static int initialized = 0;

/** to avoid second performing of checking the send type */
static int isInit[MBX_CH_NUM] = {0};

/** share memories */
extern struct vring_info vrinfo[3];
extern struct vring_info vrinfo_ch1[3];

/** flag SIGINT or SIGTERM have been received */
extern int force_stop;

/** Interrupt ID for the messaging channel */
static const unsigned int mhu_send_type_msg[] =
{
    INT_MHU_RSP_CH8_NS,
    INT_MHU_RSP_CH20_NS,
    INT_MHU_RSP_CH31_NS,
    INT_MHU_RSP_CH39_NS,
    INT_MHU_RSP_CH6_NS,
    INT_MHU_RSP_CH18_NS,
    INT_MHU_RSP_CH27_NS,
    INT_MHU_RSP_CH37_NS,
    INT_MHU_RSP_CH7_NS,
    INT_MHU_RSP_CH19_NS,
    INT_MHU_RSP_CH30_NS,
    INT_MHU_RSP_CH38_NS,
};

/** Interrupt ID for the responding channel */
static const unsigned int mhu_send_type_rsp[] =
{
    INT_MHU_MSG_CH36_NS,
    INT_MHU_MSG_CH37_NS,
    INT_MHU_MSG_CH38_NS,
    INT_MHU_MSG_CH39_NS,
    INT_MHU_MSG_CH24_NS,
    INT_MHU_MSG_CH25_NS,
    INT_MHU_MSG_CH26_NS,
    INT_MHU_MSG_CH27_NS,
    INT_MHU_MSG_CH30_NS,
    INT_MHU_MSG_CH31_NS,
    INT_MHU_MSG_CH32_NS,
    INT_MHU_MSG_CH33_NS,
};

/* Inline functions to add accessing address check to corresponding 
 * libmetal functions to avoid accessing a reserved region. 
 * They are mainly required because of larger (uio) mmap size due the 
 * 4KB page size on Verified Linux. They are also utilized for uC3 because 
 * accessing a reserved area of registers can cause system failure.
 */
static inline void metal_io_read32_with_check(struct metal_io_region *io, unsigned long offset, unsigned int *ret)
{
    if ((MBX_REG_START <= offset) && (offset < MBX_REG_END)) {
        *ret = metal_io_read32(io, offset);
    }
    else {
        LPERROR("Offset value larger than the register size in metal_io_read32.");
    }

    return ;
}

static inline void metal_io_write32_with_check(struct metal_io_region *io, unsigned long offset, uint64_t val)
{
    if ((MBX_REG_START <= offset) && (offset < MBX_REG_END)) {
        metal_io_write32(io, offset, val);
    }
    else {
        LPERROR("Offset value larger than the register size in metal_io_write32.");
    }
    return ;
}

/**
 * @fn get_device_size
 * @brief Get the correct memory size for a UIO device from device tree
 * @param name - device name
 * @return size in bytes, or 0 if not found (will use UIO device size as fallback)
 *
 * FIX for segmentation fault: Use DT-defined sizes instead of UIO device size.
 * The UIO device reports only page-aligned size (0x1000), but the actual
 * allocated regions are larger (0x100000 for vring regions). This causes
 * OpenAMP to fail lookups for vrings at higher offsets.
 */
static size_t get_device_size(const char *name)
{
    if (!name) return 0;

    /* Resource tables: 4KB each */
    if (strstr(name, ".rsctbl")) return CFG_RSCTBL_SIZE;

    /* Vring control regions: 1MB each */
    if (strstr(name, "vring-ctl")) return CFG_VRING_CTL_SIZE;

    /* Vring shared memory regions: 3MB each */
    if (strstr(name, "vring-shm")) return CFG_VRING_SHM_SIZE;

    /* MHU shared memory: 4KB each */
    if (strstr(name, "mhu-shm")) return CFG_MHU_SHM_SIZE;

    /* Default: use UIO device size (fallback) */
    return 0;
}

static int init_memory_device_individual(struct shm_info *info){
    struct metal_device *dev;
    metal_phys_addr_t mem_pa;
    size_t mem_size;
    int ret;

    if (!info->name) return 0;  /* unused slot */

    ret = metal_device_open(info->bus_name, info->name, &dev);
    if (ret) {
        LPRINTF("Warning: uio device %s not found, skipping.", info->name);
        return 0;  /* core may not be kicked */
    }
    LPRINTF("Successfully open uio device: %s.", info->name);
    
    info->dev = dev;
    info->io = metal_device_io_region(dev, 0x0U);
    if (!info->io) {
        LPRINTF("info->io is 0");
        goto err;
    }
    
    mem_pa = metal_io_phys(info->io, 0x0U);

    mem_size = get_device_size(info->name);
    if (mem_size == 0) {
        mem_size = metal_io_region_size(info->io);
        LPRINTF("Using fallback UIO size 0x%lx for %s", (unsigned long)mem_size, info->name);
    } else {
        LPRINTF("Using DT size 0x%lx for %s", (unsigned long)mem_size, info->name);
    }

    info->mem = metal_allocate_memory(sizeof(*info->mem));
    memset(info->mem, 0, sizeof(*info->mem));
    remoteproc_init_mem(info->mem, info->name, mem_pa, mem_pa,
                mem_size,
                info->io);
    LPRINTF("Successfully added memory device %s.", info->name);

    return 0;
err:
    if(info->dev)
        metal_device_close(info->dev);
    return ret;
}

/**
 * @fn add_memory_device
 * @param rproc - platform resource
 * @param info - memory management info
 * @param base - memory management template
 */
static void add_memory_device(struct remoteproc *rproc, struct shm_info* info, struct shm_info *base) {
    if (!base->io || !base->mem) return;
    memcpy(info, base, sizeof(struct shm_info));
    info->mem = metal_allocate_memory(sizeof(*info->mem));
    memcpy(info->mem, base->mem, sizeof(*info->mem));
    memset(&info->mem->node, 0, sizeof(struct metal_list));
    remoteproc_add_mem(rproc, info->mem);
}

/**
 * @fn init_memory_device
 * @param rproc - platform resource
 * @return 0(normal) else(failed)
 */
static int init_memory_device(struct remoteproc *rproc) {
    struct remoteproc_priv *prproc = rproc->priv;
    int ret = -1;

    if (!prproc || !prproc->vr_info) {
        LPRINTF("vring is null");
        goto error_return;
    }

    if (2 < prproc->notify_id) {
        LPRINTF("rscid is invalid");
        goto error_return;
    }

    if (vrinfo[0].rsc.dev == NULL) {
        init_memory_device_individual(&vrinfo[0].rsc);
        init_memory_device_individual(&vrinfo[1].rsc);
        init_memory_device_individual(&vrinfo[0].ctl);
        init_memory_device_individual(&vrinfo[0].shm);
        init_memory_device_individual(&vrinfo[1].ctl);
        init_memory_device_individual(&vrinfo[1].shm);
        
        init_memory_device_individual(&vrinfo_ch1[0].ctl);
        init_memory_device_individual(&vrinfo_ch1[0].shm);
        init_memory_device_individual(&vrinfo_ch1[1].ctl);
        init_memory_device_individual(&vrinfo_ch1[1].shm);
        init_memory_device_individual(&vrinfo[2].rsc);
        init_memory_device_individual(&vrinfo[2].ctl);
        init_memory_device_individual(&vrinfo[2].shm);
        init_memory_device_individual(&vrinfo_ch1[2].ctl);
        init_memory_device_individual(&vrinfo_ch1[2].shm);

        init_memory_device_individual(&shm[0]);
        init_memory_device_individual(&shm[1]);
        init_memory_device_individual(&shm[2]);
    } else {
        ret = 0;
    }

    metal_list_init(&rproc->mems);
    add_memory_device(rproc, &prproc->vr_info[0], &vrinfo[0].rsc);      // 42f00000 (CM33 rsctbl)
    add_memory_device(rproc, &prproc->vr_info[1], &vrinfo[1].rsc);      // 42f02000 (CR8 core0 rsctbl)
    add_memory_device(rproc, &prproc->vr_info[2], &vrinfo[0].ctl);      // 43000000 (CM33 ch0 ctl)
    add_memory_device(rproc, &prproc->vr_info[3], &vrinfo[0].shm);      // 43200000 (CM33 ch0 shm)
    add_memory_device(rproc, &prproc->vr_info[4], &vrinfo[1].ctl);      // 43800000 (CR8 core0 ch0 ctl)
    add_memory_device(rproc, &prproc->vr_info[5], &vrinfo[1].shm);      // 43a00000 (CR8 core0 ch0 shm)
    add_memory_device(rproc, &prproc->vr_info[6], &vrinfo_ch1[0].ctl);  // 43100000 (CM33 ch1 ctl)
    add_memory_device(rproc, &prproc->vr_info[7], &vrinfo_ch1[0].shm);  // 43500000 (CM33 ch1 shm)
    add_memory_device(rproc, &prproc->vr_info[8], &vrinfo_ch1[1].ctl);  // 43900000 (CR8 core0 ch1 ctl)
    add_memory_device(rproc, &prproc->vr_info[9], &vrinfo_ch1[1].shm);  // 43d00000 (CR8 core0 ch1 shm)
    add_memory_device(rproc, &prproc->vr_info[10], &shm[0]);            // 42f01000 (CM33 mhu-shm)
    add_memory_device(rproc, &prproc->vr_info[11], &shm[1]);            // 42f03000 (CR8 core0 mhu-shm)
    add_memory_device(rproc, &prproc->vr_info[12], &vrinfo[2].rsc);     // 42f04000 (CR8 core1 rsctbl)
    add_memory_device(rproc, &prproc->vr_info[13], &vrinfo[2].ctl);     // 44000000 (CR8 core1 ch0 ctl)
    add_memory_device(rproc, &prproc->vr_info[14], &vrinfo[2].shm);     // 44200000 (CR8 core1 ch0 shm)
    add_memory_device(rproc, &prproc->vr_info[15], &shm[2]);            // 42f05000 (CR8 core1 mhu-shm)
    add_memory_device(rproc, &prproc->vr_info[16], &vrinfo_ch1[2].ctl); // 44100000 (CR8 core1 ch1 ctl)
    add_memory_device(rproc, &prproc->vr_info[17], &vrinfo_ch1[2].shm); // 44500000 (CR8 core1 ch1 shm)
    return 0;

error_return:
    return ret;
}

/**
 * @fn create_vrinfo
 * @brief Create memory management information
 * @return 0(normal) else(failed)
 */
static int create_vrinfo(struct remoteproc* rproc) {
    int ret = -1;
    struct remoteproc_priv* prproc;
    size_t size;

    if (!rproc || !rproc->priv) {
        LPRINTF("priv is null");
        goto error_return;
    }
    prproc = rproc->priv;
    size = sizeof(struct shm_info) * VRING_MAX;

    prproc->vr_info = metal_allocate_memory(size);
    if (!(prproc->vr_info)) {
        LPRINTF("vr_info allocate memory failed.");
        goto error_return;
    }
    memset(prproc->vr_info, 0, size);

    ret = init_memory_device(rproc);

error_return:
    return ret;
}

/**
 * @fn remoteproc_remove_mem
 * @brief discard the holding available memory address information
 * @param pmem - memory information to be discarded
 */
static inline void remoteproc_remove_mem(struct remoteproc_mem **pmem)
{
    if (pmem && *pmem) {
        struct remoteproc_mem* mem = *pmem;
        if (mem->node.prev) {
            metal_list_del(&mem->node);
            mem->node.prev = NULL;
            mem->node.next = NULL;
        }
        metal_free_memory(mem);
        *pmem = NULL;
    }
}

/**
 * @fn deinit_memory_device_individual
 * @param info - shared memory information to be discarded.
 */
static void deinit_memory_device_individual(struct shm_info *info)
{
    if (info) {
        remoteproc_remove_mem(&info->mem);
        if (info->dev) {
            metal_device_close(info->dev);
            LPRINTF("%s closed", info->name);
            info->dev = NULL;
        }
    }
}

/**
 * @fn deinit_memory_device
 * @brief close and release memory device
 * @param rproc - platform resource
 */
static void deinit_memory_device(struct remoteproc *rproc)
{
    int i;
    struct remoteproc_priv* prproc;

    if (!rproc) goto error_return;
    prproc = rproc->priv;

    if (!prproc || !prproc->vr_info) goto error_return;

    deinit_memory_device_individual(&vrinfo[0].rsc);
    deinit_memory_device_individual(&vrinfo[1].rsc);
    deinit_memory_device_individual(&vrinfo[0].ctl);
    deinit_memory_device_individual(&vrinfo[0].shm);
    deinit_memory_device_individual(&vrinfo[1].ctl);
    deinit_memory_device_individual(&vrinfo[1].shm);

    /* Also close ch1 */
    deinit_memory_device_individual(&vrinfo_ch1[0].ctl);
    deinit_memory_device_individual(&vrinfo_ch1[0].shm);
    deinit_memory_device_individual(&vrinfo_ch1[1].ctl);
    deinit_memory_device_individual(&vrinfo_ch1[1].shm);
    deinit_memory_device_individual(&vrinfo[2].rsc);
    deinit_memory_device_individual(&vrinfo[2].ctl);
    deinit_memory_device_individual(&vrinfo[2].shm);
    deinit_memory_device_individual(&vrinfo_ch1[2].ctl);
    deinit_memory_device_individual(&vrinfo_ch1[2].shm);

    deinit_memory_device_individual(&shm[0]);
    deinit_memory_device_individual(&shm[1]);
    deinit_memory_device_individual(&shm[2]);

    if (prproc->vr_info) {
        for (i = 0; i < VRING_MAX; i++) {
            if (prproc->vr_info[i].mem) {
                if (prproc->vr_info[i].mem->node.prev != NULL)
                    metal_list_del(&prproc->vr_info[i].mem->node);
                metal_free_memory(prproc->vr_info[i].mem);
                prproc->vr_info[i].mem = NULL;
            }
        }
        metal_free_memory(prproc->vr_info);
        prproc->vr_info = NULL;
    }

error_return:
    return;
}

static int rz_proc_irq_handler(int vect_id, void *data)
{
    unsigned int val = 0U;

    (void)vect_id;
    (void)data;
    struct ipi_info *pipi;
    int th_index;
    int result;

    int shm_idx;

    if (valid_thread[UIO_RECEIVER1] &&(vect_id == (long)ipi[UIO_RECEIVER1].dev->irq_info)) {
        pipi = &ipi[UIO_RECEIVER1];
        th_index = 1;
    } else if (valid_thread[UIO_RECEIVER2] && (vect_id == (long)ipi[UIO_RECEIVER2].dev->irq_info)) {
        pipi = &ipi[UIO_RECEIVER2];
        th_index = 2;
    } else if (valid_thread[UIO_RECEIVER3] && (vect_id == (long)ipi[UIO_RECEIVER3].dev->irq_info)) {
        pipi = &ipi[UIO_RECEIVER3];
        th_index = 3;
    } else {
        result = METAL_IRQ_NOT_HANDLED;
        goto error_return;
    }

    if      (th_index == UIO_RECEIVER1) shm_idx = 0;  /* CM33      -> shm[0] */
    else if (th_index == UIO_RECEIVER2) shm_idx = 1;  /* CR8 core0 -> shm[1] */
    else                                shm_idx = 2;  /* CR8 core1 -> shm[2] */

    if (send_type_info[th_index].send_type == MHU_SEND_TYPE_MSG) {
        /* Clear the interrupt */
        metal_io_write32_with_check(ipi[UIO_MBX].io, MBX_RSP_INT_CLR_REG(chn_info[th_index].rsp), 0x1U);

        /* Get a massage from the mailbox */
        metal_io_read32_with_check(shm[shm_idx].io, MBX_SHMEM_CH_OFFSET(chn_info[th_index].msg) + MBX_SHMEM_TXD_OFFSET, &val);
    } else {
        /* Clear the interrupt */
        metal_io_write32_with_check(ipi[UIO_MBX].io, MBX_MSG_INT_CLR_REG(chn_info[th_index].msg), 0x1U);

        /* Get a massage from the mailbox */
        metal_io_read32_with_check(shm[shm_idx].io, MBX_SHMEM_CH_OFFSET(chn_info[th_index].msg) + MBX_SHMEM_RXD_OFFSET, &val);
    } 

    if (val >= RPVDEV_MAX_NUM) { /* val should have the notify_id of the sender */
        result = METAL_IRQ_NOT_HANDLED; /* Invalid message arrived */
        goto error_return;
    }
#ifdef __linux__
    pipi->notify_id = val;
    atomic_flag_clear(&pipi->sync);
    pthread_mutex_lock(&mutex);
    pthread_cond_signal(&cond[th_index]);
    pthread_mutex_unlock(&mutex);
    LPRINTF("cond signal %d sync:%d", th_index, atomic_load_bool(&pipi->sync));
#else /* uC3 */
    if (ipi[UIO_MBX].ipi_sem_id[val] != E_ID) {
        isig_sem(ipi[UIO_MBX].ipi_sem_id[val]);
    }
    else {
        result = METAL_IRQ_NOT_HANDLED;
    }
#endif

    result = METAL_IRQ_HANDLED;

error_return:
    return result;
}

static int rz_enable_interrupt(struct remoteproc *rproc, struct ipi_info* pipi)
{
    unsigned int irq_vect;
    struct metal_device *ipi_dev;
    int ret = 0;

    if (!pipi || pipi->registered || !pipi->dev) {
        goto error_return;
    }

    ipi_dev = pipi->dev;

    /* Register interrupt handler and enable interrupt for RZ/G2 CA5X or CR7 */
    irq_vect = (uintptr_t)ipi_dev->irq_info;
    ret = metal_irq_register((int)irq_vect, rz_proc_irq_handler, rproc);
    if (ret) {
        LPRINTF("metal_irq_register() failed with %d", ret);
        return ret;
    }
    metal_irq_enable(irq_vect);

error_return:
    return ret;
}

static void rz_disable_interrupt(struct remoteproc *rproc, struct ipi_info *pipi)
{
    (void)rproc;
    struct metal_device *dev;

    if (!pipi) goto error_return;
    if (!pipi->dev) goto error_return;

    dev = pipi->dev;
    metal_irq_disable((uintptr_t)dev->irq_info);
    metal_irq_unregister((uintptr_t)dev->irq_info);
    pipi->registered = 0;

error_return:
    return;
}

static struct remoteproc *
rz_proc_init(struct remoteproc *rproc,
            const struct remoteproc_ops *ops, void *arg)
{
    struct remoteproc_priv *prproc = arg;
    struct metal_device *dev[UIO_MAX];
    int ret = 0;
    int i;

    if ((!rproc) || (!prproc) || (!ops))
        return NULL;
    rproc->priv = prproc;
    rproc->ops = ops;

    if (initialized) {
        goto skip;
    }

    if (!ipi[UIO_MBX].registered) {
        /* Get an IPI device (Mailbox) */
        for (i = 0; i < UIO_MAX; i++) {
            ret |= metal_device_open("platform", ipi[i].name, &dev[i]);
        }
        if (ret) {
            LPERROR("Failed to open ipi device: %d.", ret);
            return NULL;
        }
        for (i = 0; i < UIO_MAX; i++) {
            ipi[i].dev = dev[i];
            ipi[i].io = metal_device_io_region(dev[i], 0x0U);
        }
        if (!ipi[UIO_MBX].io)
            goto err1;
#ifdef __linux__
        atomic_flag_test_and_set(&ipi[UIO_RECEIVER1].sync);
        atomic_flag_test_and_set(&ipi[UIO_RECEIVER2].sync);
        atomic_flag_test_and_set(&ipi[UIO_RECEIVER3].sync);
#endif
        LPRINTF("Successfully probed IPI device");

    }

    ipi[UIO_MBX].irq_info = chn_info[prproc->mbx_chn_id].irq_info;
    ipi[UIO_MBX].mbx_chn = chn_info[prproc->mbx_chn_id];
    ret = rz_enable_interrupt(rproc, &ipi[UIO_RECEIVER1]);
    if (ret) {
        LPERROR("Failed to register the interrupt handler.");
        goto err1;
    }
    ret = rz_enable_interrupt(rproc, &ipi[UIO_RECEIVER2]);
    if (ret) {
        LPERROR("Failed to register the interrupt handler.");
        goto err1;
    }
    ret = rz_enable_interrupt(rproc, &ipi[UIO_RECEIVER3]);
    if (ret) {
        LPERROR("Failed to register the interrupt handler.");
        goto err1;
    }
    ipi[UIO_MBX].registered++;

    initialized = 1;

skip:
    /*
     * Increment registered BEFORE create_vrinfo so that if
     * create_vrinfo (or remoteproc_set_rsc_table called later in
     * platform_create_proc) fails and triggers remoteproc_remove() ->
     * rz_proc_remove(), the remove path sees registered > 1 and only
     * decrements instead of tearing down the global vrinfo[]/shm[]
     * regions that other already-live rproc instances still depend on.
     */
    ipi[UIO_MBX].registered++;
    if (create_vrinfo(rproc)) {
        ipi[UIO_MBX].registered--;
        goto err1;
    }
    return rproc;
err1:
    metal_device_close(ipi[UIO_MBX].dev);
    return NULL;
}

static void rz_proc_remove(struct remoteproc *rproc)
{
    int i;

    if (!rproc)
        goto error_return;

    if (ipi[UIO_MBX].registered > 1) {
        ipi[UIO_MBX].registered--;
        goto error_return;
    }

    deinit_memory_device(rproc);

    /* Disable interrupts */
    if (initialized) {
        rz_disable_interrupt(rproc, &ipi[UIO_RECEIVER1]);
        rz_disable_interrupt(rproc, &ipi[UIO_RECEIVER2]);
        rz_disable_interrupt(rproc, &ipi[UIO_RECEIVER3]);
    }

    for (i = 0; i < UIO_MAX; i++) {
        if (ipi[i].dev) {
            metal_device_close(ipi[i].dev);
            ipi[i].dev = NULL;
        }
    }

    ipi[UIO_MBX].registered = 0;
    initialized = 0;
error_return:
    return;
}

static int rz_proc_notify(struct remoteproc *rproc, uint32_t id)
{
    struct remoteproc_priv *prproc = (struct remoteproc_priv*)rproc->priv;
    unsigned int val = 0U;
    int wait = 0;
    int shm_idx;
    (void)id;
    
    /* Determine which shm[] to use based on mailbox target (mbx_chn_id
     * UIO_RECEIVER1 (CM33) -> shm[0]
     * UIO_RECEIVER2 (CR8 core0) -> shm[1]
     * UIO_RECEIVER3 (CR8 core1) -> shm[2]
	 */
    if      (prproc->mbx_chn_id == UIO_RECEIVER1) shm_idx = 0;  /* CM33      -> shm[0] */
    else if (prproc->mbx_chn_id == UIO_RECEIVER2) shm_idx = 1;  /* CR8 core0 -> shm[1] */
    else                                           shm_idx = 2;  /* CR8 core1 -> shm[2] */

    /* Check the send type of the maibox channel for the first time only */
    if (!isInit[prproc->mbx_chn_id]) {
        for (int i = 0; i < MHU_CH_NUM_MAX; i++)
        {
            if (chn_info[prproc->mbx_chn_id].irq_info == mhu_send_type_msg[i])
            {
                send_type_info[prproc->mbx_chn_id].send_type = MHU_SEND_TYPE_MSG;
                break;
            }
        }

        for (int i = 0; i < MHU_CH_NUM_MAX; i++)
        {
            if (chn_info[prproc->mbx_chn_id].irq_info == mhu_send_type_rsp[i])
            {
                send_type_info[prproc->mbx_chn_id].send_type = MHU_SEND_TYPE_RSP;
                break;
            }
        }

        isInit[prproc->mbx_chn_id] = 1;
    }

    if (send_type_info[prproc->mbx_chn_id].send_type == MHU_SEND_TYPE_MSG) {
        /* Put a message saying "This is the notify_id of mine!" */
        metal_io_write32_with_check(shm[shm_idx].io, MBX_SHMEM_CH_OFFSET(chn_info[prproc->mbx_chn_id].msg) + MBX_SHMEM_RXD_OFFSET, (uint64_t)prproc->notify_id);

        do {
            metal_io_read32_with_check(ipi[UIO_MBX].io,
                MBX_MSG_INT_STS_REG(chn_info[prproc->mbx_chn_id].msg), &val);
            if ((wait++) > MAX_READ_WAIT) {
                LPRINTF("communication abort. chn=%u msg_ch=%u sts=0x%x",
                        prproc->mbx_chn_id, chn_info[prproc->mbx_chn_id].msg, val);
                return -1;
            }
        } while (0U != val && !force_stop);

        /* Send notification */
        metal_io_write32_with_check(ipi[UIO_MBX].io, MBX_MSG_INT_SET_REG(chn_info[prproc->mbx_chn_id].msg), 0x1U);

    } else {
        /* Put a message saying "This is the notify_id of mine!" */
        metal_io_write32_with_check(shm[shm_idx].io, MBX_SHMEM_CH_OFFSET(chn_info[prproc->mbx_chn_id].msg) + MBX_SHMEM_TXD_OFFSET, (uint64_t)prproc->notify_id);

        /* Check interrupt status: Has the previous message been received? */
        do {
            metal_io_read32_with_check(ipi[UIO_MBX].io,
                MBX_MSG_INT_STS_REG(chn_info[prproc->mbx_chn_id].msg), &val);
            if ((wait++) > MAX_READ_WAIT) {
                LPRINTF("communication abort. chn=%u msg_ch=%u sts=0x%x",
                        prproc->mbx_chn_id, chn_info[prproc->mbx_chn_id].msg, val);
                return -1;
            }
        } while (0U != val && !force_stop);

        /* Send notification */
        metal_io_write32_with_check(ipi[UIO_MBX].io, MBX_RSP_INT_SET_REG(chn_info[prproc->mbx_chn_id].rsp), 0x1U);
    }
  
    return 0;
}

#ifdef __linux__
static void *
rz_proc_mmap(struct remoteproc *rproc,
             metal_phys_addr_t *pa, metal_phys_addr_t *da, size_t size,
             unsigned int attribute, struct metal_io_region **io)
{
    metal_phys_addr_t lpa, lda;
    struct remoteproc_mem *mem;
    struct metal_list *node;
    struct metal_io_region *tmpio = NULL;
    (void)attribute;

    if (!rproc) {
        LPRINTF("rproc is null");
        return NULL;
    }

    lpa = *pa;
    lda = *da;

    if ((lpa == METAL_BAD_PHYS) && (lda == METAL_BAD_PHYS))
        return NULL;

    if (lpa == METAL_BAD_PHYS)
        lpa = lda;
    if (lda == METAL_BAD_PHYS)
        lda = lpa;

    /* Search registered memory regions for one that covers [lpa, lpa+size) */
    metal_list_for_each(&rproc->mems, node) {
        mem = metal_container_of(node, struct remoteproc_mem, node);
        if (!mem->io)
            continue;
        if (lpa < mem->pa)
            continue;
        if (lpa + size > mem->pa + mem->size)
            continue;
        tmpio = mem->io;
        break;
    }

    if (!tmpio) {
        LPRINTF("no memory region found for pa=0x%lx", (unsigned long)lpa);
        return NULL;
    }

    *pa = lpa;
    *da = lda;
    if (io)
        *io = tmpio;

    return metal_io_phys_to_virt(tmpio, lpa);
}
#endif

/* processor operations in rz. It defines
 * notification operation and remote processor managementi operations. */
struct remoteproc_ops rz_proc_ops = {
    .init = rz_proc_init,
    .remove = rz_proc_remove,
#ifdef __linux__
    .mmap = rz_proc_mmap,
#else
    .mmap = NULL,
#endif
    .notify = rz_proc_notify,
    .start = NULL,
    .stop = NULL,
    .shutdown = NULL,
};
