/*
 * Qualcomm SPMI PMIC Arbiter v5 Model
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

#define PMIC_ARB_VERSION_V5 0x50000000U

static uint64_t qcom_spmi_core_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset == 0x00) {
        return PMIC_ARB_VERSION_V5;
    }
    return 0;
}

static void qcom_spmi_core_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps qcom_spmi_core_ops = {
    .read = qcom_spmi_core_read,
    .write = qcom_spmi_core_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t qcom_spmi_chnls_read(void *opaque, hwaddr offset, unsigned size)
{
    /* Return transaction done / success status */
    return 0x1;
}

static void qcom_spmi_chnls_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps qcom_spmi_chnls_ops = {
    .read = qcom_spmi_chnls_read,
    .write = qcom_spmi_chnls_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t qcom_spmi_generic_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}

static void qcom_spmi_generic_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps qcom_spmi_generic_ops = {
    .read = qcom_spmi_generic_read,
    .write = qcom_spmi_generic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_spmi_pmic_arb_init(Object *obj)
{
    QComSpmiPmicArbState *s = QCOM_SPMI_PMIC_ARB(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio_core, obj, &qcom_spmi_core_ops, s,
                          "qcom.spmi-core", 0x1100);
    sysbus_init_mmio(sbd, &s->mmio_core);

    memory_region_init_io(&s->mmio_chnls, obj, &qcom_spmi_chnls_ops, s,
                          "qcom.spmi-chnls", 0x2000000);
    sysbus_init_mmio(sbd, &s->mmio_chnls);

    memory_region_init_io(&s->mmio_obsrvr, obj, &qcom_spmi_generic_ops, s,
                          "qcom.spmi-obsrvr", 0x100000);
    sysbus_init_mmio(sbd, &s->mmio_obsrvr);

    memory_region_init_io(&s->mmio_intr, obj, &qcom_spmi_generic_ops, s,
                          "qcom.spmi-intr", 0x000a0000);
    sysbus_init_mmio(sbd, &s->mmio_intr);

    memory_region_init_io(&s->mmio_cnfg, obj, &qcom_spmi_generic_ops, s,
                          "qcom.spmi-cnfg", 0x00026000);
    sysbus_init_mmio(sbd, &s->mmio_cnfg);

    sysbus_init_irq(sbd, &s->irq);
}

DeviceState *qcom_spmi_pmic_arb_create(hwaddr core_base, hwaddr core_size,
                                       hwaddr chnls_base, hwaddr chnls_size,
                                       hwaddr obsrvr_base, hwaddr obsrvr_size,
                                       hwaddr intr_base, hwaddr intr_size,
                                       hwaddr cnfg_base, hwaddr cnfg_size,
                                       qemu_irq irq)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_SPMI_PMIC_ARB);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, core_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, chnls_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, obsrvr_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 3, intr_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 4, cnfg_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

static const TypeInfo qcom_spmi_pmic_arb_info = {
    .name          = TYPE_QCOM_SPMI_PMIC_ARB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QComSpmiPmicArbState),
    .instance_init = qcom_spmi_pmic_arb_init,
};

static void qcom_spmi_pmic_arb_register_types(void)
{
    type_register_static(&qcom_spmi_pmic_arb_info);
}

type_init(qcom_spmi_pmic_arb_register_types);
