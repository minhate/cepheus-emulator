/*
 * Qualcomm SM8150 Global Clock Controller (GCC) Model
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

#define GCC_BRANCH_ENABLE_BIT  (1U << 0)
#define GCC_BRANCH_CLK_OFF_BIT (1U << 31)

static uint64_t qcom_sm8150_gcc_read(void *opaque, hwaddr offset, unsigned size)
{
    QComSm8150GccState *s = QCOM_SM8150_GCC(opaque);
    uint32_t idx = offset >> 2;

    if (idx >= s->regs_size) {
        return 0;
    }

    /*
     * For Qualcomm branch clock control registers (CBCR):
     * Bit 31 is CLK_OFF. Return 0 so branch clocks and voted branch
     * clocks are reported running (not stuck at 'off').
     */
    return s->regs[idx] & ~GCC_BRANCH_CLK_OFF_BIT;
}

static void qcom_sm8150_gcc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    QComSm8150GccState *s = QCOM_SM8150_GCC(opaque);
    uint32_t idx = offset >> 2;

    if (idx < s->regs_size) {
        s->regs[idx] = (uint32_t)value;
    }
}

static const MemoryRegionOps qcom_sm8150_gcc_ops = {
    .read = qcom_sm8150_gcc_read,
    .write = qcom_sm8150_gcc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_sm8150_gcc_init(Object *obj)
{
}

DeviceState *qcom_sm8150_gcc_create(hwaddr base, hwaddr size)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_SM8150_GCC);
    QComSm8150GccState *s = QCOM_SM8150_GCC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    s->regs_size = size / 4;
    s->regs = g_new0(uint32_t, s->regs_size);

    memory_region_init_io(&s->mmio, OBJECT(dev), &qcom_sm8150_gcc_ops, s,
                          "qcom.sm8150-gcc", size);
    sysbus_init_mmio(sbd, &s->mmio);

    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);

    return dev;
}

static const TypeInfo qcom_sm8150_gcc_info = {
    .name          = TYPE_QCOM_SM8150_GCC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QComSm8150GccState),
    .instance_init = qcom_sm8150_gcc_init,
};

static void qcom_sm8150_gcc_register_types(void)
{
    type_register_static(&qcom_sm8150_gcc_info);
}

type_init(qcom_sm8150_gcc_register_types);
