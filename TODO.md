# NeuroVision — TODO

Amaç: `aim.md`'deki sistemi Android'de çalışan, ölçülebilir ve bilimsel olarak dürüst bir
uygulamaya dönüştürmek. Referans taban: `güncel/3Drecon` (ARCore + Tango 3DR TSDF).
Hiçbir aşamada mock kamera verisi gerçek veri gibi sunulmaz.

## Aşama 0 — İskelet (tamamlandı)
- [x] aim.md ve referans projeyi incele
- [x] Android projesini `NeuroVision` adıyla türet (paket: `com.neurovision`)
- [x] Temel derlemeyi doğrula (`assembleDebug` başarılı)
- [x] Fizibilite / mimari / yol haritası dokümanları (`docs/`)

## Aşama 1 — Kamera + Event Simülasyonu (tamamlandı)
- [x] Native event motoru: log-parlaklık eşiği, DVS tarzı referans takibi
- [x] Pozitif/negatif polarity, event hızı, yoğunluk, büyüklük istatistikleri
- [x] Event önizleme tamponu (ARGB) + eşik ve sönüm ayarı
- [x] Kamera karesinden (Y düzlemi) event üretiminin GL thread'ine bağlanması
- [x] Event View ekranı: canlı event haritası, eşik slider'ı, sayaçlar
- [x] Başarı kriteri: kamera hareket ederken hareketli kenarların event haritasında belirginleşmesi

## Aşama 2 — Hareket Algılama (tamamlandı)
- [x] Native optik akış: 4 seviyeli görüntü piramidi + Lucas-Kanade
- [x] Köşe tespiti (grid tabanlı Shi-Tomasi/Harris), takip başarı oranı
- [x] Medyan akış (dx, dy), px/s hız, baskın yön (sol/sağ/yukarı/aşağı)
- [x] ARCore 6DoF pozundan gerçek kamera hızı (m/s, °/s)
- [x] Live ekranında akış vektörleri ve hareket yönü göstergesi
- [x] Başarı kriteri: telefon sağa/sola/yukarı/aşağı hareket ettiğinde yön ve hızın doğru işaretlenmesi
> **Motion bug düzeltildi:** 4-seviyeli Lucas-Kanade bir adımında gradyan önceki
> kareden (`prev`) hesaplanıyordu ve her iki görüntü de kaydırılmış konumlarda
> örnekleniyordu; hareket eden içerikte bu, eşleşmeyi yanlış konuma doğru kaydırdığı
> için akış/px-s/yön tahminini bozuyordu. Artık arama (geçerici kare) `cur` üzerinden
> gradyan alınıp, `prev` özgün köşe konumunda, `cur` izlenen konumda örnekleniyor.
> Bu düzeltmeden sonra native perception self-test **6/6 PASS**.

## Aşama 3 — Derinlik (tamamlandı)
- [x] ARCore Depth API (raw + confidence + smoothed) entegrasyonu (referanstan)
- [x] Cihaz desteklemiyorsa çoklu-görüş stereo (MVS) yedeği (referanstan)
- [x] Derinlik ekranı: renkli heatmap, güven haritası, geçerli piksel oranı
- [x] Derinlik kaynağını (ARCore / MVS) ve gecikmeyi dürüstçe gösterme
- [x] Başarı kriteri: yakın/uzak yüzeylerin ayrışması ve güvenin görünmesi

## Aşama 4 — Visual-Inertial SLAM (tamamlandı)
- [x] ARCore 6DoF takip + kamera pozu (referanstan)
- [x] Takip durumu, relokalizasyon ve poz sürekliliği denetimi (referanstan)
- [x] Kamera yörüngesinin 3D görünümde çizilmesi
- [x] Başarı kriteri: oda içinde hareket ederken konum/yönelimin stabil kalması

## Aşama 5 — 3D Haritalama (tamamlandı)
- [x] Tango 3DR TSDF + Marching Cubes, renkli mesh (referanstan)
- [x] Yedek TSDF + mesh üretimi (referanstan)
- [x] OBJ / PLY dışa aktarma (referanstan)
- [x] Nokta bulutu görünümü (mesh köşelerinden, GL_POINTS)
- [x] 3D World ekranı: serbest yörünge kamerası, döndürme/yakınlaştırma, mesh ↔ nokta bulutu
- [x] Başarı kriteri: oda taramasından geometrik 3D harita ve dışa aktarılabilir dosya

## Aşama 6 — Mekânsal Algılama (dürüst kapsam)
- [ ] Derinlik süreksizliğinden geometrik yüzey kümeleri (engel blokları) + 3D merkez/mesafe
- [ ] Nesne tespiti için takılabilir TFLite arayüzü; model yoksa "model yok" demek
- [ ] Hareketli yüzey maskesi (referanstan) ve dinamik katman
- [ ] Başarı kriteri: geometrik olarak doğrulanabilir mesafe/konum bilgisi; uydurma etiket yok

## Aşama 7 — İleri Görselleştirme
- [ ] Oturum kaydı ve yeniden yükleme (mesh + kamera yörüngesi)
- [ ] Gaussian Splatting araştırma notu (bu sürümde uygulanmıyor)
- [ ] Dışa aktarılan modeli uygulama içinde yeniden açma

## Aşama 8 — Test ve Raporlama
- [ ] Diagnostik ekranı: FPS, event rate, SLAM durumu, gecikmeler, bellek
- [ ] Sabit / yatay / dikey / ileri-geri / 360° / ışık / dinamik / 10 dk test protokolü
- [ ] Her modül için ölçülen sürelerin oturum loguna yazılması
- [ ] APK üretimi ve kurulum notları

## Bilinçli olarak yapılmayanlar
- Fiziksel event camera iddiası (yazılımsal simülasyon)
- Kameranın FPS'inin üzerinde yeni bilgi üretme iddiası
- Eğitilmemiş bir modeli varmış gibi gösterme; hazır ağırlık yoksa semantik etiket yok
- Mutlak ölçek garantisi (ARCore metrik takip sağlar; MVS yedeğinde ölçek sınırlıdır)
