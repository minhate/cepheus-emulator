/*
 * Qualcomm SM8150 (Snapdragon 855) Peripheral Models
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_ARM_QCOM_SM8150_SOC_H
#define HW_ARM_QCOM_SM8150_SOC_H

#include "hw/sysbus.h"
#include "qom/object.h"

/* Power Domain Controller (PDC) */
#define TYPE_QCOM_SM8150_PDC "qcom-sm8150-pdc"
OBJECT_DECLARE_SIMPLE_TYPE(QComSm8150PdcState, QCOM_SM8150_PDC)

struct QComSm8150PdcState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t regs[0x1000 / 4];
};

DeviceState *qcom_sm8150_pdc_create(hwaddr base, hwaddr size);

/* RPMh Resource State Coordinator (RSC) */
#define TYPE_QCOM_RPMH_RSC "qcom-rpmh-rsc"
OBJECT_DECLARE_SIMPLE_TYPE(QComRpmhRscState, QCOM_RPMH_RSC)

struct QComRpmhRscState {
    SysBusDevice parent_obj;

    MemoryRegion mmio_rsc;
    MemoryRegion mmio_tcs;
    qemu_irq irq;

    uint32_t drv_id;
    uint32_t num_tcs;
    uint32_t cmds_per_tcs;

    uint32_t *tcs_regs;
    uint32_t tcs_regs_size;
};

DeviceState *qcom_rpmh_rsc_create(hwaddr rsc_base, uint32_t drv_id, qemu_irq irq);

/* Global Clock Controller (GCC) */
#define TYPE_QCOM_SM8150_GCC "qcom-sm8150-gcc"
OBJECT_DECLARE_SIMPLE_TYPE(QComSm8150GccState, QCOM_SM8150_GCC)

struct QComSm8150GccState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t *regs;
    uint32_t regs_size;
};

DeviceState *qcom_sm8150_gcc_create(hwaddr base, hwaddr size);

/* Top-Level Mode Multiplexer / Pinctrl (TLMM) */
#define TYPE_QCOM_SM8150_TLMM "qcom-sm8150-tlmm"
OBJECT_DECLARE_SIMPLE_TYPE(QComSm8150TlmmState, QCOM_SM8150_TLMM)

struct QComSm8150TlmmState {
    SysBusDevice parent_obj;

    MemoryRegion mmio_pinctrl;
    qemu_irq irq;

    uint32_t *regs;
    uint32_t regs_size;
};

DeviceState *qcom_sm8150_tlmm_create(hwaddr pinctrl_base, hwaddr pinctrl_size,
                                     qemu_irq irq);

/* SPMI PMIC Arbiter v5 */
#define TYPE_QCOM_SPMI_PMIC_ARB "qcom-spmi-pmic-arb"
OBJECT_DECLARE_SIMPLE_TYPE(QComSpmiPmicArbState, QCOM_SPMI_PMIC_ARB)

struct QComSpmiPmicArbState {
    SysBusDevice parent_obj;

    MemoryRegion mmio_core;
    MemoryRegion mmio_chnls;
    MemoryRegion mmio_obsrvr;
    MemoryRegion mmio_intr;
    MemoryRegion mmio_cnfg;
    qemu_irq irq;

    uint32_t *core_regs;
    uint32_t *cnfg_regs;
};

DeviceState *qcom_spmi_pmic_arb_create(hwaddr core_base, hwaddr core_size,
                                       hwaddr chnls_base, hwaddr chnls_size,
                                       hwaddr obsrvr_base, hwaddr obsrvr_size,
                                       hwaddr intr_base, hwaddr intr_size,
                                       hwaddr cnfg_base, hwaddr cnfg_size,
                                       qemu_irq irq);

#endif /* HW_ARM_QCOM_SM8150_SOC_H */
