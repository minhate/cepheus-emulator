/* STUB(boot_rom_chain) */
/*
 * Qualcomm SM8150 UFS Host Controller Model
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
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "block/ufs.h"
#include "hw/arm/qcom_sm8150_ufs.h"

/* Standard UFSHCI offsets */
#define REG_CAP         0x00
#define REG_MCQCAP      0x04
#define REG_VER         0x08
#define REG_HCPID       0x10
#define REG_HCMID       0x14
#define REG_AHIT        0x18
#define REG_IS          0x20
#define REG_IE          0x24
#define REG_HCS         0x30
#define REG_HCE         0x34
#define REG_UECPA       0x38
#define REG_UECDL       0x3C
#define REG_UECN        0x40
#define REG_UECT        0x44
#define REG_UECDME      0x48
#define REG_UTRIACR     0x4C
#define REG_UTRLBA      0x50
#define REG_UTRLBAU     0x54
#define REG_UTRLDBR     0x58
#define REG_UTRLCLR     0x5C
#define REG_UTRLRSR     0x60
#define REG_UTMRLBA     0x70
#define REG_UTMRLBAU    0x74
#define REG_UTMRDBR     0x78
#define REG_UTMRLCLR    0x7C
#define REG_UTMRLRSR    0x80
#define REG_UICCMD      0x90
#define REG_UICCMDARG1  0x94
#define REG_UICCMDARG2  0x98
#define REG_UICCMDARG3  0x9C

/* Qualcomm vendor offsets */
#define REG_UFS_SYS1CLK_1US         0xC0
#define REG_UFS_TX_SYMBOL_CLK_NS_US 0xC4
#define REG_UFS_LOCAL_PORT_ATTR     0xC8
#define REG_UFS_PA_ERR_CODE         0xCC
#define REG_UFS_RETRY_TIMER_VAL     0xD0
#define REG_UFS_REG_BUSY            0xD4
#define REG_UFS_MEM_ICE_CONFIG      0xD8
#define REG_UFS_CFG1                0xDC
#define REG_UFS_CFG2                0xE0
#define REG_UFS_HW_VERSION          0xE4
#define REG_UFS_TX_SYS_CLK_FREQ     0xE8
#define REG_UFS_UNIPRO_CFG          0xF0
#define REG_UFS_UNIPRO_STATUS       0x1EC

#define UFS_NUM_LUNS                6
#define UFS_LUN_BLOCKS              8388608ULL /* 32 GB at 4KB per block */
#define UFS_BLOCK_SIZE              4096

/* STUB(boot_rom_chain) */
/*
 * Reproduce ABL / TrustZone handoff state:
 * In the stock boot chain, ABL/XBL and TrustZone set up inline crypto engine (ICE)
 * and hand off a non-busy resource to Linux. Since QEMU boots the kernel directly,
 * we clear IORESOURCE_BUSY on the 0x1d90000 ICE resource in iomem_resource so
 * devm_ioremap_resource in ufshcd_crypto_qti_init_crypto succeeds.
 */
static void qcom_sm8150_ufs_fixup_ice_resource(void)
{
    static bool done = false;
    if (done) return;
    done = true;

    uint64_t found_phys = 0;
    for (uint64_t p = 0x82620000; p < 0x82640000; p += 8) {
        uint64_t s = 0, e = 0, f = 0;
        dma_memory_read(&address_space_memory, p, &s, 8, MEMTXATTRS_UNSPECIFIED);
        dma_memory_read(&address_space_memory, p + 8, &e, 8, MEMTXATTRS_UNSPECIFIED);
        dma_memory_read(&address_space_memory, p + 24, &f, 8, MEMTXATTRS_UNSPECIFIED);
        if (s == 0 && e == 0xffffffffffffffffULL && (f & 0xfff) == 0x200) {
            found_phys = p;
            break;
        }
    }

    if (!found_phys) return;

    uint64_t child_ptr = 0;
    dma_memory_read(&address_space_memory, found_phys + 0x38, &child_ptr, 8, MEMTXATTRS_UNSPECIFIED);

    uint64_t cur = child_ptr;
    int count = 0;
    while (cur && count++ < 200) {
        uint64_t cur_phys = 0;
        if (cur >= 0xffffffc000000000ULL) {
            cur_phys = cur - 0xffffffc000000000ULL + 0x80000000ULL;
        } else if (cur >= 0xffffff8000000000ULL) {
            cur_phys = cur - 0xffffff8008080000ULL + 0x80080000ULL;
        } else {
            cur_phys = cur;
        }

        uint64_t r_start = 0, r_flags = 0, r_sibling = 0;
        dma_memory_read(&address_space_memory, cur_phys, &r_start, 8, MEMTXATTRS_UNSPECIFIED);
        dma_memory_read(&address_space_memory, cur_phys + 24, &r_flags, 8, MEMTXATTRS_UNSPECIFIED);
        dma_memory_read(&address_space_memory, cur_phys + 0x30, &r_sibling, 8, MEMTXATTRS_UNSPECIFIED);

        if (r_start == 0x01d90000) {
            uint64_t clean_flags = r_flags & ~0x80000000ULL;
            dma_memory_write(&address_space_memory, cur_phys + 24, &clean_flags, 8, MEMTXATTRS_UNSPECIFIED);
            break;
        }
        cur = r_sibling;
    }
}

static void qcom_ufs_update_irq(QComSm8150UfsState *s)
{
    bool level = (s->is & s->ie) != 0;
    qemu_set_irq(s->irq, level);
}

static void qcom_ufs_process_transfer(QComSm8150UfsState *s)
{
    uint32_t active_mask = s->utrldbr;
    if (!active_mask) {
        return;
    }

    hwaddr utrl_base = (((hwaddr)s->utrlbau) << 32) | s->utrlba;

    for (int slot = 0; slot < 32; slot++) {
        if (!(active_mask & (1U << slot))) {
            continue;
        }

        hwaddr utrd_addr = utrl_base + slot * sizeof(UtpTransferReqDesc);
        UtpTransferReqDesc utrd;
        if (dma_memory_read(&address_space_memory, utrd_addr, &utrd,
                            sizeof(utrd), MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            continue;
        }

        hwaddr req_addr = (((hwaddr)le32_to_cpu(utrd.command_desc_base_addr_hi)) << 32) |
                          le32_to_cpu(utrd.command_desc_base_addr_lo);
        hwaddr rsp_addr = req_addr + le16_to_cpu(utrd.response_upiu_offset) * sizeof(uint32_t);
        hwaddr prdt_addr = req_addr + le16_to_cpu(utrd.prd_table_offset) * sizeof(uint32_t);
        uint16_t prdt_len = le16_to_cpu(utrd.prd_table_length);

        UtpUpiuReq req;
        memset(&req, 0, sizeof(req));
        dma_memory_read(&address_space_memory, req_addr, &req, sizeof(req), MEMTXATTRS_UNSPECIFIED);

        UtpUpiuRsp rsp;
        memset(&rsp, 0, sizeof(rsp));

        uint8_t trans_type = req.header.trans_type;

        if (trans_type == UFS_UPIU_TRANSACTION_NOP_OUT) {
            rsp.header.trans_type = UFS_UPIU_TRANSACTION_NOP_IN;
            rsp.header.task_tag = req.header.task_tag;
            rsp.header.response = 0x00;
            utrd.header.dword_2 = 0; /* OCS_SUCCESS */
        } else if (trans_type == UFS_UPIU_TRANSACTION_QUERY_REQ) {
            rsp.header.trans_type = UFS_UPIU_TRANSACTION_QUERY_RSP;
            rsp.header.task_tag = req.header.task_tag;
            rsp.header.query_func = req.header.query_func;
            rsp.header.response = UFS_QUERY_RESULT_SUCCESS;

            uint8_t opcode = req.qr.opcode;
            rsp.qr.opcode = opcode;
            rsp.qr.idn = req.qr.idn;
            rsp.qr.index = req.qr.index;
            rsp.qr.selector = req.qr.selector;

            if (opcode == UFS_UPIU_QUERY_OPCODE_READ_DESC) {
                uint8_t desc_buf[256];
                memset(desc_buf, 0, sizeof(desc_buf));
                uint16_t desc_len = 0;

                switch (req.qr.idn) {
                case UFS_QUERY_DESC_IDN_DEVICE:
                    desc_len = 0x40;
                    desc_buf[0] = 0x40;
                    desc_buf[1] = UFS_QUERY_DESC_IDN_DEVICE;
                    desc_buf[2] = 0x01; /* UFS device */
                    desc_buf[3] = 0x01;
                    desc_buf[6] = UFS_NUM_LUNS;
                    desc_buf[7] = 4;    /* WLUNs */
                    desc_buf[8] = 1;    /* Boot enable */
                    desc_buf[16] = 0x03; /* wSpecVersion: UFS 3.0 (0x0300) */
                    desc_buf[17] = 0x00;
                    desc_buf[20] = 1;   /* Manufacturer String index */
                    desc_buf[21] = 2;   /* Product String index */
                    desc_buf[22] = 3;   /* Serial Number index */
                    desc_buf[23] = 4;   /* OEM ID index */
                    break;
                case UFS_QUERY_DESC_IDN_CONFIGURATION:
                    desc_len = 0x90;
                    desc_buf[0] = 0x90;
                    desc_buf[1] = UFS_QUERY_DESC_IDN_CONFIGURATION;
                    desc_buf[2] = 1;    /* Boot enable */
                    break;
                case UFS_QUERY_DESC_IDN_UNIT: {
                    desc_len = 0x2D;
                    desc_buf[0] = 0x2D;
                    desc_buf[1] = UFS_QUERY_DESC_IDN_UNIT;
                    desc_buf[2] = req.qr.index;
                    desc_buf[3] = 1;    /* bLUEnable */
                    desc_buf[4] = 0;    /* bBootLunID */
                    desc_buf[5] = 0;    /* bLUWriteProtect */
                    desc_buf[7] = 12;   /* 4096 bytes block size (2^12) */
                    uint64_t blks = cpu_to_be64(UFS_LUN_BLOCKS);
                    memcpy(&desc_buf[11], &blks, 8);
                    break;
                }
                case UFS_QUERY_DESC_IDN_GEOMETRY: {
                    desc_len = 0x57;
                    desc_buf[0] = 0x57;
                    desc_buf[1] = UFS_QUERY_DESC_IDN_GEOMETRY;
                    desc_buf[2] = 1;    /* RawDeviceType */
                    desc_buf[4] = 8;    /* MinAddrBlockSize: 4096 bytes */
                    uint64_t raw_cap = cpu_to_be64(UFS_LUN_BLOCKS * UFS_NUM_LUNS * 8);
                    memcpy(&desc_buf[12], &raw_cap, 8);
                    break;
                }
                case UFS_QUERY_DESC_IDN_STRING: {
                    const char *s_text = "CEPHEUS";
                    if (req.qr.index == 1) {
                        s_text = "SAMSUNG";
                    } else if (req.qr.index == 2) {
                        s_text = "KLUEG8U1EA-B0C1";
                    } else if (req.qr.index == 3) {
                        s_text = "000000000001";
                    }
                    size_t slen = strlen(s_text);
                    desc_len = 2 + slen * 2;
                    desc_buf[0] = desc_len;
                    desc_buf[1] = UFS_QUERY_DESC_IDN_STRING;
                    for (size_t si = 0; si < slen; si++) {
                        desc_buf[2 + si * 2] = 0;
                        desc_buf[2 + si * 2 + 1] = (uint8_t)s_text[si];
                    }
                    break;
                }
                case UFS_QUERY_DESC_IDN_HEALTH:
                    desc_len = 0x2D;
                    desc_buf[0] = 0x2D;
                    desc_buf[1] = UFS_QUERY_DESC_IDN_HEALTH;
                    break;
                default:
                    desc_len = 0x20;
                    desc_buf[0] = desc_len;
                    desc_buf[1] = req.qr.idn;
                    break;
                }

                uint16_t req_data_len = be16_to_cpu(req.qr.length);
                if (req_data_len > 0 && req_data_len < desc_len) {
                    desc_len = req_data_len;
                }
                rsp.header.data_segment_length = cpu_to_be16(desc_len);
                rsp.qr.length = cpu_to_be16(desc_len);
                memcpy(rsp.qr.data, desc_buf, desc_len);
            } else if (opcode == UFS_UPIU_QUERY_OPCODE_READ_FLAG) {
                /* Flag 1 is fDeviceInit; 0 means initialized */
                rsp.qr.value = 0;
            } else if (opcode == UFS_UPIU_QUERY_OPCODE_READ_ATTR) {
                /* Attribute 2 is bCurrentPowerMode (0x11 = Active) */
                if (req.qr.idn == 0x02) {
                    rsp.qr.value = cpu_to_be32(0x11);
                } else {
                    rsp.qr.value = 0;
                }
            } else if (opcode == UFS_UPIU_QUERY_OPCODE_SET_FLAG ||
                       opcode == UFS_UPIU_QUERY_OPCODE_CLEAR_FLAG) {
                rsp.qr.value = 0;
            }

            utrd.header.dword_2 = 0; /* OCS_SUCCESS */
        } else if (trans_type == UFS_UPIU_TRANSACTION_COMMAND) {
            rsp.header.trans_type = UFS_UPIU_TRANSACTION_RESPONSE;
            rsp.header.task_tag = req.header.task_tag;
            rsp.header.response = 0x00; /* Target Success */
            rsp.header.scsi_status = 0x00; /* SAM_STAT_GOOD */

            uint8_t cdb_op = req.sc.cdb[0];
            uint8_t lun = req.header.lun;

            uint8_t scsi_data[512];
            uint32_t scsi_data_len = 0;
            memset(scsi_data, 0, sizeof(scsi_data));

            switch (cdb_op) {
            case 0x12: /* INQUIRY */
                if (req.sc.cdb[1] & 1) { /* EVPD */
                    uint8_t page = req.sc.cdb[2];
                    if (page == 0x00) { /* Supported VPD pages */
                        scsi_data[0] = (lun < UFS_NUM_LUNS) ? 0x00 : 0x1E;
                        scsi_data[1] = 0x00;
                        scsi_data[3] = 3;
                        scsi_data[4] = 0x00;
                        scsi_data[5] = 0x80;
                        scsi_data[6] = 0x83;
                        scsi_data_len = 7;
                    } else if (page == 0x80) { /* Serial Number */
                        scsi_data[0] = (lun < UFS_NUM_LUNS) ? 0x00 : 0x1E;
                        scsi_data[1] = 0x80;
                        scsi_data[3] = 10;
                        memcpy(&scsi_data[4], "CEPHEUS001", 10);
                        scsi_data_len = 14;
                    } else if (page == 0x83) { /* Device ID */
                        scsi_data[0] = (lun < UFS_NUM_LUNS) ? 0x00 : 0x1E;
                        scsi_data[1] = 0x83;
                        scsi_data[3] = 12;
                        scsi_data[4] = 1; /* Binary */
                        scsi_data[5] = 3; /* NAA */
                        scsi_data[7] = 8;
                        scsi_data[8] = 0x51;
                        scsi_data[9] = 0x00;
                        scsi_data[10] = 0x81;
                        scsi_data[11] = lun;
                        scsi_data_len = 16;
                    }
                } else { /* Standard INQUIRY */
                    if (lun < UFS_NUM_LUNS) {
                        scsi_data[0] = 0x00; /* Direct-access block device (disk) */
                    } else {
                        scsi_data[0] = 0x1E; /* Well-known logical unit */
                    }
                    scsi_data[1] = 0x00; /* Non-removable */
                    scsi_data[2] = 0x06; /* SPC-4 */
                    scsi_data[3] = 0x02; /* Format */
                    scsi_data[4] = 31;   /* Additional length (36 total) */
                    memcpy(&scsi_data[8],  "SAMSUNG ", 8);
                    memcpy(&scsi_data[16], "KLUEG8U1EA-B0C1 ", 16);
                    memcpy(&scsi_data[32], "0100", 4);
                    scsi_data_len = 36;
                }
                break;

            case 0x25: { /* READ_CAPACITY_10 */
                uint32_t last_lba = cpu_to_be32((uint32_t)(UFS_LUN_BLOCKS - 1));
                uint32_t block_sz = cpu_to_be32(UFS_BLOCK_SIZE);
                memcpy(&scsi_data[0], &last_lba, 4);
                memcpy(&scsi_data[4], &block_sz, 4);
                scsi_data_len = 8;
                break;
            }

            case 0x9E: { /* SERVICE_ACTION_IN_16 / READ_CAPACITY_16 */
                uint64_t last_lba = cpu_to_be64(UFS_LUN_BLOCKS - 1);
                uint32_t block_sz = cpu_to_be32(UFS_BLOCK_SIZE);
                memcpy(&scsi_data[0], &last_lba, 8);
                memcpy(&scsi_data[8], &block_sz, 4);
                scsi_data_len = 32;
                break;
            }

            case 0xA0: { /* REPORT_LUNS */
                uint32_t luns_len = cpu_to_be32(UFS_NUM_LUNS * 8);
                memcpy(&scsi_data[0], &luns_len, 4);
                for (int l = 0; l < UFS_NUM_LUNS; l++) {
                    uint64_t l_val = cpu_to_be64(((uint64_t)l) << 48);
                    memcpy(&scsi_data[8 + l * 8], &l_val, 8);
                }
                scsi_data_len = 8 + UFS_NUM_LUNS * 8;
                break;
            }

            case 0x1A: /* MODE_SENSE_6 */
                scsi_data[0] = 3;
                scsi_data[1] = 0;
                scsi_data[2] = 0;
                scsi_data[3] = 0;
                scsi_data_len = 4;
                break;

            case 0x5A: /* MODE_SENSE_10 */
                scsi_data[0] = 0;
                scsi_data[1] = 6;
                scsi_data[2] = 0;
                scsi_data[3] = 0;
                scsi_data_len = 8;
                break;

            case 0x03: /* REQUEST_SENSE */
                scsi_data[0] = 0x70; /* Current error */
                scsi_data[2] = 0x00; /* NO SENSE */
                scsi_data[7] = 10;
                scsi_data_len = 18;
                break;

            case 0x00: /* TEST_UNIT_READY */
            case 0x1B: /* START_STOP_UNIT */
            case 0x35: /* SYNCHRONIZE_CACHE_10 */
                scsi_data_len = 0;
                break;

            case 0x08: /* READ_6 */
            case 0x28: /* READ_10 */
            case 0x88: /* READ_16 */ {
                /* Zero-fill guest read buffer */
                uint8_t zero_page[4096];
                memset(zero_page, 0, sizeof(zero_page));
                hwaddr prd_entry_addr = prdt_addr;
                for (int pi = 0; pi < prdt_len; pi++) {
                    UfshcdSgEntry entry;
                    dma_memory_read(&address_space_memory, prd_entry_addr, &entry,
                                    sizeof(entry), MEMTXATTRS_UNSPECIFIED);
                    uint64_t b_addr = le64_to_cpu(entry.addr);
                    uint32_t b_size = le32_to_cpu(entry.size) + 1;
                    while (b_size > 0) {
                        uint32_t chunk = MIN(b_size, (uint32_t)sizeof(zero_page));
                        dma_memory_write(&address_space_memory, b_addr, zero_page, chunk,
                                         MEMTXATTRS_UNSPECIFIED);
                        b_addr += chunk;
                        b_size -= chunk;
                    }
                    prd_entry_addr += sizeof(UfshcdSgEntry);
                }
                scsi_data_len = 0;
                break;
            }

            default:
                scsi_data_len = 0;
                break;
            }

            /* Copy SCSI command response data to PRDT buffer if requested */
            if (scsi_data_len > 0 && prdt_len > 0) {
                hwaddr prd_entry_addr = prdt_addr;
                uint32_t written = 0;
                for (int pi = 0; pi < prdt_len && written < scsi_data_len; pi++) {
                    UfshcdSgEntry entry;
                    dma_memory_read(&address_space_memory, prd_entry_addr, &entry,
                                    sizeof(entry), MEMTXATTRS_UNSPECIFIED);
                    uint64_t b_addr = le64_to_cpu(entry.addr);
                    uint32_t b_size = (le32_to_cpu(entry.size) & 0x3ffff) + 1;
                    uint32_t chunk = MIN(b_size, scsi_data_len - written);
                    dma_memory_write(&address_space_memory, b_addr, scsi_data + written,
                                     chunk, MEMTXATTRS_UNSPECIFIED);
                    written += chunk;
                    prd_entry_addr += sizeof(UfshcdSgEntry);
                }
            }

            utrd.header.dword_2 = 0; /* OCS_SUCCESS */
        }

        /* Write Response UPIU */
        dma_memory_write(&address_space_memory, rsp_addr, &rsp, sizeof(rsp),
                         MEMTXATTRS_UNSPECIFIED);

        /* Write back UTRD with OCS_SUCCESS */
        dma_memory_write(&address_space_memory, utrd_addr, &utrd, sizeof(utrd),
                         MEMTXATTRS_UNSPECIFIED);

        s->utrldbr &= ~(1U << slot);
    }

    /* Raise completion interrupt */
    s->is |= (1 << 0); /* UTRCS: UTP Transfer Request Completion Status */
    qcom_ufs_update_irq(s);
}

static uint64_t qcom_ufs_read(void *opaque, hwaddr offset, unsigned size)
{
    QComSm8150UfsState *s = QCOM_SM8150_UFS(opaque);

    switch (offset) {
    case REG_CAP:
        qcom_sm8150_ufs_fixup_ice_resource();
        return s->cap;
    case REG_VER:
        return s->ver;
    case REG_HCPID:
        return s->hcpid;
    case REG_HCMID:
        return s->hcmid;
    case REG_AHIT:
        return s->ahit;
    case REG_IS:
        return s->is;
    case REG_IE:
        return s->ie;
    case REG_HCS:
        return s->hcs;
    case REG_HCE:
        qcom_sm8150_ufs_fixup_ice_resource();
        return s->hce;
    case REG_UECPA:
        return s->uecpa;
    case REG_UTRLBA:
        return s->utrlba;
    case REG_UTRLBAU:
        return s->utrlbau;
    case REG_UTRLDBR:
        return s->utrldbr;
    case REG_UTRLCLR:
        return s->utrlclr;
    case REG_UTRLRSR:
        return s->utrlrsr;
    case REG_UTMRLBA:
        return s->utmrlba;
    case REG_UTMRLBAU:
        return s->utmrlbau;
    case REG_UTMRDBR:
        return s->utmrdbr;
    case REG_UTMRLCLR:
        return s->utmrlclr;
    case REG_UTMRLRSR:
        return s->utmrlrsr;
    case REG_UICCMD:
        return s->uiccmd;
    case REG_UICCMDARG1:
        return s->uiccmdarg1;
    case REG_UICCMDARG2:
        return s->uiccmdarg2;
    case REG_UICCMDARG3:
        return s->uiccmdarg3;

    /* Crypto Registers */
    case 0x100: /* CCAP - Crypto Capabilities */
        return 0x00001f01; /* 1 capability descriptor, 32 keyslots (val 31 in bits 8..15) */
    case 0x104: /* CRYPTOCAP 0 */
        return 0x00030200; /* Alg 0 (AES-XTS), 256-bit key (2 in bits 8..15), config 3 */

    /* Qualcomm vendor registers */
    case REG_UFS_SYS1CLK_1US:
        return s->qcom_sys1clk_1us;
    case REG_UFS_TX_SYMBOL_CLK_NS_US:
        return s->qcom_tx_symbol_clk_ns_us;
    case REG_UFS_REG_BUSY:
        return 0;
    case REG_UFS_CFG1:
        return s->qcom_cfg1;
    case REG_UFS_CFG2:
        return s->qcom_cfg2;
    case REG_UFS_HW_VERSION:
        return s->qcom_hw_ver;
    case REG_UFS_UNIPRO_CFG:
        return s->qcom_unipro_cfg;
    case REG_UFS_UNIPRO_STATUS:
        return s->qcom_unipro_status;

    default:
        return 0;
    }
}

static void qcom_ufs_write(void *opaque, hwaddr offset, uint64_t data, unsigned size)
{
    QComSm8150UfsState *s = QCOM_SM8150_UFS(opaque);

    switch (offset) {
    case REG_AHIT:
        s->ahit = (uint32_t)data;
        break;
    case REG_IS:
        s->is &= ~(uint32_t)data; /* W1C */
        qcom_ufs_update_irq(s);
        break;
    case REG_IE:
        s->ie = (uint32_t)data;
        qcom_ufs_update_irq(s);
        break;
    case REG_HCE:
        qcom_sm8150_ufs_fixup_ice_resource();
        if (data & 1) {
            s->hce = 1;
            s->hcs = 0x0F; /* DP (bit 0) | UTRLRS (bit 1) | UTMRLRS (bit 2) | UCRDY (bit 3) */
        } else {
            s->hce = 0;
            s->hcs = 0;
        }
        break;
    case REG_UTRLBA:
        s->utrlba = (uint32_t)data & ~0x3ff;
        break;
    case REG_UTRLBAU:
        s->utrlbau = (uint32_t)data;
        break;
    case REG_UTRLDBR:
        s->utrldbr |= (uint32_t)data;
        qcom_ufs_process_transfer(s);
        break;
    case REG_UTRLCLR:
        s->utrlclr = (uint32_t)data;
        s->utrldbr &= ~(uint32_t)data;
        break;
    case REG_UTRLRSR:
        s->utrlrsr = (uint32_t)data & 1;
        if (data & 1) {
            s->hcs |= (1 << 1); /* UTRLRS */
        } else {
            s->hcs &= ~(1 << 1);
        }
        break;
    case REG_UTMRLBA:
        s->utmrlba = (uint32_t)data & ~0x3ff;
        break;
    case REG_UTMRLBAU:
        s->utmrlbau = (uint32_t)data;
        break;
    case REG_UTMRDBR:
        s->utmrdbr = (uint32_t)data;
        {
            hwaddr utmrl_base = (((hwaddr)s->utmrlbau) << 32) | s->utmrlba;
            for (int slot = 0; slot < 8; slot++) {
                if (s->utmrdbr & (1U << slot)) {
                    hwaddr utmrd_addr = utmrl_base + slot * sizeof(UtpTaskReqDesc);
                    UtpTaskReqDesc utmrd;
                    if (dma_memory_read(&address_space_memory, utmrd_addr, &utmrd,
                                        sizeof(utmrd), MEMTXATTRS_UNSPECIFIED) == MEMTX_OK) {
                        utmrd.header.dword_2 = 0; /* OCS_SUCCESS */
                        utmrd.upiu_rsp.rsp_header.trans_type = UFS_UPIU_TRANSACTION_TASK_RSP;
                        utmrd.upiu_rsp.rsp_header.task_tag = utmrd.upiu_req.req_header.task_tag;
                        utmrd.upiu_rsp.rsp_header.response = 0x00; /* Function complete */
                        dma_memory_write(&address_space_memory, utmrd_addr, &utmrd,
                                         sizeof(utmrd), MEMTXATTRS_UNSPECIFIED);
                    }
                }
            }
        }
        s->utmrdbr = 0;
        s->is |= (1 << 2); /* UTP Task Management Request Completion */
        qcom_ufs_update_irq(s);
        break;
    case REG_UTMRLCLR:
        s->utmrlclr = (uint32_t)data;
        break;
    case REG_UTMRLRSR:
        s->utmrlrsr = (uint32_t)data & 1;
        if (data & 1) {
            s->hcs |= (1 << 2); /* UTMRLRS */
        } else {
            s->hcs &= ~(1 << 2);
        }
        break;
    case REG_UICCMD:
        s->uiccmd = (uint32_t)data;
        {
            uint8_t uic_op = data & 0xff;
            if (uic_op == 0x16) { /* DME_LINKSTARTUP */
                s->hcs |= (1 << 0); /* Device Present */
                s->uiccmdarg2 = 0; /* Success */
            } else if (uic_op == 0x01) { /* DME_GET */
                s->uiccmdarg2 = 0; /* Success */
                s->uiccmdarg3 = 1; /* Default attribute value 1 */
                uint32_t mib = s->uiccmdarg1 >> 16;
                if (mib == 0x156A || mib == 0x1583 || mib == 0x1560 || mib == 0x1561) {
                    s->uiccmdarg3 = 2; /* 2 lanes */
                }
            } else if (uic_op == 0x02) { /* DME_SET */
                s->uiccmdarg2 = 0; /* Success */
                uint32_t mib = s->uiccmdarg1 >> 16;
                if (mib == 0x1571) { /* PA_PWRMODE */
                    s->hcs = (s->hcs & ~0x700) | (1 << 8); /* PWR_LOCAL (0x100 in bits 8..10) */
                }
            } else if (uic_op == 0x03) { /* DME_PEER_GET */
                s->uiccmdarg2 = 0; /* Success */
                s->uiccmdarg3 = 1;
            } else if (uic_op == 0x04) { /* DME_PEER_SET */
                s->uiccmdarg2 = 0; /* Success */
            } else {
                s->uiccmdarg2 = 0; /* Success */
            }
        }
        s->hcs |= (1 << 3); /* UIC Command Ready */
        s->is |= (1 << 10); /* UCCS (UIC Command Completion Status) */
        s->is |= (1 << 4);  /* UIC_POWER_MODE (0x10) for uic_async_done */
        qcom_ufs_update_irq(s);
        break;
    case REG_UICCMDARG1:
        s->uiccmdarg1 = (uint32_t)data;
        break;
    case REG_UICCMDARG2:
        s->uiccmdarg2 = (uint32_t)data;
        break;
    case REG_UICCMDARG3:
        s->uiccmdarg3 = (uint32_t)data;
        break;

    /* Qualcomm vendor registers */
    case REG_UFS_SYS1CLK_1US:
        s->qcom_sys1clk_1us = (uint32_t)data;
        break;
    case REG_UFS_TX_SYMBOL_CLK_NS_US:
        s->qcom_tx_symbol_clk_ns_us = (uint32_t)data;
        break;
    case REG_UFS_CFG1:
        s->qcom_cfg1 = (uint32_t)data;
        break;
    case REG_UFS_CFG2:
        s->qcom_cfg2 = (uint32_t)data;
        break;
    case REG_UFS_UNIPRO_CFG:
        s->qcom_unipro_cfg = (uint32_t)data;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps qcom_ufs_ops = {
    .read = qcom_ufs_read,
    .write = qcom_ufs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t qcom_ufs_phy_read(void *opaque, hwaddr offset, unsigned size)
{
    /* QPHY_V4_PCS_UFS_READY_STATUS is at offset 0xd80 */
    if (offset == 0xd80 || offset == 0xc8 || offset == 0xcc8) {
        return 1; /* MASK_PCS_READY = 1 */
    }
    return 0;
}

static void qcom_ufs_phy_write(void *opaque, hwaddr offset, uint64_t data, unsigned size)
{
}

static const MemoryRegionOps qcom_ufs_phy_ops = {
    .read = qcom_ufs_phy_read,
    .write = qcom_ufs_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t qcom_ufs_ice_read(void *opaque, hwaddr offset, unsigned size)
{
    if (offset == 0x08) {
        return 0x03200000; /* ICE version 3.2.0: major = 3 */
    }
    return 0;
}

static void qcom_ufs_ice_write(void *opaque, hwaddr offset, uint64_t data, unsigned size)
{
}

static const MemoryRegionOps qcom_ufs_ice_ops = {
    .read = qcom_ufs_ice_read,
    .write = qcom_ufs_ice_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void qcom_sm8150_ufs_reset(void *opaque)
{
    QComSm8150UfsState *s = QCOM_SM8150_UFS(opaque);

    s->cap = 0x1107001F; /* 32 slots (0x1f), 8 TM slots (0x07), 64AS=1, CRYPTO=1 (bit 28) */
    s->ver = 0x00000210; /* UFSHCI 2.1 (0x210: major=2, minor=1) */
    s->hcpid = 0x1d84000;
    s->hcmid = 0x00000109;
    s->ahit = 0;
    s->is = 0;
    s->ie = 0;
    s->hcs = 0x0000000F; /* DP (1) | UTRLRS (2) | UTMRLRS (4) | UCRDY (8) */
    s->hce = 0;
    s->utrlba = 0;
    s->utrlbau = 0;
    s->utrldbr = 0;
    s->utrlclr = 0;
    s->utrlrsr = 0;
    s->utmrlba = 0;
    s->utmrlbau = 0;
    s->utmrdbr = 0;
    s->utmrlclr = 0;
    s->utmrlrsr = 0;
    s->uiccmd = 0;
    s->qcom_hw_ver = 0x00030000; /* SM8150 UFS 3.0 */
    s->qcom_unipro_status = 0;
}

static void qcom_sm8150_ufs_init(Object *obj)
{
    QComSm8150UfsState *s = QCOM_SM8150_UFS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio_ufs, obj, &qcom_ufs_ops, s,
                          "qcom.ufs-hc", 0x2500);
    sysbus_init_mmio(sbd, &s->mmio_ufs);

    memory_region_init_io(&s->mmio_ice, obj, &qcom_ufs_ice_ops, s,
                          "qcom.ufs-ice", 0x8000);
    sysbus_init_mmio(sbd, &s->mmio_ice);

    memory_region_init_io(&s->mmio_phy, obj, &qcom_ufs_phy_ops, s,
                          "qcom.ufs-phy", 0x3000);
    sysbus_init_mmio(sbd, &s->mmio_phy);

    sysbus_init_irq(sbd, &s->irq);

    qcom_sm8150_ufs_reset(s);
    qemu_register_reset(qcom_sm8150_ufs_reset, s);
}

DeviceState *qcom_sm8150_ufs_create(hwaddr base, hwaddr ice_base, hwaddr phy_base, qemu_irq irq)
{
    DeviceState *dev = qdev_new(TYPE_QCOM_SM8150_UFS);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    sysbus_mmio_map(sbd, 1, ice_base);
    sysbus_mmio_map(sbd, 2, phy_base);
    sysbus_connect_irq(sbd, 0, irq);

    return dev;
}

static void qcom_sm8150_ufs_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->desc = "Qualcomm SM8150 UFS Host Controller";
}

static const TypeInfo qcom_sm8150_ufs_info = {
    .name          = TYPE_QCOM_SM8150_UFS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(QComSm8150UfsState),
    .instance_init = qcom_sm8150_ufs_init,
    .class_init    = qcom_sm8150_ufs_class_init,
};

static void qcom_sm8150_ufs_register_types(void)
{
    type_register_static(&qcom_sm8150_ufs_info);
}

type_init(qcom_sm8150_ufs_register_types)
