# Mac 性能待做与复测方法

本页保留原开发树 `native-port/TODO.md`、`docs/testing-methods.md` 中 2026-10-17 的重要结论，并与本工具的 [1.4.3 记录](v1.4.3-port.md) 分开。旧实验的开关、源码和二进制须按具体分支核对，不能把旧版通过或未合入的实验当作当前 1.4.3 验收。这里只安排 Mac；不代表 iPhone 已测。

## 已有实验应保留的结论

旧版 M4 / macOS 26.5.2、监视堡垒存档副本，取消显示节拍限制的渲染能力测试；不是实际上屏 FPS。每组同一二进制，8 个 30 秒平衡交替窗口。下表是每条件 4 个窗口分位数的算术均值，不是合并帧分位数；天气和时间仍自然变化。

| 旧实验 | 基线 p50 / p95（ms） | 实验 p50 / p95（ms） | 结论 |
|---|---|---|---|
| 同次绘制准备结果复用 | 11.839 / 12.624 | 11.904 / 12.661 | 未发现收益，p50 慢约 0.55% |
| 复制批处理 | 11.741 / 12.549 | 11.727 / 12.516 | 实际优化 ops/barriers/saved 均为 0，性能未覆盖 |
| 500 µs 提交预算 | 11.690 / 12.517 | 11.636 / 12.497 | 约 0.46% 差异不足以启用 |

后续 5 组提交预算的 p50 变化约 −0.68% 到 +0.28%，仍没有可靠收益；保留首段 60 / 后续 75 draw 的默认策略。全段较小预算可以让 GPU 更早开工，却增加后续空档，不能只看首次开工就称 GPU 等待已解决。

旧成本计数：编码阶段 resource-ID 查询 99.46% 为快路，约 11 ns；准备阶段快路约 90.22%，慢路约 566 ns。这些包括计量开销、只计持锁后的局部解析，不含锁等待或整个函数，不能直接乘调用数换算提帧。

旧实验树对读后写、写后读、写后写及别名做过受控 GPU 验证：普通 Device 屏障不足以覆盖 `mach_vm_remap` / 多个 no-copy buffer 包装的别名，曾需要 ResourceAlias 可见性。该正确性结论应保留；移植到本工具的具体路径仍须独立核查和正反例验证，不能用另一个树的通过记录代替。fill 的 helper 覆盖也不等于真实游戏 fill 路径已覆盖。

## 后续优先级

1. **1.4.3 成本基线。** 固定构建、存档、镜头、内部与输出尺寸、缓存和呈现方式。先分别量总 CPU 工作、关键路径、GPU 执行/空档、复制字节和资源解析成本。
2. **准备阶段慢路和失效范围。** 先证明重复工作及误失效，保留上传、别名切换、uniform 快照、GPU 写回和完成寿命。已有编码线程不自动意味着再加线程会提帧。
3. **实际复制负载。** 找到真实 buffer copy / update fallback，确认计数非零，再测依赖批处理。保留现有 uniform rename，不关闭已有优化制造命中；未知依赖、区间重叠和别名必须保守处理。
4. **长期内存。** 区分游戏堆、序号表、贴图池槽位、管线/驱动缓存、驻留集合与 GPU 完成后的实际对象。`mtl_dead_forget` 当前保守保留已销毁序号；缩表前须补齐槽位引用关系，不能重现旧的过早遗忘错误。1.4.3 的补帧配置复用只针对重复构建，其他增长仍须归因。
5. **首次编译卡顿。** 同路线分别记录冷/热缓存，收集完整帧时间。相对尖峰日志会漏掉普通慢帧；截图 FPS、固定镜头平均值、补帧数都不能证明稳定 30/60。
6. **场景、动作及真实显示器。** 复杂能力/战斗/过场、菜单传送与跨场景资源退役、长时间快速切档、物理 4K 和高刷新率分别检查。版本对比必须同机同负载，不比较不同游戏/设备的单张 FPS。

## 每轮测试的最低记录

- 记录提交、未提交 diff、新增源码、实际二进制 SHA-256、所选版本/资源哈希和环境；只有 commit 名不够。
- 使用独立 profile 和唯一共享 game-slot，用户进程优先；禁止两份游戏/GPU 探针并行。
- 每个性能候选至少 8 × 30 秒 A/B/B/A/B/A/A/B，保留全部慢帧。采样、参考解析核对、截图和重型诊断与正式测速分窗。
- 区分世界、菜单、加载和过场；记录真实单调时间、帧号、位置/视角、天气和缓存变化。没有严格固定的变量写明限制。
- 报告完整帧分布的 p50/p95/p99/最大值和超预算比例；不要平均分位数后当作合并结果，也不要将重叠 CPU/GPU 时间相加。
- 同时核对实际画面及执行错误。复制组记录 ops/barriers/saved 差值；计数为零就是未覆盖。
- 内存分别保留 RSS、footprint、进程峰值、Metal 分配、驻留对象及完成后释放；一个总数不能证明泄漏或平台期。重型诊断记录时间窗口。
- 通过、无收益、未覆盖、负对照失败和被中断的测试分别记录；不为追求分数删掉卡顿或异常。

旧版代表场景 p95≤10 ms 是阶段目标，尚不是全图承诺；1.4.3 须建立自己的基线。单项正确性修复、版本适配和内存改进也应分别交代，不能都称为 FPS 提升。

## BotW on the M4 iPad: 3D render scale (2026-10-26)

The owner's iPad session (native 1440p with Match display: a 2560×1764 canvas, frame cap 115) was GPU-bound: in the
world GPU p50 13.5 ms rising to 17.5 ms over 25,000 frames (heat), frame p50 about 18.5 ms. The same save and settings
on the development Mac (M4 Pro, 16-core GPU) take 6.7 ms, so the iPad's 10-core GPU, not port overhead, is the limit.
A per-pass GPU profile (`TOTK_PASS_GPU=3000:3`) shows no port pass among the costly ones: the largest single pass is the
game's own FXAA into the full canvas (0.7 of 5.4 ms on the Mac), the rest is the game's 3D rendering.

`TOTK_BOTW_RENDER_SCALE=<percent>` (launcher: Graphics → 3D resolution, default Balanced 75% in docked and native modes)
draws the 3D scene at that share of the display width; the game's FXAA writes it into the full canvas and the HUD and
menus keep the full resolution. Mac, same spot: GPU p50 6.87 ms at 100%, 5.07 at 75% (−26%), 4.59 at 67%; at full size
75% is hard to tell from 100%. MetalFX does not apply here (the FXAA pass upscales and anti-aliases in one step; the
MetalFX docked hook matches the shipped 1600→1920 upscale shader only). Still to measure on the iPad itself.

## BotW pond water: one more pass store per frame (2026-10-26)

The basin by Ha Dahamar's waterfall drew clear: the pond water (FS 9a391252) blends over the G-buffer that the terrain
water (FS 2b8863c5) wrote earlier in the same Metal pass, and on a tile GPU that sample read the G-buffer from before the
pass. The store-before-sample rule in `nvn_mtl.m` (att_unstored_read) now also counts an attachment a draw writes
without sampling it, so the pass is stored before the pond water. Cost, Mac with the iPad canvas: one extra store in
frames that draw pond water (the waterfall rim, Dueling Peaks Stable, the shop save), none in the three saves without
it; GPU p50 +0.37 ms at the rim (7.02 → 7.39, now including the water itself), unchanged at the stable (6.66 → 6.65).
`TOTK_ATT_STORE_PER_ATT=0` turns the new part off for A/B.

## BotW Temple of Time on the M4 iPad (2026-10-27)

The owner's iPad log (native 1440p, Match display, 3D 75%, 60 fps cap, thermal state nominal, CPU at 3.86 GHz) had
about a minute at the Temple of Time with GPU p50 21–28 ms (35–40 fps) while the submit thread stayed at ~8 ms: GPU-bound,
not throttled. Before and after, the same area ran at 9–14 ms. Draws per frame were lower in that minute (3,900–4,400)
than just before (4,600–5,200); render passes were higher (153–170 against 137–147).

The Mac (M4 Pro, 16-core GPU) does not reproduce it with the same save, mods and settings: 4.5–5.6 ms everywhere in the
temple (six camera directions, a walk through the hall and up the stairs, twelve in-game hours facing the statue). There
the frame is ~183 encoders, 129 of them under 0.02 ms; passes at the 3D resolution are ~40% of the GPU time, and a few
single-draw passes on tiny targets (20×13, 40×27, 80×55) take 0.3–0.65 ms each whatever the resolution. 3D at 50%
instead of 75% (0.44× the pixels) only saved 20% (5.3 → 4.3 ms): in this place a dynamic resolution would not hold
60 fps. The game has one (main+0x17ef9f0, scales 0.9 / 0.8), but it reacts to GPU time over 180% / 280% of a 60 Hz frame
(a 30 fps target) and the port's counter timestamps are written at command-buffer completion, so it never acts.

Differences on the iPad side at that time: memory footprint 4.0–4.4 GB against 3.3 GB on the Mac, the residency set
3,800 allocations against 1,950, 61,000 destroyed textures kept for recording copies, and a DualSense Edge paired 30 s
before (the app was in the background 64 s). `TOTK_PASS_GPU_AUTO` (default 20 ms on iPhone / iPad, `hle_nvn.c`) now logs
the per-encoder GPU profile of two frames when the GPU time stays over budget, plus one light stretch to compare with;
the next session there names the passes that grow.

### The owner's second session there (2026-10-28)

The automatic profile caught it: frames 7,800–9,300 at GPU p50 24–27 ms (about 4,200 draws a frame) right after
6,600–7,500 at 9.6–10 ms (about 5,000 draws). The 10-27 log has the same shape (5,000 draws at 9.7 ms, then 4,000 at
21–26 ms). Per pass, compared with the light profile taken just after the save loaded (frame 807, where in the world is not
logged) and with the Mac facing the statue:

| Pass (first shader, target) | iPad, light (frame 807) | iPad, slow (7620) | Mac (M4 Pro) |
|---|---|---|---|
| G-buffer `0be4e378`, 1920×1323, ~2,000 draws | 1.21 ms | 5.49 ms | 0.69 ms |
| `1b7a33a8`, 10 draws | 0.93 | 2.59 | 0.39 |
| `2b8863c5`, ~200 draws | 0.41 | 1.81 | 0.19 |
| `119f184b`, ~80 draws | 0.15 | 1.16 | 0.12 |
| FXAA `bd7ffdd4`, 2560×1764 | 0.73 | 1.32 | 0.18 |

Every pass slowed by a similar factor (3–8x against the iPad's own light frame), with nominal thermal state, the
submit thread at 3.87 GHz, 2 GB of memory still available and no change in draws that would explain it. That looks
like the GPU running at a low clock rather than one costly effect. The renderer's own notes already say the M4 GPU
drops its clock when it idles part of each frame (the keep-alive spinner, `nvn_mtl.m` keepalive_start, which is off for
BotW on the iPad), and its notes record kernels sized for 1 ms taking up to 4.35 ms in those gaps on the iPad. On the Mac, with the
spinner off, the probe below reads 85–91% of full clock at the statue and 100% with it on (game GPU 5.5 → 5.2 ms).

The GPU clock probe (`TOTK_GPU_PROBE`, on by default on the iPad; Mac `TOTK_GPU_PROBE=1`) runs about once a second,
right after the game's command buffers complete: one fixed kernel (20,000 rounds; 0.255 ms on the M4 Pro and 0.273 ms
on the M4 iPad at full clock). Every 300 frames the log says `GPU clock probe to frame N: ... the clock was about X% of
that`.

### Result and fix (third session, 2026-10-28)

The owner played at the temple until it slowed. The probe settled it:

| Stretch | GPU per frame | Probe (clock) |
|---|---|---|
| normal play | ~10 ms | 0.27–0.33 ms (83–99%) |
| slow, frames 2,700–3,300 | 26.8 ms | 1.19 ms (23%: the lowest clock, as on the title screen) |
| keep-alive spinner on, 600 frames | 10.6 ms | 91–99% |
| spinner off again, rest of the session (12,000 frames) | 10.0–10.8 ms | mostly 77–99% (65% once) |

So the iPad's GPU got stuck at its lowest clock while the game was GPU-bound and behind, and keeping it busy in the
gaps between frames for a few seconds brought it back for good. `gpu_boost` (`hle_nvn.c`, `TOTK_GPU_BOOST=0` off; on
by default where the spinner is off, i.e. BotW on iPhone / iPad) now does that by itself: when the GPU time (48-frame
mean) passes 90% of the present period, the spinner runs until the mean has stayed under 75% for 120 frames (at most
1,200 frames), then rests 600 frames. The log says `[gpu-boost] ... the keep-alive spinner is on` / `... off again`. At
60 fps and ~10 ms normal GPU time it never runs in ordinary play. On the Mac (`TOTK_GPU_KEEPALIVE=0 TOTK_GPU_BOOST=1`,
a 4K canvas to force the GPU over budget) it cycled on 1,200 / off 600 as designed.
