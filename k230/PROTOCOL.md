# DNK230D 身份认证模块 UART 协议 V1

## 1. 范围与语义

K230D 上报检测到的身份、相似度及模块故障。`AUTH_SUCCESS` 的含义是“人脸特征连续匹配到 USER_ID”，不表示具有通行权限。权限、开门和外设控制由未来 STM32 项目自行定义。本模块没有这些动作或判断。

生产默认接口：UART2，TX=IO44，RX=IO45，115200 baud，8 数据位，无奇偶校验，1 停止位，无硬件流控。本次按用户指定的 UART2 接线同步默认配置；UART1 的 IO40/41 仍作为显式备用配置保留。该电脑配置更新不代表整个人脸模块与 STM32 的二进制 UART 联合测试已经完成。映射来自本地官方 UART 例程及[正点原子 UART 文档](https://wiki.alientek.com/docs/Boards/Kendryte/DNK230D/example-base/uart/)。电气电平、插座脚序和具体 STM32 板型号在实际接线前确认，见 TODO。

COM4 是 USB 调试接口，不是本协议的 UART。背面 CH342 提供 UART0/4 调试通道；UART4 的本机路由实读为 IO48/49，只有实验配置允许使用，不作为生产默认接口。[板卡接口说明](https://wiki.alientek.com/docs/Boards/Kendryte/DNK230D/start-guide/k230d-box-introduction/)

## 2. 帧格式

```text
AA 55 | VERSION | CMD | SEQ_LO SEQ_HI | LEN | DATA[LEN] | CRC_LO CRC_HI
```

|偏移|长度|字段|定义|
|---|---:|---|---|
|0|2|帧头|固定 AA 55|
|2|1|VERSION|固定 01，协议版本|
|3|1|CMD|命令/消息类型|
|4|2|SEQ|无符号 16 位，小端|
|6|1|LEN|仅 DATA 的字节数，0..32|
|7|LEN|DATA|二进制字段，无字符串结尾|
|7+LEN|2|CRC|无符号 16 位，小端|

最短 9 字节，最长 41 字节。所有多字节数值均为小端。没有转义机制；DATA 可以包含 AA55，接收端须按长度及 CRC 判定，不能在 DATA 内遇到帧头就直接截断。

CRC-16/CCITT-FALSE：poly=0x1021，init=0xFFFF，refin=false，refout=false，xorout=0。覆盖 VERSION 至 DATA 的全部字节，即偏移 2 到 6+LEN；不包含帧头和 CRC。标准核对向量 ASCII `123456789` → `0x29B1`。CRC 检测传输差错，不提供密码学认证。

异步消息 SEQ 从 0 开始，每成功提交完整帧后递增，65535 后回到 0。PING/GET_STATUS 回复原样回显请求 SEQ，不消耗异步序号。SEQ 用于诊断和关联，V1 没有 ACK、重传和端到端送达保证。UART.write 成功不能证明 STM32 已收到；重启后序号重新开始，不能跨重启仅凭序号判断新旧。

## 3. CMD 与 DATA

|CMD(hex)|方向|名称|LEN|DATA（按顺序）|
|---|---|---|---:|---|
|01|模块→主控|MODULE_READY|6|接口主版本 u8，次版本 u8，特征维数 u16，阈值 Q u16|
|10|模块→主控|AUTH_SUCCESS|4|USER_ID u16，相似度 Q u16|
|11|模块→主控|UNKNOWN / AUTH_FAIL|1|reason u8|
|12|模块→主控|NO_FACE|0|空|
|7F|模块→主控|ERROR|4|error_code u16，detail u16|
|80|主控→模块|PING|0|空|
|81|主控→模块|GET_STATUS|0|空|
|82|模块→主控|PONG|0|空，SEQ 回显|
|83|模块→主控|STATUS|8|state u8，last_event u8，last_user_id u16，last_similarity Q u16，last_error u16|

Q=round(value×10000)，合法范围 0..10000，解码 value=Q/10000。编码实现对非负值使用 `int(value*10000+0.5)`。相似度是官方“余弦相似度/2+0.5”，不是概率。匹配先按原浮点数和 0.75 判断，随后量化；主控不应根据量化值重新改变匹配阈值。

USER_ID=1..65535，0 表示没有身份，仅用于 STATUS。默认显式映射：USER01→1，id_1→61441(0xF001)，id_2→61442(0xF002)。后两项是既有示例特征的标识。映射只定义传输编号，不是权限表；未知文件名不会被自动赋予编号。

MODULE_READY 只在配置、数据库、模型、摄像头/显示均初始化成功后发送一次。默认接口版本 1.0、维数 512、阈值 Q=7500。主控后连接可能错过 READY，可发送 PING 和 GET_STATUS。V1 STATUS 不包含固件唯一编号或启动随机数，后续可靠性需求应作为新协议版本设计。

UNKNOWN 原因：1=有一张人脸但未达匹配阈值；2=同时多张人脸；3=匹配名称没有数字 USER_ID 映射。NO_FACE 是经过离场确认后的视频观察结果；UNKNOWN 是有脸但不能形成有效单人身份结果，两者不同。

state：0=STARTING，1=READY，2=RUNNING，3=ERROR。last_event：0=尚无确认事件，1=NO_FACE，2=UNKNOWN，3=AUTH_SUCCESS，4=ERROR。

**STATUS 的 last_event/last_user_id/last_similarity 是最近一次已确认事件的历史快照，不能当作新的身份认证。** state/last_error 随当前原始错误更新，错误消息节流时可出现 ERROR 状态和此前的成功历史同时存在。UART 本地失败会清除快照中的身份。主控只消费新的 CMD=10 作为新身份上报，STATUS 用于诊断。

## 4. 错误码

|code(hex)|含义|V1 detail|
|---|---|---|
|0001|AI 推理/结果异常|通常 0；结构转换异常 1=检测结果不一致，2=非法相似度|
|0002|UART 读写/初始化失败|0，本地记录；UART 不可用时不能承诺发送 ERROR|
|0003|数据库缺失、为空、维数/数据异常|0|
|0004|配置非法|0，初始化 UART 前失败只返回本地诊断|
|0005|摄像头/媒体/显示异常|0|
|0006|模型/anchors 加载或初始化失败|0|
|0007|资源清理失败|0|
|0010|协议方向/应用异常保留|0；V1 对模块方向消息静默丢弃|
|0011|未知且 CRC 正确的 CMD|原 CMD 数值|
|0012|已知主控请求的 DATA 长度/字段错误|原 CMD 数值|
|00FF|内部错误保留|0|

wire error_code 为 u16，现有事件接口使用 1..255 的 V1 错误空间。0 表示无错误，不允许发送 error_code=0 的 ERROR。未定义错误码留给后续版本；PC 解析器保留其数值供诊断。

## 5. 接收与异常处理

1. 逐字节寻找 AA55，至少收齐 7 字节后读 LEN，拒绝 >32，收齐 9+LEN 后校验 CRC。
2. CRC 错误、非法长度、不支持的 VERSION：丢掉当前候选的首字节，重新扫描余下内容，不回复损坏帧。
3. 截断/分包：保留候选。从第一个候选字节开始 200ms 超时，后续零星字节不延长超时；超时丢首字节再扫描，可恢复后面完整帧。
4. 已知消息必须满足各自精确 LEN 和字段范围。未知 CMD 的合法帧可被解析，PC 显示 UNKNOWN_CMD；模块回复 ERROR_UNKNOWN_CMD，并回显该 SEQ。
5. 模块只处理 PING 和 GET_STATUS。收到 MODULE_READY、AUTH_SUCCESS、UNKNOWN、NO_FACE、ERROR、PONG、STATUS 时，在检查应用字段前就静默丢弃，防止消息反射和 ERROR 循环。错误方向消息不能改变身份状态。
6. 合法但错误的主控请求，最多每 500ms 回复一个诊断 ERROR；超过本次处理预算的请求丢弃并计数。噪声没有回复，避免错误洪泛。
7. 默认每 poll 最多读 4×128 字节，最多处理 8 个请求；每帧最多调用 write 8 次来完成短写。返回 None/0 或异常立即报本地 UART 故障，不递归尝试向坏链路发送 ERROR。
8. parser 未完成帧缓冲最多 41 字节；统计 CRC、长度、版本、噪声、超时、未知命令。返回帧列表大小取决于输入；板端通过读取预算限制本次输入。

CanMV UART 设置 timeout=0（非阻塞读）；该参数来自[CanMV v1.3 UART 手册](https://www.kendryte.com/k230_canmv/zh/v1.3/api/machine/K230_CanMV_UART%E6%A8%A1%E5%9D%97API%E6%89%8B%E5%86%8C.html)。驱动 write 调用的最坏时延与 FIFO 溢出仍需生产 UART 实测，软件调用次数限制不等于硬件时延保证。推理前后均 poll，单次 KPU/摄像头调用无法被此 Python 循环抢占。

## 6. 固定测试向量

```text
READY    AA 55 01 01 00 00 06 01 00 00 02 4C 1D DB 62
SUCCESS  AA 55 01 10 01 00 04 01 00 28 23 78 12
UNKNOWN  AA 55 01 11 02 00 01 01 C3 2B
NO_FACE  AA 55 01 12 03 00 00 C2 14
ERROR    AA 55 01 7F 04 00 04 01 00 00 00 EB A4
PING     AA 55 01 80 34 12 00 11 1A
PONG     AA 55 01 82 34 12 00 79 F7
STATUS?  AA 55 01 81 35 12 00 95 5B
STATUS   AA 55 01 83 35 12 08 02 03 01 00 28 23 00 00 64 A8
```

SUCCESS 示例 USER_ID=1，相似度 Q=9000（0.9000）。固定向量已纳入纯软件测试，CRC 另与 CPython binascii.crc_hqx 独立核对。
