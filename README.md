# Microduck ScrewGuard (紧固件螺丝防漏检系统)

针对 Microduck 机器狗腿部与关节装配线开发的边缘视觉质检系统。运行于 Seeed reCamera (算能 SG2002 RISC-V SoC)，包含浏览器端现场标定工具、C++17 高性能原生检测引擎与 Supervisor 守护进程。

---

## 一、系统架构与模块分布

```text
screw-guard/
├── web/                     # 现场工人零依赖 Web 标定工具
│   ├── calib.html           # 单文件纯原生 Canvas 标定与配方导出页
│   ├── validate_schema.py   # Draft-07 JSON Schema 离线校验器
│   └── test_sample.jpg      # 关节件实物 750×882 底图与标注基准
├── native/                  # C++17 原生算法核心与离线测试
│   ├── app_config.h/.cpp    # 强类型配置解析与 64 位整数防溢出校验
│   ├── screw_inspector.h/.cpp # 粗定位、180°朝向消歧与四态互斥评分引擎
│   ├── result_payload.h/.cpp# 结构化结果报文生成
│   ├── screw_guard_cli.cpp  # 离线单帧/批处理命令行质检工具
│   └── test_screw_inspector.cpp # 8 大自动化单元测试套件
├── device/                  # reCamera SG2002 硬件守护进程
│   ├── main/main.cpp        # 基于 components/app_framework (af::App) 的服务主程序
│   ├── rootfs/etc/init.d/   # K95screw-guard 进程监控与优雅停机脚本
│   └── evidence/            # RISC-V 交叉编译日志、ELF 验签与仿真测试记录
├── build_deb.sh             # 一键交叉编译并输出 .deb 安装包脚本
├── screw-guard.json         # reCamera Supervisor 应用清单
├── SPEC.md                  # 系统工程规范与 JSON Schema 完整定义
└── DELIVERY_REPORT.md       # 交付报告与测试验证记录
```

---

## 二、使用指南

### 1. 现场工人可视化标定（Web 端）
标定工具无需安装任何环境，直接在浏览器中打开：

```bash
# 启动本地轻量预览服务（或直接双击用浏览器打开）
python3 -m http.server 8092 --directory /home/harvest/project/microduck/screw-guard/web
# 浏览器访问：http://localhost:8092/calib.html
```

- **第一步（工件框选）**：上传底图或连接设备抓拍后，在全景画布上拖拽框选工件区域（支持自由比例矩形，不锁 3:2）；
- **第二步（朝向锚点）**：点击「标定方向锚点」，在标准画布上框选单侧台阶或缺口等非对称特征，用于 180° 朝向消除歧义；
- **第三步（标记螺孔）**：点击「标记螺孔」，在标准画布上点击需检测的沉孔中心（支持 1~32 颗自由添加）；
- **第四步（实物试检与导出）**：点击「实物试检」即时查看各孔判定得分；点击「导出配方」下载标准化 JSON 配方。

### 2. 本地算法构建与单测（PC 开发端）
```bash
cd /home/harvest/project/microduck/screw-guard/native
make clean && make -j4
./build/test_screw_inspector
```

### 3. reCamera 安装包构建与部署（设备端）
```bash
# 1. 运行 Docker 交叉编译并打包为 Debian 安装包
/home/harvest/project/microduck/screw-guard/build_deb.sh 1.0.0

# 产物路径：/home/harvest/project/recamera/packages/screw-guard_1.0.0_riscv64.deb (3.45 MB)

# 2. 安装至已连接的 reCamera 设备
fleet push recamera-01 /home/harvest/project/recamera/packages/screw-guard_1.0.0_riscv64.deb /tmp/
fleet exec recamera-01 -- dpkg -i /tmp/screw-guard_1.0.0_riscv64.deb
```

---

## 三、硬件与工业打光选型建议

螺栓缺失检测依靠“沉孔内阴影”与“螺钉表面金属反光”的梯度差与高光比。2D 工业视觉在现场安装时建议遵守以下打光与安装准则：

1. **光源选型**：
   - **推荐方案**：**低角度环形光（Low Angle Ring Light）** 或 **条形双向对打光**；
   - **光学原理**：低角度光照射在橙色工件表面时产生漫反射，进入沉孔内部时被孔壁遮挡形成深黑色阴影；而如果孔内打有金属螺钉，螺栓顶部凸起十字槽会产生强烈的局部镜面高光反射，形成极高的边缘梯度对比。
   - **选配偏振片**：在镜头和光源前加装正交偏振镜片（Polarizer），消除工件塑料外壳的光滑反光，仅保留金属螺钉的消偏振反射。
2. **镜头与相机架设**：
   - 相机光轴与装配工作面保持 **90° 正交垂直**，物距保持在 150mm~300mm，确保沉孔无明显透视斜切角；
   - 单个螺孔在 1080p 画面中的成像直径建议 $\ge 24\text{ px}$。

---

## 四、数据接口与报文规范

- **WebSocket 实时调试流**：端口 `8001`，路径 `/results`
- **MQTT 主题**：`recamera/screw-guard/results`
- **输出 JSON 格式契约**（Draft-07 规范）：
```json
{
  "schema_version": "2.4",
  "device_id": "recamera-sg2002-station01",
  "applied_revision": 2,
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
      "status": "PASS",
      "expected_screws": 4,
      "actual_screws": 4,
      "screws": [
        { "id": 1, "name": "M2_1", "score": 0.91, "status": "PRESENT" }
      ]
    },
    {
      "block_id": 2,
      "status": "DEFECT",
      "expected_screws": 4,
      "actual_screws": 3,
      "screws": [
        { "id": 4, "name": "M2_4", "score": 0.11, "status": "MISSING" }
      ]
    }
  ]
}
```

---

## 五、待办事项 (Pending Tasks)

当前软件架构、算法、前端及构建流水线已全部闭环，后续真机与现场联调待办事项如下：

- [ ] **真机部署与运行时实测**：
  - [ ] 连接实际 reCamera 硬件（通过 `fleet` 或局域网 SSH），执行 `dpkg -i screw-guard_1.0.0_riscv64.deb`；
  - [ ] 验证 Supervisor 进程拉起、PID 跟踪与内存占用（`VmRSS <= 18MB`）；
  - [ ] 验证 8001 端口 WebSocket 调试流与 MQTT 报文在真实网络下的连通性。
- [ ] **物理工位打光与置信度微调**：
  - [ ] 在安装低角度环形光后，拍摄真实产线装配状态底图；
  - [ ] 在 `calib.html` 导入实拍图试检，微调 `confidence_threshold`（默认 0.60）以适应车间环境光波动。
- [ ] **装配工位 PLC / 信号机触发联动**：
  - [ ] 根据产线流水节奏，配置 `af::HttpTrigger` 同步触发或 GPIO 信号脚，实现工件到位单帧瞬时拍检。
