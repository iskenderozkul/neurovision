# Fizibilite Analizi

Bu belge, `aim.md`'de tanımlanan hedeflerin tek RGB kamera ve güncel akıllı telefon donanımıyla gerçekte hangi ölçüde karşılanabileceğini inceler. Referans taban `NeuroVision/README.md`'de tarif edilen yığındır: ARCore 6DoF takip + ARCore Depth API + Apache-2.0 Tango 3D Reconstruction çekirdeği + TSDF + Marching Cubes + yerel MVS yedeği. Hiçbir sayı ölçüm raporu olmadan "gerçek" olarak sunulmaz; tahminler **tahmini**, ürün hedefleri **hedef** olarak etiketlenir.

---

## 1. Hedeflerin sınıflandırılması

> **Güncel durum:** Yazılımsal event motoru (Aşama 1) ve hareket (motion) motoru
> (Aşama 2) bu sürümde uygulanmış; sınıflandırma satırları Aşama 1-5 tamamlandıkça
> birlikte gözden geçirilir.

| `aim.md` hedefi | Sınıf | Gerekçe |
|---|---|---|
| Kamera görüntülerinden zamansal değişim (event simülasyonu) çıkarma | **Tam uygulanabilir** | Ardışık Y düzlemleri arasında log-parlaklık farkı hesaplanabilir; DVS benzeri polarity/yoğunluk üretilebilir. |
| Hareket algılama, optik akış, baskın yön/hız tahmini | **Tam uygulanabilir** | Piramit Lucas–Kanade + köşe takibi; ayrıca ARCore 6DoF pozundan metrik hız okunabilir. |
| 6DoF kamera konumu ve yönelimi | **Tam uygulanabilir** | ARCore VIO (kamera + IMU) metrik pozu doğrudan sağlar; ek SLAM gerekmez. |
| Metriste yakın/uzak yüzey ayrımı (ARCore Depth API olan cihazlar) | **Kısmen uygulanabilir** | Derinlik metriktir fakat 160×90 civarında, menzil ve güven sınırına tabidir; cam/ayna ve 0.2–0.3 m altı mesafeler veri üretmez. |
| Point cloud + renkli mesh + OBJ/PLY dışa aktarma | **Tam uygulanabilir** | TSDF füzyonu ve Marching Cubes referansta çalışmaktadır; renk CPU lumaörneklemesiyle gelir. |
| Dinamik nesnelerin statik haritayı bozmaması | **Kısmen uygulanabilir** | Derinlik rezidü maskesi insan/araçları ayırır; sensöre paralel hareket eden ve kareyi dolduran cisimler kaçabilir; "ne olduğu" belirlenmez. |
| Monoküler derinlik tahmini (tek görüntü, genel amaçlı) | **Kısmen uygulanabilir** | Göreli derinlik üretilebilir; mutlak ölçek için ARCore pozu/güvenli ölçek ipucu gerekir. Eğitilmiş model paketlenmediği için bu sürümde **yok**. |
| Loop closure, global harita optimizasyonu, kalıcı dünya modeli | **Kısmen uygulanabilir** | ARCore relokalizasyonu ve poz sürekliliği denetimi vardır; oturumlar arası kalıcı harita ve grafik tabanlı global optimizasyon bu sürümde yoktur. |
| Nesne tanıma / semantik segmentasyon / "masa 2 m önde" gibi ifadeler | **Bu sürümde uygulanamaz** | Model ağırlığı olmadan etiket üretmek uydurmadır. Geometrik olarak doğrulanabilir merkez/mesafe bilgisi verilebilir; sınıf adı verilemez. |
| Gaussian Splatting ile fotogerçekçi yeniden görüntüleme | **Bu sürümde uygulanamaz** | Eğitim gerektirir, metrik geometri garantisi vermez; yalnızca araştırma notu (Aşama 7) olarak planlıdır. |
| Fiziksel event camera davranışı (µs çözünürlük, asenkron piksel okuma) | **Uygulanamaz** | CMOS kamera yazılımla event sensörüne dönüşmez; bkz. §2. |
| Kameranın FPS'ini aşan zamansal çözünürlük | **Uygulanamaz** | Yeni foton üretilemez; simülasyon yalnızca mevcut karelerden türetilir. |

---

## 2. Bilimsel ve donanımsal sınırlar

- **FPS üst sınırı gerçektir.** Yazılımsal event üretimi `ΔL = log I(t) − log I(t−1)` farkını kare hızında örnekler. 30 FPS'lik bir akıştan 1000 FPS'lik bir event akışı türetmek mümkün değildir; üretilen "event"lerin zaman damgası kare zaman damgasından türetilir.
- **Yeni foton yoktur.** Görülmeyen yüzeylerin geometrisi bilinemez; boşlukları doldurmak tahminidir ve belirsizlik korunmalıdır. Referans motor bu nedenle tahmin piksellerini rekonstrüksiyon girdisinden çıkarır.
- **Monoküler derinlik göreldir.** Tek görüntüden gelen derinlik ölçek belirsizliği taşır. ARCore 6DoF pozu ve ARCore Depth API ölçek sağlar; MVS yedeğinde ölçek ARCore pozuna bağlıdır ve menzille bozulur.
- **ARCore Depth API metriktir ama sınırlıdır.** Destekleyen cihazlarda ölçüm metrik birimlerdedir; tipik ızgara 160×90 mertebesindedir ve menzil sınırı vardır (referans uygulama 6 m'de durur). Cam, ayna ve parlak yüzeyler ölçüm döndürmez; yakın mesafe alt sınırı (≈0.2–0.3 m) vardır.
- **Rolling shutter ve hareket bulanıklığı.** Hızlı pan ve yürüyüşte satır bazlı pozlama farkı ve bulanıklık hem feature takibi hem derinlik eşleştirmesini bozar. Poz sürekliliği denetimi (>2.5 m/s görünen hız) relokalizasyonu ayıklar.
- **Düşük ışık ve düşük dokulu yüzeyler.** Feature yoğunluğu düşer; duvar gibi tekrarlayan dokular eşleşme belirsizliği yaratır.
- **Dinamik sahne.** Hareket eden cisimler statik dünyaya ait değildir; rezidü tabanlı maskeleme gerekir ve yine de kaçak olabilir.
- **Ölçek ve güven ayrımı.** Her tahminin güven değeri tutulmalı; düşük güvenli veri ölçüm gibi gösterilmemelidir.

---

## 3. Teknoloji karşılaştırması

Lisanslar upstream beyanlarıdır; dağıtım öncesi sürüm bazında doğrulanmalıdır. "Efor" bu projeye entegrasyon maliyetini ifade eder.

| Teknoloji | Lisans | Mobil uygunluk | Metrik derinlik | Efor | Karar |
|---|---|---|---|---|---|
| ARCore 6DoF VIO | Ticari SDK, ücretsiz kullanım | Çok iyi (Android) | Poz metrik; derinlik üretmez | Düşük | **Entegre** — takip ve pozu sağlar |
| ARCore Depth API (raw 16-bit + confidence + smoothed) | Ticari SDK, ücretsiz kullanım | Destekleyen cihazlarda iyi | **Evet**; 160×90 mertebesi, menzil sınırlı | Düşük | **Entegre** — birincil derinlik kaynağı |
| Google Tango 3D Reconstruction çekirdeği | Apache-2.0 | arm64-v8a / armeabi-v7a, `dlopen` | Evet (metrik derinliği kullanır) | Orta | **Entegre** — birincil TSDF/mesh |
| Yerel voxel-hash TSDF + Marching Cubes | Proje kodu | İyi | Kaynağın ölçeğini devralır | Orta | **Entegre** — çekirdek yüklenemezse yedek |
| Yerel MVS (Census + SGM + refine) | Proje kodu | İyi (320×240 eşleştirme) | Kısmi; ARCore pozuna bağlı ölçek | Yüksek | **Entegre** — Depth API yoksa yedek |
| OpenCV | Apache-2.0 (4.5.0+) | İyi | Yok (algoritma kütüphanesi) | Orta | **Entegre değil** — LK/Shi-Tomasi yerel olarak yazıldı (Aşama 2); ek ABA bağımlılığı istenmiyor |
| ORB-SLAM3 | GPLv3 | Zor; ağır, Android kurulumu messy | Monoküler belirsiz; VIO ile metrik | Yüksek | **Entegre değil** — ARCore zaten VIO sağlıyor; GPLv3 dağıtım riski |
| OpenVINS | BSD-3-Clause | Orta; ROS merkezli C++ | IMU ile metrik | Yüksek | **Entegre değil** — ham IMU erişim yolu bu derlemede yok; ARCore VIO yeterli |
| Depth Anything V2 | Apache-2.0 | Ölçeklenmiş TFLite ile orta | **Hayır** (göreli) | Orta–yüksek | **Entegre değil** — eğitilmiş ağırlık paketlenmedi; ağırlıksız model "varmış" denemez |
| Open3D | MIT | Masaüstü odaklı; Android ağır | Yok | Yüksek | **Entegre değil** — ihtiyaç duyulan alt küme yerel mesher ile karşılanıyor |
| Gaussian Splatting | Apache-2.0 (referans repo) | Araştırma/masaüstü; eğitim ağır | Hayır (metrik geometri garanti etmez) | Çok yüksek | **Entegre değil** — yalnızca Aşama 7 araştırma notu |
| COLMAP | BSD-3-Clause | Masaüstü/offline, gerçek zamanlı değil | Kalibrasyon/ bilinen ölçekle evet | Yüksek | **Entegre değil** — offline SfM, telefon üstü gerçek zamanlı hedefle uyumsuz |

### Referans yığınının seçilme nedeni

1. **Ölçek ücretsiz geliyor.** ARCore pozu metrik olduğu için monoküler ölçek belirsizliğini çözmek için ek donanım veya bilinen referans nesnesi gerekmiyor.
2. **Yoğun füzyon hazır.** Tango 3DR çekirdeği TSDF + Marching Cubes + renkli mesh'i tek pakette sunuyor; Apache-2.0 lisansı dağıtıma uygun.
3. **Başarısızlık zarif.** `dlopen` başarısızsa yerel TSDF, Depth API yoksa MVS devreye giriyor; uygulama geometrisiz kalmıyor.
4. **Gerçek veri.** Tüm girdiler canlı kameradan; hiçbir aşamada sentetik derinlik veya uydurma poz kullanılmıyor.

---

## 4. Beklenen performans (tahmini)

Aşağıdaki değerler **tahminidir**; cihaz bazında ölçülmüş kıyaslama sonucu değildir. Referans README'de belirtilen "60–120 ms/frame füzyon" aralığı yalnızca o referans cihaz gözlemidir, genelleştirilmez.

| Katman | Orta segment (tahmini) | Üst segment (tahmini) |
|---|---|---|
| ARCore takip / render | 30–60 FPS | 60 FPS (ekran yenileme sınırı) |
| CPU görüntü | 640×480 YUV_420_888 | 640×480+ (cihaz ayarına bağlı) |
| Depth API ızgarası | 160×90 mertebesi | 160×90 mertebesi (cihazda artabilir) |
| TSDF füzyonu | 60–120 ms/frame mertebesi (referans gözlem) | Daha kısa; yine de worker thread'de |
| Mesh / remesh | ~300 ms aralıkla yayın (hedef) | ~300 ms aralıkla yayın (hedef) |
| Event + motion (GL thread) | Yazılımsal DVS event + 4 seviyeli LK; maliyet self-test ile ölçüldü (6/6 PASS) | Aynı (bütçe aşılırsa worker'a taşıma) |
| UI örnekleme | 2 Hz (500 ms) | 2 Hz (500 ms) |
| Bellek / pil / sıcaklık | 10 dk testte ölçülecek (hedef: kararlı) | 10 dk testte ölçülecek (hedef: kararlı) |

Takip ve render ekran hızında kalırken yoğun rekonstrüksiyon worker thread'de yavaşlar; yani "FPS düştü" ifadesi kamera FPS'i ile işlem FPS'i ayrımını gerektirir.

---

## 5. Risk tablosu

| Risk | Etki | Azaltma |
|---|---|---|
| Tek RGB kamera, derinlik menzili sınırlı | Uzak yüzeyler ve ince detaylar eksik | Menzil sınırı (6 m) korunur; MVS yedeği; belirsizlik gizlenmez |
| Ölçek belirsizliği (MVS yolu) | Yanlış mesafe/ölçüm iddiası | ARCore metrik pozuyla hizalama; kaynak HUD'da açıkça belirtilir |
| ARCore Depth API olmayan cihaz | Derinlik kalitesi düşer | Yerel MVS yedeği; `depthSource` göstergesi |
| Rolling shutter / hareket bulanıklığı | Takip ve eşleşme bozulur | Poz süreklilik denetimi; şüpheli kareler atlanır |
| Düşük ışık / düşük doku | Feature kaybı | Güven eşikleri; hata toleransı; dürüst hata mesajı |
| Dinamik nesneler | Statik haritada leke | Derinlik rezidü maskesi (3 kare + 96 piksel), 1 piksel dilasyon |
| Yansıtıcı / cam yüzeyler | Derinlik yok, boşluk | Güven kapısı; boşluk doldurma rekonstrüksiyona verilmez |
| Yoğun füzyon maliyeti | Worker gecikmesi, ısınma | Kare başına tek entegrasyon; carving/mesh kısma; worker thread |
| GL thread yükü (event + motion) | Kare düşmesi, etkileşim gecikmesi | `event_ms`/`motion_ms` ve native self-test ile ölçülüyor; bütçe aşılırsa worker'a taşıma |
| Tango `.so` yüklenememesi | Geometri üretilememe | Yerel TSDF yedeği; oturum logunda `t3dr` durumu |
| Eğitilmiş model yok | Semantik etiket uydurma riski | "Model yok" durumu; sadece geometrik doğrulanabilir çıktı |
| Oturum bazlı veri | Kapatınca harita kaybı | Dürüst sınırlama; Aşama 7'de kaydetme/yükleme hedefi |

---

## 6. Uygulamanın asla yapmaması gereken iddialar

1. **Sahte FPS.** Kamera FPS'i, worker işlem FPS'i ve UI yenileme hızı ayrı raporlanır; üretilmeyen kare sayısı gösterilmez.
2. **Sahte metrik ölçek.** MVS yolunda veya güvensiz derinlikte "metre" iddiası yapılmaz; derinlik kaynağı her zaman görünürdür.
3. **Sahte semantik etiket.** Eğitilmiş model paketlenmeden "masa", "sandalye", "insan" gibi sınıf adı verilmez; yalnızca geometrik merkez/mesafe verilir.
4. **Mock veriyi kamera verisi gibi sunmak.** Sentetik mesh self-testi yalnızca deterministik birim testidir ve canlı taramadan ayrı raporlanır.
5. **Fiziksel event camera iddiası.** Üretilen akış yazılımsal simülasyondur; µs zamansal çözünürlük veya asenkron piksel okuma iddia edilmez.
6. **Görülmeyen geometri iddiası.** Boşluk doldurma bir tahmindir; rekonstrüksiyon girdisine ölçüm olarak verilmez.
7. **Eksik özelliği tamamlanmış göstermek.** Loop closure, kalıcı harita, Gaussian Splatting ve nesne tanıma bu sürümde yoktur; yol haritasında açıkça işaretlidir.

---

## 7. Sonuç

Proje, tek RGB kamera ve ARCore donanımıyla **ölçülebilir, dürüst bir yoğun 3D tarayıcı** olarak uygulanabilir. Fiziksel event camera, görülmeyen geometri ve eğitilmiş model gerektiren semantik katmanlar uygulanamaz; bunun yerine event simülasyonu, metrik 6DoF takip, güven kapılı derinlik ve TSDF tabanlı mesh sunulur. Kritik başarı koşulu, sınırların gizlenmemesi ve her tahminin güven/kaynak bilgisiyle birlikte gösterilmesidir.
