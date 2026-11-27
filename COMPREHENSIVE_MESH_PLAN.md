# 3Drecon – Kapsamlı Mesh Kalitesi Revizyonu

> Durum: Açıldı (revizyon sonrası hedef sürüm: v2.12). 3 kaynak temel alındı:
>   - Cihaz ekran görüntüsü (düzensiz/gürültülü meshler, havada sarkan
>     parçalar, nesne üstüne oturmamış yüzeyler, üst üste binen güncelleme
>     davranışı olmayan meshler),
>   - `oturum_scan_20261115_091746.txt` / `oturum_scan_20261115_091903.txt`
>     (crash senaryoları),
>   - `oturum_scan_20261112_220100.txt` (Tango yayın zinciri ölçümleri).

Temel kural: **Bozmadan.** Legacy TSDF yolu, selftest, `meshq` satırları ve
export zinciri değişmez; her müdahale aynı yüzeyde kırılmadan genişler.

---

## 1. Şikayetler → Kök nedenler

| Şikayet (kullanıcı) | Kök neden | Faz |
|---|---|---|
| Düzensiz, çok gürültülü meshler | ARCore uzak aralıkta extrapolated/gürültülü derinlik; 3×3 medyan + Kalman yetersiz | F1 |
| Nesne üstüne oturmamış meshler | 30 mm voxel kuantizasyonu + düşük varyanslı füzyon | F1/F2/F4 |
| Üst üste binen, güncellenmeyen meshler | TSDF stale voxel'ler hiç carve edilmiyor | F3 |
| Havada sarkan parçalar | Tek-piksel shock/ghost sheet'ler; carve yoksunluğu | F1/F3 |
| Ayna/parça askıda şüphesi | Uzak/ulaşılmamış bölge mesh bütünlüğüne katılıyor | F1 |

---

## 2. Fazlar

### Faz 0 – Güvenlik (yapıldı, cihazda doğrulanmadı)
- [x] `fillSmallHoles()` flat-triple indeksleme crash'ini düzelt
      (`mesh.vertices[index * 3]` hatası `vector<MeshVertex>` üzerinde
      out-of-bounds okuma yapıyordu; v2.11 crash'inin kaynağı).
- [ ] Crash fix'i cihazda doğrula (TODO – kullanıcı kurulumu bekliyor).

### Faz 1 – Derinlik kalitesi (bu tur)
- [x] F1.1 Uzak mesafede (z > 4.0 m) ARCore confidence eşiğinin
      yükseltilmesi: 128 → 192. 4 metreden sonra extrapolated değerler artık
      fusion'a girmeyecek; menzil dışı sahte geometriyi azaltır. (v2.11)
      → Kod: `prepareDepthGrid()` içinde `kFarRange` / `kFarConfidence`.
- [x] F1.2 Uzak (z > 3.0 m) piksellere 2-komşu destek kuralı uygula
      (outdoor mantığı kapalı mekanda da uzak mesafe için çalışır).
      → Kod: `prepareDepthGrid()` – indoor bütün pikseller için de uzak
      kuralı. (v2.11)
- [ ] F1.3 Smoothed-depth substitute toleransını menzil bağımlı sıkılaştır
      (TODO – riskli, trade-off; cihazda değerlendirilmeli).
- [ ] F1.4 σ (measSigma) modelini cihazda doğrula; yüksek gürültüde artan
      ağırlık (TODO – F2.1 ile birlikte).

### Faz 2 – Füzyon kalitesi
- [ ] F2.1 Kalman kazancını ve sigma modelini log'a beslenerek downstream'de
      yeniden ayarla; per-pixel varyans takibi (TODO).
- [ ] F2.2 Stale voxel / TTL carve mekanizması: hiç yeniden gözlemlenmeyen
      voxel'lerin kalıcı kalma sorunu (TODO – riskli).
- [ ] F2.3 Room profil çözünürlüğünü (30 mm → 15 mm) bedelle test (TODO – perf).

### Faz 3 – Mesh yayın tutarlılığı (üst üste binme / güncellenmeme)
- [ ] F3.1 Segment üretim kayıtları eklenip farklar karşılaştırılsın; aynı
      hücre üzerinde çelişkili yüzey tespiti (TODO – log tabanlı, risksiz
      sınıf).
- [ ] F3.2 `weldMesh` / `fillSmallHoles()` / `dropSmallBlobs()` zinciri
      cihazda ölç: `meshq` normal_agreement / boundary_edges (TODO).
- [ ] F3.3 Dağarcık dışı "havada sarkan" yüzeyler için `dropSmallBlobs()`
      zaten taşınır (tamam – log ile doğrula).

### Faz 4 – Doğrulama ve teslimat
- [x] v2.11 (weld + crash fix) APK deliverable:
      `G:\3d ölçüm deepseek\güncel\3Drecon-selftest-debug.apk`
- [ ] Kullanıcı: v2.11 + Faz 1 APK'yı kur → selftest crash'siz kal →
      kısa tarama → `scan_*.txt` + `meshq` satırlarını gönder (TODO – bekleyen).
      Teslimat APK: `G:\3d ölçüm deepseek\güncel\3Drecon-selftest-debug.apk`
      sha256=9E1559636838E3247F60A716097BBD514D4C5E9753781371077F4F5BC235037A
- [ ] Sonuçlara göre: F1.4 / F2.x fine-tune; hala sarkan/overlapping görülürse
      F3.1 ve F2.2'ye geç.

---

## 4. Araştırma özeti (renk detay)

- **GitHub araması (`tango 3d reconstruction`)**: 2 repo; biri
  `iskenderozkul/3Drecon` (bizim repo), diğeri çok eski Unity tabanlı örnek
  (`qpoisson/TangoMapper`). Umut veren bir hazır cleanup reposu yok.
- **Open3D (`isl-org/Open3D`)**: mesh cleanup için referans kalıbı (remove
  duplicated/non-manifold, Laplacian/Taubin smoothing, decimate). Kütüphane
  Android'e gömülemeyecek kadar büyük; algoritmanın sade sürümü bizde
  port edilebilir (Faz 5).
- **PointDiT (`google-research/pointdit`)**: monocular, öğrenilmiş; bizim
  metrik derinlik tabanımıza uymaz, ileride "kaçak/Uzak" senaryolar için
  roadmap.
- **Google Tango 3DR (`libtango_3d_reconstruction.so`)**: kapalı kaynak
  (opak) — yalnızca **input (depth temizliği)** ve **output (mesh cleanup) **
  üzerinde oynamayı sağlar. Yeni temizlik için: weld + hole fill + blob drop +
  Taubin smoothing + recompute = bizim yayın zincirimiz.

## 5. Fazlar (revizyon v2)

### Faz 0 – Güvenlik (yapıldı)
- [x] `fillSmallHoles()` flat-triple indexing crash'i düzeltildi (v2.11).

### Faz 1 – Derinlik kalitesi (uygulandı)
- [x] F1.1 Uzak (z > 4.0 m) pikselde confidence eşiği 128 → 192 (v2.11).
- [x] F1.2 Uzak (z > 3.0 m) pikselde 2-komşu destek kuralı (v2.11).
- [ ] F1.3 Smoothed-depth substitute toleransını menzil bağımlı sıkılaştır
      (TODO – riskli).

### Faz 2 – Füzyon kalitesi
- [ ] F2.1 Kalman kazancı + sigma model fine-tune (TODO).
- [ ] F2.2 Stale voxel / TTL carve: hiç yeniden gözlemeyen voxel kalıcı
      kalma sorunu (TODO – riskli).
- [ ] F2.3 Room profil 30mm → 15mm karşılaştırma (TODO).

### Faz 3 – Mesh yayın tutarlılığı
- [x] weld + fill + drop zinciri (v2.11 publish zinciri).
- [ ] F3.1 Aynı hücre çelişkili yüzey tespiti (TODO).
- [ ] F3.2 `meshq` cihaz-ölçümlerini karşılaştır (TODO).

### Faz 5 – Mesh denoising
- [x] F5.1 Taubin λ|μ smoothing `publishLocked()` içinde **devre dışı**
      (`constexpr bool kEnableTaubinSmooth = false`). Sebebi: adjacency
      build + 4 geçiş ~200k üçgende 100-400ms'lere çıkıp worker thread'i
      boğdu; `ms=1539-1876`, fps=27→1-10 (oturum_scan_20261115_141012.txt).
      Kullanıcı "mesh güncellenmiyor / çok çirkin" olarak bildirdi. Kod korunuyor
      (export için; live kullanım yok). Live yüzey `weld + fill + dropSmallBlobs
      + recompute` olarak ucuz kalıyor.

### Faz 4 – Doğrulama
- [ ] Kullanıcı: yeni APK'yı kur → selftest crash'siz → tarama → log/meshq
      gönder.
- [ ] İzabe sorun görünürse F2.2 / F3.1 / F2.3'e geç.

---

## 3. Güvenlik / Geri dönüş

- Değişen: `prepareDepthGrid()` (uzak reddetme) + `publishLocked()` (Taubin).
  Etki: menzil dışı sahte geometri azalır; mesh daha pürüzsüz.
- Değişmeyen: public arayüz, selftest, meshq, legacy yol, export zinciri,
  permission/AR akışı.
- Geri dönüş: tek satırla (`revert` veya `git restore`) geri alınır.

## 4. Build Komutları ve Konum

Aktif proje artık `G:\3d ölçüm deepseek\güncel\3Drecon` klasöründe; eski
repo/referanslar `G:\3d ölçüm deepseek\old` altına taşındı.

```bat
set JAVA_HOME=C:\Program Files\Android\Android Studio\jbr
"C:\Users\ixend\.gradle\wrapper\dists\gradle-8.13-bin\5xuhj0ry160q40clulazy9h7d\gradle-8.13\bin\gradle.bat" assembleDebug --console=plain
```

Not: `-PreconEmulator=true` yalnızca geliştirmede (x86_64); üretim arm-only.

## 5. Log Takibi (Karşılaştırma)

Karşılaştırılacak metrikler: `valid=`, `gate=`, `hold=`, `fill=`, `smooth=`,
`dmin/dmed/dmax`, `meshq normal_agreement`, `tris/blocks`,
`skip=staledepth` / `skip=thinDepth` sıklığı. Faz 1 reddetmelerini `valid=` ve
`meshq` ile doğrula: geçerli piksel düşmesi, sahte geometri ve sarkma azalmalı
(kabul edilebilir).

---

## 6. Açık Sorular / TODO

- [ ] Gerçek sahnede "50 m uzak" istek: ghost sheet hala görülecek mi?
- [ ] Selftest payload'da "havada sarkan" senaryosu eklensin mi?
      (TODO – F4.validasyon)
- [ ] Ayna/kaçak bölge overlay testi: decoupled segment kombinasyonu ile
      (TODO – F3.1)
