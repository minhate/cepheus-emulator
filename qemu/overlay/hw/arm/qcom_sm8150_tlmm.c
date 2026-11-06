/*
 * Qualcomm SM8150 Top-Level Mode Multiplexer / Pinctrl (TLMM) Model
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

static uint64_t qcom_sm8150_tlmm_read(void *opaque, hwaddr offset, unsigned size)
{
    QComSm8150TlmmState *s = QCOM_SM8150_TLMM(opaque);
    uint32_t idx = offset >> 2;

    if (idx < s->regs_size) {
        return s->regs[idx];
    }
    return 0;
}

static void qcom_sm8150_tlmm_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    QComSm8150TlmmState *s = QCOM_SM8150_TLMM(opaque);
    uint32_t idx = offset >> 2;

    if (idx < s->regs_size) {
        s->regs[idx] = (uint32_t)value;
    }
}

static const MemoryRegionOps qcom_sm8150_tlmm_ops = {
    .read = qcom_sm8150_tlmm_read,
    .write = qcom_sm8150_tlmm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_sm8150_tlmm_init(Object *obj)
{
    QComSm8150TlmmState *s = QCOM_SM8150_TLMM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->regs_size = 0xdc2000 / 4;
    s->regs = g_malloc0(s->regs_size * sizeof(uint32_t));

    memory_region_init_io(&s->mmio_pinctrl, obj, &qcom_sm8150_tlmm_ops, s,
                          "qcom.sm8150-tlmm", 0xdc2000);
    sysbus_init_mmio(sbd, &s->mmio_pinctrl);

    sysbus_init_irq(sbd, &s->irq);
}

DeviceState *qcom_sm8150_tlmm_create(hwaddr pinctrl_base, hwaddr pinctrl_size,
                                     qemu_irq irq)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_SM8150_TLMM);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, pinctrl_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

static const TypeInfo qcom_sm8150_tlmm_info = {
    .name          = TYPE_QCOM_SM8150_TLMM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QComSm8150TlmmState),
    .instance_init = qcom_sm8150_tlmm_init,
};

static void qcom_sm8150_tlmm_register_types(void)
{
    type_register_static(&qcom_sm8150_tlmm_info);
}

type_init(qcom_sm8150_tlmm_register_types);
