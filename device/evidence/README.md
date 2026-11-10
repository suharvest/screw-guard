# Device build evidence

No device was contacted. No package, release, install, container start/stop, or deployment was performed.

Sources are copied into `/home/harvest/recamera-dev/_scratch/screw-guard-native-5/{device,native}`; the existing container mounts `/home/harvest/recamera-dev` at `/workspace`. `screw-guard.json` is copied into the snapshot root. The local SSCMA `components` and `cmake` directories are copied into `/home/harvest/recamera-dev/_scratch/screw-guard-sdk`. `app-source-sha256.txt` and `sdk-source-sha256.txt` record byte equality between local source and these snapshots. The upstream checkout is unchanged.

Reproduce after copying these sources:

```sh
docker exec recamera-build bash -c 'export PATH=/workspace/host-tools/gcc/riscv64-linux-musl-x86_64/bin:$PATH; cmake -S /workspace/_scratch/screw-guard-native-5/device -B /workspace/_scratch/screw-guard-build-v2 -DSSCMA_ROOT=/workspace/_scratch/screw-guard-sdk -DSG200X_SDK_PATH=/workspace/sg2002_recamera_emmc -DCMAKE_BUILD_TYPE=Release && cmake --build /workspace/_scratch/screw-guard-build-v2 -j4'
python3 device/evidence/test_init.py
```

`build.log` records the first failed build: an old cache selected host `/usr/bin/cc` and rejected RISC-V flags. `build-v2.log` records the clean cross-build and its missing `nlohmann/json.hpp` include failure. The final CMake maps the SDK single JSON 3.12.0 header into the build's `include/nlohmann` path. Native's bundled host-only JSON is 3.11.3; it is not mixed into the device build. `build-final.log` contains the subsequent full build output. Existing SDK warnings are retained.

The init harness substitutes an isolated executable/config/PID/log directory. It checks missing config, repeated start, missing PID file while the executable remains alive, PID identity mismatch, graceful TERM, and TERM timeout. Timeout must return nonzero and preserve PID state. It does not simulate camera drivers or claim physical shutdown acceptance.

Runtime capture is 1920x1080 RGB888 at 30 fps, converted to BGR for the native inspector. H264 is 1920x1080 at 15 fps; inspection coordinates, snapshots, and video therefore use the same dimensions. Processing throughput is not measured by these settings. RGB/BGR buffers, the SDK capture queue, and native intermediate buffers require device RSS measurement; no 18 MB memory or latency target is claimed.

Pending physical acceptance: boot/service supervision; valid/invalid sidecar behavior on firmware; camera color/stride and orientation; RTSP/H264 and `/results` delivery; snapshot capture; MQTT reconnect/delivery; actual per-frame timestamp and recipe revision; inference accuracy/FPS/RSS; SIGTERM hardware cleanup and restart without VPSS leakage. The init script waits at most 14 seconds and reports failure if the process remains alive; SDK/driver cleanup is not claimed to complete within that interval without device observation.

`build-verified.log` is the final successful reconfiguration/build after the runtime install declarations. `binary-verification.txt` retains ELF header, dynamic dependencies and SHA256. `runtime-libraries.txt` lists the four declared private runtime files with hashes and destinations. No installation was executed. Other ELF dependencies still require firmware compatibility verification.

The final artifact is from `build-reviewed.log` (exit 0), after syncing the native source-coordinate geometry fix and the independently reviewed shutdown/MQTT corrections. VENC producer channels are stopped individually before debug/RTSP consumers are destroyed, because SDK stop-all can return early after a channel driver error. MQTT uses `publishText` so it sends exactly the same JSON as `/results`; `publishResultsJson` would inject an extra device object. `binary-verification.txt` and `app-source-sha256.txt` refer to this final artifact.
