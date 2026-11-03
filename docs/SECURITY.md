# Güvenlik kararları

Uygulama standart kullanıcıyla başlar. Servis, UAC bypass, SeDebugPrivilege, driver, otomatik elevation veya ağ istemcisi yoktur. Kullanıcının UNC seçimi Windows tarafından ağ erişimine neden olabilir; bu, telemetri değildir.

Shell komutu kabuk komut satırı (`cmd`, PowerShell) kullanmaz. CreateProcessW'nin applicationName parametresi kesin executable yoludur; her argüman backslash/quote kurallarına göre escape edilir. Çalışma dizini executable seçmek için kullanılmaz. GUI ve worker başlangıçta DLL arama dizinini System32 ile sınırlar. Load-time bağımlılıklar Windows sistem kütüphaneleridir, CRT statiktir. Shell DLL Explorer'ın arama politikasını değiştirmez. Uygulama dizini mevcut kullanıcı tarafından değiştirilebilir; aynı kullanıcı tarafından binary değişikliğine karşı sandbox/security boundary iddiası yoktur.

Süreç eylemleri için PID, creation time, executable full path ve basename yeniden doğrulanır. Handle onay penceresi boyunca tutulur ve onaydan sonra tekrar kontrol yapılır. IsProcessCritical ve GetProcessInformation(ProcessProtectionLevelInfo) başarısızsa güvenli tarafta kalınır. Windows dizinindeki executable'lar, session 0, farklı kullanıcı/oturum, kendi süreç ve erişilemeyen süreçler reddedilir. Adına göre allowlist kullanılmaz. Bu koruma politikası uygulama kapanmasını kısıtlayabilir; hiçbir yerel politika her kullanıcı uygulamasının iş önemini bilemez.

End Process yalnızca sahip olunan top-level pencerelere asenkron WM_CLOSE gönderir. Uygulama isteği reddedebilir. Force Terminate bağımsız kullanıcı onayıyla tek process handle'ına uygulanır; process tree hedeflenmez. Erişim reddi raporlanır, otomatik elevation yapılmaz. Çıkış process handle'ın signaled olmasıyla doğrulanır; yalnızca TerminateProcess dönüşü başarı kabul edilmez.

Worker tüm potansiyel bloklanan file/handle sorgularını ana UI'dan ayırır. Job Object kill-on-close kullanır. Native ABI değişikliği, erişim reddi, olmayan dosya veya identity alınamaması hata/incomplete sonucudur; kontrol atlanarak kesin eşleşme üretilmez. Silinmiş/taşınmış dosyalar için tutulan target handle tarama başındaki nesneyi temsil eder; tarama sırasında yolun yeni bir nesneyle değiştirilmediği garantisi verilmez.

## Referanslar

- [RmGetList sözleşmesi ve hata kodları](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmgetlist)
- [RmRegisterResources ve kaynak kaydı](https://learn.microsoft.com/en-us/windows/win32/api/restartmanager/nf-restartmanager-rmregisterresources)
- [IsProcessCritical](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocesscritical)
- [Microsoft: modern Explorer menüsü ve sparse package](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer)

Bu referanslar desteklenen API/entegrasyon modelini açıklar; projedeki uygulamanın çalıştığının test kanıtı değildir.
