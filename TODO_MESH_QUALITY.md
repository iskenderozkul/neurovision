# 3Drecon – Mesh Kalitesi TODO ve Onarım Raporu

Bu belge, bildirilen yedi mesh sorununu kod düzeyindeki kök nedene bağlar,
yapılan onarımı ve doğrulama yöntemini listeler. Tüm değişiklikler
`app/src/main/cpp` altındaki üretim hattındadır; `repos/3Dify` ve
`repos/OpenScan3` değiştirilmemiştir.

> **Doğrulama sınırı:** Bu ortamda ARCore derinlik sensörlü fiziksel telefon
> yok. Aşağıdaki düzeltmeler derleme ve kod muhakemesi ile doğrulanabilir;
> eşiklerin son ayarı (özellikle `kRelocalisationJumpM`, makro medyan eşiği ve
> `agreeTol`) cihazda taranmış bir oda + yakın çekim nesne ile teyit
> edilmelidir.

## Durum özeti

| # | Belirti | Kök neden | Durum |
|---|---------|-----------|-------|
| 1 | Hayali / havada mesh | Tutulan (held) tahminler ve bilateral delik doldurma Tango hacmine besleniyordu | **Onarıldı** |
| 2 | Çok pürüzlü yüzey | `gradAt` geçersiz komşu örnekleri `tsdf = 1.0` olarak fark ediyordu; blok sınırlarında sahte eğim/normal | **Onarıldı** |
| 3 | Komşu olmayan / kopuk mesh | Bekleme kuyruğu tavanı hücreleri kalıcı olarak düşürüyordu | **Onarıldı** |
| 4 | Boşluklu mesh | Aynı kuyruk kaybı + düşük çözünürlüklü makro tarama | **Onarıldı** (kuyruk + profil) |
| 5 | Üst üste binen / çift katman | Geç derinlik haritası eşiği (20 cm) ve held tahminin Kalman önsavı olması | **Onarıldı** |
| 6 | Anchor kaynaklı kayma/çift mesh | Anchor'lar yalnızca UI işareti; asıl neden relokalizasyonda dünya çerçevesinin sıçraması | **Onarıldı** (hacim sıfırlama) |
| 7 | Makro / yakın çekimde kötü mesh | 3 cm vokseller ve 6 m pencere küçük nesneyi çözemiyor; `applyProfile()` hiç çağrılmıyordu | **Onarıldı** (otomatik 1.5 cm profil) |

---

## 1. Hayali mesh (phantom geometry)

**Kök neden**

`Engine::buildFusionFeed()` Tango 3DR'ye `feedDepthMm_` gönderiyor. Bu grid iki
kaynaktan sentetik derinlik alıyordu:

1. Sensörün düşürdüğü pikseller için önceki kareden taşınan "held" tahmin.
   (Bu zaten `fusedAge_ > 0` kontrolüyle sıfırlanıyordu.)
2. Renk-güdümlü bilateral delik doldurma. Doldurulan piksel `feedDepth_` ve
   dolayısıyla `feedDepthMm_` içine yazılıyor, Tango traversal güncellemesine
   gerçek ölçüm gibi giriyordu. Parlak/vernetli yüzeylerde sensörün hiç
   görmediği bir noktaya yüzey uyduruluyordu.

**Onarım**

- `Engine::buildFusionFeed()` içinde `synthesized` maskesi eklendi.
- Doldurulan piksel sayısı (`filled`, teşhis için) korunuyor; ancak değer
  yumuşatma komşularına ve milimetre gridine girmeden önce sıfırlanıyor.
- Sonuç: sensörün ölçmediği hiçbir piksel hacme girmiyor. Gerçek delik,
  hayali yüzeyden daha iyidir; kullanıcı bölgeyi tekrar taradığında kapanır.

**Dosya:** `core/engine.cpp` (`buildFusionFeed`)

**Doğrulama:** Cihazda parlak masa/tabak yüzeyi taranırken `fill=` oranı
yüksek olsa bile mesh'te havada tabaka oluşmamalı. Delikler görünür kalabilir;
bu beklenen davranıştır.

---

## 2. Çok pürüzlü mesh

**Kök neden**

`mesh/mesher.cpp::extractBlock()` içindeki `gradAt` merkezi farkı, yumuşatılmış
alan dizisindeki **geçersiz** örnekleri de okuyordu. Geçersiz `FieldSample`
varsayılan olarak `tsdf = 1.0` taşır. Gerçek bir ölçüm (ör. `-0.02`) ile
varsayılan `1.0` farkı, tek voksellik mesafede neredeyse tam truncation bandı
kadar sahte eğim üretiyor; bu da blok sınırlarında ve delik kenarlarında
normalin kırılmasına, headlight gölgelendirmede çizgi/çivi görünümüne yol
açıyordu.

**Onarım**

- `gradAt` artık yalnızca `valid == true` örnekleri fark ediyor.
- Eksik taraf varsa yarım ağırlıklı tek taraflı fark kullanılıyor; iki taraf da
  yoksa o eksen sıfır katkı veriyor.
- Böylece komşu blok aynı voksel için aynı normali üretmeye devam ediyor
  (dikiş kaynaşması korunuyor) ama geçersiz veri normali bozamıyor.

**Dosya:** `mesh/mesher.cpp` (`gradAt`)

**Doğrulama:** Düz duvar/taban taramasında blok sınırlarına denk gelen parlak
çizgiler ve yüzeydeki iğne benzeri normaller kaybolmalı.

---

## 3. Komşu olmayan (kopuk) mesh

**Kök neden**

`TangoRecon::integrate()` bekleme kuyruğu `kMaxPending = 1536` sınırını aşınca
en uzak çeyreği atıyordu. Atılan hücreler `pendingSet_`'ten de silindiği için
Tango çekirdeği aynı hücreyi yeniden bildirene kadar **kalıcı olarak**
çıkarılmıyordu. Hızlı bir süpürmede duvarın bir parçası hiç mesh'e girmiyor,
komşu segmentlerle birleşmeyen adacıklar oluşuyordu.

**Onarım**

- Atılan hücreler `deferred_` kuyruğuna (en fazla 8192) park ediliyor.
- Canlı kuyruk 384'ün altına indiğinde kare başına 96 hücre geri alınıyor.
- Bu hücreler Tango hacminde zaten mevcut; yeniden çıkarma yalnızca önbelleğe
  kopyalıyor, yani boşluk kapanıyor.
- `reset()` / `applyProfile()` `deferred_` kuyruğunu da temizliyor.

**Dosya:** `tango/tango_recon.cpp`, `tango/tango_recon.h`

**Doğrulama:** Telefonu hızlıca bir duvar boyunca süpürüp durun; tarama
sonunda duvarda eksik kalan şerit birkaç saniye içinde kendiliğinden
dolmalı.

**Kalan TODO:** `evictSegments()` bellek bütçesi için uzak hücreleri hâlâ
düşürüyor. Bunları hemen geri koymak bütçe ile çekişir; şimdilik yalnızca kamera
o bölgeye döndüğünde geri geliyorlar. Bellek baskısı ölçülürse ayrı bir
"soğuk önbellek" (diskte/seyrek yapıda) tasarlanmalı.

---

## 4. Boşluklu mesh

**Kök neden**

- 3. maddedeki kalıcı kuyruk kaybı.
- Yakın çekimde 3 cm voksel: küçük nesnenin ince kısımları (kulp, yaprak,
  kenar) tek voksellik yüzey üretemiyor.
- `prepareDepthGrid()` içindeki sabit `kSmoothAgreeM = 0.09f`, 3 cm vokselde
  9 cm'lik gerçek bir basamağı "uyumlu" sayıp yumuşatılmış haritayla
  köprüleyebiliyordu; bu da hem yanlış yüzey hem de sonraki carving'de delik
  olarak geri dönüyordu.

**Onarım**

- Kuyruk için 3. maddedeki `deferred_` mekanizması.
- `kSmoothAgreeM` yerine menzile ölçekli tolerans:
  `min(0.06, max(0.03, 0.02 * z))`. 1 m'de 3 cm, 3 m'de 6 cm. Gerçek
  basamaklar yumuşatılmış haritayla köprülenmiyor.
- Makro profilde 1.5 cm voksel (7. madde).

**Dosyalar:** `tango/tango_recon.cpp`, `core/engine.cpp`
(`prepareDepthGrid`)

**Doğrulama:** İnce bir nesne (fincan kulpı, bitki yaprağı) yakın mesafeden
taranırken hem nesne hem arkası ayrı yüzeyler olarak kalmalı; aralarında zar
oluşmamalı.

---

## 5. Üst üste binen mesh (çift katman)

**Kök neden**

İki bağımsız hata aynı belirtiyi üretiyordu:

1. **Geç derinlik haritası.** `processDepthFrame()` yalnızca
   `displacement > 0.20 m` olunca kareyi atıyordu. 3 cm vokselde bu 6 voksel
   demek; eski pozla gelen harita gerçek yüzeyin yanına ikinci bir katman
   olarak işlenebiliyordu.
2. **Held tahminin önsav olması.** `fuseDepth()` önceki kareyi warplarken
   `fusedDepth_` içindeki held (sensörün ölçmediği, taşınmış) değerleri de
   önsav olarak kullanıyordu. Yeni bir ölçüm geldiğinde Kalman harmanı bu
   sentetik önsava doğru çekiyor, gerçek yüzeyin birkaç santim üstünde ince
   bir kabuk bırakıyordu.

**Onarım**

- Bütçe artık aktif çözünürlüğe bağlı:
  `maxDisplacement = max(0.035, 2.5 * tango_.resolution())` → 3 cm'de 7.5 cm,
  1.5 cm'de 3.75 cm.
- `predMeasured_` eklendi. Warp sırasında önsavın gerçek ölçümden mi
  (`fusedAge_ == 0`) yoksa held değerden mi geldiği işaretleniyor.
- Ölçüm dalında Kalman harmanı yalnızca `predValid_ && predMeasured_` ise
  yapılıyor; held önsav varsa ölçüm doğrudan yazılıyor ve yaş sıfırlanıyor.

**Dosyalar:** `core/engine.cpp` (`processDepthFrame`, `fuseDepth`),
`core/engine.h`

**Doğrulama:** Telefonu yavaşça yürürken masaya bakın; masa yüzeyi tek
katman olmalı, altında ikinci bir "hayalet masa" kalmamalı.

---

## 6. Anchor kaynaklı mesh sorunu

**Önce netleştirme:** `app/src/main/java/com/example/ar/ReconGlRenderer.kt`
içindeki anchor'lar yalnızca dünya koordinatında işaretçi çiziyor
(`NativeRecon.render(..., anchorPositions)`). Mesh üretimine, TSDF'ye veya
poza doğrudan etkileri yok. `centerDepthM` yalnızca anchor'ın yerleştirileceği
yüzeyi seçmek için 15×15 ham derinlik medyanı.

**Gerçek kök neden:** Anchor yerleştirme/takip düzeltmesi sırasında ARCore
dünya çerçevesini büyük bir sıçramayla düzeltebiliyor. Eski kod bunu yalnızca
"hızlı hareket" sayıp 3 kare atlıyor, sonra yeni pozla taramaya devam ediyordu.
Eski hacim eski koordinatta, yeni gözlemler yeni koordinatta kalınca aynı duvar
iki kez görünüyordu.

**Onarım**

- `processFrame()` artık hız eşiğini (`2.5 m/s`) koruyor; ek olarak tek karede
  `frameJump >= 0.75 m` ise bunu gerçek hareket değil **relokalizasyon** kabul
  ediyor.
- Bu durumda `performReset(false)` çağrılıyor: hacim yeni dünya çerçevesinde
  sıfırdan kuruluyor. `false` parametresi makro/oda profilini korur; kullanıcı
  resetinde profil yeniden oylanır.
- Ardından 3 karelik oturma penceresi uygulanıyor ve olay
  `relocalisation=volume_reset` olarak loglanıyor.

**Dosyalar:** `core/engine.cpp` (`processFrame`, `performReset`),
`core/engine.h`

**Bilinen ödün:** 0.75 m'den büyük bir sıçrama gerçekten kullanıcı hareketi
ise (çok hızlı savurma) tarama sıfırlanır. Bu, çift koordinatlı mesh'e tercih
edildi. Cihazda gerekirse eşik yükseltilebilir.

**Kalan TODO:** ARCore `TrackingState` geçişlerini (PAUSED → TRACKING) Kotlin
tarafında da loglayıp native'e iletmek, relokalizasyonu sıçrama tahminine
bırakmadan kesinleştirir.

---

## 7. Makro / yakın çekim mesh sorunu

**Kök neden**

Tango 3DR `init()` varsayılanı 3 cm voksel, 6 m `max_depth`, 0.20 m
`min_depth`. `TangoRecon::applyProfile()` kodu vardı ama **hiçbir çağrısı
yoktu**. 30-80 cm mesafedeki bir nesne 3 cm'lik ızgarada birkaç voksale
sıkışıyor; kulp, kenar ve doku kayboluyordu. Ayrıca `reset()` profili
sıfırlıyordu (`init()`), yani profil eklenmiş olsa bile her reset onu
silecekti.

**Onarım**

- `Engine::maybeSelectProfile()` eklendi. İlk 20 kullanılabilir derinlik
  karesinin medyanına bakar:
  - medyan `0.10–0.85 m` → **makro**: 1.5 cm voksel, `max_depth = 3.0`,
    `min_depth = 0.15`.
  - aksi halde **oda**: 3 cm / 6 m / 0.20 m.
- Karar, hacim henüz neredeyse boşken verilir; `applyProfile()` Tango
  context'ini yeniden kurar, bu yüzden erken çağrılır.
- `TangoRecon::init/applyProfile` artık `minDepth` parametresi taşıyor ve
  `integrate()` nokta bulutunu bu eşikle süzüyor.
- `TangoRecon::reset()` artık `init(resolution_, maxDepth_, minDepth_)`
  kullanıyor; makro profil resetten sonra korunuyor.
- Kullanıcı resetinde (`performReset(true)`) profil oylaması sıfırlanır, yani
  bir sonraki tarama oda veya makro olabilir.

**Dosyalar:** `core/engine.cpp`, `core/engine.h`, `tango/tango_recon.cpp`,
`tango/tango_recon.h`

**Doğrulama:** Telefonu küçük bir nesneye 30-50 cm mesafede tutup taramaya
başlayın; `scan.txt` içinde `profile=macro median=...` görünmeli. Aynı nesne
makro profilde 3 cm profilden belirgin şekilde daha fazla detay taşımalı.

**Kalan TODO:** Makro eşiği (0.85 m) ve `min_depth` (0.15 m) cihazda
doğrulanmalı. ARCore Depth API'nin güvenilir alt sınırı cihazdan cihaza
değişiyor; bazı telefonlarda 0.15 m gürültülü olabilir.

**Kullanım notu:** Profil ilk 20 kullanılabilir karede bir kez seçilir. Bir
oda taramasına telefonu duvara 0.85 m'den daha yakın tutarak başlarsanız
yanlışlıkla makro profil seçilebilir (max_depth 3 m'ye düşer). Oda taraması
için ilk saniyede telefonu hedeften uzak tutun; yanlış seçim olduysa
uygulamayı yeniden başlatın. Kalıcı çözüm, kullanıcıya "Oda / Makro" seçimi
sunan bir anahtar eklemektir (TODO).

---

## 8. Derleme / ortam notları

- Proje yolu ASCII olmadığı için (`G:\3d ölçüm deepseek`) AGP Windows'ta
  derlemeyi reddediyordu. `gradle.properties` içine
  `android.overridePathCheck=true` eklendi.
- Gradle 8.11.1 dağıtımı bu makinede indirilemedi (10 sn wrapper timeout).
  Derleme, önbellekte bulunan Gradle 8.13 ile yapıldı:
  `C:\Users\ixend\.gradle\wrapper\dists\gradle-8.13-bin\...\gradle.bat`.
  AGP 8.8.0 ile uyumludur. Kalıcı çözüm için `gradle-wrapper.properties`
  içindeki `networkTimeout` artırılabilir veya 8.11.1 zip'i önden indirilir.
- `local.properties` içinde `sdk.dir` bu makineye ayarlandı (depoya girmez).

## 9. Cihazda yapılacak son doğrulamalar (TODO)

- [ ] Aynı oda ve aynı küçük nesne, düzeltme öncesi/sonrası APK ile taranıp
      yan yana konmalı.
- [ ] `scan.txt` içinde şu sayaçlar izlenmeli: `fill=` (artık baskılanan),
      `hold=`, `plane=`, `smooth=`, `rel=`, `ang=`, `profile=`,
      `relocalisation=volume_reset`.
- [ ] Relokalizasyon sıçrama eşiği (0.75 m) ve makro medyan eşiği (0.85 m)
      gerçek kullanımda ayarlanmalı.
- [ ] Çok uzun taramalarda `evictSegments()` sonrası geri dönüş davranışı
      ölçülmeli; gerekirse soğuk önbellek tasarlanmalı.
- [x] Canlı önizlemede segment dikişlerinde normal sürekliliği için
      `publishLocked()` çıktısına isteğe bağlı weld + normal ortalama
      eklenmeli (export zaten `io/exporter.cpp` içinde weld ediyor).
      → **Yapıldı (v2.10):** `TangoRecon::weldMesh()` yayın adımına
      eklendi; segment sınırlarındaki tekrar eden köşeler tek bir temsilciye
      kaynaşıldı ve yüzey normali ortalaması alınıyor. Kesintili / adacık
      görünüm segment dikişlerinden kaynaklanıyordu.
- [x] Canlı önizlemede dropout kenarlarını (real kesilme / segment dikişi
      sonrası açık kalma) kapatma. → **Yapıldı (v2.11):**
      `TangoRecon::fillSmallHoles()` `publishLocked()` akışına eklendi;
      export (`io/exporter.cpp`) zaten kullanulan aynı kuralı (kısa rim
      fan-fill, 12 cm perimeter, 32 köşe limiti) uyguluyor. Böylece canlı
      mesh ve export mesh aynı cleanup'ı geçiyor.
- [x] Canlı önizlemede havada askıda kalan / parçacıklı (debris) üçgenleri
      temizle. → **Yapıldı (v2.11):** `TangoRecon::dropSmallBlobs()`
      `publishLocked()` akışına eklendi (export ile aynı kural: ana bileşen +
      12 face / 4 cm genişlik eşiği). "Havada sarkan" mesh parçaları azalır.
- [x] Yayın meshinde Taubin λ|μ smoothing. → **İptal (devre dışı, v2.11):**
      `smoothMesh()` kodu `publishLocked()` içinde `kEnableTaubinSmooth=false`
      ile devreden çıkarıldı. ~200k üçgenli meshlerde worker thread'i boğup
      `ms≈1.5-1.9 s`, fps=27→1-10'a düşürdü. Live yüzey
      `weld + fill + drop + recompute` ile çalışıyor.

---

## 10. Sentetik mesh self-test (sayısal kontrol)

Cihaz gerektirmeyen bir doğrulama düzeneği eklendi; ayrıntı `SELFTEST.md`.

- Kaynak: `app/src/main/cpp/core/mesh_selftest.{h,cpp}`,
  JNI: `NativeRecon.runMeshSelfTest()`.
- Her açılışta arka planda bir kez çalışır (kamera/ARCore/izin gerekmez, canlı
  taramaya dokunmaz) ve sonucu hem oturum loguna hem
  `logs/selftest_<tarih>.txt` dosyasına yazar.
- Sahne: 2 m × 2 m duvar (içinde ~0.64 m delik) + 0.4 m kutu + 0.2 m yarıçap
  küre; 12 kare, 160×120 derinlik, ±3 mm deterministik gürültü.
- Ölçümler: `surf_err_mean/p95`, `within3cm`, `hallucination`, `overlap`,
  `components`, `degenerate`, `normal_agreement`, `bbox`, ayrıca duvarın
  10×10 `wallmap` doluluk haritası ve `crossmap` işaret değişimi haritası.
- Bu düzenek, yedinci maddedeki onarımların yan ürünü olarak **gerçek bir
  ölçüm hatasını** da düzeltti: sahne SDF'i duvarı dolu yarı-uzay olarak
  modellediği için duvarın önündeki her nokta "1.2 m hata" sayılıyordu; ölçüm
  artık yüzeye olan mesafeyi kullanıyor.
- İlk taban ölçüm (emülatör, Tango yolu yok, legacy TSDF):
  `surf_err_mean=4 mm`, `surf_err_p95=12.7 mm`, `within3cm=%100`,
  `hallucination=0`, `components=5`, `normal_agreement=0.992`;
  `wallmap` üzerindeki tek boşluklar açılan deliğin hücreleri.
- Cihazda karşılaştırma için: aynı senaryoyu eski ve yeni APK ile açıp
  `selftest_*.txt` satırlarını yan yana koymak yeterli. `path=tango` satırı
  yalnızca üretim kitaplığının yüklendiği telefonda dolar.
- [ ] Telefonda ilk çalıştırmada `path=tango` satırını kaydet ve bu taban
      değerleri `SELFTEST.md` içine işle.
- [ ] Canlı `meshq tag=...` satırlarını (4 s'de bir) uzun bir oda taramasında
      toplayıp `normal_agreement` ve `degenerate` eşiklerini doğrula.
