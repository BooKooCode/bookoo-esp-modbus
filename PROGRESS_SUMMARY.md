# ESP-Modbus 改动实现进度总结

**更新时间**: 2026-03-22
**项目**: Fork esp-modbus 作为 ESP32S3 子项目的 Modbus 接口基础组件
**当前状态**: 超时接口扩展、wrap 范围路由器改造、router 独立拆分、slave 侧范围路由支持、router 生命周期可逆化首轮实现以及 master pending 状态并发保护首轮修复已完成，代码已通过静态校验与既有编译链验证，当前进入运行验证前的收口阶段。

---

## 1. 本轮结论

当前仓库的真实状态如下：

1. 主站单次调用超时接口已经打通。
2. 侵入式范围路由实现已经回退。
3. 新的 wrap 路由器已经接入 master 请求/响应链路，以及 slave 请求分发链路，并已从对象主文件中抽离为独立模块。
4. 公开范围注册接口已经切换到新路由器后端，当前同时支持 master 和 slave。
5. serial master 与 serial slave 示例均已同步到 dispatcher + fallback + ranges 语义。
6. range router 的空 bucket 自动回收逻辑已接入，删除 fallback/注销最后一个 range 后可自动卸载 dispatcher 并清理 bucket。
7. master pending target 的设置、清理与消费路径现已统一使用同一把 handler semaphore 保护。
8. 代码已完成 ESP-IDF 编译验证与本轮改动静态校验，但尚未在实际工程中做运行测试。

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
4. master 在请求阶段匹配范围子路由，响应阶段仅转发到 pending target 或 default fallback。
5. slave 在 dispatcher 内直接从请求 PDU 解析起始地址并匹配范围子路由，未命中时回落到 default fallback。
6. slave handler 调用路径已调整为锁内取 handler、锁外执行，避免 dispatcher 二次取锁。
7. 范围重叠注册被拒绝。
8. wrap router 核心状态与算法已迁出到独立的 `mb_wrap_router.c/.h` 内部模块。
9. 当某功能码下 `default_handler == NULL` 且已无任何 range 子路由时，会自动卸载 dispatcher 并回收对应 router bucket。
10. master pending route 的 set / clear / resolve 路径已统一到 `handler_descriptor.sema` 同步边界内，消除了此前“读加锁、写不加锁”的状态访问不对称。

核心内部接口已经落地：

```c
mbm_router_register_range()
mbm_router_unregister_range()
mbm_router_select_on_request()
mbm_router_set_pending_target()
mbm_router_clear_pending_target()
mbm_fn_router_dispatcher()
mbs_router_register_range()
mbs_router_unregister_range()
mbs_fn_router_dispatcher()
```

### 2.3 兼容性边界

当前兼容性边界已经明确：

1. `mbc_set_handler()` 仍作为默认 fallback handler 的注册入口。
2. `mbc_get_handler()` 在 wrap 已启用时返回 dispatcher，而不是原始 fallback handler。
3. `mbc_delete_handler()` 在 wrap 已启用时只清空 fallback，不删除范围子路由。
4. `mbc_register_handler_range()` 要求 `reg_len > 0`，不再接受 `reg_len == 0` 作为通配 fallback 表达方式。
5. 单区间 range 路由当前按“请求完整区间必须被已注册范围完整包含”的规则匹配；对标准寄存器/线圈访问功能码可直接生效。
6. `0x17` 当前不参与 range 路由选择，统一回落到 default fallback handler。
7. 对同一功能码，若所有 range 已注销且 fallback 为空，router 会自动回收到未启用状态。

### 2.4 示例与编译

当前示例侧已经补齐最小 wrap 路由演示：

1. serial master 示例包含一个 `mbc_set_handler()` 注册的 fallback handler，以及两个 `mbc_register_handler_range()` 注册的范围 handler。
2. serial slave 示例包含标准 holding read 的 fallback handler、两个范围 handler，以及重叠注册拒绝检查。
3. `mbc_get_handler()` 检查已调整为适配 dispatcher 返回语义。
4. serial slave 示例运行时日志已补充 `ROUTE:` 标记，用于直接观察 fallback/range0/range1 命中结果。

已确认：

1. 静态检查无当前改动相关报错。
2. 在按以下 PowerShell 初始化链路加载 ESP-IDF 环境后，`examples/serial/mb_serial_slave` 已完成 `idf.py build` 验证。
3. 本轮对 `mb_wrap_router.c`、`mb_master.c`、`mb_slave.c`、`mb_wrap_router.h` 的签名与静态错误检查已通过。
4. 本轮对 master pending 并发修复后的 `mb_master.c` 静态错误检查已通过。

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
. 'C:\Espressif\Initialize-Idf.ps1' -IdfId 'esp-idf-d25504f52949021d2087a0eeccaed50b'
idf.py build
```

---

## 3. 本轮文件清单

### 3.1 本轮新增文件

1. `ROUTER_WRAP_REVIEW.md`
2. `modbus/mb_objects/include/mb_wrap_router.h`
3. `modbus/mb_objects/mb_wrap_router.c`

### 3.2 本轮修改文件

1. `PROGRESS_SUMMARY.md`
2. `examples/serial/mb_serial_master/main/serial_master.c`
3. `examples/serial/mb_serial_slave/main/idf_component.yml`
4. `examples/serial/mb_serial_slave/main/serial_slave.c`
5. `modbus/mb_controller/common/esp_modbus_common.c`
6. `modbus/mb_controller/common/include/esp_modbus_common.h`
7. `modbus/mb_controller/serial/mbc_serial_master.c`
8. `modbus/mb_controller/tcp/mbc_tcp_master.c`
9. `modbus/mb_objects/functions/mbfunc_handling.c`
10. `modbus/mb_objects/include/mb_common.h`
11. `modbus/mb_objects/include/mb_master.h`
12. `modbus/mb_objects/include/mb_slave.h`
13. `modbus/mb_objects/mb_master.c`
14. `modbus/mb_objects/mb_slave.c`
15. `CMakeLists.txt`
16. `modbus/mb_objects/mb_wrap_router.c`
17. `modbus/mb_objects/include/mb_wrap_router.h`

### 3.3 本轮关联但不在当前未提交列表中的前序改动

这些属于同一目标链路，但已在更早阶段落地：

1. `modbus/mb_controller/common/include/esp_modbus_master.h`
2. `modbus/mb_controller/common/esp_modbus_master.c`
3. `modbus/mb_controller/common/mbc_master.h`
4. `modbus/mb_objects/include/mb_wrap_router.h`
5. `modbus/mb_objects/mb_wrap_router.c`

---

## 4. 当前未完成项

### 4.1 运行验证

当前未完成：

1. 范围命中行为的实机验证。
2. 超时路径的实机验证。
3. 异常响应路径下 pending 清理行为验证。
4. slave 标准功能码范围命中行为的实机验证。
5. router 自动回收后重新注册/重新分发链路的运行验证。
6. 回归测试。

### 4.2 文档收口

当前仍建议后续补充：

1. README 或正式接口文档中的 wrap 路由语义说明。
2. `mbc_get_handler()` 返回 dispatcher 的行为说明。
3. 重叠注册、默认 fallback 和 dispatcher 保留策略说明。
4. 单区间范围路由按请求完整区间匹配，以及 `0x17` fallback-only 的行为说明。
5. router 自动回收触发条件与回收到纯 handler 状态的行为说明。

### 4.3 后续增强项

当前尚未实现：

1. 将超时参数移入 `mb_param_request_t`。
2. 新增统一默认超时设置接口。
3. O(log n) 路由查找或优先级字段。

### 4.4 代码审查新增待解决问题

以下问题来自当前已落地代码的静态审查，状态均为：**待解决**。

1. 路由语义与接口命名存在偏差（部分解决，仍待后续完成）。
   单区间 range 路由现已按“请求完整区间必须落在注册范围内”的语义匹配；但对 `0x17` 的双区间读写请求当前仍未形成完整建模策略，现阶段先显式降级为 fallback-only。

2. router 生命周期不可逆（已完成代码改造，待运行验证）。
   已在内部 router 层接入空 bucket 自动回收逻辑；当前当功能码下无 fallback 且已无 range 子路由时，会自动卸载 dispatcher 并清理 bucket。后续仍需补运行验证与文档说明。

3. master pending 路由状态并发保护不完整（已完成代码改造，待运行验证）。
   已将 pending target 的设置、清理与消费统一收敛到同一把 `handler_descriptor.sema` 保护下；后续仍需补超时、迟到响应与异常响应场景的运行验证，评估是否还需要进一步引入 transaction 级绑定方案。

建议优先级：1 和 2 为高优先级；3 为中优先级。

---

## 5. 状态评估

按当前真实状态判断：

1. 核心代码开发：已完成。
2. 示例接入：已完成。
3. 编译验证：已完成。
4. 生命周期可逆化代码改造：已完成，待运行验证。
5. pending 并发保护首轮修复：已完成，待运行验证。
6. 运行验证：未完成。
7. 文档最终收口：进行中。

总体进度估算：**约 88% - 92%**。

当前适合对外表述为：**功能代码已完成、生命周期回收逻辑已补齐并通过静态校验，等待结合实际工程进行运行验证。**
