# Mimari

- `src/core.*`: Restart Manager RAII session, native handle snapshot ve dosya kimliği. Dosya içeriği okunmaz. Normal taramada RM'nin kendi registry işlemleri olabilir.
- `src/process.cpp`: Süreç kimliği, SID, koruma politikası, WM_CLOSE ve TerminateProcess. Sahip adı çözümlemesinin domain/network beklemesini önlemek için kullanıcı SID olarak gösterilir. Session 0 için System / Service ayrımı kesin değil; RM Service türü mevcutsa kullanılır.
- `src/scan_job.*`: UI tarafından başlatılan, anonim pipe üzerinden sabit boyutlu/version-tagged kayıt üreten tek worker. Suspended oluşturma → Job Object ataması → resume. Yalnızca output pipe inherit edilir. Job kill-on-close, maksimum bir process; tarama başına 20 saniye operasyonel sınır, 50 ms UI polling. Sürekli arka plan işi yoktur.
- `src/ui.cpp`: Liste, yeniden tarama, iptal, details, clipboard ve onay akışı. Ağ/dosya sorguları worker'dadır. Open location kullanıcının açık eylemiyle Shell API çağırır. Native tema/renkler, high contrast ve PerMonitorV2 DPI kullanılır; undocumented dark-mode fonksiyonları kullanılmaz. Native düğme/header'ların görünümü Windows temasına bağlıdır.
- `shell/ExplorerCommand.cpp`: COM factory ve IExplorerCommand. GetState yalnızca seçim sayısını kontrol eder. Invoke filesystem yolunu alır, CRT kurallarına göre quote eder ve kesin executable yoluyla CreateProcessW çağırır. Explorer içinde tarama, süreç sonlandırma, native enumeration yoktur.
- `installer/`: Mevcut kullanıcı için birbirini dışlayan classic COM registry ve modern sparse package kaydı. DLL unload davranışı Windows'a aittir.

## Tarama semantiği

Restart Manager `ERROR_MORE_DATA` değişimlerine en fazla sekiz denemeyle yanıt verir; listeler sınırlıdır. Session normal ve hata dönüşlerinde RAII ile kapanır. Worker zorla iptal edildiğinde C++ destructor çalışamaz; süreç kaynaklarını OS temizler. RM session'ın forced-death sonrası davranışı Windows üzerinde doğrulanmalıdır; uygulama bu durumda RmEndSession çağrıldığını iddia etmez.

Advanced Scan runtime'da ntdll export'unu arar. SystemExtendedHandleInformation (64) x64 düzeni ayrı modüldedir. Snapshot en fazla 64 MiB; boyut ve sayı kontrolleri yapılır. File object type indeksi worker'ın kendi target handle'ından türetilir. Sadece File tipindeki adaylar duplicate edilir; pipe ve disk olmayan nesneler sorgulanmaz. File identity yalnızca volume GUID eşleştirmesiyle kabul edilir. Duplicate edilen handle uygulamaya aittir ve RAII ile kapanır; kaynak process handle'ı kapatılmaz.

Handle snapshot sonrası handle numarası yeniden kullanılabilir. Duplicate edilen nesne yeniden kimlik kontrolünden geçtiğinden artık başka dosyaya ait olan aday kabul edilmez. Süreç oluşturulma zamanı açık process handle'ından alınır. Sonuç, tarama anındaki nesne kimliğinin kanıtıdır; bir sharing violation kanıtı değildir. Süreçler dosya eşlemesinden veya erişim kısıtından dolayı eksik kalabilir.

Bir sorgu takılırsa bütün worker sonlandırılır; önceden iletilen kayıtlar korunur, sonuç incomplete'dir. Her handle için ayrı worker oluşturulmaz. Sonuçlar PID + creation time ile birleştirilir. Yeni tarama eski snapshot'ı değiştirir; eskimiş RM/Advanced kayıtları birlikte tutulmaz.

## Kapsam sınırları

Tek seçim; klasörün kendisi; yerel disk üzerinde Advanced Scan. Çoklu dosya ve recursive klasör taraması bilinçli olarak kapalıdır. Ağ kullanımının tümünü kanıtlama, memory-mapped files için eksiksiz tespit, driver, debug privilege ve handle-force-close yoktur. Dosya/süreç için Windows stock ikonları kullanılır; dosyaya veya executable'a özel ikon yükleme yoktur. Gerçek Windows 11 Explorer/DPI/theme doğrulaması beklemektedir.
