/*
 * SPDX-FileCopyrightText: 2016-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "esp_err.h"
#include "mbc_master.h"         // for master interface define
#include "mbc_slave.h"          // for slave interface define
#include "esp_modbus_common.h"  // for public interface defines
#include "mb_wrap_router.h"     // for wrap router registration helpers

static const char TAG[] __attribute__((unused)) = "MB_CONTROLLER_COMMON";

/**
 * Register or override command handler for the command in object command handler table
 */
esp_err_t mbc_set_handler(void *ctx, uint8_t func_code, mb_fn_handler_fp handler)
{
    MB_RETURN_ON_FALSE((ctx && handler && func_code), ESP_ERR_INVALID_STATE, TAG,
                       "Incorrect arguments for the function.");
    mb_err_enum_t ret = MB_EINVAL;
    mb_controller_common_t *mb_controller = (mb_controller_common_t *)(ctx);
    mb_base_t *mb_obj = (mb_base_t *)mb_controller->mb_base;
    MB_RETURN_ON_FALSE(mb_obj, ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    if (mb_obj->descr.is_master) {
        ret = mbm_set_handler(mb_controller->mb_base, func_code, handler);
    } else {
        ret = mbs_set_handler(mb_controller->mb_base, func_code, handler);
    }
    return  MB_ERR_TO_ESP_ERR(ret);
}

/**
 * Get command handler from the command handler table of the object
 */
esp_err_t mbc_get_handler(void *ctx, uint8_t func_code, mb_fn_handler_fp *handler)
{
    MB_RETURN_ON_FALSE((ctx && func_code && handler), ESP_ERR_INVALID_STATE, TAG,
                       "Incorrect arguments for the function.");
    mb_err_enum_t ret = MB_EINVAL;
    mb_controller_common_t *mb_controller = (mb_controller_common_t *)(ctx);
    mb_base_t *mb_obj = (mb_base_t *)mb_controller->mb_base;
    MB_RETURN_ON_FALSE(mb_obj, ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    if (mb_obj->descr.is_master) {
        ret = mbm_get_handler(mb_controller->mb_base, func_code, handler);
    } else {
        ret = mbs_get_handler(mb_controller->mb_base, func_code, handler);
    }
    return  MB_ERR_TO_ESP_ERR(ret);
}

/**
 * Delete command handler from the command handler table of the object
 */
esp_err_t mbc_delete_handler(void *ctx, uint8_t func_code)
{
    MB_RETURN_ON_FALSE((ctx && func_code), ESP_ERR_INVALID_STATE, TAG,
                       "Incorrect arguments for the function.");
    mb_err_enum_t ret = MB_EINVAL;
    mb_controller_common_t *mb_controller = (mb_controller_common_t *)(ctx);
    mb_base_t *mb_obj = (mb_base_t *)mb_controller->mb_base;
    MB_RETURN_ON_FALSE(mb_obj, ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    if (mb_obj->descr.is_master) {
        ret = mbm_delete_handler(mb_controller->mb_base, func_code);
    } else {
        ret = mbs_delete_handler(mb_controller->mb_base, func_code);
    }
    return  MB_ERR_TO_ESP_ERR(ret);
}

/**
 * Get number of registered command handlers for the object
 */
esp_err_t mbc_get_handler_count(void *ctx, uint16_t *count)
{
    MB_RETURN_ON_FALSE((ctx && count), ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    mb_err_enum_t ret = MB_EINVAL;
    mb_controller_common_t *mb_controller = (mb_controller_common_t *)(ctx);
    mb_base_t *mb_obj = (mb_base_t *)mb_controller->mb_base;
    MB_RETURN_ON_FALSE(mb_obj, ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    if (mb_obj->descr.is_master) {
        ret = mbm_get_handler_count(mb_controller->mb_base, count);
    } else {
        ret = mbs_get_handler_count(mb_controller->mb_base, count);
    }
    return  MB_ERR_TO_ESP_ERR(ret);
}

/**
 * Register a handler for controller requests with func_code + register address range.
 */
esp_err_t mbc_register_handler_range(void *ctx, uint8_t func_code, uint16_t reg_start,
                                     uint16_t reg_len, mb_fn_handler_fp handler)
{
    MB_RETURN_ON_FALSE((ctx && handler && func_code), ESP_ERR_INVALID_STATE, TAG,
                       "Incorrect arguments for the function.");
    MB_RETURN_ON_FALSE((reg_len > 0), ESP_ERR_INVALID_ARG, TAG,
                       "Range registration requires reg_len > 0.");
    MB_RETURN_ON_FALSE((func_code != MB_FUNC_READWRITE_MULTIPLE_REGISTERS), ESP_ERR_NOT_SUPPORTED, TAG,
                       "Function code 0x17 does not support range subroutes.");
    mb_controller_common_t *mb_controller = (mb_controller_common_t *)(ctx);
    mb_base_t *mb_obj = (mb_base_t *)mb_controller->mb_base;
    MB_RETURN_ON_FALSE(mb_obj, ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    mb_err_enum_t ret = mb_obj->descr.is_master
                            ? mbm_router_register_range(mb_controller->mb_base, func_code,
                                                        reg_start, reg_len, handler)
                            : mbs_router_register_range(mb_controller->mb_base, func_code,
                                                        reg_start, reg_len, handler);
    return MB_ERR_TO_ESP_ERR(ret);
}

/**
 * Unregister a range-specific handler identified by (func_code, reg_start, reg_len).
 */
esp_err_t mbc_unregister_handler_range(void *ctx, uint8_t func_code, uint16_t reg_start,
                                       uint16_t reg_len)
{
    MB_RETURN_ON_FALSE((ctx && func_code), ESP_ERR_INVALID_STATE, TAG,
                       "Incorrect arguments for the function.");
    mb_controller_common_t *mb_controller = (mb_controller_common_t *)(ctx);
    mb_base_t *mb_obj = (mb_base_t *)mb_controller->mb_base;
    MB_RETURN_ON_FALSE(mb_obj, ESP_ERR_INVALID_STATE, TAG,
                       "Controller interface is not correctly initialized.");
    mb_err_enum_t ret = mb_obj->descr.is_master
                            ? mbm_router_unregister_range(mb_controller->mb_base, func_code,
                                                          reg_start, reg_len)
                            : mbs_router_unregister_range(mb_controller->mb_base, func_code,
                                                          reg_start, reg_len);
    return MB_ERR_TO_ESP_ERR(ret);
}
