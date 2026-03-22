# 寄存器范围路由器使用说明

本文只回答一件事：如何在 master 和 slave 里使用 `func_code + reg_start + reg_len` 的范围路由。

---

## 1. 先记住 4 个接口

```c
esp_err_t mbc_set_handler(void *ctx, uint8_t func_code, mb_fn_handler_fp handler);
esp_err_t mbc_get_handler(void *ctx, uint8_t func_code, mb_fn_handler_fp *handler);
esp_err_t mbc_delete_handler(void *ctx, uint8_t func_code);
esp_err_t mbc_register_handler_range(void *ctx, uint8_t func_code,
                                     uint16_t reg_start, uint16_t reg_len,
                                     mb_fn_handler_fp handler);
esp_err_t mbc_unregister_handler_range(void *ctx, uint8_t func_code,
                                       uint16_t reg_start, uint16_t reg_len);
```

语义如下：

1. `mbc_set_handler()` 注册某个功能码的默认 fallback handler。
2. `mbc_register_handler_range()` 注册某个功能码下的范围子路由。
3. 首次注册范围子路由时，系统会自动安装 dispatcher。
4. `mbc_get_handler()` 在启用范围路由后返回 dispatcher，不再返回原始 fallback handler。
5. `mbc_delete_handler()` 在启用范围路由后只清 fallback，不会删除范围子路由。
6. `reg_len` 必须大于 0。
7. 重叠范围会被拒绝，返回 `ESP_ERR_INVALID_ARG`。
8. `0x17` 不支持范围子路由注册；如需自定义处理，应继续使用 `mbc_set_handler()` 注册功能码级 handler。
9. slave 侧仅支持 `0x01`、`0x02`、`0x03`、`0x04`、`0x05`、`0x06`、`0x0F`、`0x10` 这 8 个标准单区间功能码的范围子路由注册。

---

## 2. Master 怎么用

### 2.1 适用场景

当你希望同一个功能码下，按照寄存器访问区间把请求分发给不同 handler 时使用。

最常见的写法：

```c
const uint8_t fc = 0x41;

// 只有需要未命中范围时的兜底处理，才需要先注册 fallback。
mbc_set_handler(master_handle, fc, my_fallback_handler);

// 只注册范围子路由也可以，首次注册时会自动安装 dispatcher。
mbc_register_handler_range(master_handle, fc, 0, 1, my_range0_handler);
mbc_register_handler_range(master_handle, fc, 10, 1, my_range1_handler);
```

行为如下：

1. 请求发出前，先用 `func_code + 请求完整区间` 匹配范围。
2. 命中范围时，请求对应的响应会转发给该范围 handler。
3. 未命中范围但存在 fallback 时，回退到 fallback handler。
4. 未命中范围且没有 fallback 时，请求直接失败。

判断条件不是“起始地址落在范围内”即可，而是“请求完整区间必须被注册范围完整包含”。

判断规则：

1. `mbc_register_handler_range()` 之前不必先调用 `mbc_set_handler()`。
2. 如果你只关心命中范围时走子路由，不需要 fallback，那只注册 range 即可。
3. 如果你希望未命中范围时继续走默认处理器，才需要额外调用 `mbc_set_handler()`。
4. 如果同一个功能码既注册了 fallback，又注册了 range，那么命中范围时不会再执行 fallback；只有未命中范围时才会执行 fallback。
5. `0x17` 不能调用 `mbc_register_handler_range()`；该功能码只支持功能码级 handler。
6. master 侧不额外限制功能码白名单；只要请求结构能明确给出单区间 `reg_start + reg_size`，就可以参与范围匹配。

### 2.2 `reg_start` 该填什么

Master 侧匹配的是“你发请求时使用的完整访问区间”。

也就是说：

1. 如果你走 `mbc_master_send_request()` / `mbc_master_send_request_with_timeout()`，就按 `request.reg_start + request.reg_size` 来匹配。
2. 如果你走参数表接口，通常就是参数描述表里的 `mb_reg_start`，该值是 0-based。
3. 对单寄存器/单线圈写，内部会按长度 1 参与匹配。
4. `0x17` 不支持范围子路由；如需自定义处理，应继续通过 `mbc_set_handler()` 注册功能码级 handler。

### 2.3 最小示例位置

可直接参考：

1. `examples/serial/mb_serial_master/main/serial_master.c`

---

## 3. Slave 怎么用

### 3.1 适用场景

当你希望同一个标准功能码在 slave 侧按照寄存器范围走不同处理逻辑时使用。

最常见的写法：

```c
const uint8_t fc = 0x03;

// 只有需要未命中范围时的兜底处理，才需要先注册 fallback。
mbc_set_handler(slave_handle, fc, slave_holding_fallback_handler);

// 只注册范围子路由也可以，首次注册时会自动安装 dispatcher。
mbc_register_handler_range(slave_handle, fc, range0_start, range0_len,
                           slave_holding_range0_handler);
mbc_register_handler_range(slave_handle, fc, range1_start, range1_len,
                           slave_holding_range1_handler);
```

行为如下：

1. slave 在 dispatcher 内直接从请求 PDU 解析请求区间。
2. 命中范围时，调用对应范围 handler。
3. 未命中时，回退到 `mbc_set_handler()` 设置的 fallback handler。

判断规则：

1. `mbc_register_handler_range()` 之前不必先调用 `mbc_set_handler()`。
2. 如果你只关心命中范围时走子路由，不需要 fallback，那只注册 range 即可。
3. 如果你希望未命中范围时继续走默认处理器，才需要额外调用 `mbc_set_handler()`。
4. 如果同一个功能码既注册了 fallback，又注册了 range，那么命中范围时不会再执行 fallback；只有未命中范围时才会执行 fallback。
5. `0x17` 不能调用 `mbc_register_handler_range()`；该功能码只支持功能码级 handler。
6. slave 侧仅支持 `0x01`、`0x02`、`0x03`、`0x04`、`0x05`、`0x06`、`0x0F`、`0x10` 的范围子路由；其他功能码即使有默认 handler，也不能注册范围子路由。

### 3.2 `reg_start` 该填什么

这是 slave 侧最容易写错的地方。

当前 slave 范围路由匹配的是“slave 内部处理链看到的完整访问区间”。

对标准寄存器/线圈功能码，内部地址通常比协议报文里的 0-based 起始地址多 1。

所以一般规则是：

1. 如果你的寄存器区域 `start_offset` 是 0-based 的 `N`，那 slave 路由注册时通常写 `N + 1`。
2. 例如 holding 区域从 `0` 开始，想匹配第一个寄存器，通常写 `reg_start = 1`。

### 3.3 推荐写法

对标准功能码，不要自己重写整套协议解析；最稳妥的方式是：

1. 在范围 handler 里先做你自己的分流逻辑或日志。
2. 然后继续调用原有标准 handler，例如 `mbs_fn_read_holding_reg()`。

这样可以保留原有寄存器访问逻辑、异常码和响应格式。

### 3.4 最小示例位置

可直接参考：

1. `examples/serial/mb_serial_slave/main/serial_slave.c`

该示例已经包含：

1. fallback handler
2. 两个范围 handler
3. 重叠注册失败检查
4. `mbc_get_handler()` 返回 dispatcher 的检查

---

## 4. 运行时语义

启用范围路由后，可以按下面理解：

1. 入口仍然是 `func_code -> dispatcher`
2. dispatcher 内部再根据完整单区间请求决定走哪个范围 handler 或 fallback
3. dispatcher 安装后不会因为你删除某个范围而自动卸载
4. 删除 fallback 也不会影响已注册的范围子路由
5. 只注册 range 也能工作；`mbc_set_handler()` 不是前置必需步骤
6. 同一功能码下若同时存在 range 和 fallback，则范围命中优先，fallback 只在未命中时触发
7. `0x17` 不支持 range routing，也不支持范围子路由注册；只能走功能码级 handler
8. slave 侧只对白名单中的标准单区间功能码开放范围子路由注册

---

## 5. 一页口诀

1. `mbc_set_handler()` = 默认 fallback
2. `mbc_register_handler_range()` = 范围子路由
3. `mbc_get_handler()` = wrap 启用后拿到 dispatcher
4. master 按“发请求时的完整单区间请求”匹配
5. slave 按“内部处理链里的完整单区间请求”匹配，标准寄存器功能码通常要写 `start_offset + 1`
6. 范围不能重叠
7. `mbc_set_handler()` 不是必须，只有需要 fallback 时才注册
8. 命中范围时只走范围 handler，不再走 fallback
9. `0x17` 不支持范围子路由，继续使用功能码级 handler
10. slave 侧范围子路由只支持 `0x01`、`0x02`、`0x03`、`0x04`、`0x05`、`0x06`、`0x0F`、`0x10`
