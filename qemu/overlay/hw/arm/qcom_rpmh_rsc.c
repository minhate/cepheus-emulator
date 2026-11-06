/*
 * Qualcomm RPMh Resource State Coordinator (RSC) Model
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/qcom_sm8150_soc.h"

#define RSC_DRV_PRT_CNT_REG  0x0c
#define RSC_TCS_OFFSET       0xd00
#define RSC_TOTAL_SIZE       0x4000

static uint64_t qcom_rpmh_rsc_read(void *opaque, hwaddr offset, unsigned size)
{
    QComRpmhRscState *s = QCOM_RPMH_RSC(opaque);

    if (offset == RSC_DRV_PRT_CNT_REG) {
        /*
         * Format:
         * Bits 31..27: commands per TCS (16)
         * Bits (drv_id * 6)..(drv_id * 6 + 5): TCS count for drv_id (16)
         */
        uint32_t val = (s->cmds_per_tcs << 27);
        val |= (s->num_tcs & 0x3f) << (s->drv_id * 6);
        val |= (s->num_tcs & 0x3f) << 6;
        val |= (s->num_tcs & 0x3f);
        return val;
    }

    if (offset >= RSC_TCS_OFFSET) {
        uint32_t idx = (offset - RSC_TCS_OFFSET) >> 2;
        if (idx < s->tcs_regs_size) {
            return s->tcs_regs[idx];
        }
    }

    return 0;
}

static void qcom_rpmh_rsc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    QComRpmhRscState *s = QCOM_RPMH_RSC(opaque);

    if (offset >= RSC_TCS_OFFSET) {
        uint32_t idx = (offset - RSC_TCS_OFFSET) >> 2;
        if (idx < s->tcs_regs_size) {
            s->tcs_regs[idx] = (uint32_t)value;
        }
    }
}

static const MemoryRegionOps qcom_rpmh_rsc_ops = {
    .read = qcom_rpmh_rsc_read,
    .write = qcom_rpmh_rsc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_rpmh_rsc_init(Object *obj)
{
    QComRpmhRscState *s = QCOM_RPMH_RSC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->drv_id = 2;
    s->num_tcs = 16;
    s->cmds_per_tcs = 16;
    s->tcs_regs_size = 0x3000 / 4;
    s->tcs_regs = g_new0(uint32_t, s->tcs_regs_size);

    memory_region_init_io(&s->mmio_rsc, obj, &qcom_rpmh_rsc_ops, s,
                          "qcom.rpmh-rsc", RSC_TOTAL_SIZE);
    sysbus_init_mmio(sbd, &s->mmio_rsc);

    sysbus_init_irq(sbd, &s->irq);
}

DeviceState *qcom_rpmh_rsc_create(hwaddr rsc_base, uint32_t drv_id, qemu_irq irq)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_RPMH_RSC);
    QComRpmhRscState *s = QCOM_RPMH_RSC(dev);

    s->drv_id = drv_id;
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, rsc_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

static const TypeInfo qcom_rpmh_rsc_info = {
    .name          = TYPE_QCOM_RPMH_RSC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QComRpmhRscState),
    .instance_init = qcom_rpmh_rsc_init,
};

static void qcom_rpmh_rsc_register_types(void)
{
    type_register_static(&qcom_rpmh_rsc_info);
}

type_init(qcom_rpmh_rsc_register_types);
