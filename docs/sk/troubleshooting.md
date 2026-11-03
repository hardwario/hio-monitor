# Riešenie problémov

[Dokumentácia](index.md) › Riešenie problémov

Táto stránka vám pomôže, keď niečo nefunguje. Začína najčastejšími problémami a potom uvádza všetky kódy chýb.

## Ako sa zobrazujú chyby

Každá chyba má krátku správu, pravdepodobnú príčinu a odporúčanie.

- **Desktopová aplikácia (`hwio-monitor`):** panel chyby zobrazuje správu, **Probably:** (príčinu) a **What to do:** (odporúčanie) s tlačidlom pre ďalší krok, napríklad **Reconnect**, **Open Settings** alebo **Retry**. **Details** zobrazí kód chyby (napríklad `probe.not_found`) a technické podrobnosti; **Copy** ich skopíruje.
- **CLI (`hwio`):** chyba sa vypíše na stderr:

  ```text
  Error: No J-Link probe found.
  Cause: The USB cable is disconnected or the J-Link is not powered.
  What to do: Check the USB cable between the J-Link and the computer.
  Details: <technical details>
  ```

  Príkaz skončí s [návratovým kódom](../en/cli.md#exit-codes) kategórie chyby.
- **CLI s `--json`:** výstupom je jeden riadok s `"ok":false` a objektom `error`. Jeho polia sú `code` (kód chyby), `category`, `message`, `cause`, `suggestion` a `detail`. Pozrite si [Výstup JSON](../en/cli.md#json-output).

Každý kód chyby nižšie má kotvu rovnakú ako kód, takže naň môžete odkázať priamo, napríklad `troubleshooting.md#probe.not_found`.

**Skôr než požiadate o pomoc:**

- Spustite `hwio doctor`. Skontroluje J-Link Software, nRF Util, prístup k J-Linku cez USB (Linux), iné programy, ktoré používajú J-Link, pripojené sondy a ich firmvér, priečinky nastavení a logov a Bluetooth.
- Vytvorte diagnostickú správu: v desktopovej aplikácii zvoľte **Help** › **Create diagnostic report…** alebo spustite `hwio diag report`. Správa je súbor zip so súhrnom, nedávnymi logmi a nastaveniami. Čo obsahuje, si pozrite v časti [Diagnostická správa](privacy.md#diagnostická-správa).

## Časté problémy

### J-Link sa nenašiel

- Skontrolujte kábel USB medzi J-Linkom a počítačom. Skúste iný port USB alebo iný kábel.
- Spustite `hwio jlink list` a pozrite si sondy. Ak ste zadali sériové číslo (`-n <serial>`), skontrolujte ho.
- V Linuxe skontrolujte [oprávnenia USB](#oprávnenia-usb-v-linuxe).

Kódy chýb: [`probe.not_found`](#probe.not_found), [`probe.open_failed`](#probe.open_failed), [`probe.io_failed`](#probe.io_failed).

### J-Link Software sa nenašiel alebo je príliš starý

HARDWARIO Monitor neobsahuje SEGGER J-Link Software. Nainštalujte verziu 8.00 alebo novšiu; pozrite si [SEGGER J-Link Software](install.md#segger-j-link-software). Ak je nainštalovaný na nezvyčajnom mieste, nastavte knižnicu v **Settings** › **J-Link** › **Library** alebo v CLI použite `--jlink-library PATH` (alebo `$HWIO_JLINK_LIBRARY`). Na kontrolu spustite `hwio doctor`.

Kódy chýb: [`dependency.jlink_not_found`](#dependency.jlink_not_found), [`dependency.jlink_load_failed`](#dependency.jlink_load_failed), [`dependency.jlink_too_old`](#dependency.jlink_too_old), [`dependency.jlink_function_missing`](#dependency.jlink_function_missing).

### Oprávnenia USB v Linuxe

V Linuxe potrebuje váš používateľský účet prístup k J-Linku cez USB. Balík `.deb` na to inštaluje pravidlo udev; pri iných inštaláciách ho nainštalujte sami podľa postupu v časti [Prístup k J-Linku cez USB](install.md#prístup-k-j-linku-cez-usb). Potom J-Link odpojte a znova pripojte a spustite `hwio doctor`. Niektoré texty chýb spomínajú sprievodcu nastavením; ten zatiaľ nie je k dispozícii, preto postupujte podľa stránky o inštalácii.

Kódy chýb: [`usb.access_denied`](#usb.access_denied), [`usb.world_accessible`](#usb.world_accessible).

### J-Link používa iný program

J-Link môže naraz používať len jeden program. Zatvorte druhé okno HARDWARIO Monitor, druhý príkaz `hwio` alebo iné ladiace nástroje (napríklad nástroje SEGGER). V desktopovej aplikácii je sonda, ktorú používa iný program, v zozname zariadení označená **In use by another program**. `hwio doctor` uvedie programy, ktoré môžu sondu držať.

Kódy chýb: [`probe.in_use_by_other_process`](#probe.in_use_by_other_process), [`probe.held_by_program`](#probe.held_by_program), [`probe.open_failed`](#probe.open_failed).

### Zariadenie nemá napájanie alebo neodpovedá

- CHESTER musí mať vlastné napájanie: batérie alebo napájací vstup USB-C.
- Skontrolujte plochý kábel medzi J-Linkom a zariadením CHESTER a to, či je v konektore toho MCU, ktorý chcete (app alebo LTE).
- Ak je MCU chránený proti spätnému čítaniu, pozrite si [Nahrávanie zlyhá](#nahrávanie-zlyhá-alebo-je-mcu-chránený).

Kódy chýb: [`target.no_power`](#target.no_power), [`target.connect_failed`](#target.connect_failed), [`target.wrong_family`](#target.wrong_family).

### Konzola (RTT) sa nenašla

Konzola potrebuje firmvér s konzolou RTT. Resetujte zariadenie alebo nahrajte taký firmvér. Príčinou môže byť aj veľmi nízka rýchlosť J-Linku: použite **Automatic speed**. LTE MCU zariadenia CHESTER nemá konzolu; použite aplikačný MCU. Pozrite si [Konzola](console.md).

Kódy chýb: [`target.rtt_not_found`](#target.rtt_not_found), [`target.rtt_buffer_not_found`](#target.rtt_buffer_not_found), [`target.rtt_write_timeout`](#target.rtt_write_timeout), [`target.not_connected`](#target.not_connected).

### Nahrávanie zlyhá alebo je MCU chránený

- Skontrolujte káble a napájanie a potom nahrajte firmvér znova.
- Ak overenie opakovane zlyháva, nastavte nižšiu rýchlosť J-Linku.
- Ak je MCU chránený proti spätnému čítaniu, obnovte ho. Obnovenie vymaže celý MCU vrátane UICR a bloku informácií o produkte (Product Information Block). Pozrite si [Obnovenie](flashing.md#obnovenie).
- Ak firmvér zapisuje aj UICR, povoľte to zámerne: **Allow writing UICR** na stránke **Flash** alebo `--allow-uicr` v CLI. Pozrite si [UICR a blok informácií o produkte](flashing.md#uicr-a-blok-informácií-o-produkte).

Kódy chýb: [`target.protected`](#target.protected), [`target.program_failed`](#target.program_failed), [`firmware.verify_failed`](#firmware.verify_failed), [`firmware.uicr_in_image`](#firmware.uicr_in_image), [`firmware.out_of_range`](#firmware.out_of_range), [`target.recover_failed`](#target.recover_failed).

### Firmvér LTE modemu potrebuje nRF Util

Aktualizácia firmvéru LTE modemu používa Nordic nRF Util s jeho príkazom `device`. Nič iné ho nepotrebuje. Nainštalujte ho podľa postupu v časti [Nordic nRF Util](install.md#nordic-nrf-util-len-firmvér-lte-modemu) alebo nastavte jeho cestu v **Settings** › **nRF Util**, prípadne pomocou `--nrfutil PATH` v CLI.

Kódy chýb: [`dependency.nrfutil_not_found`](#dependency.nrfutil_not_found), [`dependency.nrfutil_device_missing`](#dependency.nrfutil_device_missing), [`dependency.nrfutil_too_old`](#dependency.nrfutil_too_old), [`firmware.modem_update_failed`](#firmware.modem_update_failed).

### Zariadenie Bluetooth sa nenašlo alebo párovanie zlyhá

Bluetooth je dostupný len v CLI (`hwio ble ...`).

- Zapnite Bluetooth a spustite `hwio ble scan`. Zobrazia sa len zariadenia, ktorých názov obsahuje `CHESTER`; `--all` zobrazí všetky zariadenia.
- Priblížte zariadenie a odpojte ho od iných telefónov alebo počítačov.
- Ak párovanie zlyhá, odstráňte zariadenie v systémových nastaveniach Bluetooth a spárujte ho znova. V Linuxe musí bežať párovací agent, ktorý sa spýta na prístupový kľúč (passkey). Pozrite si [Párovanie](bluetooth.md#párovanie).

Kódy chýb: [`ble.adapter_off`](#ble.adapter_off), [`ble.device_not_found`](#ble.device_not_found), [`ble.connect_failed`](#ble.connect_failed), [`ble.not_paired`](#ble.not_paired), [`ble.pairing_failed`](#ble.pairing_failed), [`ble.no_pairing_agent`](#ble.no_pairing_agent).

### Nastavenia sú len na čítanie

Ak ste používali novšiu verziu HARDWARIO Monitor a potom spustíte staršiu, staršia verzia otvorí nastavenia len na čítanie a zobrazí pruh "Settings were created by a newer version and are read-only." Zmeny sa neukladajú. Aktualizujte HARDWARIO Monitor na novšiu verziu.

Kód chyby: [`settings.newer_schema`](#settings.newer_schema).

## Kódy chýb

Kódy chýb sú zoskupené podľa kategórií. Kategória určuje [návratový kód](../en/cli.md#exit-codes) príkazu `hwio`:

| Kategória | Význam | Návratový kód |
|---|---|---|
| [`dependency`](#category-dependency) | Chýba potrebný program alebo systémová služba: J-Link Software, nRF Util, Bluetooth | 4 |
| [`probe`](#category-probe) | Sonda J-Link alebo jej pripojenie cez USB | 5 |
| [`target`](#category-target) | Zariadenie: napájanie, kábel, MCU, konzola, spojenie Bluetooth | 6 |
| [`firmware`](#category-firmware) | Súbor firmvéru alebo jeho stiahnutie | 7 |
| [`network`](#category-network) | Pripojenie k internetu: katalóg firmvéru, kontrola aktualizácií | 8 |
| [`io`](#category-io) | Súbory, priečinky a nastavenia | 3 |
| [`usage`](#category-usage) | Nesprávny argument, možnosť alebo vstup | 2 |
| [`timeout`](#category-timeout) | Niečo neodpovedalo včas | 9 |
| [`cancelled`](#category-cancelled) | Operácia bola zastavená | 130 |
| [`internal`](#category-internal) | Vnútorný problém HARDWARIO Monitor | 1 |

Úspešný príkaz skončí s návratovým kódom 0. Texty nižšie sú predvolené anglické texty. Na mnohých miestach sa zobrazuje presnejšia správa, napríklad taká, ktorá uvádza MCU alebo súbor; tieto varianty sú opísané pri každom kóde. Hodnoty v lomených zátvorkách, napríklad `<serial>`, sa nahrádzajú skutočnými hodnotami.

<a id="category-dependency"></a>
### Závislosti (návratový kód 4)

<a id="dependency.jlink_not_found"></a>
#### `dependency.jlink_not_found`

**SEGGER J-Link Software was not found.**

- **Probably:** It is not installed, or it is installed in a non-standard location.
- **What to do:** Install J-Link Software 8.00 or newer, or set its path with --jlink-library.

Zobrazí sa, keď HARDWARIO Monitor nemôže nájsť knižnicu J-Link, napríklad hneď po inštalácii. V desktopovej aplikácii nastavte cestu v **Settings** › **J-Link** › **Library** (**Custom path**); v CLI použite `--jlink-library PATH` alebo `$HWIO_JLINK_LIBRARY`. Pozrite si [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.jlink_load_failed"></a>
#### `dependency.jlink_load_failed`

**The J-Link library could not be loaded.**

- **Probably:** The installation is damaged, is for another processor architecture, or lacks a required function.
- **What to do:** Reinstall J-Link Software.

Súbor knižnice sa našiel, ale nedal sa použiť, napríklad 32-bitová knižnica alebo knižnica x86 na počítači s ARM. Skontrolujte cestu v **Settings** › **J-Link** › **Library** alebo `--jlink-library`.

<a id="dependency.jlink_too_old"></a>
#### `dependency.jlink_too_old`

**The J-Link Software is too old.**

- **Probably:** HARDWARIO Monitor needs J-Link Software 8.00 or newer.
- **What to do:** Install the current J-Link Software.

Správa môže uvádzať verziu, napríklad "J-Link Software `<version>` is too old." Pozrite si [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.nrfutil_not_found"></a>
#### `dependency.nrfutil_not_found`

**nRF Util was not found.**

- **Probably:** It is not installed; it is needed only for the modem firmware.
- **What to do:** Install nRF Util and its device command, or set its path with --nrfutil.

nRF Util je potrebný len na aktualizáciu firmvéru LTE modemu. V desktopovej aplikácii nastavte cestu v **Settings** › **nRF Util** › **Path** (**Custom path**); v CLI použite `--nrfutil PATH`. Pozrite si [Nordic nRF Util](install.md#nordic-nrf-util-len-firmvér-lte-modemu).

<a id="dependency.nrfutil_device_missing"></a>
#### `dependency.nrfutil_device_missing`

**The nRF Util device command is not installed.**

- **Probably:** nRF Util is installed without its device command.
- **What to do:** Run `nrfutil install device`.

Pozrite si [Nordic nRF Util](install.md#nordic-nrf-util-len-firmvér-lte-modemu).

<a id="dependency.nrfutil_too_old"></a>
#### `dependency.nrfutil_too_old`

**nRF Util is too old.**

- **Probably:** HARDWARIO Monitor needs a newer nRF Util or device command.
- **What to do:** Run `nrfutil self-upgrade` and `nrfutil upgrade device`.

Správa môže uvádzať verziu, napríklad "nRF Util `<version>` is too old."

<a id="ble.adapter_not_found"></a>
#### `ble.adapter_not_found`

**No Bluetooth adapter was found.**

- **Probably:** The computer has no Bluetooth or it is disabled in the system.
- **What to do:** Enable Bluetooth or connect a USB Bluetooth adapter.

Bluetooth je dostupný len v CLI (`hwio ble ...`). `hwio doctor` to hlási aj ako varovanie. Pozrite si [Bluetooth](bluetooth.md).

<a id="ble.adapter_off"></a>
#### `ble.adapter_off`

**Bluetooth is turned off.**

- **Probably:** Bluetooth is turned off in the system settings or with a hardware switch.
- **What to do:** Turn Bluetooth on in the system settings.

Bluetooth je dostupný len v CLI (`hwio ble ...`).

<a id="ble.permission_denied"></a>
#### `ble.permission_denied`

**HARDWARIO Monitor is not allowed to use Bluetooth.**

- **Probably:** Bluetooth access was denied in the privacy settings.
- **What to do:** Allow Bluetooth for HARDWARIO Monitor (or your terminal) in System Settings → Privacy & Security → Bluetooth.

Stáva sa v macOS, keď bola výzva na povolenie Bluetooth odmietnutá. Keď spúšťate `hwio` z terminálu, povoľte Bluetooth pre aplikáciu terminálu.

<a id="ble.stack_unavailable"></a>
#### `ble.stack_unavailable`

**The Bluetooth service of the system is not available.**

- **Probably:** The Bluetooth service of the system (BlueZ on Linux) is not running.
- **What to do:** Start the Bluetooth service: `sudo systemctl start bluetooth`.

V Linuxe musí bežať služba BlueZ. Chyba sa zobrazí aj pri neznámej chybe systémového zásobníka Bluetooth počas vyhľadávania.

<a id="ble.no_pairing_agent"></a>
#### `ble.no_pairing_agent`

**No program is ready to ask for the Bluetooth passkey.**

- **Probably:** No pairing agent is running (on GNOME the agent may only be active in Settings → Bluetooth).
- **What to do:** Pair the device from the system Bluetooth settings, or with bluetoothctl: `agent KeyboardOnly`, `default-agent`, `pair <id>`.

Len Linux: párovanie bolo okamžite zrušené, pretože žiadny program nedokázal zobraziť dialóg na zadanie prístupového kľúča. Pozrite si [Párovanie](bluetooth.md#párovanie).

<a id="dependency.jlink_function_missing"></a>
#### `dependency.jlink_function_missing`

**The installed J-Link software does not provide a function this operation needs.**

- **Probably:** The J-Link library is older than, or different from, the versions HARDWARIO Monitor was tested with.
- **What to do:** Install the latest J-Link Software and Documentation Pack.

Niektoré príkazy pre cieľ (napríklad registre alebo 16-bitový prístup k pamäti) potrebujú funkcie, ktoré poskytujú len novšie knižnice J-Link. Pozrite si [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="category-probe"></a>
### Sonda (návratový kód 5)

<a id="probe.not_found"></a>
#### `probe.not_found`

**No J-Link probe found.**

- **Probably:** The USB cable is disconnected or the J-Link is not powered.
- **What to do:** Check the USB cable between the J-Link and the computer.

Zobrazí sa, keď nie je pripojený žiadny J-Link alebo keď zvolené sériové číslo (`-n <serial>`) nie je v zozname. Varianty: "No J-Link probe with serial number `<serial>` found." a pre J-Link pripojený cez sieť "The J-Link `<serial>` is a network J-Link; network J-Links are not supported for sessions yet." Použite J-Link pripojený cez USB.

<a id="probe.in_use_by_other_process"></a>
#### `probe.in_use_by_other_process`

**The J-Link is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or hwio command is using it.
- **What to do:** Close the other program, or choose another J-Link.

HARDWARIO Monitor a `hwio` J-Link počas používania zamknú. Správa môže uvádzať druhý program, napríklad "The J-Link `<serial>` is used by another program (`<app>`, PID `<pid>`)." Ak program nie je známy, odporúčanie vyzve na vymazanie súboru zámku `<lockfile>`, keď nebeží žiadny iný program HARDWARIO. Keď desktopová aplikácia J-Link uvoľnila, príčina môže byť aj "Another program opened the J-Link after it was released."

<a id="probe.open_failed"></a>
#### `probe.open_failed`

**The J-Link probe could not be opened.**

- **Probably:** It is used by another program or session, or the USB permissions are missing (Linux udev rules).
- **What to do:** Close other programs using the J-Link and try again.

V Linuxe sú častou príčinou chýbajúce oprávnenia USB; pozrite si [Prístup k J-Linku cez USB](install.md#prístup-k-j-linku-cez-usb). Variant "The J-Link `<serial>` is already used by this application." znamená, že ho má otvorený iná karta alebo relácia toho istého programu.

<a id="probe.firmware_outdated"></a>
#### `probe.firmware_outdated`

**The J-Link firmware is outdated.**

- **Probably:** The J-Link firmware differs from the firmware bundled with the J-Link Software.
- **What to do:** Run `hwio jlink update-firmware`.

Ide o varovanie; J-Link ďalej funguje. Hlási to aj `hwio doctor`. Keď je firmvér sondy novší ako knižnica, správa znie "The J-Link firmware is newer than the J-Link library." V takom prípade namiesto toho aktualizujte J-Link Software.

<a id="probe.firmware_update_failed"></a>
#### `probe.firmware_update_failed`

**The J-Link firmware could not be updated.**

- **Probably:** The update failed or the probe still reports a firmware different from the library's.
- **What to do:** Disconnect and reconnect the J-Link, then run `hwio jlink update-firmware` again.

Pochádza z príkazu `hwio jlink update-firmware`.

<a id="probe.io_failed"></a>
#### `probe.io_failed`

**Communication with the J-Link failed.**

- **Probably:** The J-Link did not answer: a USB glitch, or the USB cable was disconnected.
- **What to do:** Check the USB cable between the J-Link and the computer; the console reconnects automatically if enabled.

Stáva sa, keď sa počas behu konzoly stratí spojenie s J-Linkom cez USB. Automatické opätovné pripojenie je v desktopovej aplikácii predvolene zapnuté (**Settings** › **J-Link** alebo **Advanced** (⋯) v lište zariadenia); v CLI použite `--auto-reconnect`.

<a id="usb.access_denied"></a>
#### `usb.access_denied`

**This computer does not allow access to the J-Link (USB permissions).**

- **Probably:** The udev rule for SEGGER devices is missing.
- **What to do:** Install the udev rule shown in the setup wizard, then reconnect the J-Link. The rule gives access only to the user logged in at this computer; users connected only over SSH need their own udev rule.

Len Linux. Sprievodca nastavením spomenutý v texte zatiaľ nie je k dispozícii: nainštalujte pravidlo udev podľa postupu v časti [Prístup k J-Linku cez USB](install.md#prístup-k-j-linku-cez-usb) a potom to skontrolujte príkazom `hwio doctor`.

<a id="probe.held_by_program"></a>
#### `probe.held_by_program`

**Another program is running and may be using the J-Link.**

- **Probably:** Another debugging or programming tool keeps the probe open.
- **What to do:** Close the other program and try again.

Varovanie z `hwio doctor` a z diagnostickej správy. Môže uvádzať program, napríklad "`<program>` (PID `<pid>`) is running and may be using the J-Link."

<a id="ble.in_use_by_other_process"></a>
#### `ble.in_use_by_other_process`

**The device is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or `hwio` command is connected to it.
- **What to do:** Close the other program.

Zariadenia Bluetooth sa zamykajú rovnako ako J-Linky. Správa môže uvádzať druhý program ("The device `<id>` is used by another program (`<app>`, PID `<pid>`).") alebo znieť "The device `<id>` is already used by this application." Ak program nie je známy, odporúčanie vyzve na vymazanie súboru zámku `<lockfile>`, keď nebeží žiadny iný program HARDWARIO.

<a id="usb.world_accessible"></a>
#### `usb.world_accessible`

**Every user account on this computer can use the J-Link (USB permissions).**

- **Probably:** A udev rule, usually /etc/udev/rules.d/99-jlink.rules from the SEGGER J-Link Software, makes the probe accessible to all accounts.
- **What to do:** Remove that rule (or its MODE="0666"), install the rule shown in the setup wizard, then reconnect the J-Link. A J-Link Software update may install the rule again.

Varovanie z `hwio doctor` v Linuxe. Sprievodca nastavením spomenutý v texte zatiaľ nie je k dispozícii: nainštalujte pravidlo podľa postupu v časti [Prístup k J-Linku cez USB](install.md#prístup-k-j-linku-cez-usb).

<a id="category-target"></a>
### Cieľ (návratový kód 6)

<a id="target.no_power"></a>
#### `target.no_power`

**The device has no power.**

- **Probably:** The device is not powered or the flat cable is disconnected.
- **What to do:** Power the device and check the flat cable.

Správa môže obsahovať nameranú hodnotu napätia, napríklad "The device has no power (`<mV>` mV)." CHESTER potrebuje batérie alebo napájací vstup USB-C.

<a id="target.connect_failed"></a>
#### `target.connect_failed`

**Could not connect to the device.**

- **Probably:** The flat cable is disconnected or in the wrong connector, or the MCU is in a low-power or protected state.
- **What to do:** Check the flat cable between the J-Link and the device.

J-Link funguje, ale nedostane sa k MCU. Varianty uvádzajú MCU, napríklad "Could not connect to the device's `<device>` MCU." alebo "The type of the `<target>` MCU could not be read." Chránený MCU môže potrebovať [obnovenie](flashing.md#obnovenie).

<a id="target.rtt_not_found"></a>
#### `target.rtt_not_found`

**The device's console (RTT) was not found.**

- **Probably:** The firmware has no RTT console, is still booting, or the CPU is halted.
- **What to do:** Reset the device or flash firmware with RTT console support.

Konzola nenašla riadiaci blok RTT včas. Príčinou môže byť aj veľmi nízka rýchlosť J-Linku: použite **Automatic speed** (**Advanced** (⋯) v lište zariadenia alebo **Settings** › **J-Link**). LTE MCU zariadenia CHESTER nemá konzolu. Pozrite si [Konzola](console.md).

<a id="target.rtt_buffer_not_found"></a>
#### `target.rtt_buffer_not_found`

**An RTT buffer was not found.**

- **Probably:** The firmware is older or different.
- **What to do:** Use firmware with the CHESTER shell.

Správa uvádza vyrovnávaciu pamäť (buffer), napríklad "RTT buffer "`<name>`" not found." Keď chýba len buffer logu, ide o varovanie ("…; the device log is not shown.") a konzola naďalej funguje.

<a id="target.rtt_write_timeout"></a>
#### `target.rtt_write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is hung or its shell is disabled.
- **What to do:** Reset the device.

Zariadenie neprevzalo odoslaný príkaz včas.

<a id="target.rtt_read_failed"></a>
#### `target.rtt_read_failed`

**Reading from the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

Automatické opätovné pripojenie je v desktopovej aplikácii predvolene zapnuté (**Settings** › **J-Link** alebo **Advanced** (⋯) v lište zariadenia).

<a id="target.rtt_write_failed"></a>
#### `target.rtt_write_failed`

**Writing to the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

Automatické opätovné pripojenie je v desktopovej aplikácii predvolene zapnuté (**Settings** › **J-Link** alebo **Advanced** (⋯) v lište zariadenia).

<a id="target.not_connected"></a>
#### `target.not_connected`

**The device is not connected.**

- **Probably:** The console is disconnected or still reconnecting.
- **What to do:** Reconnect the console (F4).

Odoslali ste príkaz, keď bola konzola odpojená. Pri zapnutom automatickom opätovnom pripojení odporúčanie znie "Wait for the console to reconnect." V CLI spustite konzolu znova. Ak sa to stane počas dávkového súboru, správa znie "The batch stopped after `<done>` of `<total>` commands."

<a id="target.wrong_family"></a>
#### `target.wrong_family`

**The J-Link is connected to another MCU.**

- **Probably:** The J-Link is connected to the other connector of the device.
- **What to do:** Connect the J-Link to the connector of the MCU you want to program.

Plochý kábel je v konektore druhého MCU (napríklad v konektore LTE, keď nahrávate firmvér do aplikačného MCU). Správa môže znieť "Expected the `<target>` MCU (`<part>`) but found `<found>`."

<a id="target.protected"></a>
#### `target.protected`

**The MCU is read-back protected.**

- **Probably:** Access port protection (APPROTECT) is enabled.
- **What to do:** Recover the MCU (this erases it), then flash it again.

Obnovenie odstráni ochranu, ale úplne vymaže MCU vrátane bloku informácií o produkte (Product Information Block). Pozrite si [Obnovenie](flashing.md#obnovenie). Pre konkrétny MCU odporúčanie znie "Run `hwio <profile> <target> recover` (erases the MCU)."; v desktopovej aplikácii použite **Recover…** v časti **Maintenance** na stránke **Flash**.

<a id="target.program_failed"></a>
#### `target.program_failed`

**Programming the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Variant príčiny "Programming was interrupted; the firmware may be partly written." znamená, že firmvér musíte nahrať znova, aby zariadenie fungovalo.

<a id="target.erase_failed"></a>
#### `target.erase_failed`

**Erasing the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Dva zriedkavé varianty sa týkajú bloku informácií o produkte po vymazaní len firmvéru: "could not be checked after the erase" a "Erasing the code flash … also changed its Product Information Block". V oboch prípadoch nevymazávajte znova; postupujte podľa odporúčania a kontaktujte podporu HARDWARIO. Pozrite si [UICR a blok informácií o produkte](flashing.md#uicr-a-blok-informácií-o-produkte).

<a id="target.recover_failed"></a>
#### `target.recover_failed`

**Recovering the MCU failed.**

- **Probably:** The device has no power or its control access port does not answer.
- **What to do:** Power-cycle the device and try again.

Pozrite si [Obnovenie](flashing.md#obnovenie).

<a id="pib.invalid"></a>
#### `pib.invalid`

**The Product Information Block is missing or damaged.**

- **Probably:** It was never written, or it was erased by a full erase or a recover.
- **What to do:** Contact HARDWARIO support to restore it.

Blok informácií o produkte obsahuje sériové číslo a verziu hardvéru. Správa môže v zátvorkách uvádzať kontrolu, ktorá zlyhala. Pozrite si [UICR a blok informácií o produkte](flashing.md#uicr-a-blok-informácií-o-produkte).

<a id="ble.device_not_found"></a>
#### `ble.device_not_found`

**The Bluetooth device was not found.**

- **Probably:** The device is out of range, turned off or its Bluetooth is disabled.
- **What to do:** Bring the device closer and make sure its Bluetooth is on.

Z príkazov `hwio ble console` alebo `hwio ble command` s `--name` alebo `-a`. Spustite `hwio ble scan` a pozrite si zariadenia v dosahu. Pozrite si [Bluetooth](bluetooth.md).

<a id="ble.connect_failed"></a>
#### `ble.connect_failed`

**Could not connect to the device over Bluetooth.**

- **Probably:** The signal is weak or the device is busy with another connection.
- **What to do:** Move closer, disconnect other phones or computers from the device and try again.

Variant príčiny je "The device refused to start the console: it may still be connected (to this or another computer) or the pairing was not completed." Potom počkajte niekoľko sekúnd alebo zariadenie odpojte v systémových nastaveniach Bluetooth. Pozrite si [Párovanie](bluetooth.md#párovanie).

<a id="ble.service_not_found"></a>
#### `ble.service_not_found`

**The device does not offer the console service (NUS).**

- **Probably:** The firmware has no Bluetooth shell.
- **What to do:** Use firmware with the Bluetooth console enabled.

V zariadení nebeží firmvér CHESTER s konzolou cez Bluetooth.

<a id="ble.not_paired"></a>
#### `ble.not_paired`

**The device is not paired with this computer.**

- **Probably:** Pairing was cancelled, failed or was removed on one side.
- **What to do:** Pair the device: enter its passkey when the system asks.

Spustite `hwio ble pair` (Linux a Windows); v macOS sa systém spýta na prístupový kľúč pri pripojení. Pozrite si [Párovanie](bluetooth.md#párovanie).

<a id="ble.pairing_failed"></a>
#### `ble.pairing_failed`

**Pairing with the device failed.**

- **Probably:** A wrong passkey, a timeout or a stale bond on one side.
- **What to do:** Remove the device in the system Bluetooth settings and pair again.

V Linuxe môže byť príčinou aj to, že sa nezobrazil dialóg na zadanie prístupového kľúča, pretože nebeží žiadny párovací agent. Pozrite si [Párovanie](bluetooth.md#párovanie).

<a id="ble.write_failed"></a>
#### `ble.write_failed`

**Sending to the device over Bluetooth failed.**

- **Probably:** The link was lost during the write.
- **What to do:** Reconnect.

Spojenie Bluetooth sa stratilo počas odosielania príkazu.

<a id="ble.write_timeout"></a>
#### `ble.write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is busy or the link is stalled.
- **What to do:** Reconnect; if it repeats, restart the device.

Zariadenie nepotvrdilo včas príkaz odoslaný cez Bluetooth.

<a id="ble.disconnected"></a>
#### `ble.disconnected`

**The Bluetooth connection to the device was lost.**

- **Probably:** The device is out of range, was restarted or turned off.
- **What to do:** Bring the device closer; the console reconnects automatically if enabled.

Správa môže uvádzať zariadenie, napríklad "The Bluetooth connection to `<name>` was lost."

<a id="target.not_halted"></a>
#### `target.not_halted`

**The CPU is running.**

- **Probably:** Registers can be read only while the core is halted.
- **What to do:** Halt the core first.

Z príkazu `hwio chester app regs` (alebo `lte regs`) a z MCP servera (`hwio mcp`). Jadro najprv zastavte príkazom `hwio chester app halt`.

<a id="target.address_out_of_range"></a>
#### `target.address_out_of_range`

**The address range is not allowed for this operation.**

- **Probably:** Memory writes are allowed only in the RAM and peripheral ranges of the target.
- **What to do:** Use an address inside the RAM or peripheral ranges of the target.

Z príkazov `mem read`, `mem write` a z MCP servera (`hwio mcp`). Príčina môže uvádzať aj to, že rozsah presahuje koniec adresného priestoru alebo že sa prekrýva so systémovými alebo ladiacimi registrami CPU alebo s registrami radiča flash pamäte, do ktorých sa nikdy nezapisuje.

<a id="target.memory_access_failed"></a>
#### `target.memory_access_failed`

**Reading or writing target memory failed.**

- **Probably:** The address is not mapped, or the core is in a fault state.
- **What to do:** Check the address and try again after a reset.

Z príkazov `mem read`, `mem write` a z MCP servera (`hwio mcp`).

<a id="category-firmware"></a>
### Firmvér (návratový kód 7)

<a id="firmware.hex_parse_error"></a>
#### `firmware.hex_parse_error`

**The firmware file is not a valid HEX file.**

- **Probably:** The file is damaged or it is not a HEX file.
- **What to do:** Use the .hex file produced by the build or download it again.

Správa môže uvádzať riadok, napríklad "The firmware file is not a valid HEX file (line `<n>`)."

<a id="firmware.invalid_image"></a>
#### `firmware.invalid_image`

**The file is not a supported firmware file.**

- **Probably:** It is not a CHESTER LTE bundle, a modem firmware zip or a HEX file, or the zip exceeds the safety limits.
- **What to do:** Use a CHESTER .hex file or LTE bundle (.zip).

HARDWARIO Monitor kontroluje obsah súboru, nie príponu. Varianty: "The file is not a CHESTER LTE bundle, modem firmware or HEX file." a pri stiahnutom súbore "The downloaded file is not `<name>`."

<a id="firmware.out_of_range"></a>
#### `firmware.out_of_range`

**The firmware does not fit the MCU.**

- **Probably:** The image was built for another MCU.
- **What to do:** Check that the firmware is for this MCU.

Správa môže uvádzať adresu, napríklad "The firmware does not fit the `<target>` MCU (address `<addr>`)." Variant "The firmware writes into the Product Information Block (address `<addr>`)." znamená, že obraz by prepísal blok informácií o produkte, čo nie je nikdy povolené.

<a id="firmware.uicr_in_image"></a>
#### `firmware.uicr_in_image`

**The firmware also writes the UICR configuration area.**

- **Probably:** The build includes UICR settings.
- **What to do:** Re-run with --allow-uicr if this is intended; the Product Information Block is kept.

`--allow-uicr` je možnosť CLI; v desktopovej aplikácii zapnite **Allow writing UICR** v časti **Options** na stránke **Flash**. Pozrite si [UICR a blok informácií o produkte](flashing.md#uicr-a-blok-informácií-o-produkte).

<a id="firmware.verify_failed"></a>
#### `firmware.verify_failed`

**Verification of the written firmware failed.**

- **Probably:** The connection is unstable, or the flash is protected or faulty.
- **What to do:** Flash again; if it repeats, lower the J-Link speed or check the cables.

Správa môže uvádzať adresu, napríklad "Verification failed at address `<0xADDR>`." Nižšiu rýchlosť nastavte v **Advanced** (⋯) v lište zariadenia, v **Settings** › **J-Link** alebo pomocou `--jlink-speed`.

<a id="firmware.not_found_in_catalog"></a>
#### `firmware.not_found_in_catalog`

**The firmware was not found in the catalog.**

- **Probably:** The firmware ID is wrong or it was deleted.
- **What to do:** Check the firmware ID.

Zobrazí sa, keď nahrávate firmvér podľa 32-znakového ID firmvéru. Správa môže uvádzať ID, napríklad "Firmware `<id>` was not found in the catalog."

<a id="firmware.modem_update_failed"></a>
#### `firmware.modem_update_failed`

**The modem firmware update failed.**

- **Probably:** The modem firmware zip is wrong, the connection was lost, or nRF Util reported an error (see Details).
- **What to do:** Check the cable and power, then flash the modem firmware again.

Firmvér LTE modemu zapisuje nRF Util; jeho výstup je v **Details**. Pozrite si [Nordic nRF Util](install.md#nordic-nrf-util-len-firmvér-lte-modemu).

<a id="firmware.checksum_mismatch"></a>
#### `firmware.checksum_mismatch`

**The downloaded firmware is damaged (checksum mismatch).**

- **Probably:** The file was changed or damaged during the download.
- **What to do:** Try again; if it repeats, report it to HARDWARIO.

Stiahnutý súbor nezodpovedal kontrolnému súčtu z katalógu, preto sa nepoužil. Desktopová aplikácia môže zobraziť "The downloaded file is damaged. Try again."

<a id="category-network"></a>
### Sieť (návratový kód 8)

<a id="network.download_failed"></a>
#### `network.download_failed`

**Downloading the firmware failed.**

- **Probably:** There is no internet connection, a proxy blocks it, or the server failed.
- **What to do:** Check the internet connection and try again.

Správa môže uvádzať firmvér, napríklad "Downloading firmware `<id>` failed." alebo "Downloading `<name>` failed." V desktopovej aplikácii môžete namiesto toho zvoliť aj vlastný súbor.

<a id="update.network"></a>
#### `update.network`

**Could not reach GitHub to check for updates.**

- **Probably:** There is no internet connection, or a firewall or proxy blocks GitHub.
- **What to do:** Check your connection or try again later.

Z kontroly aktualizácií (**Settings** › **About** alebo `hwio update check`).

<a id="update.rate_limited"></a>
#### `update.rate_limited`

**GitHub temporarily refused the update check.**

- **Probably:** GitHub limits how often a network address may ask; other computers on the same network may have used up the limit.
- **What to do:** Try again in an hour.

Z kontroly aktualizácií. GitHub obmedzuje počet požiadaviek z jednej sieťovej adresy.

<a id="update.bad_response"></a>
#### `update.bad_response`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

Z kontroly aktualizácií.

<a id="update.too_large"></a>
#### `update.too_large`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

Z kontroly aktualizácií, keď je odpoveď z GitHubu väčšia, ako je povolené. Používa rovnaké texty ako `update.bad_response`.

<a id="category-io"></a>
### Súbory a nastavenia: io (návratový kód 3)

<a id="io.file_not_found"></a>
#### `io.file_not_found`

**The file was not found.**

- **Probably:** The path is wrong or the file was moved or deleted.
- **What to do:** Check the file path (see Details).

Cesta je v **Details**. Pri MCP serveri (`hwio mcp`) môže správa znieť "The `<what>` `<dir>` does not exist." pre možnosť s priečinkom, napríklad `--dump-dir`.

<a id="io.read_failed"></a>
#### `io.read_failed`

**The file could not be read.**

- **Probably:** You do not have permission to read it or the disk reported an error.
- **What to do:** Check the file path and its permissions (see Details).

Pri dávkovom súbore konzoly v desktopovej aplikácii správa znie "The batch file could not be read."

<a id="io.write_failed"></a>
#### `io.write_failed`

**The file could not be written.**

- **Probably:** The disk is full or read-only, or you do not have permission to write.
- **What to do:** Check the path, the free disk space and the permissions (see Details).

Cesta je v **Details**. Varianty: "The file could not be saved." pri ukladaní konzoly v desktopovej aplikácii a "The console log file could not be written; logging to file is off for this session." Pri MCP serveri (`hwio mcp`) môže správa znieť "The `<what>` `<dir>` is not writable."

<a id="settings.newer_schema"></a>
#### `settings.newer_schema`

**Settings were written by a newer version and are opened read-only.**

- **Probably:** An older version of HARDWARIO Monitor is running now.
- **What to do:** Changes will not be saved; update HARDWARIO Monitor.

Stáva sa, keď ste používali novšiu verziu a potom spustili staršiu. Desktopová aplikácia zobrazí pruh "Settings were created by a newer version and are read-only." Súbor nastavení sa nezmení, takže novšia verzia má stále všetky svoje nastavenia.

<a id="diag.report_write_failed"></a>
#### `diag.report_write_failed`

**The diagnostic report could not be saved.**

- **Probably:** The folder is not writable or the disk is full.
- **What to do:** Choose another folder.

Z **Help** › **Create diagnostic report…** alebo z `hwio diag report`. Pozrite si [Diagnostická správa](privacy.md#diagnostická-správa).

<a id="mcp.port_in_use"></a>
#### `mcp.port_in_use`

**The MCP port is already in use.**

- **Probably:** Another HARDWARIO Monitor, `hwio mcp --http` or another MCP server (for example rttt) is listening on it.
- **What to do:** Stop the other server or choose another port with --port.

MCP server (`hwio mcp --http`). Pozrite si [MCP](../en/mcp.md).

<a id="mcp.listen_failed"></a>
#### `mcp.listen_failed`

**The MCP server could not start.**

- **Probably:** The operating system refused to open the listening socket on 127.0.0.1.
- **What to do:** Choose another port with --port.

MCP server (`hwio mcp --http`).

<a id="mcp.token_file"></a>
#### `mcp.token_file`

**The MCP token file cannot be read or created.**

- **Probably:** The file or its folder has the wrong permissions, or the file does not contain a valid token.
- **What to do:** Check the file permissions, or delete the file to create a new token.

MCP server (`hwio mcp --http`). Pozrite si [Bezpečnosť](../en/mcp.md#security).

<a id="io.file_exists"></a>
#### `io.file_exists`

**The output file already exists.**

- **Probably:** Existing files are never overwritten by this command.
- **What to do:** Choose a new file name or delete the file first.

Z príkazov, ktoré zapisujú súbor pomocou `-o` (napríklad `uicr read` alebo `mem read`), a z výpisov pamäte MCP servera (`hwio mcp`). Ak chcete súbor nahradiť, pridajte `--force`.

<a id="cli.file_is_symlink"></a>
#### `cli.file_is_symlink`

**The output file is a symbolic link.**

- **Probably:** Symbolic links are never followed or replaced, even with --force.
- **What to do:** Choose another file name or remove the link first.

Len CLI, s `-o FILE --force`.

<a id="category-usage"></a>
### Použitie (návratový kód 2)

<a id="usage.invalid_argument"></a>
#### `usage.invalid_argument`

**An argument or option is invalid.**

- **Probably:** The request contains a value that is not supported.
- **What to do:** Check the arguments and try again.

Správa zvyčajne presne uvádza, ktorá hodnota je nesprávna, napríklad chybná možnosť (odporúčanie potom znie "Run `hwio --help`."), neznámy profil zariadenia alebo cieľ, rýchlosť J-Linku alebo latencia mimo rozsahu, chybná adresa alebo dĺžka, ID firmvéru, ktoré nemá 32 šestnástkových číslic, alebo viacero zariadení Bluetooth zodpovedajúcich `--name` (použite `--address`). V desktopovej aplikácii zahŕňa aj dávkové súbory konzoly, ktoré sú príliš veľké, obsahujú príliš dlhý príkaz alebo neobsahujú žiadne príkazy. Pozrite si [CLI](../en/cli.md).

<a id="usage.confirmation_required"></a>
#### `usage.confirmation_required`

**This operation needs confirmation.**

- **Probably:** The command runs non-interactively, so it cannot ask.
- **What to do:** Re-run with --yes.

Len CLI. Príkazy, ktoré vymazávajú alebo zapisujú (nahratie firmvéru, vymazanie, obnovenie, zápisy do pamäte a UICR, `jlink update-firmware`, dávka konzoly), sa pred spustením pýtajú. Bez terminálu alebo s `--json` sa pýtať nemôžu.

<a id="ble.pairing_not_supported"></a>
#### `ble.pairing_not_supported`

**This system pairs Bluetooth devices only when connecting.**

- **Probably:** macOS pairs a device automatically when it connects.
- **What to do:** Connect to the device; macOS asks for the passkey.

Len macOS, z príkazu `hwio ble pair`. Pozrite si [Párovanie](bluetooth.md#párovanie).

<a id="mcp.tool_not_allowed"></a>
#### `mcp.tool_not_allowed`

**This MCP tool is not allowed by the server.**

- **Probably:** Destructive tools must be allowed when the server starts.
- **What to do:** Start the server with --allow and the tool's category, or with --allow all.

MCP server (`hwio mcp`). Správa môže uvádzať nástroj a položky, ktoré treba povoliť, napríklad "Start the server with --allow `<items>` (or --allow all)." Pozrite si [Bezpečnosť](../en/mcp.md#security).

<a id="mcp.device_ambiguous"></a>
#### `mcp.device_ambiguous`

**Several devices are connected and the tool does not know which one to use.**

- **Probably:** The device argument was left out while more than one device is open.
- **What to do:** Pass the device argument; list_devices shows the ids.

MCP server (`hwio mcp`). `list_devices` je nástroj MCP.

<a id="mcp.device_not_open"></a>
#### `mcp.device_not_open`

**The device is not connected.**

- **Probably:** It was never connected, it was disconnected, or its tab was closed.
- **What to do:** Call connect first.

MCP server (`hwio mcp`). `connect` je nástroj MCP.

<a id="mcp.capability_missing"></a>
#### `mcp.capability_missing`

**This tool needs a J-Link connection.**

- **Probably:** The device is connected over Bluetooth, which provides only the console.
- **What to do:** Connect the device with a J-Link probe.

MCP server (`hwio mcp`).

<a id="mcp.to_file_not_allowed"></a>
#### `mcp.to_file_not_allowed`

**The file name must be a plain name.**

- **Probably:** Memory dumps are written only into the server's dump folder.
- **What to do:** Use a plain file name such as flash.bin, or start the server with --dump-dir DIR.

MCP server (`hwio mcp`). Správa môže uvádzať priečinok pre výpisy. Pozrite si [Bezpečnosť](../en/mcp.md#security).

<a id="category-timeout"></a>
### Časový limit (návratový kód 9)

<a id="timeout"></a>
#### `timeout`

**The operation timed out.**

- **Probably:** The other side did not respond in time.
- **What to do:** Try it again.

Všeobecné vypršanie časového limitu: napríklad zariadenie sa nepripojilo včas ("The device did not connect in time."), nahrávanie alebo vymazávanie trvalo príliš dlho, alebo nRF Util neskončil. `hwio chester app info` používa vlastné texty, keď firmvér neodpovedá na `info show`.

<a id="diag.check_timeout"></a>
#### `diag.check_timeout`

**A check did not finish in time.**

- **Probably:** The system is slow or a tool does not answer.
- **What to do:** Run the checks again.

Varovanie z `hwio doctor` alebo z diagnostickej správy. Správa môže uvádzať kontrolu, napríklad "The check "`<title>`" did not finish in time."

<a id="mcp.consent_timeout"></a>
#### `mcp.consent_timeout`

**Nobody answered the permission request.** Vyhradené pre MCP server desktopovej aplikácie, ktorý zatiaľ nie je k dispozícii.

<a id="category-cancelled"></a>
### Zrušené (návratový kód 130)

<a id="cancelled"></a>
#### `cancelled`

**Cancelled by user.**

- **Probably:** The operation was stopped before it finished.
- **What to do:** Run the operation again if needed.

Zobrazí sa, keď stlačíte Ctrl+C alebo **Cancel**, na potvrdenie odpoviete Nie alebo zatvoríte zariadenie. Variant "The output was closed." znamená, že program, ktorý číta výstup `hwio`, skončil, napríklad `hwio ... --json | head`.

<a id="mcp.consent_denied"></a>
#### `mcp.consent_denied`

**The user did not allow this action.** Vyhradené pre MCP server desktopovej aplikácie, ktorý zatiaľ nie je k dispozícii.

<a id="category-internal"></a>
### Vnútorné (návratový kód 1)

<a id="internal.unexpected"></a>
#### `internal.unexpected`

**An unexpected internal error occurred.**

- **Probably:** This is a bug in HARDWARIO Monitor.
- **What to do:** Please report it with the log file (see Details).

Vytvorte, prosím, [diagnostickú správu](privacy.md#diagnostická-správa) a pošlite ju spoločnosti HARDWARIO.

<a id="internal.queue_full"></a>
#### `internal.queue_full`

**An internal buffer is full.**

- **Probably:** This is a bug in HARDWARIO Monitor or the computer is overloaded.
- **What to do:** Please report it with the log file (see Details).

Napríklad zariadenie posiela riadky konzoly rýchlejšie, ako ich možno zobraziť. Vytvorte, prosím, [diagnostickú správu](privacy.md#diagnostická-správa).

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

Napríklad ste spustili nahrávanie firmvéru, kým bežal dávkový súbor konzoly. Ak sa dávkový súbor takto preruší, správa znie "The batch stopped after `<done>` of `<total>` commands."

<a id="mcp.too_many_requests"></a>
#### `mcp.too_many_requests`

**Too many MCP requests are running.**

- **Probably:** The server runs at most 32 tool calls at the same time.
- **What to do:** Wait for running calls to finish and try again.

MCP server (`hwio mcp`).
