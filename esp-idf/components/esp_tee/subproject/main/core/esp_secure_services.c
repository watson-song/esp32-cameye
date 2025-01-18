/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdarg.h>

#include "esp_cpu.h"
#include "esp_efuse.h"
#include "esp_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_rom_efuse.h"
#include "esp_fault.h"

#include "hal/efuse_hal.h"
#include "hal/mmu_types.h"
#include "hal/mmu_hal.h"
#include "hal/wdt_hal.h"
#include "hal/sha_hal.h"

#include "soc/soc_caps.h"
#include "aes/esp_aes.h"
#include "sha/sha_dma.h"

#include "esp_tee.h"
#include "secure_service_num.h"
#include "esp_tee_intr.h"
#include "esp_tee_aes_intr.h"
#include "esp_tee_rv_utils.h"

#include "esp_tee_flash.h"
#include "esp_tee_sec_storage.h"
#include "esp_tee_ota_ops.h"
#include "esp_attestation.h"

#define ESP_TEE_MAX_INPUT_ARG 10

static const char *TAG = "esp_tee_sec_srv";

typedef void (*secure_service_t)(void);

extern const secure_service_t tee_secure_service_table[];

void _ss_invalid_secure_service(void)
{
    assert(0);
}

/* ---------------------------------------------- Interrupts ------------------------------------------------- */

void _ss_esp_rom_route_intr_matrix(int cpu_no, uint32_t model_num, uint32_t intr_num)
{
    return esp_tee_route_intr_matrix(cpu_no, model_num, intr_num);
}

void _ss_rv_utils_intr_enable(uint32_t intr_mask)
{
    rv_utils_tee_intr_enable(intr_mask);
}

void _ss_rv_utils_intr_disable(uint32_t intr_mask)
{
    rv_utils_tee_intr_disable(intr_mask);
}

void _ss_rv_utils_intr_set_priority(int rv_int_num, int priority)
{
    rv_utils_tee_intr_set_priority(rv_int_num, priority);
}

void _ss_rv_utils_intr_set_type(int intr_num, enum intr_type type)
{
    rv_utils_tee_intr_set_type(intr_num, type);
}

void _ss_rv_utils_intr_set_threshold(int priority_threshold)
{
    rv_utils_tee_intr_set_threshold(priority_threshold);
}

void _ss_rv_utils_intr_edge_ack(uint32_t intr_num)
{
    rv_utils_tee_intr_edge_ack(intr_num);
}

void _ss_rv_utils_intr_global_enable(void)
{
    rv_utils_tee_intr_global_enable();
}

/* ---------------------------------------------- eFuse ------------------------------------------------- */

uint32_t _ss_efuse_hal_chip_revision(void)
{
    return efuse_hal_chip_revision();
}

uint32_t _ss_efuse_hal_get_chip_ver_pkg(void)
{
    return efuse_hal_get_chip_ver_pkg();
}

bool _ss_efuse_hal_get_disable_wafer_version_major(void)
{
    return efuse_hal_get_disable_wafer_version_major();
}

void _ss_efuse_hal_get_mac(uint8_t *mac)
{
    efuse_hal_get_mac(mac);
}

bool _ss_esp_efuse_check_secure_version(uint32_t secure_version)
{
    return esp_efuse_check_secure_version(secure_version);
}

esp_err_t _ss_esp_efuse_read_field_blob(const esp_efuse_desc_t *field[], void *dst, size_t dst_size_bits)
{
    if ((field != NULL) && (field[0]->efuse_block >= EFUSE_BLK4)) {
        return ESP_ERR_INVALID_ARG;
    }

    return esp_efuse_read_field_blob(field, dst, dst_size_bits);
}

bool _ss_esp_flash_encryption_enabled(void)
{
    uint32_t flash_crypt_cnt = 0;
#ifndef CONFIG_EFUSE_VIRTUAL_KEEP_IN_FLASH
    flash_crypt_cnt = efuse_ll_get_flash_crypt_cnt();
#else
    esp_efuse_read_field_blob(ESP_EFUSE_SPI_BOOT_CRYPT_CNT, &flash_crypt_cnt, ESP_EFUSE_SPI_BOOT_CRYPT_CNT[0]->bit_count)    ;
#endif
    /* __builtin_parity is in flash, so we calculate parity inline */
    bool enabled = false;
    while (flash_crypt_cnt) {
        if (flash_crypt_cnt & 1) {
            enabled = !enabled;
        }
        flash_crypt_cnt >>= 1;
    }
    return enabled;
}

/* ---------------------------------------------- RTC_WDT ------------------------------------------------- */

void _ss_wdt_hal_init(wdt_hal_context_t *hal, wdt_inst_t wdt_inst, uint32_t prescaler, bool enable_intr)
{
    wdt_hal_init(hal, wdt_inst, prescaler, enable_intr);
}

void _ss_wdt_hal_deinit(wdt_hal_context_t *hal)
{
    wdt_hal_deinit(hal);
}

/* ---------------------------------------------- AES ------------------------------------------------- */

void _ss_esp_aes_intr_alloc(void)
{
    esp_tee_aes_intr_alloc();
}

int _ss_esp_aes_crypt_cbc(esp_aes_context *ctx,
                          int mode,
                          size_t length,
                          unsigned char iv[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    return esp_aes_crypt_cbc(ctx, mode, length, iv, input, output);
}

int _ss_esp_aes_crypt_cfb128(esp_aes_context *ctx,
                             int mode,
                             size_t length,
                             size_t *iv_off,
                             unsigned char iv[16],
                             const unsigned char *input,
                             unsigned char *output)
{
    return esp_aes_crypt_cfb128(ctx, mode, length, iv_off, iv, input, output);
}

int _ss_esp_aes_crypt_cfb8(esp_aes_context *ctx,
                           int mode,
                           size_t length,
                           unsigned char iv[16],
                           const unsigned char *input,
                           unsigned char *output)
{
    return esp_aes_crypt_cfb8(ctx, mode, length, iv, input, output);
}

int _ss_esp_aes_crypt_ctr(esp_aes_context *ctx,
                          size_t length,
                          size_t *nc_off,
                          unsigned char nonce_counter[16],
                          unsigned char stream_block[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    return esp_aes_crypt_ctr(ctx, length, nc_off, nonce_counter, stream_block, input, output);
}

int _ss_esp_aes_crypt_ecb(esp_aes_context *ctx,
                          int mode,
                          const unsigned char input[16],
                          unsigned char output[16])
{
    return esp_aes_crypt_ecb(ctx, mode, input, output);
}

int _ss_esp_aes_crypt_ofb(esp_aes_context *ctx,
                          size_t length,
                          size_t *iv_off,
                          unsigned char iv[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    return esp_aes_crypt_ofb(ctx, length, iv_off, iv, input, output);
}

/* ---------------------------------------------- SHA ------------------------------------------------- */

void _ss_esp_sha(esp_sha_type sha_type, const unsigned char *input, size_t ilen, unsigned char *output)
{
    esp_sha(sha_type, input, ilen, output);
}

int _ss_esp_sha_dma(esp_sha_type sha_type, const void *input, uint32_t ilen,
                    const void *buf, uint32_t buf_len, bool is_first_block)
{
    return esp_sha_dma(sha_type, input, ilen, buf, buf_len, is_first_block);
}

void _ss_esp_sha_read_digest_state(esp_sha_type sha_type, void *digest_state)
{
    sha_hal_read_digest(sha_type, digest_state);
}

void _ss_esp_sha_write_digest_state(esp_sha_type sha_type, void *digest_state)
{
    sha_hal_write_digest(sha_type, digest_state);
}

/* ---------------------------------------------- OTA ------------------------------------------------- */

int _ss_esp_tee_ota_begin(void)
{
    return esp_tee_ota_begin();
}

int _ss_esp_tee_ota_write(uint32_t rel_offset, void *data, size_t size)
{
    return esp_tee_ota_write(rel_offset, data, size);
}

int _ss_esp_tee_ota_end(void)
{
    return esp_tee_ota_end();
}

/* ---------------------------------------------- Secure Storage ------------------------------------------------- */

esp_err_t _ss_esp_tee_sec_storage_init(void)
{
    return esp_tee_sec_storage_init();
}

esp_err_t _ss_esp_tee_sec_storage_gen_key(uint16_t slot_id, uint8_t key_type)
{
    return esp_tee_sec_storage_gen_key(slot_id, key_type);
}

esp_err_t _ss_esp_tee_sec_storage_get_signature(uint16_t slot_id, uint8_t *hash, size_t hlen, esp_tee_sec_storage_sign_t *out_sign)
{
    return esp_tee_sec_storage_get_signature(slot_id, hash, hlen, out_sign);
}

esp_err_t _ss_esp_tee_sec_storage_get_pubkey(uint16_t slot_id, esp_tee_sec_storage_pubkey_t *pubkey)
{
    return esp_tee_sec_storage_get_pubkey(slot_id, pubkey);
}

esp_err_t _ss_esp_tee_sec_storage_encrypt(uint16_t slot_id, uint8_t *input, uint8_t len, uint8_t *aad,
                                          uint16_t aad_len, uint8_t *tag, uint16_t tag_len, uint8_t *output)
{
    return esp_tee_sec_storage_encrypt(slot_id, input, len, aad, aad_len, tag, tag_len, output);
}

esp_err_t _ss_esp_tee_sec_storage_decrypt(uint16_t slot_id, uint8_t *input, uint8_t len, uint8_t *aad,
                                          uint16_t aad_len, uint8_t *tag, uint16_t tag_len, uint8_t *output)
{
    return esp_tee_sec_storage_decrypt(slot_id, input, len, aad, aad_len, tag, tag_len, output);
}

bool _ss_esp_tee_sec_storage_is_slot_empty(uint16_t slot_id)
{
    return esp_tee_sec_storage_is_slot_empty(slot_id);
}

esp_err_t _ss_esp_tee_sec_storage_clear_slot(uint16_t slot_id)
{
    return esp_tee_sec_storage_clear_slot(slot_id);
}

/* ---------------------------------------------- Attestation ------------------------------------------------- */

esp_err_t _ss_esp_tee_att_generate_token(const uint32_t nonce, const uint32_t client_id, const char *psa_cert_ref,
                                         uint8_t *token_buf, const size_t token_buf_size, uint32_t *token_len)
{
    return esp_att_generate_token(nonce, client_id, psa_cert_ref, token_buf, token_buf_size, token_len);
}

/* ---------------------------------------------- MMU HAL ------------------------------------------------- */

void _ss_mmu_hal_map_region(uint32_t mmu_id, mmu_target_t mem_type, uint32_t vaddr,
                            uint32_t paddr, uint32_t len, uint32_t *out_len)
{
    bool vaddr_chk = esp_tee_flash_check_vaddr_in_tee_region(vaddr);
    bool paddr_chk = esp_tee_flash_check_paddr_in_tee_region(paddr);
    if (vaddr_chk || paddr_chk) {
        return;
    }
    ESP_FAULT_ASSERT(!vaddr_chk && !vaddr_chk);

    mmu_hal_map_region(mmu_id, mem_type, vaddr, paddr, len, out_len);
}

void _ss_mmu_hal_unmap_region(uint32_t mmu_id, uint32_t vaddr, uint32_t len)
{
    bool vaddr_chk = esp_tee_flash_check_vaddr_in_tee_region(vaddr);
    if (vaddr_chk) {
        return;
    }
    ESP_FAULT_ASSERT(!vaddr_chk);

    mmu_hal_unmap_region(mmu_id, vaddr, len);
}

bool _ss_mmu_hal_vaddr_to_paddr(uint32_t mmu_id, uint32_t vaddr, uint32_t *out_paddr, mmu_target_t *out_target)
{
    bool vaddr_chk = esp_tee_flash_check_vaddr_in_tee_region(vaddr);
    if (vaddr_chk) {
        return false;
    }
    ESP_FAULT_ASSERT(!vaddr_chk);
    return mmu_hal_vaddr_to_paddr(mmu_id, vaddr, out_paddr, out_target);
}

bool _ss_mmu_hal_paddr_to_vaddr(uint32_t mmu_id, uint32_t paddr, mmu_target_t target, mmu_vaddr_t type, uint32_t *out_vaddr)
{
    bool paddr_chk = esp_tee_flash_check_paddr_in_tee_region(paddr);
    if (paddr_chk) {
        return false;
    }
    ESP_FAULT_ASSERT(!paddr_chk);
    return mmu_hal_paddr_to_vaddr(mmu_id, paddr, target, type, out_vaddr);
}

/* ---------------------------------------------- Secure Service Dispatcher ------------------------------------------------- */

/**
 * @brief Entry point to the TEE binary during secure service call. It decipher the call and dispatch it
 *        to corresponding Secure Service API in secure world.
 * TODO: Fix the assembly routine here for compatibility with all levels of compiler optimizations
 */
#pragma GCC push_options
#pragma GCC optimize ("Og")

int esp_tee_service_dispatcher(int argc, va_list ap)
{
    if (argc > ESP_TEE_MAX_INPUT_ARG) {
        ESP_LOGE(TAG, "Input arguments overflow! Received %d, Permitted %d",
                 argc, ESP_TEE_MAX_INPUT_ARG);
        return -1;
    }

    int ret = -1;
    void *fp_secure_service;
    uint32_t argv[ESP_TEE_MAX_INPUT_ARG], *argp;

    uint32_t sid = va_arg(ap, uint32_t);
    argc--;

    if (sid >= MAX_SECURE_SERVICES) {
        ESP_LOGE(TAG, "Invalid Service ID!");
        va_end(ap);
        return -1;
    }

    fp_secure_service = (void *)tee_secure_service_table[sid];

    for (int i = 0; i < argc; i++) {
        argv[i] = va_arg(ap, uint32_t);
    }
    argp = &argv[0];
    va_end(ap);

    asm volatile(
        "mv t0, %1 \n"
        "beqz t0, service_call \n"

        "lw a0, 0(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a1, 4(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a2, 8(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a3, 12(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a4, 16(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a5, 20(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a6, 24(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "lw a7, 28(%3) \n"
        "addi t0, t0, -1 \n"
        "beqz t0, service_call \n"

        "addi %3, %3, 32 \n"
        "mv t2, sp \n"
        "loop: \n"
        "lw t1, 0(%3) \n"
        "sw t1, 0(t2) \n"
        "addi t0, t0, -1 \n"
        "addi t2, t2, 4 \n"
        "addi %3, %3, 4 \n"
        "bnez t0, loop \n"

        "service_call: \n"
        "mv t1, %2 \n"
        "jalr 0(t1) \n"
        "mv %0, a0 \n"
        : "=r"(ret)
        : "r"(argc), "r"(fp_secure_service), "r"(argp)
        : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "t0", "t1", "t2"
    );

    return ret;
}

#pragma GCC pop_options
