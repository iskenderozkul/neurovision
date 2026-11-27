# 3Drecon – Sentetik Mesh Self-Test

Bu belge, derleme sonrası mesh kalitesini **sayı ile** kontrol etmek için
eklenen sentetik test düzeneğini anlatır. Amaç, fiziksel telefona ve gerçek bir
odaya bağlı kalmadan her yeni derlemenin öncekiyle karşılaştırılabilmesi.

Kaynak: `app/src/main/cpp/core/mesh_selftest.{h,cpp}`.
Dışarı açılan JNI: `NativeRecon.runMeshSelfTest()`.

## Ne zaman çalışır

Uygulama her açılışında, `MainActivity.onCreate()` içinde **arka plan
iş parçacığında** bir kez. Kamera, ARCore oturumu ve izin gerekmez; ARCore
desteklemeyen bir cihazda da çalışır. Süre emülatörde ~2 s (12 kare, tek
iş parçacığı); telefonda daha kısa.

Canlı taramaya dokunmaz: kendi `TangoRecon`, `VoxelHash`, `TSDFIntegrator` ve
`MeshBuilder` nesnelerini kurar, motorun hacmini kullanmaz.

## Sentetik sahne

Dünya metre cinsinden, OpenCV kamera kuralı (+X sağ, +Y aşağı, +Z ileri):

| Öğe | Konum |
|-----|-------|
| Duvar | `z = 2.0 m`, `\|x\| ≤ 1`, `\|y\| ≤ 1` (2 m × 2 m yama) |
| Kutu | merkez `(0.30, 0.20, 1.20)`, 0.40 m kenar |
| Küre | merkez `(-0.40, -0.20, 1.00)`, r = 0.20 m |
| Delik | derinlik haritasında `x∈[60,100)`, `y∈[40,80)` → ~0.64 m × 0.64 m |

Kamera, duvarı tam görecek şekilde `z = -0.30` çevresinde 0.35 m yarıçaplı
yörüngede 12 kare üretir (160×120 derinlik, `fx = fy = 140`, 320×240 renk).
Her geçerli piksele deterministik ±3 mm gürültü eklenir; gürültü tohumu
cihazdan bağımsızdır, yani sayılar tekrar üretilebilir.

## Ölçülen değerler

| Alan | Anlamı | İyi değer |
|------|--------|-----------|
| `depthcheck valid` | Geçerli derinlik pikseli oranı | ~%65 (sahne + delik ile uyumlu) |
| `depthcheck err_mean/p95` | Sentetik derinliğin analitik yüzeye uzaklığı | < 5 mm (yalnızca eklenen gürültü) |
| `tris` / `verts` | Üçgen ve köşe sayısı | Düz duvar sadeleştirildiği için düşük olabilir |
| `boundary_edges` | Tek üçgen kullanan kenar (açık yüzey sınırı) | Duvar çevresi + delik çevresi kadar |
| `components` | Bağlı bileşen sayısı | Yüzey sayısı kadar (duvar + kutu + küre ≈ 3) |
| `degenerate` | Sıfır alanlı üçgen | 0 |
| `hallucination` | Yüzeyden > 5 cm uzak üçgen | 0 |
| `overlap` | Katı cisim içine giren üçgen | 0 |
| `normal_agreement` | Yüz normali ↔ köşe normali uyumu (1 = tam) | > 0.95 |
| `surf_err_mean/p95` | Köşelerin gerçek yüzeye uzaklığı | < 10 mm / < 20 mm |
| `within3cm` | Köşelerin % kaçı 3 cm içinde | %100'e yakın |
| `bbox` | Mesh sınır kutusu | Duvar `z≈2.0`, `x,y≈±1` |
| `surface_voxels`, `w1`, `w2plus` | Yüzey bandındaki vokseller ve gözlem sayısı | `w1` düşük olmalı (tek gözlemli voksel) |
| `neg`, `pos`, `crossings` | İşaret değiştiren küp sayısı | Duvar alanıyla orantılı |
| `growth` | Kare başına üçgen sayısı | İlk karede oturmalı, sonra yavaş artmalı |
| `wallmap` | Duvarın 10×10 doluluk haritası (20 cm hücre) | Yalnızca delik hücrelerinde `.` |
| `crossmap` | İşaret değişimlerinin aynı haritada dağılımı | `wallmap` ile örtüşmeli |

`wallmap` ve `crossmap` birlikte, "mesh neden seyrek" sorusunu ikiye ayırır:
crossmap dolu + wallmap boşsa sorun **çıkarımda** (mesher), ikisi de boşsa
sorun **birleştirmede** (integrator) olur.

## Çıktı nerede

1. Oturum logunun içinde (`scan_<tarih>.txt`), `selftest ...` satırları.
2. Ayrı dosya: `selftest_<tarih>.txt`, en son 8 rapor tutulur.
   Yol: `/sdcard/Android/data/com.example.mobile3drecon/files/logs/`

```powershell
$adb = "C:\Users\ixend\AppData\Local\Android\Sdk\platform-tools\adb.exe"
& $adb shell ls /sdcard/Android/data/com.example.mobile3drecon/files/logs
& $adb shell cat /sdcard/Android/data/com.example.mobile3drecon/files/logs/selftest_*.txt
```

Android 11+ üzerinde bu dizin kabuk kullanıcısına kapalıdır. Play Store
imajı olmayan (google_apis) emülatörde `adb root` ile okunabilir; telefonda
uygulamanın kendi "Logu paylaş" düğmesi dosyayı dışa aktarır.

## Emülatörde doğrulama

Tango 3DR kitaplığı yalnızca arm için geldiğinden emülatörde `path=tango
unavailable` yazar ve **legacy TSDF yolu** ölçülür. Bu yol üretimde yalnızca
yedek yoldur, ama aynı `MeshBuilder` ve `VoxelHash` kodunu kullandığı için
metrikler ve çıkarım davranışı yine anlamlıdır.

```powershell
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$gradle = "C:\Users\ixend\.gradle\wrapper\dists\gradle-8.13-bin\5xuhj0ry160q40clulazy9h7d\gradle-8.13\bin\gradle.bat"
& $gradle assembleDebug "-PreconEmulator=true"     # yalnızca x86_64, dev derlemesi
```

`reconEmulator` özelliği ABI listesini geçici olarak `x86_64` yapar; normal
derlemede APK yine yalnızca `arm64-v8a` + `armeabi-v7a` içerir.

## Ölçülen taban değerler

Emülatör (x86_64, Android 14, API 34), sürüm 2.10 (12), Tango yolu yok:

```
selftest depthcheck valid=65% err_mean=0.0015 err_p95=0.0032 bad=0%
selftest depthworst frame=1 u=141 v=118 d=2.113 p=0.894,0.873,1.996 err=0.004
selftest path=tango unavailable
selftest path=legacy tris=1225 verts=902 boundary_edges=577 components=5
  degenerate=0 hallucination=0 overlap=1 normal_agreement=0.992
  surf_err_mean=0.0040 surf_err_p95=0.0127 within3cm=100%
  bbox=-0.94..0.94,-0.96..0.94,0.81..2.01 ms=1902
  blocks=213 surface_voxels=3800 w1=96 w2plus=3704 neg=4482 pos=47814 crossings=3192
  growth=0,1004,1028,1051,1095,1182,1232,1213,1248,1270,1301,1259
```

Yorum: derinlik girişi 1.5 mm ortalama hata ile analitik yüzeyde; legacy mesh
duvarı, kutuyu ve küreyi 4 mm ortalama hata ile kuruyor, hiç hayali üçgen yok,
köşelerin tamamı 3 cm içinde. `wallmap` üzerindeki tek boşluklar açılan deliğin
hücreleri. Üçgen sayısının düşük olması beklenen davranış:
`simplifyPlanarPatch` düz duvarı büyük üçgenlere indiriyor.

## Canlı kalite satırı

Üretim hattında (tarama sırasında) her 4 saniyede bir oturum loguna şu satır
düşer; cihazdaki gerçek taramanın sayısal özetidir:

```
meshq tag=tango tris=... verts=... v_per_tri=... degenerate=...%
      edge_mean=... edge_min=... edge_max=... normal_agreement=...
```

- `v_per_tri` ≈ 0.5–0.8 normaldir (kapalı, düzenli yüzey).
- `degenerate` yüzdesi > %1 ise çıkarımda bozulma vardır.
- `normal_agreement` < 0.9 ise normaller karışıyor (bu, `gradAt` onarımının
  cihazdaki karşılığıdır).
- `edge_max` aşırı büyükse (ör. > 0.5 m) sadeleştirici düz olmayan bölgeyi
  yutmuş olabilir.

## Canlı yayın pipeline'ı (v2.11)

`TangoRecon::publishLocked()` artık canlı mesh'i üretmeden önce şu adımları
yapıyor (export `io/exporter.cpp` ile aynı mantık):

1. `weldMesh()` – segment sınırlarındaki tekrar eden köşeler tek bir temsilciye
   kaynaşır, çökme yapan üçgenler düşer, yüzey normali ortalanır.
2. `fillSmallHoles()` – kaybolan kenarları (dropout döküntüsü / dikiş sonrası
   açık kalma) kısa rim'ler için fan-fill ile kapatır (12 cm perimeter, 32 köşe).
3. `dropSmallBlobs()` – ana bileşen dışındaki küçük/parçacıklı kümeleri
   (havada sarkan debris / kopmuş parça) düşürür (12 face / 4 cm eşiği).
4. `recomputeVertexNormals()` – yüzey normalleri incident üçgenlerin ortalaması
   ile yeniden hesaplanır; fill/drop sonrası eklenen/çıkaran üçgenlerle tutarlı
   olur.

Böylece canlı mesh ve export mesh aynı cleanup'ı geçiyor; "kesik", "delikli",
"adacık" ve "havada sarkan" görünümü segment dikişlerinden kaynaklanmıyor.

## Sınırlar

- Sentetik sahne gerçek sensörün kenar uçucu piksellerini (flying pixels),
  yansımaları ve hareketli nesneleri modellemez; bunlar için cihaz taraması
  gerekir.
- `path=tango` satırı yalnızca üretim kitaplığının yüklendiği bir telefonda
  doludur. Emülatörde `unavailable` yazması bir hata değildir.
- Test, ARCore derinlik API'sinin kendi filtrelerini (confidence, smoothed
  depth) devre dışı bırakır; onların etkisi canlı `dense ...` satırlarında
  görülür.
