# 原厂 SD 卡录音回放的宿主行为测试

从固件源码根目录运行：

```sh
python3 tests/run_app_record_tests.py
```

脚本直接编译 `main/apps/app_record/app_record.cpp`，并使用生产头文件 `record_storage.h` 读写真正的临时目录和 WAV 文件，不复制或另写被测状态机、存储算法。此目录仅提供宿主环境所需的 HAL 依赖：双请求异步麦克风、同声道双请求扬声器、时钟、按键、画布，以及指向独立临时目录的 SD 挂载接口。`tests/test_app_record.cpp` 通过公开生命周期驱动生产代码。

15 组场景覆盖：

- 整段录音超过旧版 1 秒限制，以 24 KiB PCM 池流式保存，WAV 内容与采集时间顺序一致；完整播放内容逐样本相同。
- 麦克风请求已发布但任务尚未启动时，`isRecording()` 为 0，不误认为请求已完成；两个待录槽均满时不会再次调用可能阻塞的入队函数。
- `canRecord()`、`canPlay(0)` 为假时，即使队列计数尚未满也不提交；两个待播请求均满时让出主循环。
- OK 停止录音并保存；G0 等待已提交的采样完成后保存退出；重新打开以及新建 App 实例均能从 SD 加载录音，不依赖旧 PCM 内存。
- 上下选择历史录音；新文件使用不覆盖的序号，旧文件内容保持完整。
- OK 中止播放，G0 退出播放；异步读者释放前缓冲不被重用或释放；配置、音量、键音恢复。
- SD 缺失后插卡重试、与 `RECORD` 同名的用户文件保留、24 KiB 分配失败、麦克风／扬声器启动失败及录放入队失败。
- 采集、停止排空、播放长时间无进展后的取消与错误提示。
- 文件打开后被外部截断时停止播放；用 POSIX 文件大小资源限制产生真实短写，停止采集并把所有完整 PCM 样本保留在可读 WAV 中，测试后恢复资源限制和信号处理。
- 波形按 100 ms 节流刷新，强制关闭及重新进入时清理队列状态。

Fake 扬声器在提交时保留样本快照，在消费及取消时检查原缓冲仍然有效且内容未变；Fake 麦克风检查未完成请求之间没有重叠内存。AddressSanitizer 和 UndefinedBehaviorSanitizer 同时启用。编译产物和模拟 SD 目录使用标准临时目录，结束时自动清理，不访问真实 `/sdcard` 或用户录音。

macOS 的 AddressSanitizer 不支持 LeakSanitizer，因此 macOS 仅启用 ASan／UBSan，不声称完成泄漏检测。测试前后校验三个实际生产源文件没有变化，并输出其 SHA-256。

这套测试不模拟 SD 卡挂载、FAT 驱动、真实 ESP32 任务调度、ES8311 寄存器、I2S DMA、采样电平或扬声器音质，不能代替设备端录音文件及听感验收。Fake `Mic.end()` 的队列清理行为由独立 SDK 测试检查，单靠 App 测试无法证明真实 RTOS 的取消时限。

## SDK 生产方法的独立验证

```sh
python3 tests/run_record_sdk_tests.py
```

独立脚本从当前 `components/M5Unified/src/utility/Mic_Class.cpp` 和 `Speaker_Class.cpp` 提取完整生产方法，包含整个 `Mic_Class::mic_task`、两侧生命周期及实际录放入队方法，再以可控 FreeRTOS、I2S 和寄存器依赖运行测试。方法正文不进行替换，测试没有另写 SDK 生命周期算法；测试日志列出当前方法数、场景数及生产源码 SHA-256。

原先 SDK 描述符遗留问题曾通过修改前文件回归对照：第一个 `idle Mic.end` 场景报 retained an old recording descriptor。修改后应继续通过队列清理、重开取消、资源分配失败与错误传播场景。这些结果不代表实际设备上的调度、I2S 时序或音质已经通过验收。
