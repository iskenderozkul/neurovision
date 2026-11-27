# NeuroVision — Hedef ve Durum Kaydı (Goal & Status)

> **Çalışma devam ediyor / Work in progress. Kararlı sürüm yok / no stable release.**  
> Bu dosya **hedef + ne denendi + ne yapıldı + proje ne alemde + TODO** kaydıdır. Bilimsel dürüstlük: ölçülmemiş performans iddiası yok; sentetik test verisi gerçek kamera verisi gibi sunulmaz.

---

## 1. Bizim amacımız / Goal

**Telefon kamerası + hareket → gerçek zamanlı, metrik, görüntü istemeyen 3D ortam modeli.**

Türkçe:
- Telefon hareket ettirilince (6DoF) çevre **kameradan** modellenecek; ARCore/IMU ile metrik poz; ARCore Depth API + MVS fallback.
- **Event View** yazılımsal DVS üretecek: kamera luma’sı → 320×240 grid → log-parlaklık `L=log(1+I)` → per-pixel reference → `+`/`−` polarity eşik `C`; kamera FPS’inin üstünde veri iddiası yok.
- Hareketle açığa çıkan **bütün kenar/contour farkları** + **ölçülen derinlik** + poz → **anchor’lı nokta bulutu** (`GL_POINTS`) veya mesh; **görüntü gerekmez**.
- **Kamerayı kasmamak için** event/3D modunda kamera akışı ekrana yansıtılmaz; sadece **bulut**; gerekirse çözünürlük düşürülür; performans; tek koordinatör; işçi thread’ler; kota yedeği.
- **Kaldırılan gereksiz UI kalabalığı** geri yüklenmez; **LOG** geri eklendi; ölçümler **session log**’a yazılır API geliştirme için.
- **En güçlü model orchestrator**, iyi modeller agent, kota bitince **yedek en iyi**, `opencode.json`, `rebuild.ps1`, `dist/NeuroVision-latest.apk`, **push GitHub**.
- **Bilimsel dürüstlük**: sahte veri yok; görülmeyen yüzey uydurulmaz; ölçek ARCore metrik + MVS sınırlı; semantik model/Gaussian Splatting/fiziksel event camera yok; **kararlı sürüm değil**.

English goal: **phone motion → real-time metric 3D environment model**; software event view (+/- log-luma); contour/edge differences + measured depth + pose → anchored point cloud/mesh; no camera stream in cloud mode; optimize; no invented data; log for API; push APK.

---

## 2. Ne denendi / What was attempted

- Referans `3Drecon` → `NeuroVision` (`com.neurovision.app`, v0.1.0); baseline `assembleDebug`; 5 ekran; ARCore + Tango 3DR TSDF/Marching Cubes + MVS; OBJ/PLY.
- **Event engine** yazıldı + NaN/inf + test. **Motion engine** pyramidal LK + Shi-Tomasi yazıldı; **LK bug bulundu+düzeltildi**; `motion_left` testi kendi test bug’ıydı; **6/6**.
- **Depth**: ARCore Depth + confidence + smoothed + MVS; `depthValidShare`; **getStats `IntArray(8)`**.
- **3D World**: orbit; mode 4/5; **event cloud mode 6**.
- **Performance logları incelendi**: `motion_ms`, `ev_ms`, `ms`, `ex`, `upd`, `fill`, `gate`, `smooth`, `depth_valid`, `phase`, `kf`, `tris`, `blocks`. `motion_ms≈50–120`, `ms≈2906`, `ex≈60` görüldü.
- **Clutter kaldırıldı**, **LOG** tekrar eklendi; manuel anchor kaldırıldı; motion toggle kaldırıldı; numeric log-only; LK 3. kare; cap; 1s snapshot. **Gerçek telefon performansı tekrar ölçülmedi.**
- **GitHub push** + README TR/EN + **Dr. İskender ÖZKUL**.
- Verilen video/log kanıtı kullanıldı; **video içeriği görsel olarak incelenmedi** (kanıt olarak yalnızca log).

---

## 3. Ne yapıldı / What was done

**Native (`app/src/main/cpp`)**
- `event/event_engine.{h,cpp}` software DVS: 320×240, `L=log(1+I)`, per-pixel ref, `C`, +/-, accumulator decay, preview ARGB, density, rate/density/posShare/meanMagnitude/processMs, thread-safe, NaN/inf. **At most one event/pixel/frame; ≤ camera FPS.**
- `motion/motion_engine.{h,cpp}` 4-level LK `kMaxFeatures=96`, Shi-Tomasi grid, median dx/dy, px/s, direction, scene-change, trackedShare, reset. **image-space, not metric.** Every 3rd frame in engine. `motion_right/left/static`.
- `core/engine.{h,cpp}` event+motion independent of TSDF; **`buildEventCloud` + `eventCloudMesh`**; `native render mode 6`.
- **Event cloud:** event-active `lastEvents` +/-1 + valid `depthMm`, `confidence>=128`, `Kdepth`, `depthTransform`, ARCore pose; CV→ARCore; world-space; **no invented surface; raw measured; cap 4096; approximate event→depth alignment.** `orbitView` uses event cloud if mesh empty.
- `native_bridge.cpp` JNI; mode 6→4; `getStats` 8; `neuroStats`; event preview; threshold/decay; `resetNeuro`; `orbitView`; `meshBounds`; `runPerceptionSelfTest`. `perception_selftest.{h,cpp}` 6/6.
- `gl/renderer.cpp/h` GLES3; mode 0-5; **mode 4 GL_POINTS + vColor no lighting**; mode 5 mesh; orbit `uCameraPos`; no camera texture world; `glDepthMask`; null checks. **mode 6 in native maps to mode 4.**
- `event` + `motion` in CMake; JNI.
- ARCore/Tango/TSDF/MVS/exporter etc. inherited; honest fallback. Tango 3DR loads on device test; **not ARCore on x86_64**.

**Kotlin/Compose**
- `com.neurovision`; `ReconScreen`; `ReconSurfaceView`; `ReconGlRenderer`; `NativeRecon`; `ReconTypes`.
- `NeuroScreen` LIVE/EVENTS/DEPTH/WORLD/DIAG.
- `EventPanel`, `DepthPanel`, `DiagnosticsPanel`, `WorldOverlay`, `ReconControlBar`.
- Event sliders; depth; 3D orbit; `nativeOrbitView`; `worldView` `cameraTexture=0`; **no camera stream in 3D world; only cloud.** `NativeRecon.render(...,6,...)`.
- **Clutter removed**; **numeric log-only**; **manual anchor + anchor removal removed**; `worldPoints` toggle removed; `MotionTracksOverlay`, `ReconHud`, `LivePerceptionBar`, `AnchorListSheet` not shown.
- **`LOG` re-added**: `onShareLog`, `scanLog.currentFile`, `ScanLog.shareSessionLog`. Session `scan_*.txt` still written; **all metrics retained for API.** `DiagnosticsPanel` no numeric UI.
- `ReconHud` not shown. `onAnchorFailure` no UI. `addAnchor/removeAnchor` not UI.
- `Rotation`, event/depth preview 3:4; `trackToNdc` retained for API though overlay not shown. 1s snapshots.
- `opencode.json`; `tools/rebuild.ps1`; `selftest_main.cpp`; `motion_debug.cpp`; `perception_selftest`.
- `README.md` TR/EN; `docs/`; `TODO.md`; `GOAL_AND_STATUS.md`.
- `dist/NeuroVision-latest.apk`; pushed **GitHub** `https://github.com/iskenderozkul/neurovision` `main`.
- `android:icon` fixed. `com.neurovision.app`, version 0.1.0, API 26+, `arm64-v8a`+`armeabi-v7a`, Tango `.so`.
- **Author: Dr. İskender ÖZKUL.**

---

## 4. Proje ne alemde / Current state

- **Build:** `assembleDebug` **BUILD SUCCESSFUL**; `rebuild.ps1`.
- **Native self-test:** `perception_selftest=pass checks=6/6` (event static/motion/threshold + motion right/left/static). Synthetic **test input**, not real camera. `mesh_selftest` 6/6.
- **GitHub:** `main`; README; APK; work in progress; no stable release. **No force push.**
- **Event cloud:** implemented, **not fully validated on real phone**; alignment approximate; cloud rendering not verified in attached video (not inspected as image). 
- **Performance:** reduced **measured** costs in code; **not proven kasmasız** on HONOR REA-NX9; Tango `ms`/`ex` can still dominate; log retained. 
- **Real ARCore device required.** x86_64 emulator no crash + Tango loads + **ARCore not functional**. no mock/fabricated data. no semantic labels. no Gaussian Splatting. no persistent reload. no guaranteed accuracy. no absolute scale. no moving-person reconstruction. no events above camera FPS. no new info above sensor.
- `docs/04` protocol; `GOAL_AND_STATUS.md`.
- **`getStats` `IntArray(8)`.** `mode6`. `eventCloud`. `LOG`. 
- **Amaca çok yakın ama henüz kararlı + telefonla ölçülmüş hızlı sonuç değil.**

---

## 5. TODO listesi / TODO (start now)

### P0 — Hız + ölçüm
- [x] `getStats` 8; motion 3rd; cap; 1s; noise removal; **mode 6 no camera**; `LOG`.
- [x] **event cloud** event+measured depth+pose+anchor. 
- [x] Build + selftest 6/6 + README + push. 
- [ ] **Gerçek telefon ölçümü** (HONOR/ARCore): stutter `ms`, `ex`, `upd`, `ev_ms`, `motion_ms`, `depth_valid`, `fill`, `gate`, `smooth`, `phase`; öncesi/sonrası. 
- [ ] **Event cloud kalite** testi: hareketli sahne; +/- contour; çözünürlük; `event→depth` hizalama; `orbitView`; boşken boş. 
- [ ] **Log API şeması** (scan line fields) stabilize; `ev_*`, `flow_*`, `motion_ms`, `phase`, `src`, `depth_valid`, `kf`, `tris`, `blocks`, `ex`, `upd`, `fill`, `gate`, `smooth`, `dup`, `wskip`, `rel`, `ang`, `ms`. 
- [ ] Heavy Tango `ms`/`ex` throttle **kalite kaybı olmadan**; native worker. 
- [ ] **Çözünürlük düşürme** ile FPS tradeoff; kanıtla. 
- [ ] No-camera cloud mode **resource** test. 

### P1 — Özellik tamamlama (not claiming done)
- [ ] Aşama 4 SLAM relokalizasyon + pose continuity **kalıcılık**. 
- [ ] **Camera trajectory** 3D draw (README iddiası yok). 
- [ ] **Aşama 6** geometric surface clusters; **TFLite interface**; model yoksa `model yok`. 
- [ ] **Aşama 7** session/map reload + Gaussian Splatting notu; persistent map. 
- [ ] `destroy`/GL UAF; partial depth clear; `render` arg checks; `!=JNI_FALSE`; `jclass`; `exportMesh` ANR. 
- [ ] `DiagnosticsPanel` log path. 
- [ ] `worldPoints` API unused cleanup. 

### P2 — Kalite
- [ ] Tüm senaryolar `docs/04`. 
- [ ] Konum `depth` API test. 
- [ ] `README` sync. 
- [ ] `versionCode/name`. 
- [ ] Kararlı sürüm **sadece ölçümler + hata temizliği** sonrası. 

---

## 6. Honest non-claims

No fabricated data; no semantic; no physical event camera; no event rate > camera FPS; no absolute metric; no ARCore-not-provided; no stable release. **Event cloud is measured + approximate; not proof of contour perfection.**
