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

        /*
         * QSEECOM Trusted App send_cmd (0x70000001 / owner 48/49):
         * x2: app_id
         * x3: req_ptr physical address
         * x4: req_len
         * x5: pointer to descriptor table in guest RAM containing rsp_buf physical address
         *
         * Populate Keymaster/Gatekeeper response buffer at real_rsp_pa:
         * offset 0: uint32_t status = 0 (KM_ERROR_OK)
         * offset 4: uint32_t payload_len
         * offset 8: CBOR payload
         */
        if (fn == 0x70000001 || owner == ARM_SMCCC_OWNER_TRUSTED_APP_48 || owner == ARM_SMCCC_OWNER_TRUSTED_APP_49) {
            uint64_t desc_pa = env->xregs[5];
            uint64_t real_rsp_pa = 0;
            if (desc_pa >= 0x80000000ULL && desc_pa < 0x200000000ULL) {
                cpu_physical_memory_read(desc_pa, &real_rsp_pa, sizeof(real_rsp_pa));
            }
            if (real_rsp_pa < 0x80000000ULL || real_rsp_pa >= 0x200000000ULL) {
                real_rsp_pa = desc_pa;
            }

            if (real_rsp_pa >= 0x80000000ULL && real_rsp_pa < 0x200000000ULL) {
                uint32_t cmd_id = 0;
                uint64_t req_pa = env->xregs[3];
                uint8_t req_dump[32];
                if (req_pa >= 0x80000000ULL && req_pa < 0x200000000ULL) {
                    cpu_physical_memory_read(req_pa, &cmd_id, sizeof(cmd_id));
                    cpu_physical_memory_read(req_pa, req_dump, sizeof(req_dump));
                }
                fprintf(stderr, "[KM_CMD] cmd_id=0x%x req_len=%u real_rsp_pa=0x%" PRIx64 " req[0..15]=",
                        cmd_id, (uint32_t)env->xregs[4], real_rsp_pa);
                for (int d = 0; d < 16; d++) {
                    fprintf(stderr, "%02x", req_dump[d]);
                }
                fprintf(stderr, "\n");

                /*
                 * 0. get_keymaster_version (cmd 0x200):
                 * status = 0, major = 4, is_ta = 1, hal = 4, minor = 0x300 (>= 0x21c)
                 */
                if (cmd_id == 0x200) {
                    static const uint32_t km_version_resp[5] = {
                        0,      /* status = 0 */
                        4,      /* major_version = 4 */
                        1,      /* is_ta = 1 */
                        4,      /* hal_version = 4 */
                        0x300   /* ta_minor_version = 0x300 (>= 0x21c for modern CBOR blobs) */
                    };
                    cpu_physical_memory_write(real_rsp_pa, km_version_resp, sizeof(km_version_resp));
                    env->xregs[0] = QCOM_SCM_SUCCESS;
                    env->xregs[1] = QSEOS_RESULT_SUCCESS;
                    env->xregs[2] = QSEOS_APP_ID;
                    env->xregs[3] = 1;
                    return true;
                }

                /*
                 * 1. get_hmac_sharing_parameters (keys 41 & 42): 73 bytes
                 */
                static const uint8_t km_get_hmac_resp[] = {
                    0xa2,
                    0x18, 0x29, 0x58, 0x20,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x18, 0x2a, 0x58, 0x20,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };

                /*
                 * 2. compute_shared_hmac (key 44): 37 bytes
                 */
                static const uint8_t km_compute_hmac_resp[] = {
                    0xa1,
                    0x18, 0x2c, 0x58, 0x20,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };

                /*
                 * 3. generate_key (KMKD keyblob + nested characteristics): 14 bytes
                 */
                static const uint8_t km_keyblob_resp[] = {
                    0x84,
                    0x1a, 0x4b, 0x4d, 0x4b, 0x44,
                    0x01,
                    0x01,
                    0x45, 0xa2, 0x01, 0xa0, 0x00, 0xa0
                };

                /*
                 * 4a. begin_operation without nonce (for DECRYPT or caller-supplied nonce): 6 bytes
                 */
                static const uint8_t km_begin_resp[] = {
                    0xa2,
                    0x16, 0x00,
                    0x18, 0x22, 0x01
                };

                /*
                 * 4b. begin_operation with nonce (for ENCRYPT without nonce): 24 bytes
                 * Tag 0x900003e9 is KM_TAG_NONCE (KM_BYTES | 1001).
                 */
                static const uint8_t km_begin_nonce_resp[] = {
                    0xa3,                                       /* map of 3 items */
                    0x16, 0x01,                                 /* key 22 (params count): 1 */
                    0x1a, 0x90, 0x00, 0x03, 0xe9,               /* key 0x900003e9 (KM_TAG_NONCE) */
                    0x4c,                                       /* byte string of 12 bytes */
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                    0x09, 0x0a, 0x0b, 0x0c,                     /* 12-byte GCM nonce */
                    0x18, 0x22, 0x01                            /* key 34 (handle): 1 */
                };


                /*
                 * 6. finish_operation (key 22: uint 0, key 36: 16-byte MAC tag): 22 bytes
                 */
                static const uint8_t km_finish_resp[] = {
                    0xa2,                                       /* map of 2 items */
                    0x16, 0x00,                                 /* key 22: 0 outParams */
                    0x18, 0x24,                                 /* key 36: output */
                    0x50,                                       /* byte string of 16 bytes (MAC tag) */
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };

                /*
                 * 7. export_key (key 36: 64 bytes zeros for fscrypt 512-bit XTS master key): 69 bytes
                 */
                static const uint8_t km_export_resp[] = {
                    0xa1,
                    0x18, 0x24, 0x58, 0x40,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                };

                uint32_t resp_hdr[2];
                resp_hdr[0] = 0; /* status = KM_ERROR_OK */
                if (cmd_id == 0x220e || cmd_id == 0x2706) {
                    /* get_hmac_sharing_parameters */
                    resp_hdr[1] = sizeof(km_get_hmac_resp);
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                    cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_get_hmac_resp, sizeof(km_get_hmac_resp));
                } else if (cmd_id == 0x220f || cmd_id == 0x2707) {
                    /* compute_shared_hmac */
                    resp_hdr[1] = sizeof(km_compute_hmac_resp);
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                    cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_compute_hmac_resp, sizeof(km_compute_hmac_resp));
                } else if (cmd_id == 0x2108 || cmd_id == 0x2508 ||
                           cmd_id == 0x210b || cmd_id == 0x250b ||
                           cmd_id == 0x2114 || cmd_id == 0x2514) {
                    /* generate_key, import_key, upgrade_key */
                    resp_hdr[1] = sizeof(km_keyblob_resp);
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                    cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_keyblob_resp, sizeof(km_keyblob_resp));
                } else if (cmd_id == 0x210f || cmd_id == 0x250f) {
                    /* begin_operation */
                    bool has_nonce = false;
                    uint8_t rdata[256];
                    uint32_t rlen = (uint32_t)env->xregs[4];
                    if (rlen > sizeof(rdata)) {
                        rlen = sizeof(rdata);
                    }
                    if (rlen > 0 && req_pa >= 0x80000000ULL && req_pa < 0x200000000ULL) {
                        cpu_physical_memory_read(req_pa, rdata, rlen);
                        for (uint32_t i = 0; i + 4 <= rlen; i++) {
                            if ((rdata[i] == 0x90 && rdata[i+1] == 0x00 &&
                                 rdata[i+2] == 0x03 && rdata[i+3] == 0xe9) ||
                                (rdata[i] == 0x3a && rdata[i+1] == 0x6f &&
                                 rdata[i+2] == 0xff && rdata[i+3] == 0xfc &&
                                 i + 5 <= rlen && rdata[i+4] == 0x16)) {
                                has_nonce = true;
                                break;
                            }
                        }
                    }
                    if (has_nonce) {
                        resp_hdr[1] = sizeof(km_begin_resp);
                        cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                        cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_begin_resp, sizeof(km_begin_resp));
                    } else {
                        resp_hdr[1] = sizeof(km_begin_nonce_resp);
                        cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                        cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_begin_nonce_resp, sizeof(km_begin_nonce_resp));
                    }
                } else if (cmd_id == 0x2111 || cmd_id == 0x2511) {
                    /* update_operation: parse input bytes length (key 35) to return as input_consumed (key 37) */
                    uint8_t rdata[256];
                    uint32_t rlen = (uint32_t)env->xregs[4];
                    if (rlen > sizeof(rdata)) {
                        rlen = sizeof(rdata);
                    }
                    uint32_t consumed = 64;
                    if (rlen > 0 && req_pa >= 0x80000000ULL && req_pa < 0x200000000ULL) {
                        cpu_physical_memory_read(req_pa, rdata, rlen);
                        for (uint32_t i = 0; i + 3 < rlen; i++) {
                            if (rdata[i] == 0x18 && rdata[i+1] == 0x23) {
                                if (rdata[i+2] >= 0x40 && rdata[i+2] <= 0x57) {
                                    consumed = rdata[i+2] - 0x40;
                                } else if (rdata[i+2] == 0x58 && i + 3 < rlen) {
                                    consumed = rdata[i+3];
                                } else if (rdata[i+2] == 0x59 && i + 4 < rlen) {
                                    consumed = ((uint32_t)rdata[i+3] << 8) | rdata[i+4];
                                }
                                break;
                            }
                        }
                    }
                    uint8_t dyn_resp[32];
                    uint32_t dpos = 0;
                    dyn_resp[dpos++] = 0xa3; /* map of 3 items */
                    dyn_resp[dpos++] = 0x16; /* key 22 */
                    dyn_resp[dpos++] = 0x00; /* 0 params */
                    dyn_resp[dpos++] = 0x18; /* key 36 */
                    dyn_resp[dpos++] = 0x24;
                    dyn_resp[dpos++] = 0x40; /* 0 bytes output */
                    dyn_resp[dpos++] = 0x18; /* key 37 */
                    dyn_resp[dpos++] = 0x25;
                    if (consumed < 24) {
                        dyn_resp[dpos++] = (uint8_t)consumed;
                    } else if (consumed < 256) {
                        dyn_resp[dpos++] = 0x18;
                        dyn_resp[dpos++] = (uint8_t)consumed;
                    } else {
                        dyn_resp[dpos++] = 0x19;
                        dyn_resp[dpos++] = (uint8_t)(consumed >> 8);
                        dyn_resp[dpos++] = (uint8_t)(consumed & 0xff);
                    }
                    resp_hdr[1] = dpos;
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                    cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), dyn_resp, dpos);
                } else if (cmd_id == 0x2112 || cmd_id == 0x2512) {
                    /* finish_operation */
                    resp_hdr[1] = sizeof(km_finish_resp);
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                    cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_finish_resp, sizeof(km_finish_resp));
                } else if (cmd_id == 0x210c || cmd_id == 0x250c) {
                    /* export_key */
                    resp_hdr[1] = sizeof(km_export_resp);
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                    cpu_physical_memory_write(real_rsp_pa + sizeof(resp_hdr), km_export_resp, sizeof(km_export_resp));
                } else {
                    /*
                     * Commands requiring no response payload (status 0 only):
                     * delete_key (0x210d/0x250d), delete_all_keys (0x210e),
                     * abort_operation (0x2113/0x2513), configure (0x2116),
                     * set_early_boot_ended (0x2119/0x211c), set_device_locked (0x211a/0x211d),
                     * destroy_device_ids (0x220b/0x2704), get_key_characteristics (0x2109/0x2509)
                     */
                    resp_hdr[1] = 0;
                    cpu_physical_memory_write(real_rsp_pa, resp_hdr, sizeof(resp_hdr));
                }
            }
        }

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

                uint32_t scm_resp[3] = { QSEOS_RESULT_SUCCESS, QSEOS_APP_ID, 1 };
                if (svc_id == 6 && sub_cmd == 3) {
                    scm_resp[0] = 0x00405000;
                }
                cpu_physical_memory_write(paddr + resp_hdr_offset + sizeof(resp_hdr), scm_resp, sizeof(scm_resp));
            }
        }
        env->xregs[0] = QCOM_SCM_SUCCESS;
        return true;
    }

    return false;
}
