# ESP-Modbus 改动实现进度总结

**更新时间**: 2026-03-21
**项目**: Fork esp-modbus 作为 ESP32S3 子项目的主机接口
**当前状态**: 已完成超时接口扩展与范围路由核心接入，尚未完成编译验证、功能测试和文档收口

---

## 需求目标

### 核心需求
1. 独立超时参数
   目标是让主站请求支持单次调用超时，不指定时回退到控制器默认超时。
2. 功能码 + 寄存器范围路由
   目标是把自定义 handler 的匹配从 `func_code -> handler` 扩展为 `func_code + [reg_start, reg_len) -> handler`。
3. 更低时间复杂度
   目标是后续把路由匹配优化到 O(log n) 或更优，并预留优先级扩展空间。

### 已确认约束
- 同步阻塞模式，没有异步需求
- 超时语义为调用线程阻塞直到响应返回或超时
- 旧的 `mbc_set_handler()` 必须继续可用

---

## 当前实际进展

### 已完成

#### 1. 主站超时接口已扩展完成

已经补齐公开 API、公共分发层和 serial/tcp 主站实现，使以下接口可用：

- `mbc_master_send_request_with_timeout()`
- `mbc_master_get_parameter_with_timeout()`
- `mbc_master_get_parameter_with_uid_timeout()`
- `mbc_master_set_parameter_with_timeout()`
- `mbc_master_set_parameter_with_uid_timeout()`

当前实现方式不是在 `mb_param_request_t` 内增加 `timeout_ms` 字段，而是通过新的 `*_with_timeout()` 接口把超时参数显式传入调用链。

对应改动：

- `modbus/mb_controller/common/include/esp_modbus_master.h`
- `modbus/mb_controller/common/esp_modbus_master.c`
- `modbus/mb_controller/common/mbc_master.h`
- `modbus/mb_controller/serial/mbc_serial_master.c`
- `modbus/mb_controller/tcp/mbc_tcp_master.c`

已实现的内部行为：

- 若调用方传入 `timeout_ms != 0`，则优先使用该值
- 若传入 `timeout_ms == 0`，则回退到 serial/tcp 配置中的 `response_tout_ms`
- 若配置值也为 0，则继续回退到 `MB_MASTER_TIMEOUT_MS_RESPOND`
- controller 层信号量等待超时和底层响应定时器都已经切换为使用这个有效超时值

#### 2. 范围路由核心能力已接入完成

当前没有采用此前总结里提到的独立路由表文件方案，而是直接复用了现有 handler 链表结构，在原有 handler 节点上扩展范围字段：

- `reg_start`
- `reg_len`

并新增了范围相关内部能力：

- `mb_set_handler_range()`
- `mb_get_handler_by_addr()`
- `mb_delete_handler_range()`
- `mbm_set_handler_range()`
- `mbm_get_handler_by_addr()`
- `mbm_delete_handler_range()`

对应改动：

- `modbus/mb_objects/include/mb_common.h`
- `modbus/mb_objects/functions/mbfunc_handling.c`
- `modbus/mb_objects/include/mb_master.h`
- `modbus/mb_objects/mb_master.c`

当前匹配规则：

```c
if (entry->func_code != func_code) {
    continue;
}

if (entry->reg_len > 0) {
    if (reg_addr >= entry->reg_start && reg_addr < entry->reg_start + entry->reg_len) {
        return entry->handler;
    }
} else {
    wildcard_handler = entry->handler;
}
```

含义是：

- 范围匹配优先于通配 handler
- `reg_len == 0` 表示无范围限制，兼容旧 API 语义
- 当前仍为线性扫描，时间复杂度是 O(n)

#### 3. 自定义请求派发已经切换到范围路由

在 serial/tcp master 的自定义功能码请求分支中，原来使用 `mbm_get_handler()` 按功能码取 handler，现在已改为：

1. 用 `mbm_get_handler_by_addr(func_code, reg_start)` 查找范围 handler
2. 把命中的 handler 暂存为当前飞行中的 pending custom handler
3. 发出自定义请求
4. 响应回来时优先调用 pending handler
5. 请求结束后清理 pending handler

对应改动：

- `modbus/mb_controller/serial/mbc_serial_master.c`
- `modbus/mb_controller/tcp/mbc_tcp_master.c`
- `modbus/mb_objects/mb_master.c`

这个设计适用于当前的同步串行请求模型，因为 controller 层一次只允许一个请求持有信号量。

#### 4. 公开范围注册 API 已补齐

已经新增公开接口：

- `mbc_register_handler_range()`
- `mbc_unregister_handler_range()`

对应改动：

- `modbus/mb_controller/common/include/esp_modbus_common.h`
- `modbus/mb_controller/common/esp_modbus_common.c`

兼容性现状：

- 旧接口 `mbc_set_handler()` 仍然保留可用
- 新接口仅支持 master 模式，slave 模式会返回 `ESP_ERR_NOT_SUPPORTED`
- 旧接口可以看作“无范围限制”的 fallback handler

#### 5. 示例代码已经开始接入超时接口和范围路由示例

示例文件中已经做了两个层面的接入：

- 部分调用切换为超时版本
- 增加了最小范围 handler 示例，使用同一个自定义功能码 `0x41` 按不同 `reg_start` 路由到不同 handler

当前示例会注册：

- 一个 fallback handler：`mbc_set_handler()`
- 两个范围 handler：`mbc_register_handler_range(..., reg_start=0, reg_len=1, ...)` 和 `mbc_register_handler_range(..., reg_start=10, reg_len=1, ...)`

并发送两次自定义请求：

- `reg_start = 0`，命中 `range0` handler
- `reg_start = 10`，命中 `range1` handler

这样可以最小化验证“旧 API 兼容存在，同时新 API 优先按范围路由”的行为。

已切换或新增的示例点包括：

- `mbc_master_get_parameter_with_timeout()`
- `mbc_master_set_parameter_with_timeout()`
- `mbc_master_send_request_with_timeout()`
- `mbc_register_handler_range()`

对应文件：

- `examples/serial/mb_serial_master/main/serial_master.c`

这部分现在同时覆盖了单次调用超时和最小范围路由示例。

---

## 已定位信息

### 关键头文件位置

- `mbcontroller.h` 实际位置：
  `modbus/mb_controller/common/include/mbcontroller.h`

### 本次实际涉及的关键实现文件

- `modbus/mb_controller/common/include/esp_modbus_master.h`
- `modbus/mb_controller/common/include/esp_modbus_common.h`
- `modbus/mb_controller/common/esp_modbus_master.c`
- `modbus/mb_controller/common/esp_modbus_common.c`
- `modbus/mb_controller/common/mbc_master.h`
- `modbus/mb_controller/serial/mbc_serial_master.c`
- `modbus/mb_controller/tcp/mbc_tcp_master.c`
- `modbus/mb_objects/include/mb_common.h`
- `modbus/mb_objects/include/mb_master.h`
- `modbus/mb_objects/functions/mbfunc_handling.c`
- `modbus/mb_objects/mb_master.c`
- `examples/serial/mb_serial_master/main/serial_master.c`

---

## 当前未完成项

### 1. 与最初方案仍有偏差的点

以下目标尚未按最初设想落地：

- `mb_param_request_t` 还没有新增 `timeout_ms` 字段
- 尚未提供 `mbc_set_default_timeout()` 这类新的全局默认超时设置 API
- 没有单独创建 `mb_handler_route.h/.c` 路由表文件
- 没有实现优先级字段
- 没有实现 O(log n) 查询，当前仍是 O(n)

也就是说，目前代码已经具备“超时接口”和“按范围分派 handler”的能力，但实现路径与最早草案不同。

### 2. 验证工作未完成

以下工作尚未执行或尚未形成确认结果：

- CMake/ESP-IDF 编译验证
- 链接验证
- 范围 handler 功能测试
- 超时行为测试
- 回归测试

目前只能确认源码层面的改动已经落地，不能确认构建和运行结果。

### 3. 示例和文档仍需补充

尚未完成：

- 在示例中展示旧 API 和新 API 的兼容关系
- 在 README 或接口说明中补充新接口说明

---

## 状态评估

### 按能力划分

- 独立超时接口支持：已完成
- 范围路由核心接入：已完成
- 公开范围注册接口：已完成
- 示例超时调用改造：已完成
- 示例范围路由演示：已完成
- 编译验证：未完成
- 测试验证：未完成
- 性能优化到 O(log n)：未完成

### 按阶段重估

如果按当前真实状态重新划分：

1. 接口扩展与 controller 串联：已完成
2. 范围路由核心接入：已完成
3. 示例接入：已完成
4. 编译与测试验证：未完成
5. 性能优化与补充 API：未完成

### 总体进度

按当前落地代码估算：**约 60% - 70%**

更保守地说：

- 核心功能代码已经写入主干改动
- 但尚未经过编译、联调、测试闭环

因此当前建议对外表述为：**功能开发基本完成，验证阶段尚未开始**。

---

## 当前关键实现说明

### 超时选择逻辑

```c
effective_timeout_ms = timeout_ms ? timeout_ms : response_tout_ms;
if (!effective_timeout_ms) {
    effective_timeout_ms = MB_MASTER_TIMEOUT_MS_RESPOND;
}
```

### 范围路由语义

```c
匹配条件 = function code 相同
        && (reg_len == 0
            || reg_start <= request.reg_start < reg_start + reg_len)
```

### 向后兼容语义

```c
mbc_set_handler(ctx, func_code, handler)
```

仍然有效，可视为：

```c
mbc_register_handler_range(ctx, func_code, 0, 0, handler)
```

区别在于旧接口不会显式写入范围参数，但在匹配层面相当于通配 fallback。

---

## 下一步建议

1. 先做编译验证，确认这组接口扩展和 pending handler 机制没有引入编译错误或链接错误。
2. 补一个最小范围 handler 示例，验证 `func_code + reg_start` 的路由确实生效。
3. 再决定是否继续推进最初草案中的两个增强项：
   - 把超时移入 `mb_param_request_t`
   - 把 O(n) 路由查找升级为 O(log n)

---

## 本次总结结论

旧版进度总结里提到的 `components/mb_modbus_master/mb_handler_route.*` 和“阶段 1 已完成、阶段 2-4 未开始”已经不符合当前仓库状态。

当前仓库的真实情况是：

- 超时扩展已经贯通到公开接口和 serial/tcp 主站实现
- 范围路由已经接入现有 handler 体系
- 公开范围注册接口已经补齐
- 示例已接入超时接口和最小范围路由示例
- 尚未完成编译、测试和文档收口
