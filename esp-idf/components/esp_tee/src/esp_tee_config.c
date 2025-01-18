/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdarg.h>
#include "esp_tee.h"

/* U-mode interrupt handler */
extern int _tee_interrupt_handler(void);
/* U-to-M mode switch */
extern uint32_t _u2m_switch(int argc, va_list ap);
/* REE IRAM end */
extern uint32_t _iram_end;
/* REE IROM end */
extern uint32_t _instruction_reserved_end;
/* REE DROM end */
extern uint32_t _rodata_reserved_end;

esp_tee_config_t esp_tee_app_config __attribute__((section(".esp_tee_app_cfg"))) = {
    .magic_word = ESP_TEE_APP_CFG_MAGIC,
    .api_major_version = ESP_TEE_API_MAJOR_VER,
    .api_minor_version = ESP_TEE_API_MINOR_VER,

    /* .s_entry_addr and .s_intr_handler are NULL in the
       app binary, but will be written by the TEE before it loads the binary
    */

    .ns_int_handler = &_tee_interrupt_handler,
    .ns_entry_addr = &_u2m_switch,
    .ns_iram_end = &_iram_end,
    .ns_irom_end = &_instruction_reserved_end,
    .ns_drom_end = &_rodata_reserved_end,
};
