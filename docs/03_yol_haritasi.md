# Geliştirme Yol Haritası ve Test Planı

Aşamalar `TODO.md` ile birebir eşleşir (0. aşama iskelet tamamlandı). Her aşama sonunda çalışan, test edilebilir bir sürüm çıkar; mock kamera verisi hiçbir aşamada gerçek veri gibi sunulmaz. Ölçülmemiş sayılar "tahmini", ürün hedefleri "hedef" olarak etiketlenir.

---

## 1. Aşama planı

### Aşama 1 — Kamera + Event Simülasyonu

- **Hedef:** Canlı karelerden DVS benzeri event akışı üretmek ve görselleştirmek.
- **Durum:** Tamamlandı.
- **Teknolojiler:** ARCore CPU YUV görüntüsü, yerel C++ `event/`, log-parlaklık eşiği, ARGB önizleme tamponu.
- **Geliştirme görevleri:**
  - Native event motoru: önceki Y düzlemi referans takibi, `ΔL = log I(t) − log I(t−1)`.
  - Pozitif/negatif polarity, event hızı, yoğunluk ve büyüklük istatistikleri.
  - Eşik ve sönüm ayarı; 128×96 ARGB önizleme tamponu.
  - Kamera karesinden (Y düzlemi) event üretiminin GL thread'ine bağlanması.
  - Event View ekranı: canlı harita, eşik slider'ı, sayaçlar.
- **Başarı kriteri:** Kamera hareket ettirildiğinde hareketli kenarlar event haritasında belirginleşir; sabit tutulduğunda event hızı düşer.
- **Test yöntemi:** Test 1 + Test 2; eşik değiştirilerek gürültü/hareket ayrımı gözlenir; `event_ms` oturum loguna yazılır.

### Aşama 2 — Hareket Algılama

- **Hedef:** Optik akış ve köşe takibiyle yön/hız tahmini; gerçek metrik hızı ARCore'dan okuma.
- **Durum:** Tamamlandı (motion bug düzeltildi: LK gradyanı/search sampling düzeltmesi sonrası self-test 6/6 PASS).
- **Teknolojiler:** 4 seviyeli piramit + Lucas–Kanade, Shi-Tomasi/Harris, ARCore 6DoF poz.
- **Geliştirme görevleri:**
  - Yerel `motion/` modülü; grid tabanlı köşe tespiti ve takip başarı oranı.
  - Medyan akış (dx, dy), px/s hız, baskın yön (sol/sağ/yukarı/aşağı).
  - ARCore pozundan gerçek hız (m/s, °/s) ve güven göstergesi.
  - Live ekranında akış vektörleri ve yön göstergesi.
- **Başarı kriteri:** Telefon dört yönde hareket ettirildiğinde yön işareti doğru, hız sırası makul; düşük ışıkta takip başarı oranı düşüşü dürüstçe gösterilir.
- **Test yöntemi:** Test 2 + Test 3 + Test 6; akış vektörleri ekran görüntüsüyle karşılaştırılır.

### Aşama 3 — Derinlik

- **Hedef:** Derinlik kaynağını güvenle göstermek ve yakın/uzak ayrımını doğrulamak.
- **Teknolojiler:** ARCore Depth API (raw + confidence + smoothed), yerel MVS yedeği, zamansal Kalman füzyonu.
- **Durum:** ARCore ve MVS entegrasyonu **tamamlandı** (referanstan); derinlik ekranı da **tamamlandı**.
- **Geliştirme görevleri:**
  - Depth View: renkli heatmap, güven haritası, geçerli piksel oranı.
  - Derinlik kaynağı (`ARCore` / `MVS`) ve gecikmenin dürüst gösterimi.
  - `depthValid`, `gate`, `cthr` sayaçlarının HUD/log'a bağlanması.
- **Başarı kriteri:** Yakın/uzak yüzeyler ayrışır; güven düşük olduğunda ekran bunu açıkça belirtir; kaynak yanlış etiketlenmez.
- **Test yöntemi:** Test 4 + Test 6 + Test 7; `Depth` render modu ham sensör görünümüyle karşılaştırılır.

### Aşama 4 — Visual-Inertial SLAM

- **Hedef:** ARCore 6DoF takibiyle stabil poz ve kamera yörüngesi.
- **Teknolojiler:** ARCore VIO, poz süreklilik denetimi, düzlem bulma.
- **Durum:** 6DoF takip **tamamlandı**; süreklilik denetimi ve 3D görünümde çizim de **tamamlandı**.
- **Geliştirme görevleri:**
  - Tracking state, relokalizasyon ve poz sürekliliği denetiminin HUD'a bağlanması.
  - Kamera yörüngesinin 3D görünümde çizilmesi.
- **Başarı kriteri:** Oda içinde hareket ederken konum/yönelim stabil; relokalizasyon sırasında haritada sıçrama görülmez.
- **Test yöntemi:** Test 5 + Test 8; yörünge sürekliliği ve `tracking=` durumu logda izlenir.

### Aşama 5 — 3D Haritalama

- **Hedef:** Oda taramasından geometrik harita ve dışa aktarılabilir dosya.
- **Teknolojiler:** Tango 3DR TSDF + Marching Cubes, yedek voxel-hash TSDF, OBJ/PLY.
- **Durum:** Rekonstrüksiyon ve export **tamamlandı**; nokta bulutu görünümü ve 3D World ekranı da **tamamlandı**.
- **Geliştirme görevleri:**
  - Mesh köşelerinden `GL_POINTS` ile nokta bulutu görünümü.
  - 3D World ekranı: serbest yörünge kamerası, döndürme/yakınlaştırma, mesh ↔ nokta bulutu geçişi.
- **Başarı kriteri:** 360° tarama sonrası tutarlı mesh; OBJ/PLY dosyası başka bir araçta açılabilir.
- **Test yöntemi:** Test 5; üçgen/vertex sayısı ve dosya bütünlüğü kontrolü.

### Aşama 6 — Mekânsal Algılama (dürüst kapsam)

- **Hedef:** Geometrik olarak doğrulanabilir mesafe/konum bilgisi; uydurma etiket yok.
- **Teknolojiler:** Derinlik süreksizliği, dinamik yüzey maskesi, takılabilir TFLite arayüzü.
- **Geliştirme görevleri:**
  - Derinlik süreksizliğinden geometrik yüzey kümeleri (engel blokları) + 3D merkez/mesafe.
  - Nesne tespiti için TFLite arayüzü; model yoksa "model yok" mesajı.
  - Hareketli yüzey maskesi ve dinamik katmanın (amber mesh) doğrulanması.
- **Başarı kriteri:** "Kamera önünde yaklaşık X m'de bir yüzey" denilebilir; sınıf adı iddia edilmez.
- **Test yöntemi:** Test 7 + Test 4; maskenin statik hacme sızmadığı mesh üzerinde kontrol edilir.

### Aşama 7 — İleri Görselleştirme

- **Hedef:** Oturum kaydı/yeniden yükleme ve araştırma notu.
- **Teknolojiler:** Mesh + kamera yörüngesi serileştirme, Gaussian Splatting araştırması.
- **Geliştirme görevleri:**
  - Oturumu kaydetme ve yeniden yükleme (mesh + yörünge).
  - Gaussian Splatting araştırma notu — **bu sürümde uygulanmıyor**.
  - Dışa aktarılan modeli uygulama içinde yeniden açma.
- **Başarı kriteri:** Kaydedilen oturum aynı geometri ve yörüngeyle geri yüklenir.
- **Test yöntemi:** Kaydet → kapat → aç → üçgen sayısı ve yörünge noktaları karşılaştırması.

### Aşama 8 — Test ve Raporlama

- **Hedef:** Ölçülebilir, raporlanabilir ürün ve APK.
- **Teknolojiler:** `ScanLog.kt`, `ReconMetrics`, 2 Hz polling, `getStats`, `getDiagnostics`.
- **Geliştirme görevleri:**
  - Diagnostik ekranı: FPS, event rate, SLAM durumu, gecikmeler, bellek, sıcaklık.
  - 8 senaryoluk test protokolünün uygulanması (§2).
  - Her modülün ölçülen sürelerinin oturum loguna yazılması (`event_ms`, `motion_ms`, `prep=`, `fuse=`, `feed=`, `upd=`, `ex=`).
  - APK üretimi ve kurulum notları.
- **Başarı kriteri:** 8 senaryonun tamamı log + ekran görüntüsüyle belgelenir; kalan açık hatalar liste halinde raporlanır.
- **Test yöntemi:** §2 protokolü; MVP kabul kriterleri §4.

---

## 2. Test protokolü (`aim.md` bölüm 14)

Her test aynı cihazda, aynı sürümle, oturum logu açıkken yapılır. Başlangıçta `Reset` ile hacim temizlenir; cihaz/model/Android sürümü rapora yazılır.

### Test 1 — Sabit ortam

- **Yöntem:** Telefonu sabit bir yüzeye koyup 60 s beklet; event haritası, event rate, `framesProcessed` ve mesh'ı izle.
- **Kontrol:** Gereksiz event üretimi düşük olmalı; harita kaymaması; `tracking` stabil. Tekrarlanan karelerde derinlik füzyonu `dup=` sayacıyla atlanmalı.
- **Kayıt:** event/s, event_ms, depth valid oranı, triangle sayısı, poz drift'i (başlangıç/bitiş).

### Test 2 — Yatay hareket

- **Yöntem:** Telefonu 0.5–1 m sağa, sonra sola yavaşça hareket ettir; akış vektörlerini ve ARCore hızını izle.
- **Kontrol:** Baskın yön sol/sağ doğru işaretlenmeli; poz sürekliliği korunmalı; duvar yüzeyi bozulmamalı.
- **Kayıt:** medyan dx/dy, px/s, m/s, °/s, tracking state, event rate.

### Test 3 — Dikey hareket

- **Yöntem:** Telefonu yukarı/aşağı ve hafif eğimle hareket ettir; zemin/tavan geometrisini izle.
- **Kontrol:** Yön işareti doğru; harita geometrisinde yırtılma veya "perde" oluşmamalı.
- **Kayıt:** medyan akış, poz, depth valid oranı, mesh üçgen sayısı.

### Test 4 — İleri / geri hareket

- **Yöntem:** Bir nesneye 0.5 m'den 2 m'ye yaklaş/uzaklaş; derinlik heatmap ve mesafe göstergesini izle.
- **Kontrol:** Derinlik değeri mesafeyle tutarlı değişmeli; yakın mesafe alt sınırının altında veri yoksa "ölçülemedi" denilmeli.
- **Kayıt:** merkez derinlik, geçerli piksel oranı, güven eşiği (`cthr`), `gate` reddi, mesafe tahmini.

### Test 5 — Tam oda taraması (360°)

- **Yöntem:** 3–4 m genişliğinde bir odada duvarları, zemini, tavanı ve mobilyaları kapsayacak şekilde 360° yürüyüş yap; daha sonra başlangıç noktasına dön.
- **Kontrol:** Harita tek parça birleşmeli; aynı duvar iki kez oluşturulmamalı; geçişte sıçrama olmamalı (relokalizasyon denetimi).
- **Kayıt:** kamera yörüngesi, üçgen/vertex sayısı, blok sayısı, `framesProcessed`, loop/relokalizasyon olayları, OBJ/PLY dosyası.

### Test 6 — Işık değişimi

- **Yöntem:** Aynı sahneyi parlak gün ışığında, ardından loş ve yüksek kontrastlı yapay ışıkta tara; ayrıca ışık açılıp kapanırken kayıt al.
- **Kontrol:** Takip ve derinlik kaynağı dürüstçe raporlanmalı; düşük ışıkta feature kaybı gizlenmemeli; aşırı parlaklıkta speckle temizliği çalışmalı.
- **Kayıt:** ortalama luma, depth valid oranı, takip durumu, işlem süresi, görsel örnekler.

### Test 7 — Dinamik nesneler

- **Yöntem:** Statik duvar önünde bir kişinin yürümesini sağla; ayrıca sensöre paralel hızlı geçen bir cisim dene.
- **Kontrol:** İnsan statik hacme füzyonlanmamalı; amber dinamik katman doğru mesafede görünmeli. Paralel hareket eden cisimde kaçak olabiliyorsa raporlanmalı.
- **Kayıt:** dinamik piksel sayısı, maske alanı, statik mesh'te sızma olup olmadığı, `dynms`.

### Test 8 — 10 dakikalık çalışma

- **Yöntem:** Uygulamayı kesintisiz 10 dk çalıştır; her 30 s'de oturum logundan örnek al; cihaz sıcaklığını ve pil yüzdesini kaydet.
- **Kontrol:** Bellek sürekli büyümemeli; `stall` satırı olmamalı; tracking kararlı kalmalı; ısınma/pil düşüşü raporlanmalı.
- **Kayıt:** FPS, processMs, üçgen/blok sayısı, bellek, pil, sıcaklık, `stall` olayları, `session_end`.

---

## 3. Toplanacak metrikler

| Metrik | Birim | Kaynak | Yorum |
|---|---|---|---|
| Kamera / render FPS | FPS | `getStats`, ARCore | Kamera hızı ile işlem hızı karıştırılmaz |
| Worker işlem süresi | ms/frame | `processMs`, `prep=/fuse=/feed=/upd=/ex=` | Darboğazı adlandırır |
| Event rate | event/s | `event/` | Eşik ve sönümle birlikte raporlanır |
| Motion maliyeti | ms/frame | `motion/` | GL thread bütçesi için |
| Tracking state | enum | ARCore `camera.trackingState` | `TRACKING` / `PAUSED` / `STOPPED` |
| Drift / relokalizasyon | m, ° | Poz süreklilik denetimi | >2.5 m/s şüpheli sıçrama |
| Depth valid oranı | % | `hrDepthValid`, `gate` | Düşük oran sensor kısıtını gösterir |
| Derinlik kaynağı | 0/1/2 | `depthSource` | 0 yok, 1 ARCore, 2 MVS |
| Mesh üçgen / vertex | adet | `triangleCount`, `vertexCount` | Zaman içinde büyüme izlenir |
| Blok / keyframe sayısı | adet | `blockCount`, `keyframeCount` | Bellek ve kapsama göstergesi |
| Bellek | MB | Android profil / `dumpsys` | 10 dk testte eğilim önemli |
| Pil | % | Android battery stats | 10 dk testte tüketim |
| Sıcaklık | °C | `dumpsys batterystats` / termal API | Uzun testte raporlanır |

> Ölçülmemiş hiçbir metrik için sayı yazılmaz. Event/motion maliyeti artık native self-test
> ve oturum logundaki `ev_ms` / `motion_ms` ile ölçülüyor; kalan maliyet optimizasyonu Aşama 8'de takip edilir.

---

## 4. Kabul tanımları

### MVP kabulü (Aşama 1–5 + 8'in ilk kısmı)

- [ ] Kameralı cihazda uygulama açılıp ARCore oturumu başlıyor; izin ve hata yolları çalışıyor.
- [ ] Event View, kamera hareketinde event üretiyor; eşik ayarı çalışıyor; `event_ms` logda.
- [ ] Live ekran yön/hız göstergesi üretiyor; metrik hız ARCore'dan geliyor.
- [ ] Depth View gerçek derinlik + güven gösteriyor; kaynak etiketi doğru.
- [ ] 360° tarama sonrası mesh üretiliyor ve OBJ/PLY dışa aktarılıyor.
- [ ] Oturum logu her çalıştırmada yazılıyor; kapanışta `session_end=clean` var.
- [ ] Test 1, 2, 3, 4, 5, 8 tamamlandı ve belgelendi.
- [ ] Hiçbir ekranda uydurma FPS, ölçek, etiket veya mock veri yok.

### v1.0 kabulü (Aşama 1–8 tamamı)

- [ ] Yukarıdakilere ek olarak 5 ekran ayrımı (Live / Event / Depth / 3D World / Diagnostics) mevcut.
- [ ] Test 6 ve 7 dahil **8 senaryonun tamamı** cihazda geçti; raporlanan açık riskler listeli.
- [ ] Nokta bulutu ↔ mesh geçişi ve kamera yörüngesi görünümü çalışıyor.
- [ ] Mekânsal algılama yalnızca geometrik doğrulanabilir çıktı veriyor; model yoksa "model yok" mesajı görünüyor.
- [ ] Oturum kaydet/yükle çalışıyor.
- [ ] APK üretildi, kurulum notları yazıldı; 10 dk testte `stall` yok, bellek eğilimi stabil (hedef).
- [ ] Fizibilite belgesindeki "asla iddia edilmeyecekler" listesi üründe birebir karşılanıyor.

---

## 5. Bilinçli olarak bu sürümde olmayanlar

- Fiziksel event camera iddiası ve kamera FPS'ini aşan zamansal çözünürlük.
- Eğitilmemiş modeli varmış gibi gösterme; hazır ağırlık yoksa semantik etiket yok.
- Mutlak ölçek garantisi (ARCore metrik takip sağlar; MVS yedeğinde ölçek sınırlıdır).
- Kalıcı / oturumlar arası dünya modeli ve grafik tabanlı global harita optimizasyonu.
- Gaussian Splatting, fotogerçekçi yeniden görüntüleme ve eğitim hattı.
- iOS sürümü; çapraz platform arayüz katmanı.
- Bulut servisleri ve ağ bağımlı işleme.
- Kullanıcıya "nereye bakması gerektiği" yönlendirmesi (next-best-view); rehberlik bilinçli olarak eklenmedi.
