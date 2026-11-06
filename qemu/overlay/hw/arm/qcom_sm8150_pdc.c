/*
 * Qualcomm SM8150 Power Domain Controller (PDC) Interrupt Model
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
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/qcom_sm8150_soc.h"

static uint64_t qcom_sm8150_pdc_read(void *opaque, hwaddr offset, unsigned size)
{
    QComSm8150PdcState *s = QCOM_SM8150_PDC(opaque);
    uint32_t idx = offset >> 2;

    if (idx < ARRAY_SIZE(s->regs)) {
        return s->regs[idx];
    }
    return 0;
}

static void qcom_sm8150_pdc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    QComSm8150PdcState *s = QCOM_SM8150_PDC(opaque);
    uint32_t idx = offset >> 2;

    if (idx < ARRAY_SIZE(s->regs)) {
        s->regs[idx] = (uint32_t)value;
    }
}

static const MemoryRegionOps qcom_sm8150_pdc_ops = {
    .read = qcom_sm8150_pdc_read,
    .write = qcom_sm8150_pdc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_sm8150_pdc_init(Object *obj)
{
    QComSm8150PdcState *s = QCOM_SM8150_PDC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &qcom_sm8150_pdc_ops, s,
                          "qcom.sm8150-pdc", 0x1000);
    sysbus_init_mmio(sbd, &s->mmio);
}

DeviceState *qcom_sm8150_pdc_create(hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_SM8150_PDC);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);

    return dev;
}

static const TypeInfo qcom_sm8150_pdc_info = {
    .name          = TYPE_QCOM_SM8150_PDC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QComSm8150PdcState),
    .instance_init = qcom_sm8150_pdc_init,
};

static void qcom_sm8150_pdc_register_types(void)
{
    type_register_static(&qcom_sm8150_pdc_info);
}

type_init(qcom_sm8150_pdc_register_types);
