/*
 * Xiaomi Mi 9 (cepheus, Snapdragon 855 / SM8150) Machine Model
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "hw/arm/boot.h"
#include "hw/arm/bsa.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/misc/unimp.h"
#include "hw/char/qcom_geni_uart.h"
#include "hw/arm/qcom_sm8150_soc.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"
#include "qom/object.h"
#include "qapi/qmp/qlist.h"

#define TYPE_CEPHEUS_MACHINE MACHINE_TYPE_NAME("cepheus")
OBJECT_DECLARE_SIMPLE_TYPE(CepheusMachineState, CEPHEUS_MACHINE)

#define NUM_SPIS        960
#define NUM_IRQS        (NUM_SPIS + 32)
#define SM8150_MAX_CPUS 8
#define GIC_INTERNAL    32

#define CEPHEUS_RAM_BASE        0x80000000ULL
#define CEPHEUS_GIC_DIST_BASE   0x17a00000ULL
#define CEPHEUS_GIC_DIST_SIZE   0x00010000ULL
#define CEPHEUS_GIC_REDIST_BASE 0x17a60000ULL
#define CEPHEUS_GIC_REDIST_SIZE 0x00100000ULL
#define CEPHEUS_GENI_UART_BASE  0x00a90000ULL
#define CEPHEUS_GENI_UART_SIZE  0x00004000ULL

/* Peripheral Base Addresses */
#define SM8150_RPMH_SRAM_BASE   0x0c3f0000ULL
#define SM8150_RPMH_SRAM_SIZE   0x00010000ULL
#define SM8150_GCC_BASE         0x00100000ULL
#define SM8150_GCC_SIZE         0x001f0000ULL
#define SM8150_TLMM_BASE        0x03000000ULL
#define SM8150_TLMM_SIZE        0x00dc2000ULL
#define SM8150_TLMM_SPI_BASE    0x17c000f0ULL
#define SM8150_TLMM_SPI_SIZE    0x00000060ULL
#define SM8150_APPS_RSC_BASE    0x18220000ULL
#define SM8150_APPS_TCS_BASE    0x18220d00ULL
#define SM8150_DISP_RSC_BASE    0x0af20000ULL
#define SM8150_DISP_TCS_BASE    0x0af20d00ULL
#define SM8150_SPMI_CORE_BASE   0x0c440000ULL
#define SM8150_SPMI_CORE_SIZE   0x00001100ULL
#define SM8150_SPMI_CHNLS_BASE  0x0c600000ULL
#define SM8150_SPMI_CHNLS_SIZE  0x02000000ULL
#define SM8150_SPMI_OBSRVR_BASE 0x0e600000ULL
#define SM8150_SPMI_OBSRVR_SIZE 0x00100000ULL
#define SM8150_SPMI_INTR_BASE   0x0e700000ULL
#define SM8150_SPMI_INTR_SIZE   0x000a0000ULL
#define SM8150_SPMI_CNFG_BASE   0x0c40a000ULL
#define SM8150_SPMI_CNFG_SIZE   0x00026000ULL

/* SPI Interrupt IDs (INTID = SPI + 32) */
#define SM8150_APPS_RSC_SPI     5
#define SM8150_DISP_RSC_SPI     12
#define SM8150_TLMM_SPI         208
#define SM8150_GENI_UART_SPI    357
#define SM8150_SPMI_SPI         481

struct CepheusMachineState {
    MachineState parent;
    struct arm_boot_info bootinfo;
    DeviceState *gic;
    bool enable_geni_uart;
    bool enable_clk_pinctrl_pmic;
    MemoryRegion mmio_rpmh_sram;
    uint8_t *rpmh_sram;
    MemoryRegion mmio_dcc;
    MemoryRegion mmio_cpu_osm;
    uint32_t *cpu_osm_regs;
    MemoryRegion mmio_mpm;
};

static uint64_t cepheus_mpm_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset >= 0x1000 && offset < 0x2000) {
        return (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 30517);
    }
    return 0;
}

static void cepheus_mpm_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps cepheus_mpm_ops = {
    .read = cepheus_mpm_read,
    .write = cepheus_mpm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t cepheus_cpu_osm_read(void *opaque, hwaddr offset, unsigned size)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(opaque);
    uint32_t idx = offset >> 2;

    if (idx < 0x40000 / 4) {
        return cms->cpu_osm_regs[idx];
    }
    return 0;
}

static void cepheus_cpu_osm_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(opaque);
    uint32_t idx = offset >> 2;

    if (idx < 0x40000 / 4) {
        cms->cpu_osm_regs[idx] = (uint32_t)value;
    }
}

static const MemoryRegionOps cepheus_cpu_osm_ops = {
    .read = cepheus_cpu_osm_read,
    .write = cepheus_cpu_osm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t cepheus_dcc_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}

static void cepheus_dcc_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps cepheus_dcc_ops = {
    .read = cepheus_dcc_read,
    .write = cepheus_dcc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static uint64_t cepheus_rpmh_sram_read(void *opaque, hwaddr offset, unsigned size)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(opaque);
    uint64_t val = 0;

    if (offset + size <= SM8150_RPMH_SRAM_SIZE) {
        memcpy(&val, cms->rpmh_sram + offset, size);
    }
    return val;
}

static void cepheus_rpmh_sram_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(opaque);

    if (offset + size <= SM8150_RPMH_SRAM_SIZE) {
        memcpy(cms->rpmh_sram + offset, &value, size);
    }
}

static const MemoryRegionOps cepheus_rpmh_sram_ops = {
    .read = cepheus_rpmh_sram_read,
    .write = cepheus_rpmh_sram_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void cepheus_setup_cmd_db(uint8_t *sram)
{
    static const char * const arc_resources[] = {
        "cx.lvl", "mx.lvl", "mss.lvl", "ebi.lvl", "lmx.lvl", "lcx.lvl",
        "gfx.lvl", "mmcx.lvl", "xo.lvl",
    };
    static const char * const vrm_resources[] = {
        "smpa2", "smpa5", "smpa6", "smpc1", "smpc8", "bobc1",
        "ldoa1", "ldoa2", "ldoa3", "ldoa5", "ldoa6", "ldoa7",
        "ldoa9", "ldoa10", "ldoa11", "ldoa12", "ldoa13", "ldoa14",
        "ldoa15", "ldoa16", "ldoa17", "ldoa18", "ldoc1", "ldoc2",
        "ldoc3", "ldoc4", "ldoc5", "ldoc6", "ldoc7", "ldoc8", "ldoc9",
        "ldoc10", "ldoc11", "ldof2", "ldof5", "ldof6", "smpf2",
        "lnbclka2", "lnbclka3", "rfclka1", "rfclka2", "rfclka3",
        "rfclkd1", "rfclkd2", "rfclkd3", "rfclkd4", "QP0",
        "SH0", "MC0", "IP0", "CW0", "CP0",
    };

    uint32_t cmddb_base = 0x0c3f1000;
    uint32_t cmddb_size = 0x2000;
    uint32_t cmddb_magic = 0x0c0330db;
    uint32_t cmddb_version = 1;

    memcpy(sram + 0x0c, &cmddb_base, 4);
    memcpy(sram + 0x10, &cmddb_size, 4);

    uint8_t *hdr = sram + 0x1000;
    memcpy(hdr + 0x00, &cmddb_version, 4);
    memcpy(hdr + 0x04, &cmddb_magic, 4);

    /* Standalone mode flag at byte 140 */
    hdr[140] = 1;

    int total_entries = ARRAY_SIZE(arc_resources) + ARRAY_SIZE(vrm_resources);

    /* rsc_hdr[0]: VRM (slv_id = 4) */
    uint16_t vrm_cnt = ARRAY_SIZE(vrm_resources);
    uint16_t vrm_slv = 4;
    uint16_t vrm_hdr_off = 0;
    memcpy(hdr + 0x08, &vrm_slv, 2);
    memcpy(hdr + 0x0a, &vrm_hdr_off, 2);
    memcpy(hdr + 0x0e, &vrm_cnt, 2);

    /* rsc_hdr[1]: ARC (slv_id = 3) */
    uint16_t arc_cnt = ARRAY_SIZE(arc_resources);
    uint16_t arc_slv = 3;
    uint16_t arc_hdr_off = vrm_cnt * 24;
    memcpy(hdr + 0x18, &arc_slv, 2);
    memcpy(hdr + 0x1a, &arc_hdr_off, 2);
    memcpy(hdr + 0x1e, &arc_cnt, 2);

    uint8_t *entries = hdr + 0x90;
    int entry_idx = 0;

    /* Populate VRM (Slave 4: 0x40000) */
    for (int i = 0; i < vrm_cnt; i++, entry_idx++) {
        uint8_t *e = entries + entry_idx * 24;
        strncpy((char *)e, vrm_resources[i], 8);
        uint32_t addr = 0x40000 + i * 4;
        memcpy(e + 16, &addr, 4);
        uint16_t len = 0;
        memcpy(e + 20, &len, 2);
    }

    /* Populate ARC (Slave 3: 0x30000) */
    uint16_t aux_levels[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    uint8_t *aux_data = entries + total_entries * 24;
    memcpy(aux_data, aux_levels, sizeof(aux_levels));

    for (int i = 0; i < arc_cnt; i++, entry_idx++) {
        uint8_t *e = entries + entry_idx * 24;
        strncpy((char *)e, arc_resources[i], 8);
        uint32_t addr = 0x30000 + i * 4;
        memcpy(e + 16, &addr, 4);
        uint16_t len = sizeof(aux_levels);
        memcpy(e + 20, &len, 2);
        uint16_t off = 0;
        memcpy(e + 22, &off, 2);
    }
}

static uint64_t cepheus_cpu_mp_affinity(int idx)
{
    /*
     * Snapdragon 855 / SM8150 assigns MPIDR Aff1 = core index (0..7),
     * matching the stock DTB: reg = <0x00 0x00>, <0x00 0x100>, ..., <0x00 0x700>.
     */
    return (uint64_t)idx << 8;
}

static const CPUArchIdList *cepheus_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    unsigned int max_cpus = ms->smp.max_cpus;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;
    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id = cepheus_cpu_mp_affinity(n);
        ms->possible_cpus->cpus[n].props.has_thread_id = true;
        ms->possible_cpus->cpus[n].props.thread_id = n;
    }
    return ms->possible_cpus;
}

static CpuInstanceProperties
cepheus_cpu_index_to_props(MachineState *ms, unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static void cepheus_init(MachineState *machine)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *sysmem = get_system_memory();
    const CPUArchIdList *possible_cpus;
    SysBusDevice *gicbusdev;
    const char *gictype;
    QList *redist_region_count;
    unsigned int smp_cpus = machine->smp.cpus;
    int n;

    if (smp_cpus > SM8150_MAX_CPUS) {
        error_report("cepheus: SMP CPUs (%d) exceeds max (%d)", smp_cpus, SM8150_MAX_CPUS);
        exit(1);
    }

    possible_cpus = mc->possible_cpu_arch_ids(machine);

    /* 1. Initialize CPUs */
    for (n = 0; n < smp_cpus; n++) {
        Object *cpuobj = object_new(possible_cpus->cpus[n].type);
        object_property_set_int(cpuobj, "mp-affinity",
                                possible_cpus->cpus[n].arch_id, NULL);

        CPU(cpuobj)->cpu_index = n;
        object_property_set_int(cpuobj, "cntfrq", 19200000, &error_abort);
        object_property_set_link(cpuobj, "memory", OBJECT(sysmem), &error_abort);
        qdev_realize(DEVICE(cpuobj), NULL, &error_fatal);
        object_unref(cpuobj);
    }

    /* 2. Map system RAM at 0x80000000 */
    memory_region_add_subregion(sysmem, CEPHEUS_RAM_BASE, machine->ram);

    /* 3. Initialize GICv3 (1024 IRQs to support all SM8150 SPIs) */
    gictype = gicv3_class_name();
    cms->gic = qdev_new(gictype);
    qdev_prop_set_uint32(cms->gic, "revision", 3);
    qdev_prop_set_uint32(cms->gic, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(cms->gic, "num-irq", NUM_IRQS);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, smp_cpus);
    qdev_prop_set_array(cms->gic, "redist-region-count", redist_region_count);

    object_property_set_link(OBJECT(cms->gic), "sysmem", OBJECT(sysmem), &error_fatal);

    gicbusdev = SYS_BUS_DEVICE(cms->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, CEPHEUS_GIC_DIST_BASE);
    sysbus_mmio_map(gicbusdev, 1, CEPHEUS_GIC_REDIST_BASE);

    /* Connect timer PPIs and CPU interrupt lines according to Qualcomm SM8150 DTB */
    for (n = 0; n < smp_cpus; n++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(n));
        int intidbase = NUM_SPIS + n * GIC_INTERNAL;
        const int timer_irq[] = {
            [GTIMER_PHYS] = 18, /* Qualcomm SM8150 DTB PPI 2 (INTID 18) */
            [GTIMER_VIRT] = 19, /* Qualcomm SM8150 DTB PPI 3 (INTID 19) */
            [GTIMER_HYP]  = 16, /* Qualcomm SM8150 DTB PPI 0 (INTID 16) */
            [GTIMER_SEC]  = 17, /* Qualcomm SM8150 DTB PPI 1 (INTID 17) */
            [GTIMER_HYPVIRT] = ARCH_TIMER_NS_EL2_VIRT_IRQ,
        };
        for (int irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(cms->gic, intidbase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(cms->gic, intidbase + ARCH_GIC_MAINT_IRQ));
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(cms->gic, intidbase + 21)); /* PPI 5 (INTID 21) */

        sysbus_connect_irq(gicbusdev, n, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, n + smp_cpus, qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, n + 2 * smp_cpus, qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, n + 3 * smp_cpus, qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    /* 4. Map SM8150 Qualcomm GENI UART region at 0x00a90000 */
    if (cms->enable_geni_uart) {
        qcom_geni_uart_create(CEPHEUS_GENI_UART_BASE,
                              qdev_get_gpio_in(cms->gic, SM8150_GENI_UART_SPI),
                              serial_hd(0));
    } else {
        create_unimplemented_device("qcom.geni-uart", CEPHEUS_GENI_UART_BASE, CEPHEUS_GENI_UART_SIZE);
    }

    /* 4.1 Map SM8150 APCS subsystem (timer-mem, watchdog, syscon) at 0x17c00000 */
    create_unimplemented_device("qcom.apcs", 0x17c00000, 0x30000);

    /* 4.2 Map SM8150 internal SRAM (IMEM) at 0x14680000 */
    create_unimplemented_device("qcom.imem", 0x14680000, 0x80000);

    /* 4.2.1 Map SM8150 TCSR mutex block at 0x01f40000 */
    create_unimplemented_device("qcom.tcsr-mutex", 0x01f40000, 0x40000);

    /* 4.2.2 Map SM8150 SP SCSR block at 0x01880000 */
    create_unimplemented_device("qcom.sp-scsr", 0x01880000, 0x10000);

    /* 4.3 Map Native Clock, Pinctrl, and SPMI/PMIC Models */
    if (cms->enable_clk_pinctrl_pmic) {
        /* Power Domain Controller (PDC) */
        qcom_sm8150_pdc_create(0x0b220000, 0x1000);

        /* RPMh RSC: apps_rsc and disp_rsc */
        qcom_rpmh_rsc_create(SM8150_APPS_RSC_BASE, 2,
                             qdev_get_gpio_in(cms->gic, SM8150_APPS_RSC_SPI));
        qcom_rpmh_rsc_create(SM8150_DISP_RSC_BASE, 0,
                             qdev_get_gpio_in(cms->gic, SM8150_DISP_RSC_SPI));

        /* Global and Subsystem Clock Controllers */
        qcom_sm8150_gcc_create(SM8150_GCC_BASE, SM8150_GCC_SIZE);
        qcom_sm8150_gcc_create(0x0ab00000, 0x010000); /* VideoCC */
        qcom_sm8150_gcc_create(0x0ad00000, 0x010000); /* CamCC */
        qcom_sm8150_gcc_create(0x0af00000, 0x020000); /* DispCC */
        qcom_sm8150_gcc_create(0x09900000, 0x020000); /* NPUCC */
        qcom_sm8150_gcc_create(0x02c90000, 0x010000); /* GPUCC */
        qcom_sm8150_gcc_create(0x09200000, 0x450000); /* LLCC */

        /* TLMM Pinctrl & GPIO */
        qcom_sm8150_tlmm_create(SM8150_TLMM_BASE, SM8150_TLMM_SIZE,
                                qdev_get_gpio_in(cms->gic, SM8150_TLMM_SPI));

        /* SPMI PMIC Arbiter v5 */
        qcom_spmi_pmic_arb_create(SM8150_SPMI_CORE_BASE, SM8150_SPMI_CORE_SIZE,
                                  SM8150_SPMI_CHNLS_BASE, SM8150_SPMI_CHNLS_SIZE,
                                  SM8150_SPMI_OBSRVR_BASE, SM8150_SPMI_OBSRVR_SIZE,
                                  SM8150_SPMI_INTR_BASE, SM8150_SPMI_INTR_SIZE,
                                  SM8150_SPMI_CNFG_BASE, SM8150_SPMI_CNFG_SIZE,
                                  qdev_get_gpio_in(cms->gic, SM8150_SPMI_SPI));

        /* RPMh SRAM / Command DB at 0x0c3f0000 */
        cms->rpmh_sram = g_malloc0(SM8150_RPMH_SRAM_SIZE);
        cepheus_setup_cmd_db(cms->rpmh_sram);

        memory_region_init_io(&cms->mmio_rpmh_sram, OBJECT(cms),
                              &cepheus_rpmh_sram_ops, cms,
                              "qcom.rpmh-sram", SM8150_RPMH_SRAM_SIZE);
        memory_region_add_subregion(sysmem, SM8150_RPMH_SRAM_BASE, &cms->mmio_rpmh_sram);

        /* DCC (Data Capture & Compare) at 0x010a0000 */
        memory_region_init_io(&cms->mmio_dcc, OBJECT(cms),
                              &cepheus_dcc_ops, cms,
                              "qcom.dcc", 0x10000);
        memory_region_add_subregion(sysmem, 0x010a0000, &cms->mmio_dcc);

        /* CPU Clock / Operating State Manager (OSM) at 0x18320000 */
        cms->cpu_osm_regs = g_malloc0(0x40000);
        memory_region_init_io(&cms->mmio_cpu_osm, OBJECT(cms),
                              &cepheus_cpu_osm_ops, cms,
                              "qcom.cpu-osm", 0x40000);
        memory_region_add_subregion(sysmem, 0x18320000, &cms->mmio_cpu_osm);

        /* MPM2 Sleep Counter and TSENS at 0x0c220000 */
        memory_region_init_io(&cms->mmio_mpm, OBJECT(cms),
                              &cepheus_mpm_ops, cms,
                              "qcom.mpm", 0x10000);
        memory_region_add_subregion(sysmem, 0x0c220000, &cms->mmio_mpm);
    }

    /* 5. Kernel bootloader setup */
    cms->bootinfo.ram_size = machine->ram_size;
    cms->bootinfo.loader_start = CEPHEUS_RAM_BASE;
    cms->bootinfo.board_id = -1;
    cms->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    arm_load_kernel(ARM_CPU(first_cpu), machine, &cms->bootinfo);
}

static bool cepheus_get_geni_uart(Object *obj, Error **errp)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(obj);
    return cms->enable_geni_uart;
}

static void cepheus_set_geni_uart(Object *obj, bool value, Error **errp)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(obj);
    cms->enable_geni_uart = value;
}

static bool cepheus_get_clk_pinctrl_pmic(Object *obj, Error **errp)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(obj);
    return cms->enable_clk_pinctrl_pmic;
}

static void cepheus_set_clk_pinctrl_pmic(Object *obj, bool value, Error **errp)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(obj);
    cms->enable_clk_pinctrl_pmic = value;
}

static void cepheus_instance_init(Object *obj)
{
    CepheusMachineState *cms = CEPHEUS_MACHINE(obj);
    cms->enable_geni_uart = true;
    cms->enable_clk_pinctrl_pmic = true;
}

static void cepheus_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-a57"),
        ARM_CPU_TYPE_NAME("cortex-a72"),
        ARM_CPU_TYPE_NAME("max"),
        NULL,
    };

    mc->desc = "Xiaomi Mi 9 (Snapdragon 855 / SM8150)";
    mc->init = cepheus_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a57");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = SM8150_MAX_CPUS;
    mc->default_cpus = SM8150_MAX_CPUS;
    mc->default_ram_size = 4 * GiB;
    mc->default_ram_id = "cepheus.ram";
    mc->possible_cpu_arch_ids = cepheus_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = cepheus_cpu_index_to_props;

    object_class_property_add_bool(oc, "geni-uart",
                                   cepheus_get_geni_uart,
                                   cepheus_set_geni_uart);
    object_class_property_set_description(oc, "geni-uart",
                                          "Enable or disable native Qualcomm GENI UART");

    object_class_property_add_bool(oc, "clk-pinctrl-pmic",
                                   cepheus_get_clk_pinctrl_pmic,
                                   cepheus_set_clk_pinctrl_pmic);
    object_class_property_set_description(oc, "clk-pinctrl-pmic",
                                          "Enable or disable native Qualcomm Clock, Pinctrl, and SPMI/PMIC models");
}

static const TypeInfo cepheus_machine_info = {
    .name          = TYPE_CEPHEUS_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(CepheusMachineState),
    .instance_init = cepheus_instance_init,
    .class_init    = cepheus_class_init,
};

static void cepheus_machine_register_types(void)
{
    type_register_static(&cepheus_machine_info);
}

type_init(cepheus_machine_register_types);
