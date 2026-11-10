# Microduck 缺螺丝工业视觉检测系统 (ScrewGuard) 交接文档

**生成时间**：2026-11-06  
**项目路径**：`/home/harvest/project/microduck/screw-guard`  
**核心成果物报告**：[`DELIVERY_REPORT.md`](/home/harvest/project/microduck/screw-guard/DELIVERY_REPORT.md)

---

## 1. 任务背景与核心约束

针对 Microduck 机器人装配线（橙色腿部关节件）中“多工件缺螺丝检测”场景，开发一套端到端视觉防错质检系统：
- **目标设备**：Seeed reCamera 一代（SG2002 平台，256MB RAM）。
- **技术栈硬边界**：
  - 设备端算法必须采用纯 **C++17 原生实现**，禁止 Python/GC 依赖，常驻内存设计目标 $\le 18\text{MB}$，基础 4×2 负载单帧处理设计目标 $\le 16.5\text{ms}$；
  - 目标运行时固定为 SG2002 **RISC-V/musl**；aarch64 不列为替代验收目标；
  - 必须符合 `sscma-example-sg200x` 的 Supervisor 原生应用契约（`type: native`, `debug_ws`, MQTT 结果上报）；
  - 现场工人端必须使用单文件零依赖 Web 交互标定工具，生成 Draft-07 标准化 JSON 配方。

---

## 2. 当前工程状态与成果物清单

当前交接记录源码、离线测试、浏览器和 RISC-V/musl 交叉编译证据；设备部署、真机硬件/通道/性能验收尚未完成。旧日志中的浏览器样本是 3:2、每件 2 孔，不能代表当前 4×2 基准或 4×32 压力负载。

| 模块 | 关键文件路径 | 当前状态与关键指标 |
|---|---|---|
| **工程规范** | [`SPEC.md`](/home/harvest/project/microduck/screw-guard/SPEC.md) | 规定 1~32 螺孔、$W,H \in [30,1280]$ 整数画布、180° 朝向消歧与四态仲裁 |
| **应用清单** | [`screw-guard.json`](/home/harvest/project/microduck/screw-guard/screw-guard.json) | Supervisor 原生应用清单，默认提供 4 颗螺孔示例配方 |
| **原生 C++ 核心库** | [`native/`](/home/harvest/project/microduck/screw-guard/native/) | `build-offline` 构建证据位于 `native/artifacts/final-*.log` |
| **自动化测试套件** | [`native/test_screw_inspector.cpp`](/home/harvest/project/microduck/screw-guard/native/test_screw_inspector.cpp) | `final-ctest.log` 记录 2/2；保留原 8 个测试套件，并新增几何及横/竖向 2/32 孔用例 |
| **原生 CLI** | [`native/screw_guard_cli.cpp`](/home/harvest/project/microduck/screw-guard/native/screw_guard_cli.cpp) | 4 种 Web 配方 validate/schema 链路有效；实物整图 inspect 输出 `CANNOT_INSPECT`，不构成精度通过 |
| **现场标定工具** | [`web/calib.html`](/home/harvest/project/microduck/screw-guard/web/calib.html) | 单文件 0 外部依赖；画布边界为 30~1280 像素整数，比例受画布和图像边界校验 |
| **Schema 验证器** | [`web/validate_schema.py`](/home/harvest/project/microduck/screw-guard/web/validate_schema.py) | Draft-07 验证入口；当前浏览器 run 的四孔/32 孔导出 schema 证据已记录 |
| **实物底图与切片** | [`web/test_sample.jpg`](/home/harvest/project/microduck/screw-guard/web/test_sample.jpg) | 原图 750×882 实物切片与螺栓/空孔物理中心对齐数据 |

---

## 3. 最新需求调整与实现细节

当前源码与浏览器证据记录以下行为：
1. **螺孔数量放宽**：
   - 螺孔数量值域为 **1~32 颗**；`expected_screws_per_block == screw_rois.size()` 时才接受配方；
   - 浏览器运行记录包含两孔实物试检、四孔横向/竖向配方和 32 孔配方；性能与真机负载另行验收；
   - 不一致的期望孔数和实际 ROI 数会被导入校验拒绝。
2. **长宽比自由框选（解除 3:2 锁定）**：
   - 标定画布允许非 3:2 的整数宽高，但仍受 $30\le W,H\le1280$、ROI 边界和极端比例校验限制；
   - 前端与 C++ 使用整数坐标和 `round` 规则映射，允许的误差由像素舍入和边界约束决定；
   - 横向与纵向配方均有浏览器导出样本，设备端朝向和性能仍需独立测量。

---

## 4. 集成与验收边界

1. **离线交叉编译**：`device/evidence/build-reviewed.log` 记录 exit 0；产物为 ELF64 RISC-V musl double-float，binary SHA-256 为 `3de9bd007ae58a17517db782c59075b0bd33b076746d3a22909a1b1cf0fe2704`。源码输入和 SDK 文件哈希见 `device/evidence/app-source-sha256.txt` 与 `sdk-source-sha256.txt`。
2. **生命周期与运行库静态核对**：`device/evidence/init-test.log` 覆盖 PID 身份、丢 PID、正常停止和 14 秒超时；`runtime-libraries.txt` 记录库声明/NEEDED。该证据不包含设备部署或真机运行。
3. **复现入口**：见 [`device/evidence/README.md`](/home/harvest/project/microduck/screw-guard/device/evidence/README.md)。
4. **未完成验收**：原生真实图片精度、真机性能/RSS、硬件图像通道、Supervisor/MQTT/WebSocket 和动态库加载仍需在目标设备核实；现场光照按 SPEC 75°~90° 高角度工况执行。

---

## 5. 推荐启用的技能 (Suggested Skills)

新接手会话的 Agent 建议按需调用以下技能：
- **`fleet`**：管理并连接本地边缘设备集群（reCamera / Jetson / RPi），执行远程部署、进程查看与传输。
- **`device-gotchas`**：reCamera (SG2002 / CV180x) 平台专有的硬件与驱动避坑指南（内存限制、NPU/OpenCV 交叉编译依赖）。
- **`edge-computing-optimize`**：如需进一步利用 SG2002 TPU 加速或算子优化时使用。
- **`dev-flow`**：若有新的重大功能迭代，保持 Taste 规划与独立双审协同流程。
- **`ux-review`**：对标定页面 `calib.html` 在真实工业平板/工控机触摸屏上的交互走查。

## 6. 真机验收清单（未验收项目）

- [ ] SSH/设备连接：记录设备标识、时间、RISC-V/musl 版本与原始命令输出。
- [ ] 磁盘空间与现有容器/进程状态：部署前记录，避免覆盖未知运行态。
- [ ] 进程启动与重启：记录退出码、启动日志、`applied_revision` 前后值。
- [ ] MQTT 与 WebSocket：记录主题、连接地址、合法首帧和 revision 一致性。
- [ ] 图像契约：记录 RGB 通道顺序、分辨率、帧号与时间戳。
- [ ] 性能：4×2 基础负载连续 10,000 帧记录 P95/P99；分别记录 4×32 压力负载，不承诺压力负载达标。
- [ ] RSS：记录 `/proc/<pid>/status` 的 VmRSS，目标不超过 18MB。
- [ ] 128 孔：确认 4×32 配方可导入、结果字段完整、超时或退化可观测。
- [ ] 长稳：按现场计划执行并记录运行时长、错误计数、RSS 趋势；未取得数据前不标记完成。

已具备的离线证据：`native/artifacts/final-ctest.log`（2/2）、`native/artifacts/final-cli/summary.json`（四种配方 validate/schema 链路）、`device/evidence/build-reviewed.log`（RISC-V/musl 交叉编译）、`device/evidence/init-test.log`（生命周期替身）和 `device/evidence/runtime-libraries.txt`（动态依赖清单核对）。这些证据不替代真实图片精度和目标设备运行验收。
