# ESP-Modbus 改动实现进度总结

**更新时间**: 2026-03-21
**项目**: Fork esp-modbus 作为 ESP32S3 子项目的主机接口
**当前状态**: 超时接口扩展、wrap 范围路由器改造和 router 独立拆分已完成，代码已通过编译验证，当前进入运行验证前的收口阶段。

---

## 1. 本轮结论

当前仓库的真实状态如下：

1. 主站单次调用超时接口已经打通。
2. 侵入式范围路由实现已经回退。
3. 新的 wrap 路由器已经接入 master 请求/响应链路，并已从 `mb_master.c` 中迁出到独立文件。
4. 公开范围注册接口已经切换到新路由器后端。
5. serial master 示例已同步到 dispatcher + fallback + ranges 语义。
6. 代码已完成 ESP-IDF 编译验证，但尚未在实际工程中做运行测试。

---

## 2. 已完成项

### 2.1 独立超时参数链路

已完成 `*_with_timeout()` 调用链扩展，当前有效接口包括：

```c
mbc_master_send_request_with_timeout()
mbc_master_get_parameter_with_timeout()
mbc_master_get_parameter_with_uid_timeout()
mbc_master_set_parameter_with_timeout()
mbc_master_set_parameter_with_uid_timeout()
```

当前超时选择逻辑为：

```c
effective_timeout_ms = timeout_ms ? timeout_ms : response_tout_ms;
if (!effective_timeout_ms) {
   effective_timeout_ms = MB_MASTER_TIMEOUT_MS_RESPOND;
}
```

### 2.2 wrap 范围路由器

当前已经完成以下改造：

1. 旧 handler 表恢复为纯功能码入口语义。
2. 范围路由改为独立 router bucket + subroute 模型。
3. 首次注册范围子路由时自动安装 dispatcher。
4. 请求阶段匹配范围子路由，响应阶段仅转发到 pending target 或 default fallback。
5. 范围重叠注册被拒绝。
6. 范围接口当前仅支持 Master。
7. wrap router 核心状态与算法已迁出到独立的 `mb_wrap_router.c/.h` 内部模块。

核心内部接口已经落地：

```c
mbm_router_register_range()
mbm_router_unregister_range()
mbm_router_select_on_request()
mbm_router_set_pending_target()
mbm_router_clear_pending_target()
mbm_fn_router_dispatcher()
```

### 2.3 兼容性边界

当前兼容性边界已经明确：

1. `mbc_set_handler()` 仍作为默认 fallback handler 的注册入口。
2. `mbc_get_handler()` 在 wrap 已启用时返回 dispatcher，而不是原始 fallback handler。
3. `mbc_delete_handler()` 在 wrap 已启用时只清空 fallback，不删除范围子路由。
4. `mbc_register_handler_range()` 要求 `reg_len > 0`，不再接受 `reg_len == 0` 作为通配 fallback 表达方式。

### 2.4 示例与编译

当前 serial master 示例已经补齐最小 wrap 路由演示：

1. 一个 `mbc_set_handler()` 注册的 fallback handler。
2. 两个 `mbc_register_handler_range()` 注册的范围 handler。
3. `mbc_get_handler()` 检查已调整为适配 dispatcher 返回语义。

已确认：

1. 静态检查无当前改动相关报错。
2. ESP-IDF 编译验证通过。

---

## 3. 本轮文件清单

### 3.1 本轮新增文件

1. `ROUTER_WRAP_REVIEW.md`
2. `modbus/mb_objects/include/mb_wrap_router.h`
3. `modbus/mb_objects/mb_wrap_router.c`

### 3.2 本轮修改文件

1. `PROGRESS_SUMMARY.md`
2. `examples/serial/mb_serial_master/main/serial_master.c`
3. `modbus/mb_controller/common/esp_modbus_common.c`
4. `modbus/mb_controller/common/include/esp_modbus_common.h`
5. `modbus/mb_controller/serial/mbc_serial_master.c`
6. `modbus/mb_controller/tcp/mbc_tcp_master.c`
7. `modbus/mb_objects/functions/mbfunc_handling.c`
8. `modbus/mb_objects/include/mb_common.h`
9. `modbus/mb_objects/include/mb_master.h`
10. `modbus/mb_objects/mb_master.c`
11. `CMakeLists.txt`

### 3.3 本轮关联但不在当前未提交列表中的前序改动

这些属于同一目标链路，但已在更早阶段落地：

1. `modbus/mb_controller/common/include/esp_modbus_master.h`
2. `modbus/mb_controller/common/esp_modbus_master.c`
3. `modbus/mb_controller/common/mbc_master.h`

---

## 4. 当前未完成项

### 4.1 运行验证

当前未完成：

1. 范围命中行为的实机验证。
2. 超时路径的实机验证。
3. 异常响应路径下 pending 清理行为验证。
4. 回归测试。

### 4.2 文档收口

当前仍建议后续补充：

1. README 或正式接口文档中的 wrap 路由语义说明。
2. `mbc_get_handler()` 返回 dispatcher 的行为说明。
3. 重叠注册、默认 fallback 和 dispatcher 保留策略说明。

### 4.3 后续增强项

当前尚未实现：

1. 将超时参数移入 `mb_param_request_t`。
2. 新增统一默认超时设置接口。
3. O(log n) 路由查找或优先级字段。

---

## 5. 状态评估

按当前真实状态判断：

1. 核心代码开发：已完成。
2. 示例接入：已完成。
3. 编译验证：已完成。
4. 运行验证：未完成。
5. 文档最终收口：进行中。

总体进度估算：**约 80% - 85%**。

当前适合对外表述为：**功能代码已完成并编译通过，等待结合实际工程进行运行验证。**
