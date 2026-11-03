# Rozwiązywanie problemów

[Dokumentacja](index.md) › Rozwiązywanie problemów

Ta strona pomaga, gdy coś nie działa. Zaczyna się od najczęstszych problemów, a następnie wymienia wszystkie kody błędów.

## Sposób wyświetlania błędów

Każdy błąd ma krótki komunikat, prawdopodobną przyczynę i sugestię.

- **Aplikacja desktopowa (`hwio-monitor`):** panel błędu pokazuje komunikat, **Probably:** (przyczynę) i **What to do:** (sugestię) oraz przycisk następnego kroku, na przykład **Reconnect**, **Open Settings** lub **Retry**. **Details** pokazuje kod błędu (na przykład `probe.not_found`) i szczegóły techniczne; **Copy** je kopiuje.
- **CLI (`hwio`):** błąd jest wypisywany na stderr:

  ```text
  Error: No J-Link probe found.
  Cause: The USB cable is disconnected or the J-Link is not powered.
  What to do: Check the USB cable between the J-Link and the computer.
  Details: <technical details>
  ```

  Polecenie kończy się [kodem wyjścia](../en/cli.md#exit-codes) kategorii błędu.
- **CLI z `--json`:** wyjście to jeden wiersz z `"ok":false` i obiektem `error`. Jego pola to `code` (kod błędu), `category`, `message`, `cause`, `suggestion` i `detail`. Zobacz [JSON output](../en/cli.md#json-output).

Każdy kod błędu poniżej ma kotwicę równą kodowi, więc można do niego linkować bezpośrednio, na przykład `troubleshooting.md#probe.not_found`.

**Zanim poprosisz o pomoc:**

- Uruchom `hwio doctor`. Sprawdza J-Link Software, nRF Util, dostęp USB do J-Link (Linux), inne programy używające J-Link, podłączone sondy i ich firmware, foldery ustawień i logów oraz Bluetooth.
- Utwórz raport diagnostyczny: w aplikacji desktopowej wybierz **Help** › **Create diagnostic report…** lub uruchom `hwio diag report`. Raport to plik zip z podsumowaniem, ostatnimi logami i ustawieniami. Jego zawartość opisuje sekcja [Raport diagnostyczny](privacy.md#raport-diagnostyczny).

## Typowe problemy

### Nie znaleziono J-Link

- Sprawdź kabel USB między J-Link a komputerem. Wypróbuj inny port USB lub inny kabel.
- Uruchom `hwio jlink list`, aby zobaczyć sondy. Jeśli podano numer seryjny (`-n <serial>`), sprawdź go.
- W systemie Linux sprawdź [uprawnienia USB](#uprawnienia-usb-w-systemie-linux).

Kody błędów: [`probe.not_found`](#probe.not_found), [`probe.open_failed`](#probe.open_failed), [`probe.io_failed`](#probe.io_failed).

### Nie znaleziono J-Link Software lub jest zbyt stare

HARDWARIO Monitor nie zawiera SEGGER J-Link Software. Zainstaluj wersję 8.00 lub nowszą; zobacz [SEGGER J-Link Software](install.md#segger-j-link-software). Jeśli jest zainstalowane w nietypowym miejscu, ustaw bibliotekę w **Settings** › **J-Link** › **Library** lub użyj w CLI `--jlink-library PATH` (albo `$HWIO_JLINK_LIBRARY`). Uruchom `hwio doctor`, aby to sprawdzić.

Kody błędów: [`dependency.jlink_not_found`](#dependency.jlink_not_found), [`dependency.jlink_load_failed`](#dependency.jlink_load_failed), [`dependency.jlink_too_old`](#dependency.jlink_too_old), [`dependency.jlink_function_missing`](#dependency.jlink_function_missing).

### Uprawnienia USB w systemie Linux

W systemie Linux Twoje konto użytkownika potrzebuje dostępu do J-Link przez USB. Pakiet `.deb` instaluje w tym celu regułę udev; w przypadku innych instalacji zainstaluj ją samodzielnie zgodnie z opisem w sekcji [Dostęp USB do J-Link](install.md#dostęp-usb-do-j-link). Następnie odłącz i ponownie podłącz J-Link i uruchom `hwio doctor`. Niektóre teksty błędów wspominają o kreatorze konfiguracji (setup wizard); nie jest on jeszcze dostępny, więc postępuj zgodnie ze stroną instalacji.

Kody błędów: [`usb.access_denied`](#usb.access_denied), [`usb.world_accessible`](#usb.world_accessible).

### J-Link jest używany przez inny program

Z J-Link może jednocześnie korzystać tylko jeden program. Zamknij drugie okno HARDWARIO Monitor, drugie polecenie `hwio` lub inne narzędzia do debugowania (na przykład narzędzia SEGGER). W aplikacji desktopowej sonda używana przez inny program jest oznaczona na liście urządzeń jako **In use by another program**. `hwio doctor` wymienia programy, które mogą blokować sondę.

Kody błędów: [`probe.in_use_by_other_process`](#probe.in_use_by_other_process), [`probe.held_by_program`](#probe.held_by_program), [`probe.open_failed`](#probe.open_failed).

### Urządzenie nie ma zasilania lub nie odpowiada

- CHESTER musi mieć własne zasilanie: baterie lub wejście zasilania USB-C.
- Sprawdź taśmę między J-Link a CHESTER oraz to, czy jest w złączu właściwego MCU (app lub LTE).
- Jeśli MCU ma ochronę przed odczytem, zobacz [Wgrywanie firmware nie powiodło się](#wgrywanie-firmware-nie-powiodło-się-lub-mcu-jest-chronione).

Kody błędów: [`target.no_power`](#target.no_power), [`target.connect_failed`](#target.connect_failed), [`target.wrong_family`](#target.wrong_family).

### Nie znaleziono konsoli (RTT)

Konsola wymaga firmware z konsolą RTT. Zresetuj urządzenie lub wgraj takie firmware. Przyczyną może być też bardzo niska prędkość J-Link: użyj **Automatic speed**. MCU LTE w CHESTER nie ma konsoli; użyj MCU app. Zobacz [Konsola](console.md).

Kody błędów: [`target.rtt_not_found`](#target.rtt_not_found), [`target.rtt_buffer_not_found`](#target.rtt_buffer_not_found), [`target.rtt_write_timeout`](#target.rtt_write_timeout), [`target.not_connected`](#target.not_connected).

### Wgrywanie firmware nie powiodło się lub MCU jest chronione

- Sprawdź kable i zasilanie, a następnie wgraj firmware ponownie.
- Jeśli weryfikacja wciąż się nie udaje, ustaw niższą prędkość J-Link.
- Jeśli MCU ma ochronę przed odczytem, wykonaj recover. Recover kasuje całe MCU, łącznie z UICR i blokiem informacji o produkcie (Product Information Block). Zobacz [Odzyskiwanie (Recover)](flashing.md#odzyskiwanie-recover).
- Jeśli firmware zapisuje także UICR, zezwól na to świadomie: **Allow writing UICR** na stronie **Flash** lub `--allow-uicr` w CLI. Zobacz [UICR i blok informacji o produkcie](flashing.md#uicr-i-blok-informacji-o-produkcie).

Kody błędów: [`target.protected`](#target.protected), [`target.program_failed`](#target.program_failed), [`firmware.verify_failed`](#firmware.verify_failed), [`firmware.uicr_in_image`](#firmware.uicr_in_image), [`firmware.out_of_range`](#firmware.out_of_range), [`target.recover_failed`](#target.recover_failed).

### Firmware modemu LTE wymaga nRF Util

Aktualizacja firmware modemu LTE korzysta z Nordic nRF Util z poleceniem `device`. Nic innego go nie wymaga. Zainstaluj je zgodnie z opisem w sekcji [Nordic nRF Util](install.md#nordic-nrf-util-tylko-firmware-modemu-lte) lub ustaw jego ścieżkę w **Settings** › **nRF Util** albo za pomocą `--nrfutil PATH` w CLI.

Kody błędów: [`dependency.nrfutil_not_found`](#dependency.nrfutil_not_found), [`dependency.nrfutil_device_missing`](#dependency.nrfutil_device_missing), [`dependency.nrfutil_too_old`](#dependency.nrfutil_too_old), [`firmware.modem_update_failed`](#firmware.modem_update_failed).

### Nie znaleziono urządzenia Bluetooth lub parowanie nie powiodło się

Bluetooth jest dostępny tylko w CLI (`hwio ble ...`).

- Włącz Bluetooth i uruchom `hwio ble scan`. Wyświetlane są tylko urządzenia, których nazwa zawiera `CHESTER`; `--all` wyświetla wszystkie urządzenia.
- Przybliż urządzenie i odłącz je od innych telefonów lub komputerów.
- Jeśli parowanie się nie udaje, usuń urządzenie w systemowych ustawieniach Bluetooth i sparuj je ponownie. W systemie Linux musi działać agent parowania, który zapyta o passkey. Zobacz [Parowanie](bluetooth.md#parowanie).

Kody błędów: [`ble.adapter_off`](#ble.adapter_off), [`ble.device_not_found`](#ble.device_not_found), [`ble.connect_failed`](#ble.connect_failed), [`ble.not_paired`](#ble.not_paired), [`ble.pairing_failed`](#ble.pairing_failed), [`ble.no_pairing_agent`](#ble.no_pairing_agent).

### Ustawienia są tylko do odczytu

Jeśli używano nowszej wersji HARDWARIO Monitor, a następnie uruchomiono starszą, starsza wersja otwiera ustawienia tylko do odczytu i wyświetla baner „Settings were created by a newer version and are read-only.” Zmiany nie są zapisywane. Zaktualizuj HARDWARIO Monitor do nowszej wersji.

Kod błędu: [`settings.newer_schema`](#settings.newer_schema).

## Kody błędów

Kody błędów są pogrupowane według kategorii. Kategoria określa [kod wyjścia](../en/cli.md#exit-codes) `hwio`:

| Kategoria | Znaczenie | Kod wyjścia |
|---|---|---|
| [`dependency`](#category-dependency) | Brakuje wymaganego programu lub usługi systemowej: J-Link Software, nRF Util, Bluetooth | 4 |
| [`probe`](#category-probe) | Sonda J-Link lub jej połączenie USB | 5 |
| [`target`](#category-target) | Urządzenie: zasilanie, kabel, MCU, konsola, łącze Bluetooth | 6 |
| [`firmware`](#category-firmware) | Plik firmware lub jego pobieranie | 7 |
| [`network`](#category-network) | Połączenie z internetem: katalog firmware, sprawdzanie aktualizacji | 8 |
| [`io`](#category-io) | Pliki, foldery i ustawienia | 3 |
| [`usage`](#category-usage) | Nieprawidłowy argument, opcja lub dane wejściowe | 2 |
| [`timeout`](#category-timeout) | Coś nie odpowiedziało na czas | 9 |
| [`cancelled`](#category-cancelled) | Operacja została zatrzymana | 130 |
| [`internal`](#category-internal) | Wewnętrzny problem HARDWARIO Monitor | 1 |

Polecenie zakończone powodzeniem kończy się kodem wyjścia 0. Poniższe teksty to domyślne teksty angielskie. W wielu miejscach wyświetlany jest dokładniejszy komunikat, na przykład podający MCU lub plik; takie warianty są opisane przy każdym kodzie. Wartości w nawiasach ostrych, takie jak `<serial>`, są zastępowane rzeczywistymi wartościami.

<a id="category-dependency"></a>
### Zależności (kod wyjścia 4)

<a id="dependency.jlink_not_found"></a>
#### `dependency.jlink_not_found`

**SEGGER J-Link Software was not found.**

- **Probably:** It is not installed, or it is installed in a non-standard location.
- **What to do:** Install J-Link Software 8.00 or newer, or set its path with --jlink-library.

Pojawia się, gdy HARDWARIO Monitor nie może znaleźć biblioteki J-Link, na przykład zaraz po instalacji. W aplikacji desktopowej ustaw ścieżkę w **Settings** › **J-Link** › **Library** (**Custom path**); w CLI użyj `--jlink-library PATH` lub `$HWIO_JLINK_LIBRARY`. Zobacz [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.jlink_load_failed"></a>
#### `dependency.jlink_load_failed`

**The J-Link library could not be loaded.**

- **Probably:** The installation is damaged, is for another processor architecture, or lacks a required function.
- **What to do:** Reinstall J-Link Software.

Plik biblioteki został znaleziony, ale nie można go użyć, na przykład biblioteka 32-bitowa lub x86 na komputerze z procesorem ARM. Sprawdź ścieżkę w **Settings** › **J-Link** › **Library** lub `--jlink-library`.

<a id="dependency.jlink_too_old"></a>
#### `dependency.jlink_too_old`

**The J-Link Software is too old.**

- **Probably:** HARDWARIO Monitor needs J-Link Software 8.00 or newer.
- **What to do:** Install the current J-Link Software.

Komunikat może podawać wersję, np. „J-Link Software `<version>` is too old.” Zobacz [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.nrfutil_not_found"></a>
#### `dependency.nrfutil_not_found`

**nRF Util was not found.**

- **Probably:** It is not installed; it is needed only for the modem firmware.
- **What to do:** Install nRF Util and its device command, or set its path with --nrfutil.

nRF Util jest potrzebny tylko do aktualizacji firmware modemu LTE. W aplikacji desktopowej ustaw ścieżkę w **Settings** › **nRF Util** › **Path** (**Custom path**); w CLI użyj `--nrfutil PATH`. Zobacz [Nordic nRF Util](install.md#nordic-nrf-util-tylko-firmware-modemu-lte).

<a id="dependency.nrfutil_device_missing"></a>
#### `dependency.nrfutil_device_missing`

**The nRF Util device command is not installed.**

- **Probably:** nRF Util is installed without its device command.
- **What to do:** Run `nrfutil install device`.

Zobacz [Nordic nRF Util](install.md#nordic-nrf-util-tylko-firmware-modemu-lte).

<a id="dependency.nrfutil_too_old"></a>
#### `dependency.nrfutil_too_old`

**nRF Util is too old.**

- **Probably:** HARDWARIO Monitor needs a newer nRF Util or device command.
- **What to do:** Run `nrfutil self-upgrade` and `nrfutil upgrade device`.

Komunikat może podawać wersję, np. „nRF Util `<version>` is too old.”

<a id="ble.adapter_not_found"></a>
#### `ble.adapter_not_found`

**No Bluetooth adapter was found.**

- **Probably:** The computer has no Bluetooth or it is disabled in the system.
- **What to do:** Enable Bluetooth or connect a USB Bluetooth adapter.

Bluetooth jest dostępny tylko w CLI (`hwio ble ...`). `hwio doctor` zgłasza to również jako ostrzeżenie. Zobacz [Bluetooth](bluetooth.md).

<a id="ble.adapter_off"></a>
#### `ble.adapter_off`

**Bluetooth is turned off.**

- **Probably:** Bluetooth is turned off in the system settings or with a hardware switch.
- **What to do:** Turn Bluetooth on in the system settings.

Bluetooth jest dostępny tylko w CLI (`hwio ble ...`).

<a id="ble.permission_denied"></a>
#### `ble.permission_denied`

**HARDWARIO Monitor is not allowed to use Bluetooth.**

- **Probably:** Bluetooth access was denied in the privacy settings.
- **What to do:** Allow Bluetooth for HARDWARIO Monitor (or your terminal) in System Settings → Privacy & Security → Bluetooth.

Występuje w macOS, gdy odrzucono prośbę o uprawnienie do Bluetooth. Gdy uruchamiasz `hwio` z terminala, zezwól na Bluetooth dla aplikacji terminala.

<a id="ble.stack_unavailable"></a>
#### `ble.stack_unavailable`

**The Bluetooth service of the system is not available.**

- **Probably:** The Bluetooth service of the system (BlueZ on Linux) is not running.
- **What to do:** Start the Bluetooth service: `sudo systemctl start bluetooth`.

W systemie Linux musi działać usługa BlueZ. Błąd pojawia się też przy nieznanym błędzie systemowego stosu Bluetooth podczas skanowania.

<a id="ble.no_pairing_agent"></a>
#### `ble.no_pairing_agent`

**No program is ready to ask for the Bluetooth passkey.**

- **Probably:** No pairing agent is running (on GNOME the agent may only be active in Settings → Bluetooth).
- **What to do:** Pair the device from the system Bluetooth settings, or with bluetoothctl: `agent KeyboardOnly`, `default-agent`, `pair <id>`.

Tylko Linux: parowanie zostało natychmiast anulowane, ponieważ żaden program nie mógł wyświetlić okna z prośbą o passkey. Zobacz [Parowanie](bluetooth.md#parowanie).

<a id="dependency.jlink_function_missing"></a>
#### `dependency.jlink_function_missing`

**The installed J-Link software does not provide a function this operation needs.**

- **Probably:** The J-Link library is older than, or different from, the versions HARDWARIO Monitor was tested with.
- **What to do:** Install the latest J-Link Software and Documentation Pack.

Niektóre polecenia celu (na przykład rejestry lub 16-bitowy dostęp do pamięci) wymagają funkcji, które udostępniają tylko nowsze biblioteki J-Link. Zobacz [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="category-probe"></a>
### Sonda (kod wyjścia 5)

<a id="probe.not_found"></a>
#### `probe.not_found`

**No J-Link probe found.**

- **Probably:** The USB cable is disconnected or the J-Link is not powered.
- **What to do:** Check the USB cable between the J-Link and the computer.

Pojawia się, gdy nie jest podłączony żaden J-Link lub gdy wybranego numeru seryjnego (`-n <serial>`) nie ma na liście. Warianty: „No J-Link probe with serial number `<serial>` found.” oraz, dla J-Link podłączonego przez sieć, „The J-Link `<serial>` is a network J-Link; network J-Links are not supported for sessions yet.” Użyj J-Link podłączonego przez USB.

<a id="probe.in_use_by_other_process"></a>
#### `probe.in_use_by_other_process`

**The J-Link is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or hwio command is using it.
- **What to do:** Close the other program, or choose another J-Link.

HARDWARIO Monitor i `hwio` blokują J-Link na czas jego używania. Komunikat może podawać nazwę drugiego programu, np. „The J-Link `<serial>` is used by another program (`<app>`, PID `<pid>`).” Jeśli program jest nieznany, sugestia zaleca usunięcie pliku blokady `<lockfile>`, gdy nie działa żaden inny program HARDWARIO. Po zwolnieniu J-Link przez aplikację desktopową przyczyną może być też „Another program opened the J-Link after it was released.”

<a id="probe.open_failed"></a>
#### `probe.open_failed`

**The J-Link probe could not be opened.**

- **Probably:** It is used by another program or session, or the USB permissions are missing (Linux udev rules).
- **What to do:** Close other programs using the J-Link and try again.

W systemie Linux częstą przyczyną jest brak uprawnień USB; zobacz [Dostęp USB do J-Link](install.md#dostęp-usb-do-j-link). Wariant „The J-Link `<serial>` is already used by this application.” oznacza, że ma go otwartego inna karta lub sesja tego samego programu.

<a id="probe.firmware_outdated"></a>
#### `probe.firmware_outdated`

**The J-Link firmware is outdated.**

- **Probably:** The J-Link firmware differs from the firmware bundled with the J-Link Software.
- **What to do:** Run `hwio jlink update-firmware`.

To jest ostrzeżenie; J-Link nadal działa. `hwio doctor` również je zgłasza. Gdy firmware sondy jest nowsze niż biblioteka, komunikat brzmi „The J-Link firmware is newer than the J-Link library.” W takim przypadku zaktualizuj zamiast tego J-Link Software.

<a id="probe.firmware_update_failed"></a>
#### `probe.firmware_update_failed`

**The J-Link firmware could not be updated.**

- **Probably:** The update failed or the probe still reports a firmware different from the library's.
- **What to do:** Disconnect and reconnect the J-Link, then run `hwio jlink update-firmware` again.

Pochodzi z `hwio jlink update-firmware`.

<a id="probe.io_failed"></a>
#### `probe.io_failed`

**Communication with the J-Link failed.**

- **Probably:** The J-Link did not answer: a USB glitch, or the USB cable was disconnected.
- **What to do:** Check the USB cable between the J-Link and the computer; the console reconnects automatically if enabled.

Występuje, gdy połączenie USB z J-Link zostanie utracone podczas działania konsoli. W aplikacji desktopowej automatyczne ponowne łączenie jest domyślnie włączone (**Settings** › **J-Link** lub **Advanced** (⋯) na pasku urządzenia); w CLI użyj `--auto-reconnect`.

<a id="usb.access_denied"></a>
#### `usb.access_denied`

**This computer does not allow access to the J-Link (USB permissions).**

- **Probably:** The udev rule for SEGGER devices is missing.
- **What to do:** Install the udev rule shown in the setup wizard, then reconnect the J-Link. The rule gives access only to the user logged in at this computer; users connected only over SSH need their own udev rule.

Tylko Linux. Kreator konfiguracji (setup wizard) wspomniany w tekście nie jest jeszcze dostępny: zainstaluj regułę udev zgodnie z opisem w sekcji [Dostęp USB do J-Link](install.md#dostęp-usb-do-j-link), a następnie uruchom `hwio doctor`, aby to sprawdzić.

<a id="probe.held_by_program"></a>
#### `probe.held_by_program`

**Another program is running and may be using the J-Link.**

- **Probably:** Another debugging or programming tool keeps the probe open.
- **What to do:** Close the other program and try again.

Ostrzeżenie z `hwio doctor` i raportu diagnostycznego. Może podawać nazwę programu, np. „`<program>` (PID `<pid>`) is running and may be using the J-Link.”

<a id="ble.in_use_by_other_process"></a>
#### `ble.in_use_by_other_process`

**The device is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or `hwio` command is connected to it.
- **What to do:** Close the other program.

Urządzenia Bluetooth są blokowane tak jak J-Link. Komunikat może podawać nazwę drugiego programu („The device `<id>` is used by another program (`<app>`, PID `<pid>`).”) lub brzmieć „The device `<id>` is already used by this application.” Jeśli program jest nieznany, sugestia zaleca usunięcie pliku blokady `<lockfile>`, gdy nie działa żaden inny program HARDWARIO.

<a id="usb.world_accessible"></a>
#### `usb.world_accessible`

**Every user account on this computer can use the J-Link (USB permissions).**

- **Probably:** A udev rule, usually /etc/udev/rules.d/99-jlink.rules from the SEGGER J-Link Software, makes the probe accessible to all accounts.
- **What to do:** Remove that rule (or its MODE="0666"), install the rule shown in the setup wizard, then reconnect the J-Link. A J-Link Software update may install the rule again.

Ostrzeżenie z `hwio doctor` w systemie Linux. Kreator konfiguracji (setup wizard) wspomniany w tekście nie jest jeszcze dostępny: zainstaluj regułę zgodnie z opisem w sekcji [Dostęp USB do J-Link](install.md#dostęp-usb-do-j-link).

<a id="category-target"></a>
### Cel (kod wyjścia 6)

<a id="target.no_power"></a>
#### `target.no_power`

**The device has no power.**

- **Probably:** The device is not powered or the flat cable is disconnected.
- **What to do:** Power the device and check the flat cable.

Komunikat może zawierać zmierzone napięcie, np. „The device has no power (`<mV>` mV).” CHESTER wymaga baterii lub wejścia zasilania USB-C.

<a id="target.connect_failed"></a>
#### `target.connect_failed`

**Could not connect to the device.**

- **Probably:** The flat cable is disconnected or in the wrong connector, or the MCU is in a low-power or protected state.
- **What to do:** Check the flat cable between the J-Link and the device.

J-Link działa, ale nie może dotrzeć do MCU. Warianty podają MCU, na przykład „Could not connect to the device's `<device>` MCU.” lub „The type of the `<target>` MCU could not be read.” Chronione MCU może wymagać [odzyskiwania (Recover)](flashing.md#odzyskiwanie-recover).

<a id="target.rtt_not_found"></a>
#### `target.rtt_not_found`

**The device's console (RTT) was not found.**

- **Probably:** The firmware has no RTT console, is still booting, or the CPU is halted.
- **What to do:** Reset the device or flash firmware with RTT console support.

Konsola nie znalazła na czas bloku sterującego RTT. Przyczyną może być też bardzo niska prędkość J-Link: użyj **Automatic speed** (**Advanced** (⋯) na pasku urządzenia lub **Settings** › **J-Link**). MCU LTE w CHESTER nie ma konsoli. Zobacz [Konsola](console.md).

<a id="target.rtt_buffer_not_found"></a>
#### `target.rtt_buffer_not_found`

**An RTT buffer was not found.**

- **Probably:** The firmware is older or different.
- **What to do:** Use firmware with the CHESTER shell.

Komunikat podaje nazwę bufora, np. „RTT buffer "`<name>`" not found.” Gdy brakuje tylko bufora logu, jest to ostrzeżenie („…; the device log is not shown.”), a konsola nadal działa.

<a id="target.rtt_write_timeout"></a>
#### `target.rtt_write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is hung or its shell is disabled.
- **What to do:** Reset the device.

Urządzenie nie przyjęło na czas wysłanego polecenia.

<a id="target.rtt_read_failed"></a>
#### `target.rtt_read_failed`

**Reading from the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

W aplikacji desktopowej automatyczne ponowne łączenie jest domyślnie włączone (**Settings** › **J-Link** lub **Advanced** (⋯) na pasku urządzenia).

<a id="target.rtt_write_failed"></a>
#### `target.rtt_write_failed`

**Writing to the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

W aplikacji desktopowej automatyczne ponowne łączenie jest domyślnie włączone (**Settings** › **J-Link** lub **Advanced** (⋯) na pasku urządzenia).

<a id="target.not_connected"></a>
#### `target.not_connected`

**The device is not connected.**

- **Probably:** The console is disconnected or still reconnecting.
- **What to do:** Reconnect the console (F4).

Polecenie zostało wysłane, gdy konsola była rozłączona. Przy włączonym automatycznym ponownym łączeniu sugestia brzmi „Wait for the console to reconnect.” W CLI uruchom konsolę ponownie. Jeśli zdarzy się to podczas wykonywania pliku wsadowego, komunikat brzmi „The batch stopped after `<done>` of `<total>` commands.”

<a id="target.wrong_family"></a>
#### `target.wrong_family`

**The J-Link is connected to another MCU.**

- **Probably:** The J-Link is connected to the other connector of the device.
- **What to do:** Connect the J-Link to the connector of the MCU you want to program.

Taśma jest w złączu drugiego MCU (na przykład w złączu LTE, gdy wgrywasz firmware do MCU app). Komunikat może brzmieć „Expected the `<target>` MCU (`<part>`) but found `<found>`.”

<a id="target.protected"></a>
#### `target.protected`

**The MCU is read-back protected.**

- **Probably:** Access port protection (APPROTECT) is enabled.
- **What to do:** Recover the MCU (this erases it), then flash it again.

Recover usuwa ochronę, ale kasuje MCU całkowicie, łącznie z blokiem informacji o produkcie (Product Information Block). Zobacz [Odzyskiwanie (Recover)](flashing.md#odzyskiwanie-recover). Dla konkretnego MCU sugestia brzmi „Run `hwio <profile> <target> recover` (erases the MCU).”; w aplikacji desktopowej użyj **Recover…** w sekcji **Maintenance** na stronie **Flash**.

<a id="target.program_failed"></a>
#### `target.program_failed`

**Programming the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Wariant przyczyny „Programming was interrupted; the firmware may be partly written.” oznacza, że przed dalszym używaniem urządzenia trzeba ponownie wgrać firmware.

<a id="target.erase_failed"></a>
#### `target.erase_failed`

**Erasing the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Dwa rzadkie warianty dotyczą bloku informacji o produkcie po skasowaniu samego firmware: „could not be checked after the erase” oraz „Erasing the code flash … also changed its Product Information Block”. W obu przypadkach nie kasuj ponownie; postępuj zgodnie z sugestią i skontaktuj się ze wsparciem technicznym HARDWARIO. Zobacz [UICR i blok informacji o produkcie](flashing.md#uicr-i-blok-informacji-o-produkcie).

<a id="target.recover_failed"></a>
#### `target.recover_failed`

**Recovering the MCU failed.**

- **Probably:** The device has no power or its control access port does not answer.
- **What to do:** Power-cycle the device and try again.

Zobacz [Odzyskiwanie (Recover)](flashing.md#odzyskiwanie-recover).

<a id="pib.invalid"></a>
#### `pib.invalid`

**The Product Information Block is missing or damaged.**

- **Probably:** It was never written, or it was erased by a full erase or a recover.
- **What to do:** Contact HARDWARIO support to restore it.

Blok informacji o produkcie zawiera numer seryjny i wersję sprzętu. Komunikat może podawać w nawiasie nieudany test. Zobacz [UICR i blok informacji o produkcie](flashing.md#uicr-i-blok-informacji-o-produkcie).

<a id="ble.device_not_found"></a>
#### `ble.device_not_found`

**The Bluetooth device was not found.**

- **Probably:** The device is out of range, turned off or its Bluetooth is disabled.
- **What to do:** Bring the device closer and make sure its Bluetooth is on.

Pochodzi z `hwio ble console` lub `hwio ble command` z `--name` lub `-a`. Uruchom `hwio ble scan`, aby zobaczyć urządzenia w zasięgu. Zobacz [Bluetooth](bluetooth.md).

<a id="ble.connect_failed"></a>
#### `ble.connect_failed`

**Could not connect to the device over Bluetooth.**

- **Probably:** The signal is weak or the device is busy with another connection.
- **What to do:** Move closer, disconnect other phones or computers from the device and try again.

Wariant przyczyny to „The device refused to start the console: it may still be connected (to this or another computer) or the pairing was not completed.” Wtedy odczekaj kilka sekund lub rozłącz urządzenie w systemowych ustawieniach Bluetooth. Zobacz [Parowanie](bluetooth.md#parowanie).

<a id="ble.service_not_found"></a>
#### `ble.service_not_found`

**The device does not offer the console service (NUS).**

- **Probably:** The firmware has no Bluetooth shell.
- **What to do:** Use firmware with the Bluetooth console enabled.

Na urządzeniu nie działa firmware CHESTER z konsolą Bluetooth.

<a id="ble.not_paired"></a>
#### `ble.not_paired`

**The device is not paired with this computer.**

- **Probably:** Pairing was cancelled, failed or was removed on one side.
- **What to do:** Pair the device: enter its passkey when the system asks.

Uruchom `hwio ble pair` (Linux i Windows); w macOS system pyta o passkey przy łączeniu. Zobacz [Parowanie](bluetooth.md#parowanie).

<a id="ble.pairing_failed"></a>
#### `ble.pairing_failed`

**Pairing with the device failed.**

- **Probably:** A wrong passkey, a timeout or a stale bond on one side.
- **What to do:** Remove the device in the system Bluetooth settings and pair again.

W systemie Linux przyczyną może być też to, że nie pojawiło się okno z prośbą o passkey, ponieważ nie działa żaden agent parowania. Zobacz [Parowanie](bluetooth.md#parowanie).

<a id="ble.write_failed"></a>
#### `ble.write_failed`

**Sending to the device over Bluetooth failed.**

- **Probably:** The link was lost during the write.
- **What to do:** Reconnect.

Łącze Bluetooth zostało utracone podczas wysyłania polecenia.

<a id="ble.write_timeout"></a>
#### `ble.write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is busy or the link is stalled.
- **What to do:** Reconnect; if it repeats, restart the device.

Urządzenie nie potwierdziło na czas polecenia wysłanego przez Bluetooth.

<a id="ble.disconnected"></a>
#### `ble.disconnected`

**The Bluetooth connection to the device was lost.**

- **Probably:** The device is out of range, was restarted or turned off.
- **What to do:** Bring the device closer; the console reconnects automatically if enabled.

Komunikat może podawać nazwę urządzenia, np. „The Bluetooth connection to `<name>` was lost.”

<a id="target.not_halted"></a>
#### `target.not_halted`

**The CPU is running.**

- **Probably:** Registers can be read only while the core is halted.
- **What to do:** Halt the core first.

Pochodzi z `hwio chester app regs` (lub `lte regs`) i serwera MCP (`hwio mcp`). Najpierw zatrzymaj rdzeń poleceniem `hwio chester app halt`.

<a id="target.address_out_of_range"></a>
#### `target.address_out_of_range`

**The address range is not allowed for this operation.**

- **Probably:** Memory writes are allowed only in the RAM and peripheral ranges of the target.
- **What to do:** Use an address inside the RAM or peripheral ranges of the target.

Pochodzi z `mem read`, `mem write` i serwera MCP (`hwio mcp`). Przyczyna może też wskazywać, że zakres wykracza poza koniec przestrzeni adresowej lub nakłada się na rejestry systemowe albo debugowania CPU bądź rejestry kontrolera pamięci flash, które nigdy nie są zapisywane.

<a id="target.memory_access_failed"></a>
#### `target.memory_access_failed`

**Reading or writing target memory failed.**

- **Probably:** The address is not mapped, or the core is in a fault state.
- **What to do:** Check the address and try again after a reset.

Pochodzi z `mem read`, `mem write` i serwera MCP (`hwio mcp`).

<a id="category-firmware"></a>
### Firmware (kod wyjścia 7)

<a id="firmware.hex_parse_error"></a>
#### `firmware.hex_parse_error`

**The firmware file is not a valid HEX file.**

- **Probably:** The file is damaged or it is not a HEX file.
- **What to do:** Use the .hex file produced by the build or download it again.

Komunikat może podawać wiersz, np. „The firmware file is not a valid HEX file (line `<n>`).”

<a id="firmware.invalid_image"></a>
#### `firmware.invalid_image`

**The file is not a supported firmware file.**

- **Probably:** It is not a CHESTER LTE bundle, a modem firmware zip or a HEX file, or the zip exceeds the safety limits.
- **What to do:** Use a CHESTER .hex file or LTE bundle (.zip).

HARDWARIO Monitor sprawdza zawartość pliku, a nie rozszerzenie. Warianty: „The file is not a CHESTER LTE bundle, modem firmware or HEX file.” oraz, dla pobranego pliku, „The downloaded file is not `<name>`.”

<a id="firmware.out_of_range"></a>
#### `firmware.out_of_range`

**The firmware does not fit the MCU.**

- **Probably:** The image was built for another MCU.
- **What to do:** Check that the firmware is for this MCU.

Komunikat może podawać adres, np. „The firmware does not fit the `<target>` MCU (address `<addr>`).” Wariant „The firmware writes into the Product Information Block (address `<addr>`).” oznacza, że obraz nadpisałby blok informacji o produkcie, na co nigdy nie ma zgody.

<a id="firmware.uicr_in_image"></a>
#### `firmware.uicr_in_image`

**The firmware also writes the UICR configuration area.**

- **Probably:** The build includes UICR settings.
- **What to do:** Re-run with --allow-uicr if this is intended; the Product Information Block is kept.

`--allow-uicr` to opcja CLI; w aplikacji desktopowej włącz **Allow writing UICR** w sekcji **Options** na stronie **Flash**. Zobacz [UICR i blok informacji o produkcie](flashing.md#uicr-i-blok-informacji-o-produkcie).

<a id="firmware.verify_failed"></a>
#### `firmware.verify_failed`

**Verification of the written firmware failed.**

- **Probably:** The connection is unstable, or the flash is protected or faulty.
- **What to do:** Flash again; if it repeats, lower the J-Link speed or check the cables.

Komunikat może podawać adres, np. „Verification failed at address `<0xADDR>`.” Niższą prędkość ustawisz w **Advanced** (⋯) na pasku urządzenia, w **Settings** › **J-Link** lub za pomocą `--jlink-speed`.

<a id="firmware.not_found_in_catalog"></a>
#### `firmware.not_found_in_catalog`

**The firmware was not found in the catalog.**

- **Probably:** The firmware ID is wrong or it was deleted.
- **What to do:** Check the firmware ID.

Pojawia się, gdy wgrywasz firmware według 32-znakowego identyfikatora. Komunikat może podawać identyfikator, np. „Firmware `<id>` was not found in the catalog.”

<a id="firmware.modem_update_failed"></a>
#### `firmware.modem_update_failed`

**The modem firmware update failed.**

- **Probably:** The modem firmware zip is wrong, the connection was lost, or nRF Util reported an error (see Details).
- **What to do:** Check the cable and power, then flash the modem firmware again.

Firmware modemu LTE zapisuje nRF Util; jego wyjście znajduje się w **Details**. Zobacz [Nordic nRF Util](install.md#nordic-nrf-util-tylko-firmware-modemu-lte).

<a id="firmware.checksum_mismatch"></a>
#### `firmware.checksum_mismatch`

**The downloaded firmware is damaged (checksum mismatch).**

- **Probably:** The file was changed or damaged during the download.
- **What to do:** Try again; if it repeats, report it to HARDWARIO.

Pobrany plik nie odpowiadał sumie kontrolnej z katalogu, więc nie został użyty. Aplikacja desktopowa może wyświetlić „The downloaded file is damaged. Try again.”

<a id="category-network"></a>
### Sieć (kod wyjścia 8)

<a id="network.download_failed"></a>
#### `network.download_failed`

**Downloading the firmware failed.**

- **Probably:** There is no internet connection, a proxy blocks it, or the server failed.
- **What to do:** Check the internet connection and try again.

Komunikat może podawać firmware, np. „Downloading firmware `<id>` failed.” lub „Downloading `<name>` failed.” W aplikacji desktopowej można też zamiast tego wybrać własny plik.

<a id="update.network"></a>
#### `update.network`

**Could not reach GitHub to check for updates.**

- **Probably:** There is no internet connection, or a firewall or proxy blocks GitHub.
- **What to do:** Check your connection or try again later.

Pochodzi ze sprawdzania aktualizacji (**Settings** › **About** lub `hwio update check`).

<a id="update.rate_limited"></a>
#### `update.rate_limited`

**GitHub temporarily refused the update check.**

- **Probably:** GitHub limits how often a network address may ask; other computers on the same network may have used up the limit.
- **What to do:** Try again in an hour.

Pochodzi ze sprawdzania aktualizacji. GitHub ogranicza liczbę żądań z jednego adresu sieciowego.

<a id="update.bad_response"></a>
#### `update.bad_response`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

Pochodzi ze sprawdzania aktualizacji.

<a id="update.too_large"></a>
#### `update.too_large`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

Pochodzi ze sprawdzania aktualizacji, gdy odpowiedź z GitHub jest większa niż dozwolona. Używa tych samych tekstów co `update.bad_response`.

<a id="category-io"></a>
### Pliki i ustawienia: io (kod wyjścia 3)

<a id="io.file_not_found"></a>
#### `io.file_not_found`

**The file was not found.**

- **Probably:** The path is wrong or the file was moved or deleted.
- **What to do:** Check the file path (see Details).

Ścieżka znajduje się w **Details**. W przypadku serwera MCP (`hwio mcp`) komunikat może brzmieć „The `<what>` `<dir>` does not exist.” dla opcji folderu, takiej jak `--dump-dir`.

<a id="io.read_failed"></a>
#### `io.read_failed`

**The file could not be read.**

- **Probably:** You do not have permission to read it or the disk reported an error.
- **What to do:** Check the file path and its permissions (see Details).

W przypadku pliku wsadowego konsoli w aplikacji desktopowej komunikat brzmi „The batch file could not be read.”

<a id="io.write_failed"></a>
#### `io.write_failed`

**The file could not be written.**

- **Probably:** The disk is full or read-only, or you do not have permission to write.
- **What to do:** Check the path, the free disk space and the permissions (see Details).

Ścieżka znajduje się w **Details**. Warianty: „The file could not be saved.” przy zapisywaniu konsoli w aplikacji desktopowej oraz „The console log file could not be written; logging to file is off for this session.” W przypadku serwera MCP (`hwio mcp`) komunikat może brzmieć „The `<what>` `<dir>` is not writable.”

<a id="settings.newer_schema"></a>
#### `settings.newer_schema`

**Settings were written by a newer version and are opened read-only.**

- **Probably:** An older version of HARDWARIO Monitor is running now.
- **What to do:** Changes will not be saved; update HARDWARIO Monitor.

Występuje po użyciu nowszej wersji i uruchomieniu starszej. Aplikacja desktopowa wyświetla baner „Settings were created by a newer version and are read-only.” Plik ustawień nie jest zmieniany, więc nowsza wersja nadal ma wszystkie swoje ustawienia.

<a id="diag.report_write_failed"></a>
#### `diag.report_write_failed`

**The diagnostic report could not be saved.**

- **Probably:** The folder is not writable or the disk is full.
- **What to do:** Choose another folder.

Pochodzi z **Help** › **Create diagnostic report…** lub `hwio diag report`. Zobacz [Raport diagnostyczny](privacy.md#raport-diagnostyczny).

<a id="mcp.port_in_use"></a>
#### `mcp.port_in_use`

**The MCP port is already in use.**

- **Probably:** Another HARDWARIO Monitor, `hwio mcp --http` or another MCP server (for example rttt) is listening on it.
- **What to do:** Stop the other server or choose another port with --port.

Serwer MCP (`hwio mcp --http`). Zobacz [MCP](../en/mcp.md).

<a id="mcp.listen_failed"></a>
#### `mcp.listen_failed`

**The MCP server could not start.**

- **Probably:** The operating system refused to open the listening socket on 127.0.0.1.
- **What to do:** Choose another port with --port.

Serwer MCP (`hwio mcp --http`).

<a id="mcp.token_file"></a>
#### `mcp.token_file`

**The MCP token file cannot be read or created.**

- **Probably:** The file or its folder has the wrong permissions, or the file does not contain a valid token.
- **What to do:** Check the file permissions, or delete the file to create a new token.

Serwer MCP (`hwio mcp --http`). Zobacz [Security](../en/mcp.md#security).

<a id="io.file_exists"></a>
#### `io.file_exists`

**The output file already exists.**

- **Probably:** Existing files are never overwritten by this command.
- **What to do:** Choose a new file name or delete the file first.

Pochodzi z poleceń zapisujących plik z `-o` (na przykład `uicr read` lub `mem read`) oraz ze zrzutów pamięci serwera MCP (`hwio mcp`). Dodaj `--force`, aby zastąpić plik.

<a id="cli.file_is_symlink"></a>
#### `cli.file_is_symlink`

**The output file is a symbolic link.**

- **Probably:** Symbolic links are never followed or replaced, even with --force.
- **What to do:** Choose another file name or remove the link first.

Tylko CLI, z `-o FILE --force`.

<a id="category-usage"></a>
### Użycie (kod wyjścia 2)

<a id="usage.invalid_argument"></a>
#### `usage.invalid_argument`

**An argument or option is invalid.**

- **Probably:** The request contains a value that is not supported.
- **What to do:** Check the arguments and try again.

Komunikat zwykle dokładnie wskazuje, która wartość jest nieprawidłowa, na przykład błędna opcja (sugestia brzmi wtedy „Run `hwio --help`.”), nieznany profil urządzenia lub cel, prędkość J-Link lub opóźnienie poza zakresem, błędny adres lub długość, identyfikator firmware, który nie ma 32 cyfr szesnastkowych, lub kilka urządzeń Bluetooth pasujących do `--name` (użyj `--address`). W aplikacji desktopowej obejmuje też pliki wsadowe konsoli, które są zbyt duże, zawierają zbyt długie polecenie lub nie zawierają żadnych poleceń. Zobacz [CLI](../en/cli.md).

<a id="usage.confirmation_required"></a>
#### `usage.confirmation_required`

**This operation needs confirmation.**

- **Probably:** The command runs non-interactively, so it cannot ask.
- **What to do:** Re-run with --yes.

Tylko CLI. Polecenia, które kasują lub zapisują (flash, erase, recover, zapisy pamięci i UICR, `jlink update-firmware`, plik wsadowy konsoli), pytają o potwierdzenie przed rozpoczęciem. Bez terminala lub z `--json` nie mogą zapytać.

<a id="ble.pairing_not_supported"></a>
#### `ble.pairing_not_supported`

**This system pairs Bluetooth devices only when connecting.**

- **Probably:** macOS pairs a device automatically when it connects.
- **What to do:** Connect to the device; macOS asks for the passkey.

Tylko macOS, z `hwio ble pair`. Zobacz [Parowanie](bluetooth.md#parowanie).

<a id="mcp.tool_not_allowed"></a>
#### `mcp.tool_not_allowed`

**This MCP tool is not allowed by the server.**

- **Probably:** Destructive tools must be allowed when the server starts.
- **What to do:** Start the server with --allow and the tool's category, or with --allow all.

Serwer MCP (`hwio mcp`). Komunikat może podawać narzędzie i elementy, na które należy zezwolić, np. „Start the server with --allow `<items>` (or --allow all).” Zobacz [Security](../en/mcp.md#security).

<a id="mcp.device_ambiguous"></a>
#### `mcp.device_ambiguous`

**Several devices are connected and the tool does not know which one to use.**

- **Probably:** The device argument was left out while more than one device is open.
- **What to do:** Pass the device argument; list_devices shows the ids.

Serwer MCP (`hwio mcp`). `list_devices` to narzędzie MCP.

<a id="mcp.device_not_open"></a>
#### `mcp.device_not_open`

**The device is not connected.**

- **Probably:** It was never connected, it was disconnected, or its tab was closed.
- **What to do:** Call connect first.

Serwer MCP (`hwio mcp`). `connect` to narzędzie MCP.

<a id="mcp.capability_missing"></a>
#### `mcp.capability_missing`

**This tool needs a J-Link connection.**

- **Probably:** The device is connected over Bluetooth, which provides only the console.
- **What to do:** Connect the device with a J-Link probe.

Serwer MCP (`hwio mcp`).

<a id="mcp.to_file_not_allowed"></a>
#### `mcp.to_file_not_allowed`

**The file name must be a plain name.**

- **Probably:** Memory dumps are written only into the server's dump folder.
- **What to do:** Use a plain file name such as flash.bin, or start the server with --dump-dir DIR.

Serwer MCP (`hwio mcp`). Komunikat może podawać folder zrzutów. Zobacz [Security](../en/mcp.md#security).

<a id="category-timeout"></a>
### Przekroczenie czasu (kod wyjścia 9)

<a id="timeout"></a>
#### `timeout`

**The operation timed out.**

- **Probably:** The other side did not respond in time.
- **What to do:** Try it again.

Ogólne przekroczenie czasu: na przykład urządzenie nie połączyło się na czas („The device did not connect in time.”), wgrywanie lub kasowanie trwało zbyt długo albo nRF Util nie zakończył działania. `hwio chester app info` używa własnych tekstów, gdy firmware nie odpowiada na `info show`.

<a id="diag.check_timeout"></a>
#### `diag.check_timeout`

**A check did not finish in time.**

- **Probably:** The system is slow or a tool does not answer.
- **What to do:** Run the checks again.

Ostrzeżenie z `hwio doctor` lub raportu diagnostycznego. Komunikat może podawać nazwę testu, np. „The check "`<title>`" did not finish in time.”

<a id="mcp.consent_timeout"></a>
#### `mcp.consent_timeout`

**Nobody answered the permission request.** Zarezerwowane dla serwera MCP aplikacji desktopowej, który nie jest jeszcze dostępny.

<a id="category-cancelled"></a>
### Anulowano (kod wyjścia 130)

<a id="cancelled"></a>
#### `cancelled`

**Cancelled by user.**

- **Probably:** The operation was stopped before it finished.
- **What to do:** Run the operation again if needed.

Pojawia się po naciśnięciu Ctrl+C lub **Cancel**, odpowiedzi „No” na pytanie o potwierdzenie albo zamknięciu urządzenia. Wariant „The output was closed.” oznacza, że program odczytujący wyjście `hwio` zakończył działanie, na przykład `hwio ... --json | head`.

<a id="mcp.consent_denied"></a>
#### `mcp.consent_denied`

**The user did not allow this action.** Zarezerwowane dla serwera MCP aplikacji desktopowej, który nie jest jeszcze dostępny.

<a id="category-internal"></a>
### Błąd wewnętrzny (kod wyjścia 1)

<a id="internal.unexpected"></a>
#### `internal.unexpected`

**An unexpected internal error occurred.**

- **Probably:** This is a bug in HARDWARIO Monitor.
- **What to do:** Please report it with the log file (see Details).

Utwórz [raport diagnostyczny](privacy.md#raport-diagnostyczny) i wyślij go do HARDWARIO.

<a id="internal.queue_full"></a>
#### `internal.queue_full`

**An internal buffer is full.**

- **Probably:** This is a bug in HARDWARIO Monitor or the computer is overloaded.
- **What to do:** Please report it with the log file (see Details).

Na przykład urządzenie wysyła wiersze konsoli szybciej, niż można je wyświetlić. Utwórz [raport diagnostyczny](privacy.md#raport-diagnostyczny).

<a id="internal.session_limit"></a>
#### `internal.session_limit`

**Too many devices are open (max 8).**

- **Probably:** HARDWARIO Monitor works with at most 8 devices at the same time.
- **What to do:** Close a device first.

<a id="internal.busy"></a>
#### `internal.busy`

**Another operation is running on this device.**

- **Probably:** Only one operation can run at a time.
- **What to do:** Wait for it to finish or cancel it.

Na przykład rozpoczęto wgrywanie firmware podczas wykonywania pliku wsadowego konsoli. Jeśli plik wsadowy zostanie w ten sposób przerwany, komunikat brzmi „The batch stopped after `<done>` of `<total>` commands.”

<a id="mcp.too_many_requests"></a>
#### `mcp.too_many_requests`

**Too many MCP requests are running.**

- **Probably:** The server runs at most 32 tool calls at the same time.
- **What to do:** Wait for running calls to finish and try again.

Serwer MCP (`hwio mcp`).
