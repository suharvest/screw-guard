# Microduck 缺螺丝工业视觉检测系统 (ScrewGuard) 交付报告

本报告承接旧交付报告的章节结构，统一当前规范和验收边界。报告记录源码、离线样本和模拟接口证据；不将历史浏览器日志或未执行的真机步骤标记为生产完成。

## 一、交付物全景

| 模块 | 文件路径 | 当前记录 |
|---|---|---|
| 工程规范 | [`SPEC.md`](SPEC.md) | v3.6；RISC-V/musl；RSS 18MB；基础 4×2 负载目标 16.5ms；4×32 为扩展压力负载 |
| Supervisor 应用配置 | [`screw-guard.json`](screw-guard.json) | native 类型、debug_ws、MQTT 和配置 schema；真机接入待验收 |
| C++ 配置解析器 | [`native/app_config.h`](native/app_config.h)、[`native/app_config.cpp`](native/app_config.cpp) | 配置值域和 1~32 孔约束；由 [`final-ctest.log`](native/artifacts/final-ctest.log) 覆盖 |
| C++ 报文序列化器 | [`native/result_payload.h`](native/result_payload.h)、[`native/result_payload.cpp`](native/result_payload.cpp) | 结果报文结构和 schema 对齐；真机 MQTT 仍待验证 |
| C++ 核心检测引擎 | [`native/screw_inspector.h`](native/screw_inspector.h)、[`native/screw_inspector.cpp`](native/screw_inspector.cpp) | 状态机、几何/朝向和逐孔评分实现 |
| 原生自动化测试 | [`native/test_screw_inspector.cpp`](native/test_screw_inspector.cpp) | `build-offline` 下 ctest 2/2；保留原 8 个测试套件，并新增几何及横/竖向 2/32 孔用例 |
| 原生 CLI | [`native/screw_guard_cli.cpp`](native/screw_guard_cli.cpp) | 4 种 Web 导出配置可完成 validate/schema 链路；实物整图 inspect 结果仍需按精度验收 |
| 现场标定工具 | [`web/calib.html`](web/calib.html) | 30~1280 像素整数宽高；比例、ROI 和导入状态受边界校验 |
| schema 验证器 | [`web/validate_schema.py`](web/validate_schema.py) | Draft-07 验证入口；当前浏览器 run 的四孔/32 孔导出 schema 证据见 [`sha256.txt`](web/artifacts/run-2026-11-06T04-39-13-167Z/sha256.txt) |

## 二、端到端浏览器交互与实测验收证据

旧的 [`web/e2e.log`](web/e2e.log) 仍是历史 3:2、2 孔记录。当前浏览器证据位于 [`web/artifacts/run-2026-11-06T04-39-13-167Z/`](web/artifacts/run-2026-11-06T04-39-13-167Z/)，其 `stdout.log` 记录 25 个检查点：实物两孔试检、四孔横向/竖向导出、30×30 与 1280×1280 边界、32 孔导出、33 孔拒绝、非法尺寸/孔数不一致拒绝、非法导入保持原状态和无 pageerror。该运行的最终 [`sha256.txt`](web/artifacts/run-2026-11-06T04-39-13-167Z/sha256.txt) 绑定当前截图、JSON、`calib.html`、`validate_schema.py` 和运行脚本源码；不把旧 stdout 文件哈希作为目录清单结论。

当前证据分层如下：

- **合成图原生单测**：验证配置值域、四态互斥、朝向异常、数量仲裁和 JSON 报文字段。
- **实物离线图片**：验证 RGB 图片输入、标定映射和实物样本的孔级裁决。
- **历史模拟接口/E2E**：旧日志覆盖配置读写合并、revision 生效等待和 WebSocket/HTTP 超时；当前 25 项运行主要覆盖导入、导出、尺寸/孔数边界和状态保持。
- **native 最终测试**：[`final-ctest.log`](native/artifacts/final-ctest.log) 记录 `2/2` 通过；[`final-tests-full.log`](native/artifacts/final-tests-full.log) 保留原 8 个测试套件，并新增几何修复、横向/竖向 2 孔与 32 孔全有/缺 1 的孔级和数量断言，以及整数溢出拒绝。
- **native CLI 结果**：[`final-cli/summary.json`](native/artifacts/final-cli/summary.json) 中 4 种配置的 validate 退出码为 0 且 schema 结构有效；`two-hole` inspect 为 `CANNOT_INSPECT_ORIENTATION`，`four-horizontal` 另有 `CANNOT_INSPECT_LOW_CONFIDENCE`。CLI schema/链路通过不表示原生实物精度通过，也不等于浏览器两孔试检的 `PRESENT/MISSING` 结论。
- **浏览器 25 项运行**：[`stdout.log`](web/artifacts/run-2026-11-06T04-39-13-167Z/stdout.log)、[`sha256.txt`](web/artifacts/run-2026-11-06T04-39-13-167Z/sha256.txt)；覆盖四孔横向/竖向、32 孔、尺寸边界和非法导入状态保持。
- **独立交互审查证据**：[`web/artifacts/review/`](web/artifacts/review/)；包含四孔导出、非法导入保持状态和超比例拒绝截图/状态文件。该目录的控制台记录含 favicon 404 与密码字段表单提示，不将其描述为零错误。
- **RISC-V/musl 交叉编译**：[`device/evidence/build-reviewed.log`](device/evidence/build-reviewed.log)、[`binary-verification.txt`](device/evidence/binary-verification.txt)、[`init-test.log`](device/evidence/init-test.log)、[`runtime-libraries.txt`](device/evidence/runtime-libraries.txt)；构建、生命周期替身和动态依赖清单核对已有离线证据，未执行部署或真机运行。
- **RISC-V/musl 真机**：设备启动/重启、MQTT、WebSocket、图像通道、性能、RSS 和长稳仍未验收。

### 复现命令

以下命令只用于本地证据采集，不表示命令已经在真机执行：

```sh
cd native
cmake -S . -B build-offline -DCMAKE_BUILD_TYPE=Release
cmake --build build-offline --parallel
ctest --test-dir build-offline --output-on-failure
./build-offline/screw_guard_cli --config <recipe.json> --image <image.rgb> --validate-only

cd screw-guard
uv run python web/validate_schema.py <recipe.json>
node web/test_e2e_walkthrough.mjs
sha256sum SPEC.md DELIVERY_REPORT.md native/*.cpp native/*.h web/calib.html web/validate_schema.py
```

### 当前源码身份与证据登记

- 当前规范版本：`SPEC.md` v3.6。
- 设备目标：SG2002 RISC-V/Linux Yocto，验收运行时固定为 RISC-V/musl。
- 基础性能负载：4 工件 × 2 孔 = 8 ROI。
- 扩展压力负载：4 工件 × 32 孔 = 128 ROI；只记录实际耗时/RSS，不承诺达到 16.5ms。
- 当前 native 构建和单测证据：`native/artifacts/final-configure.log`、`final-build.log`、`final-ctest.log`、`final-source-sha256.txt`。
- `native/artifacts/final-cli/summary.json` 的四种配方 validate/schema 链路有效；`two-hole` 实物整图 inspect 是 `CANNOT_INSPECT_ORIENTATION`，`four-horizontal` 另有 `CANNOT_INSPECT_LOW_CONFIDENCE`，当前未形成原生实物精度验收。
- 当前真机命令输出、进程 revision、MQTT/WS 报文、10,000 帧 P95/P99 和 RSS 未形成验收证据。

### 历史证据与当前证据的区别

旧报告曾将浏览器截图、2 孔导出和本地构建描述为全系统通过。现有日志可复核其历史输入与输出，但不能证明当前源码、当前规范或 SG2002 真机状态。本报告按证据层记录当前浏览器结果，不将其扩展为真机或生产结论。

## 三、架构关键决策与安全防线

1. **硬件与内存边界**：设备算法为 C++17 原生实现，目标 RSS 不超过 18MB；16.5ms 只适用于基础 4×2 设计负载，需在 RISC-V/musl 真机测量。
2. **前后端几何契约**：归一化坐标映射覆盖 `W,H ∈ [30,1280]` 的任意整数宽高；画布和原图 patch 均执行边界检查。
3. **朝向消歧**：锚点分数必须有限且满足互斥阈值；不确定时输出 `CANNOT_INSPECT_ORIENTATION`，不使用螺丝存在性反推朝向。
4. **整数窄化防御**：配置整数字段在进入 `int` 前进行范围检查，拒绝越界和孔数/期望数不一致的配方。
5. **四态和整批仲裁**：孔级状态为 `PRESENT`、`MISSING`、`UNCERTAIN`、`CANNOT_INSPECT`；整批优先级为 `DEFECT > CANNOT_INSPECT > PASS`。
6. **配置生效闭环**：前端使用 Read-Merge-Write 保留完整 Supervisor `values`，以合法首帧和 `applied_revision` 一致作为生效确认，6 秒内失败则熔断。

## 四、待真机验收清单

部署或发布不属于本报告范围。取得真机授权后，按下列顺序采集原始证据：

1. 连接：记录 SSH 连通性、设备标识、系统/RISC-V/musl 版本。
2. 部署前状态：记录磁盘空间、现有容器、Supervisor 和目标进程状态。
3. 启动与重启：记录进程启动、退出/重启原因和 revision 前后值。
4. 通道：记录 MQTT topic、WebSocket 地址、合法首帧和 `applied_revision`。
5. 图像：确认 RGB 通道顺序、分辨率、帧号和时间戳。
6. 性能：基础 4×2 连续 10,000 帧取 P95/P99；再测 4×32 并单独记录，不承诺压力负载达标。
7. 内存：采样 `/proc/<pid>/status` 的 VmRSS，核对 18MB 目标。
8. 容量：验证 128 孔配方导入、结果字段完整和超容量行为。
9. 长稳：记录计划时长、错误计数和 RSS 趋势；无数据时保持“待验收”。

### 真机验收记录

| 项目 | 原始证据位置 | 结果 |
|---|---|---|
| 连接/磁盘/现容器 | 未提供目标设备记录 | 未验收 |
| 启动/重启/revision | 未提供设备记录 | 未验收 |
| MQTT/WS 合法首帧 | 未提供设备记录 | 未验收 |
| RGB 图像契约 | 未提供设备记录 | 未验收 |
| 4×2，10,000 帧 P95/P99 | 未提供设备记录 | 未验收 |
| 4×32，128 ROI 压力负载 | 原生 32 孔合成断言已记录；设备负载未提供 | 未验收 |
| RSS 与长稳 | 未提供设备记录 | 未验收 |
| 原生真实图片精度 | `final-cli/summary.json`：`CANNOT_INSPECT` | 未通过 |
| RISC-V/musl 交叉编译 | [`device/evidence/README.md`](device/evidence/README.md) 及 build/binary/init/runtime 证据 | 离线完成，未部署 |

## 五、变更证据与哈希登记

本次文档变更只涉及 `SPEC.md`、`/tmp/handoff-screw-guard.md` 和本报告。旧外部报告未修改。以下命令生成文档哈希：

```sh
sha256sum \
  SPEC.md \
  DELIVERY_REPORT.md \
  /tmp/handoff-screw-guard.md
```

修改前后重点段落：

- SPEC 顶部：20ms 设计指标统一为 16.5ms 设计目标，并固定 RISC-V/musl。
- SPEC §6：明确 4×2=8 ROI 基础基准和 4×32=128 ROI 扩展压力负载，不承诺扩展负载达标。
- SPEC §6.1：分离合成图原生单测、实物离线图片、模拟接口/E2E 和真机证据。
- handoff §2/§3/§4/§6：修正链接和历史绝对措辞，加入当前浏览器证据范围与真机清单。

## 六、结论

当前交付状态为：源码、文档、浏览器证据、native 合成/链路证据和 RISC-V/musl 交叉编译证据已登记；`test_sample` 的 native CLI 实物整图结果为 `CANNOT_INSPECT_ORIENTATION`，原生实物精度尚未通过验收。性能目标仍是设计目标；真机连接、硬件图像通道、RISC-V/musl 性能、RSS、MQTT/WS、128 孔设备负载、动态库加载和长稳尚未验收。
