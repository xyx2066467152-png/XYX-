# K230D + STM32F103 智能门禁锁源码

这是人脸识别 UART V1 与 STM32 门禁主控的**源码副本**。K230D 只上报身份和状态，STM32 决定是否开门并控制舵机。

- `k230/`：13 个候选板端 Python 文件、PC 串口监视器、协议文档与纯软件测试。`k230/run_auth.py` 是手动入口，`boot_candidate.py` 尚未设置为自启动。
- `k230/hardware_checks/board_face_diagnostic.py`：从 CanMV IDE 内存运行的 360 帧诊断，保留失败匹配分数；使用虚拟 UART，不写 TF 卡或触发开门。
- `stm32/AccessSkeleton/`：Keil 工程源码与测试。原工程中的真实授权 RFID UID 已从这个公开副本移除。编译前请将 `Config/rfid_authorized_uid.h.example` 复制为 `Config/rfid_authorized_uid.h` 并填入自己的卡号；该文件被 Git 忽略。
- 协议格式见 `k230/PROTOCOL.md`；串口为 UART2 115200 8N1，K230 IO44/45 对 STM32 PA3/PA2。

**当前实机状态：**曾实测 USER01 经 V1 协议触发 STM32 舵机开门；持续站立去重、未注册人脸和调参后的版本尚未完成最终验收。2026-10-14 的 TF 卡写入发生错误，因此没有进一步写卡或启用 V1 自启动。本仓库不含 `USER01.bin`、注册照片、TF 卡镜像、原厂启动备份或串口原始记录。

本次失败分数诊断改动仅通过 PC 纯软件测试，板上 13 个候选文件尚未同步这些新改动。官方 0.75 阈值与 V1 线协议保持不变。

STM32 Keil 工程依赖本机 STM32F1 HAL/CMSIS 包；项目文件保留原工程的本机依赖路径，换电脑编译时需在 Keil 中调整。
