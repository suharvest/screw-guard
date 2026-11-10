# ScrewGuard for reCamera SG2002 — Microduck C++ 螺丝漏检应用 SPEC (v3.6)

> **目标设备**：Seeed reCamera 一代 (SG2002, 256MB RAM, 1.0 TOPS TPU, RISC-V/Linux Yocto)。  
> **技术栈**：**纯 C++ (C++17 / CMake)**，无 Python/GC 开销，常驻内存设计指标 $\le 18\text{MB}$，单帧处理耗时设计目标 $\le 16.5\text{ms}$。  
> **目标运行时**：SG2002 的 **RISC-V / musl** 运行时；本规范不以 aarch64 工具链作为替代验收目标。  
> **上游契约依据**：严格对齐 `sscma-example-sg200x`（基准 commit `ebbc901b`）Supervisor 原生应用规范（`type: native`）。

---

## 1. 核心工况边界与质检定义

### 1.1 场景与检测范围
- **检测目标**：Microduck 双腿关节等装配分件在分总成治具上的紧固件（M2/M2.5 沉头/盘头螺栓）的**可见在位存在性（Visible Presence）**。
- **缺陷检出能力**：**漏装（确诊缺螺丝）**、**完全空孔**、**工件欠放（数量不足）**、**工件超放（杂乱多放）**。
- **明确边界排除**：本 2D 视觉系统**不负责**螺纹啮合扭矩、微米级拧紧深度公差（微小浮锁）及螺牙规格真伪的判定；机械扭矩指标由装配工步智能电批闭环。
- **光学环境**：工位安装 75°~90° 高角度环形漫射光或同轴光源，中心照度 $800 \sim 1200\text{ lux}$，配置物理遮光罩阻断车间日光灯与人影晃动。
  该角度与照度是规范工况；低角度光照仅作为现场对照试验，不替代规范工况验收。

---

## 2. Supervisor 接入契约与配置全量闭环

### 2.1 Supervisor 描述清单 (`screw-guard.json`)
严格遵从 Supervisor `config_schema.hpp` 语法规范（数值型一律使用 `number`，字符串显式定义 `maxLength`，`confidence_threshold` 严格下限为 0.50）：

```json
{
  "id": "screw-guard",
  "name": "ScrewGuard Inspector",
  "name_zh": "紧固件螺丝防漏检系统",
  "scene": "Industrial QA",
  "scene_zh": "工业质检",
  "description": "Ultra-low-footprint C++ screw missing and presence inspector for Microduck on reCamera SG2002.",
  "description_zh": "专为 reCamera SG2002 打造的超低内存 C++ 螺丝漏打与质检应用，支持多工件自动对齐与整批防呆。",
  "type": "native",
  "init_script": "/etc/init.d/K95screw-guard",
  "image": "/apps/screw-guard.png",
  "rtsp_url": "rtsp://{host}:8554/live0",
  "mqtt_topic": "recamera/screw-guard/results",
  "debug_ws": {
    "port": 8001,
    "video_path": "/",
    "results_path": "/results"
  },
  "version": "1.0.0",
  "author": "Seeed Studio",
  "config_schema": {
    "groups": [
      {
        "key": "inspection",
        "title": "Inspection Configuration",
        "title_zh": "质检配置参数",
        "items": [
          {
            "key": "profile_json",
            "type": "string",
            "title": "Recipe Profile (JSON)",
            "title_zh": "工件与螺孔配方参数",
            "description": "由前端标定工具生成的标准化配方 JSON 字符串 (支持 1~32 颗螺孔与任意尺寸对齐画布, 最大 4096 字节)",
            "maxLength": 4096,
            "default": "{\"revision\":1,\"part_name\":\"leg_joint\",\"expected_blocks\":2,\"expected_screws_per_block\":4,\"canonical_size\":[300,200],\"min_block_area\":4000,\"anchor\":{\"norm_x\":0.15,\"norm_y\":0.5,\"norm_w\":0.15,\"norm_h\":0.25,\"ref_edge_mean\":32.5},\"screw_rois\":[{\"id\":1,\"name\":\"侧面M2_1\",\"norm_x\":0.283,\"norm_y\":0.300,\"norm_r\":0.080},{\"id\":2,\"name\":\"侧面M2_2\",\"norm_x\":0.283,\"norm_y\":0.700,\"norm_r\":0.080},{\"id\":3,\"name\":\"关节M2_1\",\"norm_x\":0.600,\"norm_y\":0.300,\"norm_r\":0.080},{\"id\":4,\"name\":\"关节M2_2\",\"norm_x\":0.600,\"norm_y\":0.700,\"norm_r\":0.080}]}"
          },
          {
            "key": "confidence_threshold",
            "type": "number",
            "title": "Confidence Threshold",
            "title_zh": "螺丝综合置信度门限",
            "min": 0.50,
            "max": 0.95,
            "step": 0.05,
            "default": 0.60
          },
          {
            "key": "max_blocks",
            "type": "number",
            "title": "Max Blocks / Frame",
            "title_zh": "单帧最大检测工件数",
            "min": 1,
            "max": 4,
            "step": 1,
            "default": 4
          }
        ]
      }
    ]
  }
}
```

### 2.2 标定工具与 Supervisor 配置读写安全闭环
1. **全量保留策略 (Read-Merge-Write)**：
   - 前端下发新配方时，先通过 `GET /api/appMgr/getConfig?app_id=screw-guard` 读取完整 `values` 对象；
   - 将更新的 `profile_json` 合并入对象，避免单项提交导致其他运行时参数丢失；
   - 最终调用 `POST /api/appMgr/setConfig` 发送包含完整键值对的载荷。
2. **应用重启与真实生效确认状态机**：
   - `setConfig` 成功后，Supervisor 将自动向后台发送停止与启动信号；
   - 前端立即转入“等待应用重启”状态，以 200ms 间隔轮询重连 `debug_ws` (`ws://<ip>:8001/results`)；
   - **统一定时熔断机制**：从下发配置起计算，**累计 6 秒内若出现以下任一情况，一律判定为生效失败并报警熔断**：
     - ① WebSocket 连接失败且超时未建立；
     - ② WebSocket 连接成功但持续静默（未收到任何报文）；
     - ③ 收到的报文非法、缺失必填字段，或其 `applied_revision != draft_revision`；
   - 仅当收到合法首帧且 `applied_revision == draft_revision` 时，才提示“新配方已在设备运行生效”；失败时提供【重新提交】或【回退配置】操作。

---

## 3. 几何映射与 180° 朝向消除

### 3.1 坐标规范与原图 Patch 边界防御
- 标准工件对齐画布：$W_{canon} \in [30, 1280]\text{ px}, H_{canon} \in [30, 1280]\text{ px}$（支持任意宽高比整数对，不再限制 3:2 比例，默认标定推荐尺寸为 $300 \times 200$）。
- 标准画布坐标映射：
  $$x_c = \text{round}(norm\_x \times (W_{canon} - 1))$$
  $$y_c = \text{round}(norm\_y \times (H_{canon} - 1))$$
  $$r = \text{round}(norm\_r \times \min(W_{canon}, H_{canon}))$$
- **画布边界检查**：强制断言 $x_c - r \ge 0 \land x_c + r < W_{canon} \land y_c - r \ge 0 \land y_c + r < H_{canon}$，前端标定时若超出直接禁用保存。
- **原图局部裁剪与边界防御**：
  根据仿射逆矩阵 $M^{-1}$，将标准画布螺孔中心 $(x_c, y_c)$ 映射回 $1920 \times 1080$ 原图像素坐标 $(X_{orig}, Y_{orig})$。
  在原图上截取以 $(X_{orig}, Y_{orig})$ 为中心的 $48 \times 48$ 像素高精 ROI：
  $$\text{ValidPatch} = (X_{orig} - 24 \ge 0) \land (X_{orig} + 24 < W_{orig}) \land (Y_{orig} - 24 \ge 0) \land (Y_{orig} + 24 < H_{orig})$$
  若 $\text{ValidPatch}$ 为假（逆变换后触碰原图边缘）：**该孔位状态严格标记为 `CANNOT_INSPECT`，`score` 输出 -1.0**。工件最终状态与主原因统一交由 §4.2 状态机裁决，避免出现局部裁决冲突。

### 3.2 180° 朝向消歧与前置有效性防御
工件经几何外接矩形检出时，存在 0° 与 180° 两种退化解。
1. **配方锚点**：配方保存非对称结构在归一化坐标下的 ROI 及参考特征值 `ref_edge_mean`。
2. **前置有效性校验与互斥分支**：
   - 提取 0° 与 180° 候选姿态下的锚点 ROI，计算归一化匹配分数 $S_0$ 与 $S_{180}$。
   - **前置硬防御**：必须严格满足 `std::isfinite(S0) && std::isfinite(S180)`。**若任一候选为 NaN、Inf 或 ROI 提取失败，立刻判定为朝向歧义，严禁将异常值置零后继续参与比较！**
   - **互斥决策分支**：
     - **分支 1（正向确定）**：$S_0 \ge 0.65 \land (S_0 - S_{180} \ge 0.25) \implies 0^\circ$ 姿态确认为正向；
     - **分支 2（倒向确定）**：$S_{180} \ge 0.65 \land (S_{180} - S_0 \ge 0.25) \implies 180^\circ$ 姿态确认为倒向，旋转矩阵补旋转 $180^\circ$；
     - **分支 3（其余所有情况穷尽归一）**：不满足上述两个确定分支的任意输入（包括分差不足、分数过低、噪点扰动），一律判定为：
       $$\text{block.status} = \text{"CANNOT_INSPECT"}, \quad \text{block.reason} = \text{"CANNOT_INSPECT_ORIENTATION"}$$
       **绝对禁止使用螺丝存在性反推朝向！该工件直接阻断自动放行。**

---

## 4. 逐孔判定互斥性与整批完整性

### 4.1 互斥且穷尽的逐孔四态判定
```cpp
// 1. Sobel 绝对加权梯度均值 (衡量十字槽/内六角边缘能量)
cv::Mat grad_x, grad_y, abs_grad;
cv::Sobel(patch_gray, grad_x, CV_16S, 1, 0, 3);
cv::Sobel(patch_gray, grad_y, CV_16S, 0, 1, 3);
cv::convertScaleAbs(grad_x, grad_x);
cv::convertScaleAbs(grad_y, grad_y);
cv::addWeighted(grad_x, 0.5, grad_y, 0.5, 0, abs_grad);
double edge_mean = cv::mean(abs_grad)[0];

// 2. 金属镜面反光占比 (同轴光下金属顶部高亮)
int bright_count = cv::countNonZero(patch_gray > 165);
double bright_ratio = static_cast<double>(bright_count) / (patch_gray.rows * patch_gray.cols);

// 3. 归一化综合得分
double score = 0.55 * std::min(1.0, edge_mean / 28.0) + 0.45 * std::min(1.0, bright_ratio / 0.08);
```

**逐孔四态判定区间（互斥穷尽，无重叠）**：
- **`PRESENT` (在位)**：`std::isfinite(score) && score >= confidence_threshold`（$\ge 0.50$）；
- **`MISSING` (确诊漏装)**：`std::isfinite(score) && score < 0.35`；
- **`UNCERTAIN` (可疑异常)**：`std::isfinite(score) && (score >= 0.35 && score < confidence_threshold)`；
- **`CANNOT_INSPECT` (越界/无效)**：`!std::isfinite(score)` 或原图 Patch 越界（`score` 输出 -1.0）。

### 4.2 孔级到工件级的状态聚合映射规则 (优先级裁决)
对于单个工件的综合判定与原因码，严格按下述优先级向下裁决，消除任何混合异常冲突：
1. **`DEFECT` (优先级 1)**：若螺孔列表中存在任一孔为 `MISSING` $\implies$ 工件 `status: "DEFECT"`, `reason: "MISSING_SCREW"`（哪怕另一孔存在越界或可疑，确诊缺件直接定性为缺陷）；
2. **`CANNOT_INSPECT (ROI_OOB)` (优先级 2)**：若无 `MISSING`，但存在任一孔为 `CANNOT_INSPECT` $\implies$ 工件 `status: "CANNOT_INSPECT"`, `reason: "CANNOT_INSPECT_ROI_OOB"`；
3. **`CANNOT_INSPECT (LOW_CONFIDENCE)` (优先级 3)**：若无 `MISSING` 和 `CANNOT_INSPECT`，但存在孔为 `UNCERTAIN` $\implies$ 工件 `status: "CANNOT_INSPECT"`, `reason: "CANNOT_INSPECT_LOW_CONFIDENCE"`；
4. **`PASS` (优先级 4)**：当且仅当全部配置螺孔均为 `PRESENT` $\implies$ 工件 `status: "PASS"`, `reason: "OK"`。

### 4.3 统一负载约束与整批数量核验
1. **配方合法性硬约束 (消除漏配隐患)**：
   - 强制工件数约束：$1 \le expected\_blocks \le max\_blocks \le 4$；
   - **螺孔数量与期望等式强约束**：强制要求 $expected\_screws\_per\_block == screw\_rois.size()$，且 $1 \le screw\_rois.size() \le 32$（当前配方值域覆盖 1~32 颗螺孔）。若 $expected\_screws\_per\_block \ne screw\_rois.size()$，设备端直接拒绝保存配方并返回 `INVALID_PROFILE_SCREWS_MISMATCH`，避免期望数量与实际 ROI 数量不一致。
2. **全局连通域统计与超量保护**：
   - 算法统计画面内**全部有效独立工件数** $N_{detected}$；
   - 若 $N_{detected} < expected\_blocks$：整体判定为 `overall_status: "DEFECT"`, `overall_reason: "DEFECT_UNDER_COUNT"`（漏放工件）；
   - 若 $N_{detected} > expected\_blocks$：整体判定为 `overall_status: "DEFECT"`, `overall_reason: "DEFECT_OVER_COUNT"`（多放/杂乱）；
   - 若 $N_{detected} > max\_blocks$（如检测到 5 件）：真实记录 `summary.actual_blocks: 5`，仅对前 $max\_blocks$ 件执行精检；第 5 件及后续工件字段填充约定：
     - `status: "CANNOT_INSPECT"`, `reason: "UNINSPECTED_OVER_CAPACITY"`；
     - `orientation: "ORIENTATION_AMBIGUOUS"`；
     - `expected_screws: expected_screws_per_block`, `actual_screws: 0`；
     - `screws: []`（空数组）。
3. **整批仲裁优先级与主原因选择规则**：
   $$\mathbf{DEFECT} > \mathbf{CANNOT\_INSPECT} > \mathbf{PASS}$$
   当多个缺陷/异常原因同时成立时，`overall_reason` 按如下严格优先级裁决：
   `DEFECT_UNDER_COUNT` > `DEFECT_OVER_COUNT` > `DEFECT_MISSING_SCREW` > `CANNOT_INSPECT_ORIENTATION` > `CANNOT_INSPECT_ROI_OOB` > `CANNOT_INSPECT_LOW_CONFIDENCE`。

---

## 5. 数据追溯报文契约与 JSON Schema

### 5.1 完整 JSON Schema 定义 (Draft-07 规范)
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "ScrewGuardResultPayload",
  "type": "object",
  "required": [
    "schema_version", "device_id", "app_version", "applied_revision",
    "cycle_id", "frame_id", "timestamp", "overall_status", "overall_reason",
    "summary", "blocks"
  ],
  "properties": {
    "schema_version": { "type": "string", "enum": ["2.4"] },
    "device_id": { "type": "string" },
    "app_version": { "type": "string" },
    "applied_revision": { "type": "integer", "minimum": 1 },
    "cycle_id": { "type": "string" },
    "frame_id": { "type": "integer", "minimum": 0 },
    "timestamp": { "type": "number" },
    "overall_status": { "type": "string", "enum": ["PASS", "DEFECT", "CANNOT_INSPECT"] },
    "overall_reason": {
      "type": "string",
      "enum": [
        "OK", "DEFECT_UNDER_COUNT", "DEFECT_OVER_COUNT", "DEFECT_MISSING_SCREW",
        "CANNOT_INSPECT_ORIENTATION", "CANNOT_INSPECT_ROI_OOB", "CANNOT_INSPECT_LOW_CONFIDENCE"
      ]
    },
    "summary": {
      "type": "object",
      "required": ["expected_blocks", "actual_blocks", "passed_blocks", "defect_blocks", "cannot_inspect_blocks"],
      "properties": {
        "expected_blocks": { "type": "integer", "minimum": 1, "maximum": 4 },
        "actual_blocks": { "type": "integer", "minimum": 0 },
        "passed_blocks": { "type": "integer", "minimum": 0 },
        "defect_blocks": { "type": "integer", "minimum": 0 },
        "cannot_inspect_blocks": { "type": "integer", "minimum": 0 }
      }
    },
    "blocks": {
      "type": "array",
      "items": {
        "type": "object",
        "required": [
          "block_id", "bbox_xywh", "angle_deg", "orientation", "status",
          "reason", "expected_screws", "actual_screws", "screws"
        ],
        "properties": {
          "block_id": { "type": "integer", "minimum": 1 },
          "bbox_xywh": { "type": "array", "items": { "type": "number" }, "minItems": 4, "maxItems": 4 },
          "angle_deg": { "type": "number" },
          "orientation": { "type": "string", "enum": ["0_DEG_CONFIRMED", "180_DEG_CONFIRMED", "ORIENTATION_AMBIGUOUS"] },
          "status": { "type": "string", "enum": ["PASS", "DEFECT", "CANNOT_INSPECT"] },
          "reason": {
            "type": "string",
            "enum": [
              "OK", "MISSING_SCREW", "CANNOT_INSPECT_ORIENTATION",
              "CANNOT_INSPECT_ROI_OOB", "CANNOT_INSPECT_LOW_CONFIDENCE", "UNINSPECTED_OVER_CAPACITY"
            ]
          },
          "expected_screws": { "type": "integer", "minimum": 1, "maximum": 32 },
          "actual_screws": { "type": "integer", "minimum": 0, "maximum": 32 },
          "screws": {
            "type": "array",
            "items": {
              "type": "object",
              "required": ["id", "name", "status", "score", "roi_center_xy"],
              "properties": {
                "id": { "type": "integer", "minimum": 1 },
                "name": { "type": "string" },
                "status": { "type": "string", "enum": ["PRESENT", "MISSING", "UNCERTAIN", "CANNOT_INSPECT"] },
                "score": { "type": "number", "minimum": -1.0, "maximum": 1.0 },
                "roi_center_xy": { "type": "array", "items": { "type": "number" }, "minItems": 2, "maxItems": 2 }
              }
            }
          }
        }
      }
    }
  }
}
```

### 5.2 规范报文 Payload 示例
```json
{
  "schema_version": "2.4",
  "device_id": "recamera-sg2002-station01",
  "app_version": "1.0.0",
  "applied_revision": 1,
  "cycle_id": "cyc_1793874000_001",
  "frame_id": 2048,
  "timestamp": 1793874000.125,
  "overall_status": "DEFECT",
  "overall_reason": "DEFECT_MISSING_SCREW",
  "summary": {
    "expected_blocks": 2,
    "actual_blocks": 2,
    "passed_blocks": 1,
    "defect_blocks": 1,
    "cannot_inspect_blocks": 0
  },
  "blocks": [
    {
      "block_id": 1,
      "bbox_xywh": [120, 80, 240, 160],
      "angle_deg": 12.5,
      "orientation": "0_DEG_CONFIRMED",
      "status": "PASS",
      "reason": "OK",
      "expected_screws": 2,
      "actual_screws": 2,
      "screws": [
        {"id": 1, "name": "侧面M2", "status": "PRESENT", "score": 0.88, "roi_center_xy": [152, 120]},
        {"id": 2, "name": "关节M2", "status": "PRESENT", "score": 0.91, "roi_center_xy": [188, 110]}
      ]
    },
    {
      "block_id": 2,
      "bbox_xywh": [420, 210, 240, 160],
      "angle_deg": -5.0,
      "orientation": "180_DEG_CONFIRMED",
      "status": "DEFECT",
      "reason": "MISSING_SCREW",
      "expected_screws": 2,
      "actual_screws": 1,
      "screws": [
        {"id": 1, "name": "侧面M2", "status": "MISSING", "score": 0.12, "roi_center_xy": [452, 250]},
        {"id": 2, "name": "关节M2", "status": "PRESENT", "score": 0.86, "roi_center_xy": [488, 240]}
      ]
    }
  ]
}
```

---

## 6. SG2002 资源与性能验收设计指标

以下参数为针对目标硬件（SG2002 单核 RISC-V @ 1.0GHz, 256MB DDR）的**设计验收基准（Design Acceptance Targets）**，不是当前实机结果：

- **基础验收负载**：**4 个工件 × 每件 2 个螺孔 = 8 个 ROI**；单帧最大处理工件数上限统一为 **4 个工件**。
- **扩展压力负载**：**4 个工件 × 每件 32 个螺孔 = 128 个 ROI**，单独测量耗时与 RSS；该压力负载用于容量和退化记录，规范不承诺其达到 16.5ms 目标。
- **阶段耗时设计指标**：
  - 粗定位阶段（$1080P \to 640\times360$ 双线性降采样 + HSV 阈值 + 轮廓分析）：$\le 8.5\text{ms}$；
  - 精检阶段（4 工件朝向消歧 + 原图 8 个 $48\times48$ Patch 逆变换裁剪与 Sobel/反光评分）：$\le 8.0\text{ms}$；
  - 基础验收负载的单帧总计算耗时设计目标：$\le 16.5\text{ms}$（对应处理帧率约 $> 60\text{ fps}$）；扩展压力负载只记录实测结果。
- **常驻物理内存 (RSS)**：
  - 静态常驻物理内存设计指标 $\le 18\text{MB}$；
  - 连续 $7 \times 24$ 小时常温运行无内存增长趋势（零内存泄漏）。
- **实测方法规范**：
    在设备上通过 `perf stat` 及 `/proc/<pid>/status` 中的 `VmRSS` 采样连续 10,000 帧，分别对基础验收负载（4×2）与扩展压力负载（4×32）取 P95、P99 延迟和 RSS；只有 RISC-V/musl 真机数据可填入最终验收结论。

### 6.1 证据分层与现场验收边界

- **合成图原生单测**：验证 C++ 状态机、配置值域、四态互斥、数量仲裁和 JSON 报文；不能证明摄像头、光学或设备性能。
- **实物离线图片**：验证 Web 标定映射、RGB 图像处理和实物样本裁决；不能证明 Supervisor、MQTT、WebSocket 或持续运行。
- **模拟接口/E2E**：验证前端导入、严格 schema、配置读写状态机和超时分支；不能替代真机连接与重启。
- **RISC-V/musl 真机**：唯一用于设备启动、revision 生效、MQTT/WS、性能、RSS 和长稳结论的证据层。
