/*
 * Qualcomm Snapdragon 855 (SM8150) TrustZone / QSEE Secure Monitor Stub
 *
 * Copyright (c) 2026 Minoru / cepheus-emulator contributors.
 *
 * STUB(trustzone_qsee)
 */

#include "qemu/osdep.h"
#include "target/arm/cpu.h"
#include "exec/address-spaces.h"
#include "hw/arm/qcom_sm8150_soc.h"

#define ARM_SMCCC_OWNER_MASK           0x3f000000
#define ARM_SMCCC_OWNER_SHIFT          24
#define ARM_SMCCC_OWNER_SIP            2
#define ARM_SMCCC_OWNER_TRUSTED_APP_48 48
#define ARM_SMCCC_OWNER_TRUSTED_APP_49 49
#define ARM_SMCCC_OWNER_TRUSTED_OS     50

/* SCM return values */
#define QCOM_SCM_SUCCESS                0
#define QCOM_SCM_EBUSY                 -55
#define QCOM_SCM_ENODEV                -19

#define QSEOS_RESULT_SUCCESS            0
#define QSEOS_APP_ID                    0xEE01

bool qcom_sm8150_smc_handler(ARMCPU *cpu);

bool qcom_sm8150_smc_handler(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    uint64_t fn = env->xregs[0];
    uint32_t fn32 = (uint32_t)fn;
    uint32_t owner = (fn32 >> ARM_SMCCC_OWNER_SHIFT) & 0x3f;
    uint32_t svc = (fn32 >> 8) & 0xff;
    uint32_t cmd = fn32 & 0xff;

    /*
     * 1. QCOM SMCCC Fast Calls: Availability check (0xc2000601 / 0x82000601)
     */
    if (fn32 == 0xc2000601 || fn32 == 0x82000601) {
        env->xregs[0] = QCOM_SCM_SUCCESS;
        env->xregs[1] = 1; /* true: call is available */
        env->xregs[2] = 0;
        env->xregs[3] = 0;
        return true;
    }

    /*
     * 2. QCOM SCM Feature Version check:
     *    SVC_INFO (6), CMD_GET_FEAT_VERSION (3)
     *    (0xc2000603 / 0x82000603 / 0x02000603)
     */
    if (fn32 == 0xc2000603 || fn32 == 0x82000603 || fn32 == 0x02000603 ||
        (svc == 6 && cmd == 3)) {
        uint64_t feat_id = env->xregs[1];
        env->xregs[0] = QCOM_SCM_SUCCESS;
        if (feat_id == 10) {
            /* Feature ID 10 = QSEE/QSEOS version (QSEE 4.0.5) */
            env->xregs[1] = 0x00405000;
        } else {
            /* Default feature version */
            env->xregs[1] = 0x00010000;
        }
        env->xregs[2] = 0;
        env->xregs[3] = 0;
        return true;
    }

    /*
     * 3. General Silicon Partner (SiP) calls, Trusted OS, and Trusted App calls
     */
    if (owner == ARM_SMCCC_OWNER_SIP ||
        owner == ARM_SMCCC_OWNER_TRUSTED_APP_48 ||
        owner == ARM_SMCCC_OWNER_TRUSTED_APP_49 ||
        owner >= ARM_SMCCC_OWNER_TRUSTED_OS) {
        env->xregs[0] = QCOM_SCM_SUCCESS;
        env->xregs[1] = QSEOS_RESULT_SUCCESS;
        env->xregs[2] = QSEOS_APP_ID;
        env->xregs[3] = 1;
        return true;
    }

    /*
     * 4. Legacy Qualcomm SCM Calling Convention: fn == 1
     */
    if (fn == 1) {
        uint64_t paddr = env->xregs[2];
        if (paddr) {
            uint32_t cmd_hdr[4];
            cpu_physical_memory_read(paddr, cmd_hdr, sizeof(cmd_hdr));
            uint32_t len = cmd_hdr[0];
            uint32_t resp_hdr_offset = cmd_hdr[2];
            uint32_t cmd_id = cmd_hdr[3];
            uint32_t svc_id = (cmd_id >> 10) & 0x3ff;
            uint32_t sub_cmd = cmd_id & 0x3ff;

            if (resp_hdr_offset && resp_hdr_offset + 12 <= len + 0x1000) {
                uint32_t resp_hdr[3];
                resp_hdr[0] = sizeof(resp_hdr) + 16;
                resp_hdr[1] = sizeof(resp_hdr);
                resp_hdr[2] = 1; /* is_complete = 1 */
                cpu_physical_memory_write(paddr + resp_hdr_offset, resp_hdr, sizeof(resp_hdr));

                uint32_t resp_val = 0;
                if (svc_id == 6 && sub_cmd == 3) {
                    resp_val = 0x00405000;
                }
                cpu_physical_memory_write(paddr + resp_hdr_offset + sizeof(resp_hdr), &resp_val, sizeof(resp_val));
            }
        }
        env->xregs[0] = QCOM_SCM_SUCCESS;
        return true;
    }

    return false;
}
