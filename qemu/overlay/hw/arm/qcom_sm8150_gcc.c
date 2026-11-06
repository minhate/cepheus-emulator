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

static bool is_gdsc_offset(hwaddr offset)
{
    switch (offset) {
    /* GCC GDSCs */
    case 0x77004: /* ufs_phy_gdsc */
    case 0x75004: /* ufs_card_gdsc */
    case 0x0f004: /* usb30_prim_gdsc */
    case 0x10004: /* usb30_sec_gdsc */
    case 0x06004: /* emac_gdsc */
    case 0x6b004: /* pcie_0_gdsc */
    case 0x8d004: /* pcie_1_gdsc */
    case 0x7d040 ... 0x7d060: /* hlos1_vote_* */
    /* CamCC GDSCs */
    case 0x7004: /* bps_gdsc */
    case 0x8004: /* ipe_0_gdsc */
    case 0x9004: /* ipe_1_gdsc */
    case 0xa004: /* ife_0_gdsc */
    case 0xb004: /* ife_1_gdsc */
    case 0xc1bc: /* titan_top_gdsc */
    /* DispCC GDSC */
    case 0x3000: /* mdss_core_gdsc */
    /* VideoCC GDSCs */
    case 0x0814: /* mvsc_gdsc */
    case 0x0874: /* mvs0_gdsc */
    case 0x08b4: /* mvs1_gdsc */
    /* NPUCC GDSC */
    case 0x11028: /* npu_core_gdsc */
        return true;
    default:
        return false;
    }
}

static uint64_t qcom_sm8150_gcc_read(void *opaque, hwaddr offset, unsigned size)
{
    QComSm8150GccState *s = QCOM_SM8150_GCC(opaque);
    uint32_t idx = offset >> 2;

    if (idx >= s->regs_size) {
        return 0;
    }

    uint32_t val = s->regs[idx];

    /*
     * For PLL_MODE (GPLL0 at 0x00000, GPLL4 at 0x76000):
     * Bit 31 is PLL_LOCK_DET, Bit 30 is PLL_CAL_DONE / ACK.
     * Must return both so wait_for_pll() succeeds.
     */
    if (offset == 0 || offset == 0x76000) {
        return val | (3U << 30);
    }

    /*
     * For GDSC (Globally Distributed Switch Controller):
     * Bit 0 is SW_COLLAPSE. If SW_COLLAPSE == 0 (sw enabled power),
     * bit 31 (PWR_ON) must be 1.
     * If SW_COLLAPSE == 1 (sw disabled power), bit 31 (PWR_ON) must be 0.
     */
    if (is_gdsc_offset(offset)) {
        if ((val & 1) == 0) {
            return val | (1U << 31);
        } else {
            return val & ~(1U << 31);
        }
    }

    /*
     * For Qualcomm branch clock control registers (CBCR):
     * Bit 31 is CLK_OFF. Return 0 so branch clocks and voted branch
     * clocks are reported running (not stuck at 'off').
     */
    return val & ~GCC_BRANCH_CLK_OFF_BIT;
}

static void qcom_sm8150_gcc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    QComSm8150GccState *s = QCOM_SM8150_GCC(opaque);
    uint32_t idx = offset >> 2;

    if (idx < s->regs_size) {
        /*
         * In Qualcomm RCG CMD_RCGR registers, bit 0 is CMD_UPDATE.
         * Software writes 1 to strobe a frequency update, and hardware
         * clears it immediately upon completion.
         * Auto-clear bit 0 so update_config() polling succeeds immediately.
         * Do not clear for PLL_MODE (offset 0, 0x76000) or GDSC registers.
         */
        if ((value & 1) && offset != 0 && offset != 0x76000 && !is_gdsc_offset(offset)) {
            value &= ~1U;
        }
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

    /* Initialize bootloader PLL handoff state (GPLL0 and GPLL4 enabled) */
    if (s->regs_size > 0) {
        s->regs[0] = 0x00100007; /* GPLL0: OUTCTRL | BYPASSNL | RESET_N | VOTE_FSM */
    }
    if (s->regs_size > (0x76000 >> 2)) {
        s->regs[0x76000 >> 2] = 0x00100007; /* GPLL4 */
    }

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
