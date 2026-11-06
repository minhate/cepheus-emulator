/*
 * Qualcomm GENI Serial UART Model
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qapi/error.h"
#include "qemu/timer.h"
#include "sysemu/reset.h"
#include "exec/cpu-common.h"
#include "hw/char/qcom_geni_uart.h"

#define SE_GENI_TX_FIFOn    0x700
#define KERNEL_LOG_BUF_BASE 0x8289e03cULL

/*
 * Linux 4.14 ARM64 struct printk_log (16 bytes header):
 * - u64 ts_nsec;      (0..7)
 * - u16 len;          (8..9)
 * - u16 text_len;     (10..11)
 * - u16 dict_len;     (12..13)
 * - u8 facility;      (14)
 * - u8 flags;         (15)
 * Followed by text_len bytes of message text at offset 16.
 */
static void qcom_geni_uart_flush_records(QcomGeniUartState *s)
{
    uint8_t hdr[16];

    while (1) {
        cpu_physical_memory_read(KERNEL_LOG_BUF_BASE + s->log_buf_offset, hdr, 16);
        uint16_t len = hdr[8] | (hdr[9] << 8);
        uint16_t text_len = hdr[10] | (hdr[11] << 8);

        if (len == 0 || len < 16 || len > 2048) {
            break;
        }

        if (text_len == 0) {
            s->log_buf_offset += len;
            continue;
        }

        if (len < 16 + text_len) {
            break;
        }

        char buf[2048];
        cpu_physical_memory_read(KERNEL_LOG_BUF_BASE + s->log_buf_offset + 16, buf, text_len);

        if (qemu_chr_fe_backend_connected(&s->chr)) {
            qemu_chr_fe_write_all(&s->chr, (const uint8_t *)buf, text_len);
            static const uint8_t nl = '\n';
            qemu_chr_fe_write_all(&s->chr, &nl, 1);
        }
        s->log_buf_offset += len;
    }
}

static void qcom_geni_uart_poll_log(void *opaque)
{
    QcomGeniUartState *s = opaque;
    qcom_geni_uart_flush_records(s);
    timer_mod(s->poll_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 5);
}

static void qcom_geni_uart_reset_cb(void *opaque)
{
    QcomGeniUartState *s = opaque;
    qcom_geni_uart_flush_records(s);
}

static uint64_t qcom_geni_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    QcomGeniUartState *s = opaque;

    switch (addr) {
    case 0x00: /* GENI_STATUS */
    case 0x40: /* SE_GENI_STATUS */
        return 0; /* Idle / ready */
    case 0x68: /* GENI_FW_REVISION_RO */
        return 0x00000100;
    case 0x6c: /* GENI_IF_FIFO_UNSPECIFIED */
        return 0x00000002; /* Protocol: UART = 2 */
    case 0x610: /* SE_GENI_M_IRQ_STATUS */
        return 0x60000001;
    case 0x614: /* SE_GENI_M_IRQ_EN */
        return s->m_irq_en;
    case 0x640: /* SE_GENI_S_IRQ_STATUS */
        return 0x00000001;
    case 0x644: /* SE_GENI_S_IRQ_EN */
        return s->s_irq_en;
    case 0x800: /* SE_GENI_TX_FIFO_STATUS */
        return 64 << 16; /* 64 words available, 0 words filled */
    case 0x804: /* SE_GENI_RX_FIFO_STATUS */
        return 0;
    case 0xe24: /* SE_HW_PARAM_0 */
        return 0x00000002; /* UART protocol */
    case 0xe28: /* SE_HW_PARAM_1 */
        return 0x00000040; /* FIFO depth = 64 */
    default:
        return 0;
    }
}

static void qcom_geni_uart_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    QcomGeniUartState *s = opaque;

    switch (addr) {
    case 0x614: /* SE_GENI_M_IRQ_EN */
        s->m_irq_en = val;
        break;
    case 0x644: /* SE_GENI_S_IRQ_EN */
        s->s_irq_en = val;
        break;
    case 0x700 ... 0x73c: { /* SE_GENI_TX_FIFOn */
        for (unsigned int i = 0; i < size; i++) {
            uint8_t ch = (val >> (i * 8)) & 0xff;
            if (ch && qemu_chr_fe_backend_connected(&s->chr)) {
                qemu_chr_fe_write_all(&s->chr, &ch, 1);
            }
        }
        break;
    }
    default:
        break;
    }
}

static const MemoryRegionOps qcom_geni_uart_ops = {
    .read = qcom_geni_uart_read,
    .write = qcom_geni_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void qcom_geni_uart_realize(DeviceState *dev, Error **errp)
{
    QcomGeniUartState *s = QCOM_GENI_UART(dev);
    s->poll_timer = timer_new_ms(QEMU_CLOCK_REALTIME, qcom_geni_uart_poll_log, s);
    timer_mod(s->poll_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + 5);
    qemu_register_reset(qcom_geni_uart_reset_cb, s);
}

static void qcom_geni_uart_init(Object *obj)
{
    QcomGeniUartState *s = QCOM_GENI_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &qcom_geni_uart_ops, s,
                          "qcom.geni-uart", 0x4000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static Property qcom_geni_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", QcomGeniUartState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void qcom_geni_uart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = qcom_geni_uart_realize;
    device_class_set_props(dc, qcom_geni_uart_properties);
}

static const TypeInfo qcom_geni_uart_info = {
    .name          = TYPE_QCOM_GENI_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QcomGeniUartState),
    .instance_init = qcom_geni_uart_init,
    .class_init    = qcom_geni_uart_class_init,
};

static void qcom_geni_uart_register_types(void)
{
    type_register_static(&qcom_geni_uart_info);
}

type_init(qcom_geni_uart_register_types);

DeviceState *qcom_geni_uart_create(hwaddr addr, qemu_irq irq, Chardev *chr)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_GENI_UART);
    SysBusDevice *s = SYS_BUS_DEVICE(dev);
    if (chr) {
        qdev_prop_set_chr(dev, "chardev", chr);
    }
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    if (irq) {
        sysbus_connect_irq(s, 0, irq);
    }
    return dev;
}
