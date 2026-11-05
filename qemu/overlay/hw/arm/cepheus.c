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
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "target/arm/gtimer.h"
#include "qom/object.h"
#include "qapi/qmp/qlist.h"

#define TYPE_CEPHEUS_MACHINE MACHINE_TYPE_NAME("cepheus")
OBJECT_DECLARE_SIMPLE_TYPE(CepheusMachineState, CEPHEUS_MACHINE)

#define NUM_IRQS        256
#define SM8150_MAX_CPUS 8
#define GIC_INTERNAL    32

#define CEPHEUS_RAM_BASE        0x80000000ULL
#define CEPHEUS_GIC_DIST_BASE   0x17a00000ULL
#define CEPHEUS_GIC_DIST_SIZE   0x00010000ULL
#define CEPHEUS_GIC_REDIST_BASE 0x17a60000ULL
#define CEPHEUS_GIC_REDIST_SIZE 0x00100000ULL
#define CEPHEUS_GENI_UART_BASE  0x00a90000ULL
#define CEPHEUS_GENI_UART_SIZE  0x00004000ULL

struct CepheusMachineState {
    MachineState parent;
    struct arm_boot_info bootinfo;
    DeviceState *gic;
};

static uint64_t cepheus_cpu_mp_affinity(int idx)
{
    return arm_build_mp_affinity(idx, ARM_DEFAULT_CPUS_PER_CLUSTER);
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

    /* 3. Initialize GICv3 */
    gictype = gicv3_class_name();
    cms->gic = qdev_new(gictype);
    qdev_prop_set_uint32(cms->gic, "revision", 3);
    qdev_prop_set_uint32(cms->gic, "num-cpu", smp_cpus);
    qdev_prop_set_uint32(cms->gic, "num-irq", NUM_IRQS + 32);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, smp_cpus);
    qdev_prop_set_array(cms->gic, "redist-region-count", redist_region_count);

    object_property_set_link(OBJECT(cms->gic), "sysmem", OBJECT(sysmem), &error_fatal);

    gicbusdev = SYS_BUS_DEVICE(cms->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);
    sysbus_mmio_map(gicbusdev, 0, CEPHEUS_GIC_DIST_BASE);
    sysbus_mmio_map(gicbusdev, 1, CEPHEUS_GIC_REDIST_BASE);

    /* Connect timer PPIs and CPU interrupt lines */
    for (n = 0; n < smp_cpus; n++) {
        DeviceState *cpudev = DEVICE(qemu_get_cpu(n));
        int intidbase = NUM_IRQS + n * GIC_INTERNAL;
        const int timer_irq[] = {
            [GTIMER_PHYS] = ARCH_TIMER_NS_EL1_IRQ,
            [GTIMER_VIRT] = ARCH_TIMER_VIRT_IRQ,
            [GTIMER_HYP]  = ARCH_TIMER_NS_EL2_IRQ,
            [GTIMER_SEC]  = ARCH_TIMER_S_EL1_IRQ,
            [GTIMER_HYPVIRT] = ARCH_TIMER_NS_EL2_VIRT_IRQ,
        };
        for (int irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(cms->gic, intidbase + timer_irq[irq]));
        }

        qdev_connect_gpio_out_named(cpudev, "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(cms->gic, intidbase + ARCH_GIC_MAINT_IRQ));
        qdev_connect_gpio_out_named(cpudev, "pmu-interrupt", 0,
                                    qdev_get_gpio_in(cms->gic, intidbase + VIRTUAL_PMU_IRQ));

        sysbus_connect_irq(gicbusdev, n, qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
        sysbus_connect_irq(gicbusdev, n + smp_cpus, qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
        sysbus_connect_irq(gicbusdev, n + 2 * smp_cpus, qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
        sysbus_connect_irq(gicbusdev, n + 3 * smp_cpus, qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));
    }

    /* 4. Map SM8150 Qualcomm GENI UART region at 0x00a90000 */
    create_unimplemented_device("qcom.geni-uart", CEPHEUS_GENI_UART_BASE, CEPHEUS_GENI_UART_SIZE);

    /* 5. Kernel bootloader setup */
    cms->bootinfo.ram_size = machine->ram_size;
    cms->bootinfo.loader_start = CEPHEUS_RAM_BASE;
    cms->bootinfo.board_id = -1;
    cms->bootinfo.psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    arm_load_kernel(ARM_CPU(first_cpu), machine, &cms->bootinfo);
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
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("max");
    mc->valid_cpu_types = valid_cpu_types;
    mc->max_cpus = SM8150_MAX_CPUS;
    mc->default_cpus = SM8150_MAX_CPUS;
    mc->default_ram_size = 4 * GiB;
    mc->default_ram_id = "cepheus.ram";
    mc->possible_cpu_arch_ids = cepheus_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = cepheus_cpu_index_to_props;
}

static const TypeInfo cepheus_machine_info = {
    .name          = TYPE_CEPHEUS_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(CepheusMachineState),
    .class_init    = cepheus_class_init,
};

static void cepheus_machine_register_types(void)
{
    type_register_static(&cepheus_machine_info);
}

type_init(cepheus_machine_register_types);
