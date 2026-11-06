/*
 * Qualcomm SM8150 UFS Host Controller Model
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#ifndef HW_ARM_QCOM_SM8150_UFS_H
#define HW_ARM_QCOM_SM8150_UFS_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_QCOM_SM8150_UFS "qcom.sm8150-ufs"
OBJECT_DECLARE_SIMPLE_TYPE(QComSm8150UfsState, QCOM_SM8150_UFS)

struct QComSm8150UfsState {
    SysBusDevice parent_obj;

    MemoryRegion mmio_ufs;
    MemoryRegion mmio_ice;
    MemoryRegion mmio_phy;

    qemu_irq irq;

    /* UFSHCI standard registers */
    uint32_t cap;
    uint32_t ver;
    uint32_t hcpid;
    uint32_t hcmid;
    uint32_t ahit;
    uint32_t is;
    uint32_t ie;
    uint32_t hcs;
    uint32_t hce;
    uint32_t uecpa;
    uint32_t uecdl;
    uint32_t uecn;
    uint32_t uect;
    uint32_t uecdme;
    uint32_t utriacr;
    uint32_t utrlba;
    uint32_t utrlbau;
    uint32_t utrldbr;
    uint32_t utrlclr;
    uint32_t utrlrsr;
    uint32_t utmrlba;
    uint32_t utmrlbau;
    uint32_t utmrdbr;
    uint32_t utmrlclr;
    uint32_t utmrlrsr;
    uint32_t uiccmd;
    uint32_t uiccmdarg1;
    uint32_t uiccmdarg2;
    uint32_t uiccmdarg3;

    /* Qualcomm vendor registers */
    uint32_t qcom_sys1clk_1us;
    uint32_t qcom_tx_symbol_clk_ns_us;
    uint32_t qcom_cfg1;
    uint32_t qcom_cfg2;
    uint32_t qcom_hw_ver;
    uint32_t qcom_unipro_cfg;
    uint32_t qcom_unipro_status;
};

DeviceState *qcom_sm8150_ufs_create(hwaddr base, hwaddr ice_base, hwaddr phy_base, qemu_irq irq);

#endif /* HW_ARM_QCOM_SM8150_UFS_H */
