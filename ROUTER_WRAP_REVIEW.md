# 功能码 + 寄存器范围路由 Wrap 方案实现后复核稿

**更新时间**: 2026-03-21
**适用范围**: ESP-Modbus Master
**文档定位**: 记录本轮 wrap 路由器方案的最终落地结果、兼容性边界和后续验证重点。

---

## 1. 结论

本轮方案已经按既定方向落地，当前实现满足以下目标：

1. 旧 handler 主链已恢复为纯 `func_code -> handler` 语义。
2. “功能码 + 寄存器范围” 路由已从旧 handler 表中解耦为独立 wrap 路由器。
3. 路由匹配发生在请求阶段，响应阶段只做 dispatcher 转发，不做二次地址匹配。
4. 首次注册范围子路由时，dispatcher 由内部自动安装。

本轮锁定并已落实的约束：

1. 仅 Master 支持范围子路由。
2. 拒绝重叠范围注册。
3. 子路由清空后 dispatcher 不自动卸载。

---

## 2. 当前实现语义

### 2.1 旧接口兼容边界

以下旧接口继续保留：

```c
esp_err_t mbc_set_handler(void *ctx, uint8_t func_code, mb_fn_handler_fp handler);
esp_err_t mbc_get_handler(void *ctx, uint8_t func_code, mb_fn_handler_fp *handler);
esp_err_t mbc_delete_handler(void *ctx, uint8_t func_code);
```

当前语义为：

1. `mbc_set_handler()` 仍表示为某个功能码设置默认处理器。
2. 若该功能码已启用 wrap 路由，默认处理器会保存在 router bucket 中作为 fallback。
3. `mbc_get_handler()` 在 wrap 已启用时返回 dispatcher 入口，而不是原始 fallback handler 指针。
4. `mbc_delete_handler()` 在 wrap 已启用时只清空默认 fallback，不删除范围子路由和 dispatcher。

### 2.2 范围接口语义

以下范围接口已接入新的独立路由器后端：

```c
esp_err_t mbc_register_handler_range(void *ctx,
                                     uint8_t func_code,
                                     uint16_t reg_start,
                                     uint16_t reg_len,
                                     mb_fn_handler_fp handler);

esp_err_t mbc_unregister_handler_range(void *ctx,
                                       uint8_t func_code,
                                       uint16_t reg_start,
                                       uint16_t reg_len);
```

当前语义为：

1. `reg_len` 必须大于 0。
2. 重叠范围直接拒绝注册。
3. 若请求未命中任何范围，但该功能码存在默认处理器，则允许继续发送并在响应阶段回退到 fallback。
4. 若未命中范围且也不存在默认处理器，则请求路径返回未找到/不支持错误。

---

## 3. 内部结构与流程

### 3.1 结构边界

当前内部结构分为两层：

1. 旧 handler 表
   仅维护 `func_code -> default_handler/dispatcher` 入口关系。
2. wrap router bucket
   按 `func_code` 组织范围子路由列表，并保存该功能码的默认 fallback handler。

核心内部接口已经落地：

```c
mbm_router_register_range()
mbm_router_unregister_range()
mbm_router_select_on_request()
mbm_router_set_pending_target()
mbm_router_clear_pending_target()
mbm_fn_router_dispatcher()
```

### 3.2 请求阶段

请求发送前执行：

```text
根据 func_code + reg_start 做范围匹配
    -> 命中子路由: 记录 pending target
    -> 未命中但存在 fallback: 允许继续发包
    -> 未命中且无 fallback: 直接返回错误
```

该逻辑已接入：

1. `mbc_serial_master_send_request_internal()`
2. `mbc_tcp_master_send_request_internal()`

### 3.3 响应阶段

响应阶段继续收敛在 `mbm_check_invoke_handler()`，但行为已调整为：

1. 先按功能码取入口 handler。
2. 若入口为 dispatcher，则由 dispatcher 优先消费 pending target。
3. 若没有 pending target，则回退到 bucket 中保存的默认 fallback。
4. 若两者都不存在，则返回非法功能异常。

响应阶段不再根据寄存器地址做二次路由。

---

## 4. 已落实的关键修正

本轮不仅完成了架构切换，还处理了两个实现层面的关键问题：

1. 避免在持锁状态下调用 handler，降低死锁风险。
2. 统一请求结束后的 pending 清理路径，覆盖正常返回、超时和错误返回场景。

另外，dispatcher 的函数指针签名已对齐 `mb_fn_handler_fp`，当前可以正常参与旧 handler 表注册和查找。

---

## 5. 已知行为变化

这些不是缺陷，但需要明确记录：

1. `mbc_get_handler()` 在 wrap 模式下返回 dispatcher，而不是原始 fallback handler。
2. `mbc_register_handler_range()` 不再接受 `reg_len == 0` 作为通配 fallback 表达方式。
3. 默认 fallback 的来源已固定为 `mbc_set_handler()`，不再通过范围接口复用。
4. 即使一个功能码下的范围子路由被全部删除，dispatcher 仍会保留在该功能码入口上。

---

## 6. 当前验证状态

已完成：

1. 静态检查无当前改动相关报错。
2. ESP-IDF 编译验证通过。
3. serial master 示例已同步到新语义并可编译通过。

未完成：

1. 实机或联调环境中的范围命中验证。
2. 超时与异常响应路径下的运行时行为验证。
3. README 和正式接口文档中的语义补充。

---

## 7. 当前结论

当前代码已经从“侵入式范围路由”稳定切换到“wrap 路由器 + dispatcher + fallback”模型，代码和编译状态已可作为后续联调基础。

如果后续要继续增强，优先级建议如下：

1. 先完成实机功能验证和异常路径验证。
2. 再决定是否拆分 router 专用文件。
3. 最后再考虑 O(log n) 查询、优先级字段或额外调试接口。