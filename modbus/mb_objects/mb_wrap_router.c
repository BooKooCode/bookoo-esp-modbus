/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include "mb_wrap_router.h"

static const char *TAG = "mb_object.wrap_router";

static mb_wrap_router_bucket_t *mb_wrap_router_find_bucket(mb_wrap_router_state_t *state, uint8_t func_code)
{
    mb_wrap_router_bucket_t *bucket = NULL;
    LIST_FOREACH(bucket, &state->buckets, entries) {
        if (bucket->func_code == func_code) {
            return bucket;
        }
    }
    return NULL;
}

static bool mb_wrap_router_ranges_overlap(uint16_t a_start, uint16_t a_len, uint16_t b_start, uint16_t b_len)
{
    uint32_t a_end = (uint32_t)a_start + a_len;
    uint32_t b_end = (uint32_t)b_start + b_len;
    return ((uint32_t)a_start < b_end) && ((uint32_t)b_start < a_end);
}

static bool mb_wrap_router_range_contains(uint16_t route_start,
                                          uint16_t route_len,
                                          uint16_t req_start,
                                          uint16_t req_len)
{
    uint32_t route_end = (uint32_t)route_start + route_len;
    uint32_t req_end = (uint32_t)req_start + req_len;
    return ((uint32_t)req_start >= route_start) && (req_end <= route_end);
}

static mb_err_enum_t mb_wrap_router_ensure_dispatcher_locked(mb_wrap_router_state_t *state,
                                                             handler_descriptor_t *descriptor,
                                                             uint8_t func_code,
                                                             mb_fn_handler_fp dispatcher)
{
    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (!bucket) {
        bucket = (mb_wrap_router_bucket_t *)calloc(1, sizeof(mb_wrap_router_bucket_t));
        MB_RETURN_ON_FALSE(bucket, MB_ENORES, TAG, "no mem for router bucket fc=0x%x.", (int)func_code);
        bucket->func_code = func_code;
        LIST_INIT(&bucket->routes);
        LIST_INSERT_HEAD(&state->buckets, bucket, entries);
    }

    mb_fn_handler_fp current_handler = NULL;
    mb_err_enum_t status = mb_get_handler(descriptor, func_code, &current_handler);
    if ((status == MB_ENOERR) && (current_handler != dispatcher)) {
        bucket->default_handler = current_handler;
    }
    status = mb_set_handler(descriptor, func_code, dispatcher);
    MB_RETURN_ON_FALSE((status == MB_ENOERR), status, TAG,
                       "dispatcher install failed for fc=0x%x.", (int)func_code);
    return MB_ENOERR;
}

static mb_err_enum_t mb_wrap_router_cleanup_bucket_if_empty_locked(mb_wrap_router_state_t *state,
                                                                   handler_descriptor_t *descriptor,
                                                                   uint8_t func_code,
                                                                   mb_fn_handler_fp dispatcher)
{
    MB_RETURN_ON_FALSE((state && descriptor && dispatcher), MB_EINVAL, TAG,
                       "router cleanup arguments are invalid.");

    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (!bucket) {
        return MB_ENORES;
    }
    if (bucket->default_handler || !LIST_EMPTY(&bucket->routes)) {
        return MB_ENOERR;
    }

    mb_fn_handler_fp current_handler = NULL;
    mb_err_enum_t status = mb_get_handler(descriptor, func_code, &current_handler);
    if (status == MB_ENOERR) {
        if (current_handler == dispatcher) {
            status = mb_delete_handler(descriptor, func_code);
            MB_RETURN_ON_FALSE((status == MB_ENOERR), status, TAG,
                               "dispatcher uninstall failed for fc=0x%x.", (int)func_code);
        }
    } else {
        MB_RETURN_ON_FALSE((status == MB_ENORES), status, TAG,
                           "handler query failed for fc=0x%x.", (int)func_code);
    }

    LIST_REMOVE(bucket, entries);
    free(bucket);
    return MB_ENOERR;
}

void mb_wrap_router_init(mb_wrap_router_state_t *state)
{
    if (state) {
        LIST_INIT(&state->buckets);
        state->pending_handler = NULL;
        state->pending_func_code = 0;
    }
}

void mb_wrap_router_destroy(mb_wrap_router_state_t *state)
{
    if (!state) {
        return;
    }
    mb_wrap_router_bucket_t *bucket = NULL;
    while ((bucket = LIST_FIRST(&state->buckets))) {
        mb_wrap_subroute_entry_t *route = NULL;
        while ((route = LIST_FIRST(&bucket->routes))) {
            LIST_REMOVE(route, entries);
            free(route);
        }
        LIST_REMOVE(bucket, entries);
        free(bucket);
    }
    state->pending_handler = NULL;
    state->pending_func_code = 0;
}

mb_err_enum_t mb_wrap_router_set_default_locked(mb_wrap_router_state_t *state,
                                                handler_descriptor_t *descriptor,
                                                uint8_t func_code,
                                                mb_fn_handler_fp handler,
                                                mb_fn_handler_fp dispatcher)
{
    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (bucket) {
        bucket->default_handler = handler;
        return mb_wrap_router_ensure_dispatcher_locked(state, descriptor, func_code, dispatcher);
    }
    return mb_set_handler(descriptor, func_code, handler);
}

mb_err_enum_t mb_wrap_router_get_entry_handler_locked(mb_wrap_router_state_t *state,
                                                      handler_descriptor_t *descriptor,
                                                      uint8_t func_code,
                                                      mb_fn_handler_fp dispatcher,
                                                      mb_fn_handler_fp *handler)
{
    MB_RETURN_ON_FALSE(handler, MB_EINVAL, TAG, "handler pointer is null.");
    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (bucket) {
        *handler = dispatcher;
        return MB_ENOERR;
    }
    return mb_get_handler(descriptor, func_code, handler);
}

mb_err_enum_t mb_wrap_router_clear_default_locked(mb_wrap_router_state_t *state,
                                                  handler_descriptor_t *descriptor,
                                                  uint8_t func_code,
                                                  mb_fn_handler_fp dispatcher)
{
    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (bucket) {
        bucket->default_handler = NULL;
        return mb_wrap_router_cleanup_bucket_if_empty_locked(state, descriptor, func_code, dispatcher);
    }
    return mb_delete_handler(descriptor, func_code);
}

mb_err_enum_t mb_wrap_router_register_range_locked(mb_wrap_router_state_t *state,
                                                   handler_descriptor_t *descriptor,
                                                   uint8_t func_code,
                                                   uint16_t reg_start,
                                                   uint16_t reg_len,
                                                   mb_fn_handler_fp handler,
                                                   mb_fn_handler_fp dispatcher)
{
    MB_RETURN_ON_FALSE((handler && reg_len > 0), MB_EINVAL, TAG, "invalid range registration arguments.");
    mb_err_enum_t status = mb_wrap_router_ensure_dispatcher_locked(state, descriptor, func_code, dispatcher);
    if (status != MB_ENOERR) {
        return status;
    }

    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    MB_RETURN_ON_FALSE(bucket, MB_EILLSTATE, TAG, "router bucket is missing for fc=0x%x.", (int)func_code);

    mb_wrap_subroute_entry_t *route = NULL;
    LIST_FOREACH(route, &bucket->routes, entries) {
        if (mb_wrap_router_ranges_overlap(route->reg_start, route->reg_len, reg_start, reg_len)) {
            return MB_EINVAL;
        }
    }

    route = (mb_wrap_subroute_entry_t *)calloc(1, sizeof(mb_wrap_subroute_entry_t));
    MB_RETURN_ON_FALSE(route, MB_ENORES, TAG, "no mem for router route fc=0x%x.", (int)func_code);
    route->reg_start = reg_start;
    route->reg_len = reg_len;
    route->handler = handler;
    LIST_INSERT_HEAD(&bucket->routes, route, entries);
    return MB_ENOERR;
}

mb_err_enum_t mb_wrap_router_unregister_range_locked(mb_wrap_router_state_t *state,
                                                     handler_descriptor_t *descriptor,
                                                     uint8_t func_code,
                                                     uint16_t reg_start,
                                                     uint16_t reg_len,
                                                     mb_fn_handler_fp dispatcher)
{
    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (!bucket) {
        return MB_ENORES;
    }

    mb_wrap_subroute_entry_t *route = NULL;
    mb_wrap_subroute_entry_t *temp = NULL;
    LIST_FOREACH_SAFE(route, &bucket->routes, entries, temp) {
        if ((route->reg_start == reg_start) && (route->reg_len == reg_len)) {
            LIST_REMOVE(route, entries);
            free(route);
            return mb_wrap_router_cleanup_bucket_if_empty_locked(state, descriptor, func_code, dispatcher);
        }
    }
    return MB_ENORES;
}

mb_err_enum_t mb_wrap_router_select_locked(mb_wrap_router_state_t *state,
                                           handler_descriptor_t *descriptor,
                                           uint8_t func_code,
                                           uint16_t reg_start,
                                           uint16_t reg_len,
                                           mb_fn_handler_fp *selected_handler)
{
    MB_RETURN_ON_FALSE(selected_handler, MB_EINVAL, TAG, "selected handler pointer is null.");
    *selected_handler = NULL;

    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    if (bucket) {
        mb_wrap_subroute_entry_t *route = NULL;
        if (reg_len > 0) {
            LIST_FOREACH(route, &bucket->routes, entries) {
                if (mb_wrap_router_range_contains(route->reg_start, route->reg_len, reg_start, reg_len)) {
                    *selected_handler = route->handler;
                    return MB_ENOERR;
                }
            }
        }
        return bucket->default_handler ? MB_ENOERR : MB_ENORES;
    }

    mb_fn_handler_fp handler = NULL;
    return mb_get_handler(descriptor, func_code, &handler);
}

void mb_wrap_router_set_pending(mb_wrap_router_state_t *state,
                                uint8_t func_code,
                                mb_fn_handler_fp handler)
{
    if (state) {
        state->pending_handler = handler;
        state->pending_func_code = func_code;
    }
}

void mb_wrap_router_clear_pending(mb_wrap_router_state_t *state)
{
    if (state) {
        state->pending_handler = NULL;
        state->pending_func_code = 0;
    }
}

mb_fn_handler_fp mb_wrap_router_resolve_dispatch_target_locked(mb_wrap_router_state_t *state,
                                                               uint8_t func_code)
{
    MB_RETURN_ON_FALSE(state, NULL, TAG, "router state is null.");
    if (state->pending_handler && (state->pending_func_code == func_code)) {
        mb_fn_handler_fp handler = state->pending_handler;
        state->pending_handler = NULL;
        state->pending_func_code = 0;
        return handler;
    }

    mb_wrap_router_bucket_t *bucket = mb_wrap_router_find_bucket(state, func_code);
    return bucket ? bucket->default_handler : NULL;
}