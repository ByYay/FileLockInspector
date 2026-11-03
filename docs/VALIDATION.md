# Doğrulama kaydı — 2026-11-03

## Gerçekleştirilen kontroller

- MSVC 19.44, Windows SDK 10.0.26100 ve CMake ile Release derlemesi başarılı oldu.
- CLI smoke testleri normal/Advanced taramada iki holder tespiti, bir holder kapandığında yenileme ve kullanılmayan dosyada boş sonucu doğruladı.
- Tam CTest çalıştırması test EXE'sine erişim engeli nedeniyle tamamlanamadı; geçti olarak değerlendirilmez.
- PowerShell sözdizimi, manifest XML ve kaynak referansları kontrol edildi.
- Son UI düzenlemesi için görsel/etkileşimli doğrulama yapılmadı. Kurulum/kaldırma, imzalı modern paket, Explorer kararlılığı ve tam kabul matrisi bekliyor.
- Makineye özgü ham günlükler ve performans raporları kaynak dağıtımına eklenmedi.

## Hazır otomatik test kapsamı (çalıştırılması bekleniyor)

`tests/tests.cpp`: Windows argument quote roundtrip (Unicode, boşluk, tırnak, trailing backslash), device path reddi, kendi/system süreç koruması, iki bağımsız holder, RM/Advanced tespiti, hard link kimliği, stale creation time, yanlış executable path, yalnızca test çocuğunun force termination'ı, WM_CLOSE graceful kapanma, exited process reddi, surviving holder refresh, boş sonuç, olmayan dosya, klasör kapsamı, iptal döngüsünde parent handle sayısı, 20 saniyelik takılan worker timeout fixture.

`tests/validate.ps1`: CTest çıktısı + OS/CPU/RAM kaydı; UI launch-to-input-idle, CPU zamanı/peak working set; UI kapandıktan sonra aynı parent PID'ye ait worker kontrolü. `.github/workflows/windows.yml` MSVC x64 build/test/portable artifact hazırlığını tanımlar; henüz çalıştırılmamıştır. CI runner Windows Server'dır; Windows 11 Explorer testinin yerine geçmez.

## Release öncesi zorunlu manuel kapsam

| Senaryo | Durum |
|---|---|
| MSVC Release derleme | Başarılı |
| Debug derleme ve tam CTest | Doğrulanmadı; CTest erişim engeli |
| Tüm kullanıcı onayları, Cancel varsayılanı, force sonucu | Çalıştırılmadı |
| Erişilemeyen/protected/critical process görünümü (sonlandırmadan) | Çalıştırılmadı |
| Unicode, uzantısız dosya, >260 karakter, silme/rename yarışı | Çalıştırılmadı |
| Symlink/junction ve aynı adlı farklı dosyalar | Çalıştırılmadı |
| UNC ve mapped-drive hata/kısıt mesajları | Çalıştırılmadı |
| Modern imzalı MSIX, classic fallback, tekrar install/uninstall, mod geçişi | Çalıştırılmadı |
| Explorer çoklu seçimin gizlenmesi, klasör/root path aktarımı | Çalıştırılmadı |
| Light/dark/high contrast, 100–200% DPI, keyboard/screen reader | Çalıştırılmadı |
| Uzun süreli memory/handle leak, worker crash/forced UI exit | Çalıştırılmadı |
| Explorer menü açılış maliyeti ve COM unload davranışı | Çalıştırılmadı |

## Performans

Sabit RAM veya gecikme hedefi ileri sürülmez. Process Details, worker wall time, CPU zamanı ve peak working set gösterir. `tests/validate.ps1` ölçümleri makine bilgileriyle kaydeder. CPU zamanı CPU yüzdesi değildir; ilgili aralık için `CPU ms / elapsed ms * 100` tek mantıksal çekirdeğe göre ortalama kullanım verir. UI input-idle ölçümü ilk taramanın tamamlanma süresi değildir. Her ikisi ayrı raporlanır.

20 saniye worker timeout ve 64 MiB native snapshot sınırı kaynak/güvenilirlik sınırıdır, ölçülmüş performans sonucu değildir. Kapalı uygulama için startup, service ve scheduled task kaydı üretilmez; çalışan yardımcı process kalmadığı gerçek Windows testinde ayrıca doğrulanmalıdır.
