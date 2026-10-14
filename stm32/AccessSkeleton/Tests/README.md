# STM32 软件测试

`run_protocol_tests.py` 使用 ARM 编译器构建实际的 C 解析器和认证策略，
再用 Unicorn 执行；默认读取仓库中的 `k230/uart_protocol.py` 作为独立协议参照。
需要安装 Unicorn 与 pyelftools，并通过 `--deps` 指向依赖目录、
通过 `--compiler` 指向 ARM 编译器。

`run_uart_ring_tests.py` 对已编译 AXF 中的 UART 环形缓冲逻辑做寄存器模拟。
这两类测试都是软件模拟，不代表双板实机验收。
