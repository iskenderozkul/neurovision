# NeuroVision

> ⚠️ **Çalışma devam ediyor / Work in progress.**
> **Kararlı sürüm yok / No stable release.** Bu bir geliştirme sürümüdür; hata, eksik özellik ve geri dönüşümlü değişiklikler olabilir. Bu sürüm production/ürün kullanımı için önerilmez. This is a development build; bugs, missing features and breaking changes are expected. It is not recommended for production use.

---

## ⬇ En güncel indirilebilir APK / Latest downloadable APK

**En son sürüm:** **NeuroVision 0.1.0** (`versionCode 1`, debug APK, `com.neurovision.app`).  
**Latest version:** **NeuroVision 0.1.0** (`versionCode 1`, debug APK, `com.neurovision.app`).

- **İndir / Download:** [https://github.com/iskenderozkul/neurovision/raw/main/dist/NeuroVision-latest.apk](https://github.com/iskenderozkul/neurovision/raw/main/dist/NeuroVision-latest.apk)
- **Dosya / File:** `dist/NeuroVision-latest.apk` (+ zaman damgalı kopyalar / timestamped copies under `dist/`)
- **Gereksinim / Requirements:** Android 8.0 (API 26)+, OpenGL ES 3.0, **Google Play Services for AR (ARCore)**, kamera izni / camera permission. ARCore x86_64 emülatörde desteklenmez / not supported on x86_64 emulators. **ARCore destekli gerçek cihaz gerekir / a real ARCore-capable device is required.**

Kurulum / Installation: `adb install -r dist/NeuroVision-latest.apk`. İmza anahtarı debug; üretim sürümü değildir / debug signing; not a production release.

---

## Kapsam (Türkçe)

**NeuroVision**, Android telefon kamerasından çalışan, bilimsel olarak dürüst, salt-ölçüm tabanlı bir görsel algı ve 3D dünya-modelleme uygulamasıdır. Referans taban olarak `3Drecon` (ARCore + Google Tango 3D Reconstruction) kullanılır; uygulama `com.neurovision.app`, sürüm `0.1.0`, paket `com.neurovision`. 

Amaç: canlı kamera görüntüsünü, **sahte veri eklemeden**, ölçülebilir biçimde olay-haritasına, hareket tahminine, derinlik + güven haritasına, Visual-Inertial SLAM pozuna ve artımlı 3D haritaya dönüştürmek. **Mock kamera verisi gerçek veri gibi sunulmaz.** 

### Uygulama içeriği

- **Canlı kamera (Live):** ARCore `Session`, 6DoF metrik poz, kamera iç parametreleri, YUV_420_888 kare; sağlıklı görüntü; takip kaybı dürüstçe gösterilir. **Sayısal ayrıntı kaldırıldı; ölçümler oturum loguna yazılır.**
- **Event View (Yazılımsal DVS):** kamera **luma**'sından 320×240 grid; log-parlaklık `L=log(1+I)`; piksel başına per-reference eşik `C`; **+/-** polarity; eşik/sönüm (decay) kontrolü; event rate (ev/s), yoğunluk, ortalama `|ΔL|`, pozitif/negatif sayı; event maske preview. **Fiziksel event kamerası iddiası yok; kamera FPS'inin üstünde yeni bilgi üretme iddiası yok.**
- **Motion / Hareket:** 4 seviyeli piramit **Lucas-Kanade**, grid tabanlı **Shi-Tomasi/Harris** köşe tespiti (`kMaxFeatures=96`), takip oranı, **medyan dx/dy**, `px/s` hız, baskın yön (sağ/sol/yukarı/aşağı), scene-change; **görüntü-uzayı piksel akışı metrik kamera hareketi DEĞİLDİR**; metrik 6DoF ayrıca ARCore pozundan alınır.
- **Depth View:** **ARCore Depth API** (raw + confidence + smoothed) öncelikli; destek yoksa **cihaz içi MVS** (SGM + census) fallback; renkli heatmap, güven haritası, geçerli piksel oranı, kaynak (`ARCore Depth` / `MVS Depth` / no depth), gecikme. **Görülmeyen yüzeyler uydurulmaz.**
- **Visual-Inertial SLAM:** ARCore + IMU; takip durumu, relokalizasyon, pose sürekliliği metrik 6DoF. 
- **3D World:** **Tango 3D Reconstruction** TSDF + Marching Cubes renkli mesh; **ölçülen event cloud** (event-aktif + geçerli ölçülen derinlik, dünya-uzayı nokta bulutu; görüntü gerekmez; event→depth hizalaması yaklaşık; uydurulmuş yüzey yok); nokta bulutu ↔ mesh; serbest yörünge; fallback **TSDF + MVS**. **Manuel anchor / anchor-kaldırma düğmesi kaldırıldı; event anchor otomatik.** 
- **Export:** OBJ / PLY, ASCII PLY; renkli köşe/normaller; session log; OBJ/PLY dosyası; oturum içi harita; uzun süreli kalıcı harita + yeniden açma henüz kararlı değil.
- **Diagnostics:** ölçülen FPS, kare süresi, worker `processMs`, event `ev_ms`, `motion_ms`, derinlik kaynağı, geçerli derinlik %, triangle/vertex/keyframe/block, SLAM durumu, relokalizasyon, bellek; session log; native perception self-test; mesh self-test. **Sayılar canlı ölçüm; uydurulmaz.**
- **İstenmeyen/uygulanmayan iddialar:** fiziksel event camera; kamera FPS'ini aşan event; mutlak ölçek garantisi; MVS'te garantili metrik doğruluk; **eğitilmemiş TFLite modeli varmış gibi semantik etiket**; nesne tespiti/model yoksa "model yok"; **Gaussian Splatting** bu sürümde yok; dinamik insan/araç 3D yeniden kurulması yok; ARCore'un döndürmediği yüzeyler tamamlanmış gibi gösterilmez.

### Dürüstlük kuralları

Mock/veri sentetik test **yalnızca** `runPerceptionSelfTest` / `runMeshSelfTest` içinde açık test girdisi olarak kullanılır; gerçek kamera verisi gibi sunulmaz. Gerçek ölçüm olmayan FPS/gecikme iddiası yok. Event yoğunluğu kamera FPS'ini aşamaz. Ölçek **ARCore metrik takibi** ile sağlanır; **MVS fallback'inde ölçek sınırlıdır**. Derinlik API yoksa fallback, geometri yoksa boş; **sahte veri yok.**

---

## Scope (English)

**NeuroVision** is a scientifically honest, measurement-only Android app for visual perception and incremental 3D world modelling from the phone camera. Reference base: `3Drecon` (ARCore + Google Tango 3D Reconstruction); app id `com.neurovision.app`, version `0.1.0`, package `com.neurovision`.

Goal: convert live camera input, **without injecting synthetic data**, into a measurable event map, motion estimate, metric depth + confidence, Visual-Inertial SLAM pose, and an incremental 3D map. **No mock camera data is presented as real.**

### What it includes

- **Live Camera:** ARCore `Session`, 6DoF metric pose, intrinsics, YUV_420_888; tracking loss shown honestly; **motion vectors** + direction; event summary.
- **Event View (software DVS):** 320×240 grid from camera **luma**; log intensity `L=log(1+I)`; per-pixel reference threshold `C`; **+/-** polarity; threshold/decay controls; event rate (ev/s), density, mean `|ΔL|`, positive/negative counts; event preview. **No physical event camera; no claim to produce information faster than the camera FPS.**
- **Motion:** 4-level pyramidal **Lucas-Kanade**, grid-based **Shi-Tomasi/Harris** corners (`kMaxFeatures=96`), tracking share, **median dx/dy**, `px/s`, dominant direction; **image-space pixel flow is not metric camera motion**; metric 6DoF comes separately from ARCore pose.
- **Depth View:** **ARCore Depth API** first; **on-device MVS** fallback; heatmap, confidence, valid-pixel share, source, latency. **Unobserved surfaces are not invented.**
- **Visual-Inertial SLAM:** ARCore + IMU; tracking, relocalisation, pose continuity; metric 6DoF.
- **3D World:** Tango 3D Reconstruction **TSDF + Marching Cubes** coloured mesh; **measured event cloud** (event-active + valid measured depth, world-space points; no image required; approximate event→depth alignment; no invented surface); point cloud ↔ mesh; free orbit; **TSDF + MVS** fallback. **Manual anchor / remove-anchor buttons removed; event anchors are automatic.**
- **Export:** OBJ / PLY, ASCII PLY, colours + normals; session log; OBJ/PLY files; session-scoped map; stable persistent mapping + reload not yet.
- **Diagnostics:** measured FPS, frame time, worker `processMs`, event `ev_ms`, `motion_ms`, depth source, valid-depth %, triangles/vertices/keyframes/blocks, SLAM state, relocalisation, memory; session log; native perception self-test; mesh self-test. **No invented numbers.**
- **Not claimed / not implemented:** physical event camera; event rate beyond camera FPS; absolute-scale guarantee; guaranteed metric accuracy on MVS; **semantic labels with an untrained model**; object detection without a model ("model yok"); **Gaussian Splatting**; reconstruction of moving people/vehicles; unseen geometry; ARCore-not-provided data.

### Honesty rules

Synthetic data is used **only** inside `runPerceptionSelfTest` / `runMeshSelfTest` as clearly-labelled test input, never as real camera data. No unmeasured FPS/latency claims. Event density cannot exceed camera FPS. Scale comes from **ARCore metric tracking**; **MVS fallback scale is limited**. If depth is unavailable, fall back; if no geometry, empty; **no fabrication.**

---

## Build / Requirements

- Android 8.0 (API 26)+, OpenGL ES 3.0
- **Google Play Services for AR (ARCore)** — required; no fabricated pose/depth; **not supported on x86_64 emulators**
- ARCore Depth API optional; MVS fallback
- Android SDK 35, NDK `27.0.12077973`, CMake `3.22.1`, JDK 17
- ABIs `arm64-v8a` + `armeabi-v7a` (Tango prebuilt `.so`, Apache-2.0)

```powershell
$env:JAVA_HOME="C:\Program Files\Android\Android Studio\jbr"
.\gradlew.bat :app:assembleDebug --console=plain
.\tools\rebuild.ps1            # build + native perception self-test + dist refresh
.\tools\rebuild.ps1 -SkipSelfTest
```

APK: `app\build\outputs\apk\debug\app-debug.apk` / `dist\NeuroVision-latest.apk`.  
Logs/exports: `Android/data/com.neurovision.app/files/`. 

**Native verification:** `perception_selftest=pass checks=6/6` (event static/motion/threshold + motion right/left/static); `assembleDebug` **BUILD SUCCESSFUL**. 

**Performance / clutter:** no numeric clutter on screen; triangle/vertex/keyframe/block, event/motion `STATIC 0.0 px/s`, etc. are **logged only**; manual anchor + anchor-removal buttons removed; sparse LK every 3rd frame; event cloud capped; 1s UI snapshots; lighter event/depth rendering. Emulator install/launch has no crash, Tango 3DR loads, **ARCore not functional on x86_64 emulator**; use a real device. **Work in progress; no stable release.**

---

## Project layout

```
NeuroVision/
  app/src/main/java/com/neurovision/...   # Android + Compose + ARCore + JNI caller
  app/src/main/cpp/...                     # native C++ (ARCore/TSDF/MVS/event/motion/render/export)
    event/event_engine.{h,cpp}             # software DVS
    motion/motion_engine.{h,cpp}           # pyramidal Lucas-Kanade
    core/engine.{h,cpp}                    # reconstruction + perception orchestration
    tango/, tsdf/, mesh/, mvs/, gl/, io/, jni/
  docs/01_fizibilite.md, 02_mimari.md, 03_yol_haritasi.md, 04_test_protokolu.md
  TODO.md
  dist/NeuroVision-latest.apk
  tools/rebuild.ps1, tools/selftest_main.cpp, tools/motion_debug.cpp
  opencode.json
```

---

## Yazar / Author

**Dr. İskender ÖZKUL**

---

## Honest limitations (short)

Depth depends on device; MVS accuracy limited; no absolute scale guarantee; no trained semantic model; no physical event camera; no claims above camera FPS; session-scoped in-memory map; exports in session ARCore frame; stable release not yet. Real device + ARCore required. Work continues.
