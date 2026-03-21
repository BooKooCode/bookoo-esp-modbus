/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "mb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mb_wrap_subroute_entry_s {
    uint16_t reg_start;
    uint16_t reg_len;
    mb_fn_handler_fp handler;
    LIST_ENTRY(mb_wrap_subroute_entry_s) entries;
} mb_wrap_subroute_entry_t;

typedef LIST_HEAD(mb_wrap_subroute_head, mb_wrap_subroute_entry_s) mb_wrap_subroute_head_t;

typedef struct mb_wrap_router_bucket_s {
    uint8_t func_code;
    mb_fn_handler_fp default_handler;
    mb_wrap_subroute_head_t routes;
    LIST_ENTRY(mb_wrap_router_bucket_s) entries;
} mb_wrap_router_bucket_t;

typedef LIST_HEAD(mb_wrap_router_bucket_head, mb_wrap_router_bucket_s) mb_wrap_router_bucket_head_t;

typedef struct mb_wrap_router_state_s {
    mb_wrap_router_bucket_head_t buckets;
    mb_fn_handler_fp pending_handler;
    uint8_t pending_func_code;
} mb_wrap_router_state_t;

void mb_wrap_router_init(mb_wrap_router_state_t *state);
void mb_wrap_router_destroy(mb_wrap_router_state_t *state);

mb_err_enum_t mb_wrap_router_set_default_locked(mb_wrap_router_state_t *state, handler_descriptor_t *descriptor, uint8_t func_code,
                                                mb_fn_handler_fp handler, mb_fn_handler_fp dispatcher);

mb_err_enum_t mb_wrap_router_get_entry_handler_locked(mb_wrap_router_state_t *state, handler_descriptor_t *descriptor, uint8_t func_code,
                                                      mb_fn_handler_fp dispatcher, mb_fn_handler_fp *handler);

mb_err_enum_t mb_wrap_router_clear_default_locked(mb_wrap_router_state_t *state, handler_descriptor_t *descriptor, uint8_t func_code);

mb_err_enum_t mb_wrap_router_register_range_locked(mb_wrap_router_state_t *state, handler_descriptor_t *descriptor, uint8_t func_code,
                                                   uint16_t reg_start, uint16_t reg_len, mb_fn_handler_fp handler, mb_fn_handler_fp dispatcher);

mb_err_enum_t mb_wrap_router_unregister_range_locked(mb_wrap_router_state_t *state, uint8_t func_code, uint16_t reg_start, uint16_t reg_len);

mb_err_enum_t mb_wrap_router_select_locked(mb_wrap_router_state_t *state, handler_descriptor_t *descriptor, uint8_t func_code,
                                           uint16_t reg_addr, mb_fn_handler_fp *selected_handler);

void mb_wrap_router_set_pending(mb_wrap_router_state_t *state, uint8_t func_code, mb_fn_handler_fp handler);

void mb_wrap_router_clear_pending(mb_wrap_router_state_t *state);

mb_fn_handler_fp mb_wrap_router_resolve_dispatch_target_locked(mb_wrap_router_state_t *state, uint8_t func_code);

mb_err_enum_t mbm_router_register_range(mb_base_t *inst, uint8_t func_code, uint16_t reg_start, uint16_t reg_len, mb_fn_handler_fp handler);

mb_err_enum_t mbm_router_unregister_range(mb_base_t *inst, uint8_t func_code, uint16_t reg_start, uint16_t reg_len);

mb_err_enum_t mbm_router_select_on_request(mb_base_t *inst, uint8_t func_code, uint16_t reg_addr, mb_fn_handler_fp *selected_handler);

void mbm_router_set_pending_target(mb_base_t *inst, uint8_t func_code, mb_fn_handler_fp handler);

void mbm_router_clear_pending_target(mb_base_t *inst);

#ifdef __cplusplus
}
#endif