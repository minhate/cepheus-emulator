/*
 * Qualcomm GENI Serial UART Model
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_QCOM_GENI_UART_H
#define HW_CHAR_QCOM_GENI_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_QCOM_GENI_UART "qcom-geni-uart"
OBJECT_DECLARE_SIMPLE_TYPE(QcomGeniUartState, QCOM_GENI_UART)

struct QcomGeniUartState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;

    uint32_t m_irq_en;
    uint32_t s_irq_en;

    QEMUTimer *poll_timer;
    uint32_t log_buf_offset;
};

DeviceState *qcom_geni_uart_create(hwaddr addr, qemu_irq irq, Chardev *chr);

#endif
