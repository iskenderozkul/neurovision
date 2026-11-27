# Test Protokolü — NeuroVision 0.1.0

Bu belge, `NeuroVision` Android uygulamasının (applicationId `com.neurovision.app`,
versionName `0.1.0`, versionCode `1`, minSdk `26` / Android 8.0) fiziksel bir ARCore
cihazda nasıl kurulup, nasıl test edileceğini ve sonuçların nasıl raporlanacağını tarif
eder. Tüm sayılar **ölçülmüş** değerlerdir; hiçbir senaryoda tahmini veya uydurma
metrik yazılmaz. Ölçülemeyen bir değer `UNKNOWN` olarak işaretlenir; `PASS` sayılmaz.

> **Bilimsel dürüstlük (kısa):** Bu protokolde mock kamera verisi, sahte FPS, sahte metrik
> ölçek, eğitilmiş model varmış gibi semantik etiket veya fiziksel event camera iddiası
> kullanılmaz. Sentetik self-test sonuçları her zaman "sentetik/deterministik" olarak
> etiketlenir ve canlı tarama performansı gibi sunulmaz.

---

## 1. Kurulum ve ortam

### 1.1 Gereksinimler

- Android 8.0 (API 26) veya daha yeni; OpenGL ES 3.0.
- **Google Play Services for AR (ARCore)** yüklü ve cihaz tarafından desteklenen.
  ARCore manifest'te zorunludur; desteklenmeyen cihazda uygulama tarama yapamaz.
- Kamera izni (`android.permission.CAMERA`).
- USB hata ayıklama (adb) etkin test cihazı; fiziksel cihaz zorunlu.
- **ARCore emülatörde desteklenmez.** Testler fiziksel ARCore cihazda yapılır.
  Emülatörde yalnızca sentetik self-test (x86_64 dev derlemesi) çalıştırılabilir;
  bu sonuçlar canlı tarama performansını temsil etmez.

### 1.2 APK kurulumu

Depo kökünden:

```powershell
adb devices
adb install -r dist\NeuroVision-latest.apk
```

Alternatif tam yol:

```powershell
adb install -r "G:\neurovision\NeuroVision\dist\NeuroVision-latest.apk"
```

`dist\NeuroVision-latest.apk` her zaman repodaki en güncel derlemeyi verir.
Kurulumdan sonra uygulama ilk açılışta kamera izni ister ve gerekirse ARCore
Play Services'ı kurar/günceller; durum oturum loguna `stage=arcore_*` olarak yazılır.

### 1.3 Her test öncesi hazırlık

1. Uygulamayı aç; kamera iznini ver.
2. ARCore kurulumu tamamlanana kadar bekle (`stage=arcore_installed`).
3. `Reset` ile hacmi temizle.
4. Cihaz/model/Android sürümü/ABI/uygulama sürümü/derinlik kaynağı bilgisini rapora yaz.
5. Oturum logunun aktif olduğunu doğrula (`scan_<timestamp>.txt`).
6. Her senaryo aynı cihazda, aynı sürümle, oturum logu açıkken yapılır.

### 1.4 Metrikler ve kaynakları

| Metrik | Birim | Kaynak | Not |
|---|---|---|---|
| Kamera / render FPS | FPS | `ReconMetrics.fps`, ARCore | İşlem FPS'i ile karıştırılmaz |
| Worker işlem süresi | ms/kare | `processMs`, `prep=`, `fuse=`, `feed=`, `dynms=`, `upd=`, `pts=`, `cells=`, `ex=` | Darboğazı adlandırır |
| Event rate / yoğunluk / pozitif-negatif | event/s, 0–1, adet | `NeuroMetrics` (eventRate, eventDensity, posCount, negCount) | Eşik ve sönümle birlikte raporlanır |
| Event maliyeti | ms/kare | `eventProcessMs` (`ev_ms`) | GL thread bütçesi |
| Akış dx / dy / px/s / yön | px, px/s, etiket | `NeuroMetrics` (flowDx, flowDy, flowSpeedPxPerS, motionDirection) | 0 statik, 1 sağ, 2 sol, 3 aşağı, 4 yukarı (görüntü uzayı) |
| Takip oranı | 0–1 | `trackedShare`, ARCore `trackingState` | Düşük ışıkta düşüş dürüstçe raporlanır |
| Gerçek hız | m/s, °/s | ARCore 6DoF poz | Metrik hız kaynağı |
| Derinlik kaynağı | 0 / 1 / 2 | `depthSource` (0 yok, 1 ARCore, 2 MVS) | HUD etiketiyle aynı olmalı |
| Geçerli derinlik oranı | 0–1 | `depthValidShare`, `gate`, `cthr` | Düşük oran sensör kısıtını gösterir |
| Mesh üçgen / vertex | adet | `triangles`, `vertices` | Zaman içinde büyüme izlenir |
| Blok / keyframe | adet | `blocks`, `keyframes` | Bellek ve kapsama göstergesi |
| İşlenmiş kare | adet | `framesProcessed` | Donma tespiti için |
| Faz | 0–6 | `phase` | 1 hazırlık, 2 füzyon, 3 besleme, 4 entegrasyon, 5 mesh, 6 remesh |
| SLAM takip / relokalizasyon | enum / olay | ARCore `trackingState`, poz süreklilik denetimi (>2.5 m/s şüpheli) | Sıçrama gözlenirse logda işaretlenir |
| Bellek | MB | Android Profiler / `dumpsys meminfo` | 10 dk testte eğilim önemli |
| Pil / sıcaklık | %, °C | Android battery stats / termal API | Uzun testte raporlanır |
| Motion maliyeti | ms/kare | `motionProcessMs` (`motion_ms`) | GL thread bütçesi |

> `prep=`, `fuse=`, `feed=`, `dynms=`, `upd=`, `pts=`, `cells=`, `ex=` satırları sürümde
> mevcutsa logdan okunur; yoksa `UNKNOWN` yazılır, uydurma değer yazılmaz.
> Event/motion maliyeti bu sürümde ölçülür; eski notlarda "henüz ölçülmedi" ifadesi
> artık geçerli değildir.

### 1.5 Sonuç sınıflandırması

- **PASS:** Tüm zorunlu metrikler toplanmış ve kabul kriteri sağlanmış.
- **FAIL:** Kabul kriteri sağlanmamış, çökme, `stall` satırı veya veri tutarsızlığı.
- **UNKNOWN:** Metrik toplanamamış veya ortam eksik (ARCore destek yok, ışık ölçümü
  yok, log çekilememiş vb.). `UNKNOWN` asla `PASS` sayılmaz.

---

## 2. Oturum logu ve paylaşım

- Log dizini: `/sdcard/Android/data/com.neurovision.app/files/logs/` (alternatif:
  `/storage/emulated/0/Android/data/com.neurovision.app/files/logs/`).
- Dosya adı: `scan_<timestamp>.txt`; en yeni 12 log tutulur.
- Uygulama içi: üst bardaki `LOG` düğmesi mevcut oturum logunu her an paylaşır.
- Normal çıkış: `onStop` içinde `session_end=clean` yazılır; `onStart` ile
  `session_resume=` ile aynı dosya yeniden açılır.
- Beklenmedik kapanma: en yeni log 24 saatten yeniyse ve temiz kapanış işareti yoksa
  uygulama güvenli modda açılır ve log paylaşılabilir.
- Java çökmesi: `crash/java_crash_<stamp>.txt`; paylaşım sırasında oturum loguna eklenir.

### 2.1 adb ile log çekme

```powershell
$adb = "C:\Users\ixend\AppData\Local\Android\Sdk\platform-tools\adb.exe"
& $adb shell ls /sdcard/Android/data/com.neurovision.app/files/logs
& $adb pull /sdcard/Android/data/com.neurovision.app/files/logs/scan_<timestamp>.txt .
```

Android 11+ üzerinde kabuk erişimi kısıtlı olabilir; bu durumda uygulamanın `LOG`
düğmesi ile paylaşım kullanılır ve dosya adı rapora yazılır.

---

## 3. Native perception self-test adımları

Bu test kamera ve ARCore gerektirmez; deterministik sentetik bir hareketli
dama tahtası deseni (320×240) üzerinden gerçek `EventEngine` ve `MotionEngine`
modüllerini çalıştırır. Uygulama açılışında arka planda bir kez otomatik çalışır
(1.5 s gecikme). Canlı taramaya dokunmaz.

Beklenen rapor alanları:
- `perception_selftest=pass checks=6/6` (veya `fail` ve kaç kontrol geçti)
- `event_static` — sabit karede event sayısı 0 olmalı.
- `event_motion` — hareketli karede event sayısı > 1000, pos/neg ayrımı.
- `event_threshold` — daha yüksek eşikte daha az event (monotonluk).
- `motion_right` — sağa hareket: `dx` 2–6 px, yön 1, en az 20 feature.
- `motion_left` — sola hareket: `dx` −6…−2 px, yön 2.
- `motion_static` — sabit kareler: `|dx|<1`, `|dy|<1`, yön 0.
- Ek satır: `event stats: rate=... ev/s density=... mean|dL|=... ms=...`
- Ek satır: `motion right: attempted=... tracked=...% speed=... px/s ms=...`

### 3.1 Self-test raporunu çekme

```powershell
$adb = "C:\Users\ixend\AppData\Local\Android\Sdk\platform-tools\adb.exe"
& $adb shell ls /sdcard/Android/data/com.neurovision.app/files/logs
& $adb shell cat /sdcard/Android/data/com.neurovision.app/files/logs/selftest_perception_*.txt
```

Uygulama içi logda da aynı satırlar `logBlock` ile yer alır.

### 3.2 Host (emülatör) çalıştırma (isteğe bağlı)

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
& .\gradlew assembleDebug "-PreconEmulator=true"
& $adb push build\host\selftest_x86_64 /data/local/tmp/selftest_x86_64
& $adb shell chmod 755 /data/local/tmp/selftest_x86_64
& $adb shell /data/local/tmp/selftest_x86_64
```

Bu yol emülatör/x86_64 içindir; üretim APK'sı yalnızca `arm64-v8a` + `armeabi-v7a`
içerir. Bu sonuçlar canlı tarama performansını temsil etmez.

### 3.3 Kabul

- `perception_selftest=pass checks=6/6` ise PASS.
- Herhangi bir `FAIL` satırı varsa FAIL; ilgili satır rapora eklenir.
- Rapor çekilememişse UNKNOWN.

---

## 4. Mesh self-test adımları

Sentetik sahne: 2 m × 2 m duvar (z=2 m), 0.40 m kenarlı kutu, r=0.20 m küre,
derinlik haritasında ~0.64 m × 0.64 m delik. 160×120 derinlik, 320×240 renk,
kamera z=−0.30 çevresinde 0.35 m yarıçaplı yörüngede 12 kare. Deterministik ±3 mm
gürültü; tohum cihazdan bağımsız.

Beklenen rapor alanları ve anlam:
- `depthcheck valid` — geçerli derinlik pikseli oranı (sahne + delik ile uyumlu).
- `depthcheck err_mean/p95` — sentetik derinliğin analitik yüzeye uzaklığı.
- `tris` / `verts` — üçgen ve köşe sayısı.
- `boundary_edges` — açık yüzey sınırı kenar sayısı.
- `components` — bağlı bileşen sayısı.
- `degenerate` — sıfır alanlı üçgen; ideal 0.
- `hallucination` — yüzeyden >5 cm uzak üçgen; ideal 0.
- `overlap` — katı cisim içine giren üçgen; ideal 0.
- `normal_agreement` — yüz normali ↔ köşe normali uyumu (1 = tam).
- `surf_err_mean/p95` — köşelerin gerçek yüzeye uzaklığı.
- `within3cm` — köşelerin % kaçı 3 cm içinde.
- `bbox` — mesh sınır kutusu.
- `surface_voxels`, `w1`, `w2plus` — yüzey bandı vokselleri ve gözlem sayısı.
- `neg`, `pos`, `crossings` — işaret değiştiren küp sayısı.
- `growth` — kare başına üçgen sayısı.
- `wallmap` / `crossmap` — 20 cm hücreli 10×10 doluluk haritaları.
- `path=tango` veya `path=legacy` — Tango çekirdeği yüklenebildiyse tango, yoksa
  legacy TSDF yolu. Emülatörde `unavailable` yazması hatadır.

### 4.1 Mesh self-test raporunu çekme

```powershell
$adb = "C:\Users\ixend\AppData\Local\Android\Sdk\platform-tools\adb.exe"
& $adb shell ls /sdcard/Android/data/com.neurovision.app/files/logs
& $adb shell cat /sdcard/Android/data/com.neurovision.app/files/logs/selftest_*.txt
```

Canlı taramada her 4 saniyede bir `meshq tag=... tris=... verts=... ...` satırı
oturum loguna düşer; bu satır cihazdaki gerçek taramanın sayısal özetidir.

### 4.2 Kabul

- `degenerate=0` ve `hallucination=0` olmalı.
- `path=tango` satırı yalnızca üretim kitaplığı yüklenebilen telefonda doludur;
  emülatörde `unavailable` yazması hatadır.
- Rapor çekilememişse UNKNOWN.
- Referans değerler `SELFTEST.md` içindeki taban değerlerdir; cihaz/yol farklıysa
  aynı sayı beklenmez, yalnızca ideal alanlar (`degenerate`, `hallucination`,
  `overlap`) sabit kabul edilir.

---

## 5. Test senaryoları

Her senaryoda `Reset` ile başlanır. Her senaryo için kanıt: oturum logu dosya adı +
ekran görüntüleri + (varsa) export dosyası.

### Senaryo 1 — Statik ortam

- **Hazırlık:** Hacim temiz; telefon sabit bir yüzeye konur; aydınlatma sabit.
- **Adımlar:** 60 s bekle. Her 10 s oturum logundan örnek al. Başlangıç ve bitiş
  ekran görüntüsü al. Mesh ve depth görünümünü izle.
- **Ölçülecek metrikler:** kamera FPS, worker ms (`prep/fuse/feed/upd/ex`),
  event rate/yoğunluk/pos/neg, motion dx/dy/px/s/yön/takip oranı, tracking state,
  relokalizasyon olayları, depth source, depth valid %, tri/vert/block/keyframe,
  framesProcessed, phase, bellek MB, ev_ms, motion_ms, `dup` (varsa).
- **Kabul kriteri:** Sabit sahnede hareket yönü STATIC ve akış düşük olmalı;
  event rate, hareket senaryosundaki aynı cihaz/oturum karşılaştırmasında daha düşük
  olmalı; `framesProcessed` artmalı; `stall` satırı olmamalı; tracking stabil
  kalmalı; mesh tek parça ve kayma olmamalı.
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 2 — Yatay hareket

- **Hazırlık:** Hacim temiz; duvar veya nesne 1–2 m mesafede; yatay düzlemde hareket
  alanı.
- **Adımlar:** Telefonu 0.5–1 m sağa, sonra sola yavaşça hareket ettir. Her yön için
  10–15 s bekle. Akış vektörlerinin ekran görüntüsünü al.
- **Ölçülecek metrikler:** medyan dx/dy, px/s, m/s (ARCore), °/s, yön etiketi,
  takip oranı, feature count, tracking state, relokalizasyon, depth valid %,
  depth source, tri/vert.
- **Kabul kriteri:** Manuel yön ile yön etiketi uyumlu olmalı; akış büyüklüğü
  hareket yönünde olmalı; poz sürekliliği korunmalı (sıçrama olursa logda
  işaretlenmeli); duvar yüzeyi bozulmamalı.
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 3 — Dikey hareket

- **Hazırlık:** Hacim temiz; zemin ve tavan görünür.
- **Adımlar:** Telefonu yukarı/aşağı ve hafif eğimle hareket ettir. Zemin/tavan
  geometrisini izle.
- **Ölçülecek metrikler:** medyan akış (dx/dy/px/s/yön), poz, tracking state,
  relokalizasyon, depth valid %, depth source, tri/vert.
- **Kabul kriteri:** Yön etiketi doğru olmalı; mesh'te yırtılma veya "perde"
  oluşmamalı; depth valid düşüşü varsa raporlanmalı (gizlenmemeli).
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 4 — İleri / geri hareket

- **Hazırlık:** Hacim temiz; nesne 0.5–2 m aralığında.
- **Adımlar:** Nesneye 0.5 m'den 2 m'ye yaklaş ve uzaklaş. Depth render modunu kullan.
- **Ölçülecek metrikler:** merkez derinlik, geçerli piksel oranı, güven eşiği
  (`cthr`), `gate` reddi, depth source, mesafe tahmini, tracking state.
- **Kabul kriteri:** Derinlik değeri mesafeyle tutarlı değişmeli; yakın mesafe alt
  sınırının (cihaza bağlı, tipik ≈0.2–0.3 m) altında veri yoksa "ölçülemedi"
  denilmeli; derinlik kaynağı yanlış etiketlenmemeli.
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 5 — Tam oda taraması (360°)

- **Hazırlık:** Hacim temiz; 3–4 m genişliğinde oda.
- **Adımlar:** Duvarları, zemini, tavanı ve mobilyaları kapsayacak şekilde 360° yürü.
  Başlangıç noktasına dön. Export (OBJ veya PLY) al.
- **Ölçülecek metrikler:** kamera yörüngesi, tri/vert, block, keyframe,
  framesProcessed, loop/relokalizasyon olayları, depth source, bellek MB, export
  dosyası.
- **Kabul kriteri:** Harita tek parça birleşmeli; aynı duvar iki kez oluşturulmamalı;
  geçişte sıçrama olmamalı; export dosyası başka bir araçta açılabilir (dosya
  bütünlüğü: başlık + vertex/triangle sayısı > 0).
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 6 — Işık değişimi

- **Hazırlık:** Hacim temiz; aynı sahne.
- **Adımlar:** Parlak gün ışığında tara; ardından loş ve yüksek kontrastlı yapay
  ışıkta tara; ışık açılıp kapanırken kayıt al.
- **Ölçülecek metrikler:** depth valid %, tracking state, depth source, process ms,
  görsel örnekler, (varsa) ortalama luma. Luma ölçümü yoksa `UNKNOWN`.
- **Kabul kriteri:** Takip ve derinlik kaynağı dürüstçe raporlanmalı; düşük ışıkta
  feature kaybı gizlenmemeli; aşırı parlaklıkta speckle temizliği çalışmalı (görsel
  kanıt); depth valid düşüşü raporlanmalı.
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 7 — Dinamik nesneler

- **Hazırlık:** Hacim temiz; statik duvar önünde hareket alanı.
- **Adımlar:** Bir kişinin duvar önünde yürümesini sağla. Ayrıca sensöre paralel
  hızlı geçen bir cisim dene. Amber dinamik katmanı izle.
- **Ölçülecek metrikler:** dinamik piksel sayısı (varsa), maske alanı, statik mesh'te
  sızma olup olmadığı, `dynms`, depth valid %, depth source.
- **Kabul kriteri:** İnsan statik hacme füzyonlanmamalı; amber dinamik katman doğru
  mesafede görünmeli. Sensöre paralel hareket eden ve kareyi dolduran cisimlerde
  kaçak olursa bu bilinen sınırlamadır; `FAIL` sayılmaz, açık risk olarak rapora
  yazılır.
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

### Senaryo 8 — 10 dakikalık çalışma

- **Hazırlık:** Hacim temiz; cihaz şarj/pil ve sıcaklık başlangıç değerleri kaydedilir.
- **Adımlar:** Uygulamayı kesintisiz 10 dk çalıştır. Her 30 s oturum logundan örnek al.
  Cihaz sıcaklığını ve pil yüzdesini kaydet.
- **Ölçülecek metrikler:** FPS, processMs, tri/block sayısı, bellek MB, pil %,
  sıcaklık °C, `stall` olayları, tracking state, `session_end`.
- **Kabul kriteri:** Bellek sürekli büyümemeli (eğilim raporlanır; uydurma eşik
  yazılmaz); `stall` satırı olmamalı; tracking kararlı kalmalı; ısınma ve pil düşüşü
  raporlanmalı; oturum sonunda `session_end=clean` bulunmalı.
- **Sonuç:** [ ] PASS  [ ] FAIL  [ ] UNKNOWN

---

## 6. Sonuç tablosu

| # | Senaryo | Sonuç | Kanıt (log dosyası / ekran görüntüsü / export) |
|---|---|---|---|
| 1 | Statik ortam |  |  |
| 2 | Yatay hareket |  |  |
| 3 | Dikey hareket |  |  |
| 4 | İleri / geri |  |  |
| 5 | 360° tam oda |  |  |
| 6 | Işık değişimi |  |  |
| 7 | Dinamik nesneler |  |  |
| 8 | 10 dakika |  |  |
| P | Native perception self-test |  |  |
| M | Mesh self-test |  |  |

---

## 7. Kısa özet rapor (şablon)

```
NeuroVision Test Özeti — v0.1.0
Cihaz: <marka model>
Android: <sürüm> (API <xx>), ABI: <abi>
Uygulama: com.neurovision.app 0.1.0 (1)
Derinlik kaynağı: <ARCore Depth / MVS Depth / yok>
Test tarihi: <tarih>

Senaryo sonuçları: PASS=<n> FAIL=<n> UNKNOWN=<n>
Self-test: perception=<pass/fail checks=x/y>, mesh=<path, degenerate=, hallucination=>

Açık riskler:
- <risk 1>
- <risk 2>

Kanıt dosyaları:
- scan_<timestamp>.txt
- selftest_<timestamp>.txt
- selftest_perception_<timestamp>.txt
- <export dosyası>
```

---

## 8. Bilimsel dürüstlük kuralları

1. **Sahte FPS yok.** Kamera FPS, worker işlem FPS ve UI yenileme hızı ayrı
   raporlanır; üretilmeyen kare sayısı gösterilmez.
2. **Sahte metrik ölçek yok.** MVS yolunda veya güvensiz derinlikte "metre"
   iddiası yapılmaz; derinlik kaynağı her zaman görünürdür.
3. **Sahte semantik etiket yok.** Eğitilmiş model paketlenmeden sınıf adı verilmez;
   yalnızca geometrik merkez/mesafe verilir.
4. **Mock veri kamera verisi gibi sunulmaz.** Sentetik self-test yalnızca
   deterministik birim testidir ve canlı taramadan ayrı raporlanır.
5. **Fiziksel event camera iddiası yok.** Üretilen akış yazılımsal simülasyondur;
   µs zamansal çözünürlük veya asenkron piksel okuma iddia edilmez.
6. **Görülmeyen geometri iddiası yok.** Boşluk doldurma bir tahmindir;
   rekonstrüksiyon girdisine ölçüm olarak verilmez.
7. **Ölçülemeyen metrik `UNKNOWN`.** Uydurma sayı yazılmaz; eksik veri `PASS`
   sayılmaz.
8. **Düşük güven gizlenmez.** Düşük ışık, düşük doku, yansıtıcı yüzey ve sensör
   alt sınırı gibi durumlar raporda açıkça belirtilir.