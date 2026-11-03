# Řešení problémů

[Dokumentace](index.md) › Řešení problémů

Tato stránka vám pomůže, když něco nefunguje. Začíná nejčastějšími problémy a potom uvádí všechny kódy chyb.

## Jak se chyby zobrazují

Každá chyba má krátkou zprávu, pravděpodobnou příčinu a doporučení.

- **Desktopová aplikace (`hwio-monitor`):** panel chyby ukazuje zprávu, **Pravděpodobně:** (příčinu) a **Co dělat:** (doporučení) a tlačítko pro další krok, například **Znovu připojit**, **Otevřít nastavení** nebo **Zkusit znovu**. **Podrobnosti** ukážou kód chyby (například `probe.not_found`) a technické podrobnosti; **Kopírovat** je zkopíruje.
- **CLI (`hwio`):** chyba se vypíše na stderr. CLI vypisuje vždy anglicky:

  ```text
  Error: No J-Link probe found.
  Cause: The USB cable is disconnected or the J-Link is not powered.
  What to do: Check the USB cable between the J-Link and the computer.
  Details: <technical details>
  ```

  Příkaz skončí [návratovým kódem](../en/cli.md#exit-codes) podle kategorie chyby.
- **CLI s `--json`:** výstupem je jeden řádek s `"ok":false` a objektem `error`. Jeho pole jsou `code` (kód chyby), `category`, `message`, `cause`, `suggestion` a `detail`. Viz [JSON output](../en/cli.md#json-output).

Každý kód chyby níže má kotvu shodnou s kódem, takže na něj můžete odkázat přímo, například `troubleshooting.md#probe.not_found`.

**Než požádáte o pomoc:**

- Spusťte `hwio doctor`. Zkontroluje J-Link Software, nRF Util, přístup k J-Linku přes USB (Linux), jiné programy, které J-Link používají, připojené sondy a jejich firmware, složky nastavení a logů a Bluetooth.
- Vytvořte diagnostickou zprávu: v desktopové aplikaci zvolte **Nápověda** › **Vytvořit diagnostickou zprávu…**, nebo spusťte `hwio diag report`. Zpráva je soubor zip se souhrnem, nedávnými logy a nastavením. Co obsahuje, popisuje [Diagnostická zpráva](privacy.md#diagnostická-zpráva).

## Časté problémy

### Nenalezen žádný J-Link

- Zkontrolujte kabel USB mezi J-Linkem a počítačem. Zkuste jiný port USB nebo kabel.
- Sondy zobrazíte příkazem `hwio jlink list`. Pokud jste zadali sériové číslo (`-n <serial>`), zkontrolujte ho.
- V Linuxu zkontrolujte [oprávnění USB](#oprávnění-usb-v-linuxu).

Kódy chyb: [`probe.not_found`](#probe.not_found), [`probe.open_failed`](#probe.open_failed), [`probe.io_failed`](#probe.io_failed).

### J-Link Software nenalezen nebo je příliš starý

HARDWARIO Monitor neobsahuje SEGGER J-Link Software. Nainstalujte verzi 8.00 nebo novější; viz [SEGGER J-Link Software](install.md#segger-j-link-software). Pokud je nainstalovaný na neobvyklém místě, nastavte knihovnu v **Nastavení** › **J-Link** › **Knihovna**, nebo v CLI použijte `--jlink-library PATH` (či `$HWIO_JLINK_LIBRARY`). Zkontrolujete to příkazem `hwio doctor`.

Kódy chyb: [`dependency.jlink_not_found`](#dependency.jlink_not_found), [`dependency.jlink_load_failed`](#dependency.jlink_load_failed), [`dependency.jlink_too_old`](#dependency.jlink_too_old), [`dependency.jlink_function_missing`](#dependency.jlink_function_missing).

### Oprávnění USB v Linuxu

V Linuxu potřebuje váš uživatelský účet přístup k J-Linku přes USB. Balíček `.deb` k tomu instaluje pravidlo udev; u jiných instalací ho nainstalujte sami podle [Přístup k J-Linku přes USB](install.md#přístup-k-j-linku-přes-usb). Potom J-Link odpojte a znovu připojte a spusťte `hwio doctor`. Některé texty chyb zmiňují průvodce nastavením; ten zatím není k dispozici, postupujte proto podle stránky o instalaci.

Kódy chyb: [`usb.access_denied`](#usb.access_denied), [`usb.world_accessible`](#usb.world_accessible).

### J-Link používá jiný program

J-Link může současně používat jen jeden program. Zavřete druhé okno HARDWARIO Monitor, druhý příkaz `hwio` nebo jiné ladicí nástroje (například nástroje SEGGER). V desktopové aplikaci je sonda, kterou používá jiný program, v seznamu zařízení označená **Používá jiný program**. `hwio doctor` vypíše programy, které mohou sondu držet.

Kódy chyb: [`probe.in_use_by_other_process`](#probe.in_use_by_other_process), [`probe.held_by_program`](#probe.held_by_program), [`probe.open_failed`](#probe.open_failed).

### Zařízení nemá napájení nebo neodpovídá

- CHESTER musí mít vlastní napájení: baterie nebo napájecí vstup USB-C.
- Zkontrolujte plochý kabel mezi J-Linkem a zařízením CHESTER a to, zda je v konektoru požadovaného MCU (app nebo LTE).
- Pokud je MCU chráněný proti čtení, viz [Flashování selhává](#flashování-selhává-nebo-je-mcu-chráněný).

Kódy chyb: [`target.no_power`](#target.no_power), [`target.connect_failed`](#target.connect_failed), [`target.wrong_family`](#target.wrong_family).

### Konzole (RTT) nebyla nalezena

Konzole potřebuje firmware s konzolí RTT. Resetujte zařízení, nebo takový firmware nahrajte. Příčinou může být také velmi nízká rychlost J-Linku: použijte **Automatická rychlost**. LTE MCU zařízení CHESTER nemá konzoli; použijte aplikační MCU. Viz [Konzole](console.md).

Kódy chyb: [`target.rtt_not_found`](#target.rtt_not_found), [`target.rtt_buffer_not_found`](#target.rtt_buffer_not_found), [`target.rtt_write_timeout`](#target.rtt_write_timeout), [`target.not_connected`](#target.not_connected).

### Flashování selhává nebo je MCU chráněný

- Zkontrolujte kabely a napájení a potom flashujte znovu.
- Pokud ověření selhává opakovaně, nastavte nižší rychlost J-Linku.
- Pokud je MCU chráněný proti čtení, obnovte ho (recover). Obnovení smaže celý MCU, včetně UICR a bloku informací o produktu. Viz [Obnovení (recover)](flashing.md#obnovení-recover).
- Pokud firmware zapisuje také do UICR, povolte to záměrně: **Povolit zápis UICR** na stránce **Nahrát FW**, nebo `--allow-uicr` v CLI. Viz [UICR a blok informací o produktu](flashing.md#uicr-a-blok-informací-o-produktu).

Kódy chyb: [`target.protected`](#target.protected), [`target.program_failed`](#target.program_failed), [`firmware.verify_failed`](#firmware.verify_failed), [`firmware.uicr_in_image`](#firmware.uicr_in_image), [`firmware.out_of_range`](#firmware.out_of_range), [`target.recover_failed`](#target.recover_failed).

### Firmware modemu LTE potřebuje nRF Util

Aktualizace firmwaru modemu LTE používá Nordic nRF Util s příkazem `device`. Nic jiného ho nepotřebuje. Nainstalujte ho podle [Nordic nRF Util](install.md#nordic-nrf-util-jen-firmware-modemu-lte), nebo nastavte jeho cestu v **Nastavení** › **nRF Util**, případně v CLI volbou `--nrfutil PATH`.

Kódy chyb: [`dependency.nrfutil_not_found`](#dependency.nrfutil_not_found), [`dependency.nrfutil_device_missing`](#dependency.nrfutil_device_missing), [`dependency.nrfutil_too_old`](#dependency.nrfutil_too_old), [`firmware.modem_update_failed`](#firmware.modem_update_failed).

### Zařízení Bluetooth nenalezeno nebo párování selhává

Bluetooth je k dispozici jen v CLI (`hwio ble ...`).

- Zapněte Bluetooth a spusťte `hwio ble scan`. Zobrazí se jen zařízení, jejichž název obsahuje `CHESTER`; `--all` ukáže všechna zařízení.
- Přibližte zařízení a odpojte ho od jiných telefonů nebo počítačů.
- Pokud párování selže, odeberte zařízení v systémovém nastavení Bluetooth a spárujte ho znovu. V Linuxu musí běžet párovací agent, který se zeptá na přístupový klíč. Viz [Párování](bluetooth.md#párování).

Kódy chyb: [`ble.adapter_off`](#ble.adapter_off), [`ble.device_not_found`](#ble.device_not_found), [`ble.connect_failed`](#ble.connect_failed), [`ble.not_paired`](#ble.not_paired), [`ble.pairing_failed`](#ble.pairing_failed), [`ble.no_pairing_agent`](#ble.no_pairing_agent).

### Nastavení je jen pro čtení

Pokud jste použili novější verzi HARDWARIO Monitor a potom spustíte starší, otevře starší verze nastavení jen pro čtení a ukáže banner „Nastavení vytvořila novější verze a jsou jen pro čtení.“ Změny se neukládají. Aktualizujte HARDWARIO Monitor na novější verzi.

Kód chyby: [`settings.newer_schema`](#settings.newer_schema).

## Kódy chyb

Kódy chyb jsou seskupené podle kategorií. Kategorie určuje [návratový kód](../en/cli.md#exit-codes) příkazu `hwio`:

| Kategorie | Význam | Návratový kód |
|---|---|---|
| [`dependency`](#category-dependency) | Chybí potřebný program nebo systémová služba: J-Link Software, nRF Util, Bluetooth | 4 |
| [`probe`](#category-probe) | Sonda J-Link nebo její připojení přes USB | 5 |
| [`target`](#category-target) | Zařízení: napájení, kabel, MCU, konzole, spojení Bluetooth | 6 |
| [`firmware`](#category-firmware) | Soubor s firmwarem nebo jeho stažení | 7 |
| [`network`](#category-network) | Připojení k internetu: katalog firmwaru, kontrola aktualizací | 8 |
| [`io`](#category-io) | Soubory, složky a nastavení | 3 |
| [`usage`](#category-usage) | Chybný argument, volba nebo vstup | 2 |
| [`timeout`](#category-timeout) | Něco neodpovědělo včas | 9 |
| [`cancelled`](#category-cancelled) | Operace byla zastavena | 130 |
| [`internal`](#category-internal) | Vnitřní problém aplikace HARDWARIO Monitor | 1 |

Úspěšný příkaz skončí návratovým kódem 0. Texty níže jsou výchozí texty, které aplikace zobrazuje v češtině (CLI je vypisuje anglicky). Na mnoha místech se zobrazí přesnější zpráva, například taková, která uvádí MCU nebo soubor; tyto varianty jsou popsané u jednotlivých kódů. Hodnoty ve špičatých závorkách, například `<serial>`, se nahrazují skutečnými hodnotami.

<a id="category-dependency"></a>
### Závislosti (návratový kód 4)

<a id="dependency.jlink_not_found"></a>
#### `dependency.jlink_not_found`

**Software SEGGER J-Link nebyl nalezen.**

- **Pravděpodobně:** Není nainstalován, nebo je nainstalován v nestandardním umístění.
- **Co dělat:** Nainstalujte J-Link Software 8.00 nebo novější, nebo zadejte jeho cestu volbou --jlink-library.

Zobrazí se, když HARDWARIO Monitor nemůže najít knihovnu J-Link, například hned po instalaci. V desktopové aplikaci nastavte cestu v **Nastavení** › **J-Link** › **Knihovna** (**Vlastní cesta**); v CLI použijte `--jlink-library PATH` nebo `$HWIO_JLINK_LIBRARY`. Viz [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.jlink_load_failed"></a>
#### `dependency.jlink_load_failed`

**Knihovnu J-Link se nepodařilo načíst.**

- **Pravděpodobně:** Instalace je poškozená, je určena pro jinou architekturu procesoru, nebo v ní chybí potřebná funkce.
- **Co dělat:** Přeinstalujte J-Link Software.

Soubor knihovny byl nalezen, ale nelze ho použít, například 32bitová knihovna nebo knihovna pro x86 na počítači ARM. Zkontrolujte cestu v **Nastavení** › **J-Link** › **Knihovna**, nebo `--jlink-library`.

<a id="dependency.jlink_too_old"></a>
#### `dependency.jlink_too_old`

**J-Link Software je příliš starý.**

- **Pravděpodobně:** HARDWARIO Monitor potřebuje J-Link Software 8.00 nebo novější.
- **Co dělat:** Nainstalujte aktuální J-Link Software.

Zpráva může uvádět verzi, například „J-Link Software `<version>` je příliš starý.“ Viz [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.nrfutil_not_found"></a>
#### `dependency.nrfutil_not_found`

**nRF Util nebyl nalezen.**

- **Pravděpodobně:** Není nainstalován; je potřeba jen pro firmware modemu.
- **Co dělat:** Nainstalujte nRF Util a jeho příkaz device, nebo zadejte jeho cestu volbou --nrfutil.

nRF Util je potřeba jen pro aktualizaci firmwaru modemu LTE. V desktopové aplikaci nastavte cestu v **Nastavení** › **nRF Util** › **Cesta** (**Vlastní cesta**); v CLI použijte `--nrfutil PATH`. Viz [Nordic nRF Util](install.md#nordic-nrf-util-jen-firmware-modemu-lte).

<a id="dependency.nrfutil_device_missing"></a>
#### `dependency.nrfutil_device_missing`

**Příkaz device programu nRF Util není nainstalován.**

- **Pravděpodobně:** nRF Util je nainstalován bez příkazu device.
- **Co dělat:** Spusťte `nrfutil install device`.

Viz [Nordic nRF Util](install.md#nordic-nrf-util-jen-firmware-modemu-lte).

<a id="dependency.nrfutil_too_old"></a>
#### `dependency.nrfutil_too_old`

**nRF Util je příliš starý.**

- **Pravděpodobně:** HARDWARIO Monitor potřebuje novější nRF Util nebo příkaz device.
- **Co dělat:** Spusťte `nrfutil self-upgrade` a `nrfutil upgrade device`.

Zpráva může uvádět verzi, například „nRF Util `<version>` je příliš starý.“

<a id="ble.adapter_not_found"></a>
#### `ble.adapter_not_found`

**Nebyl nalezen žádný adaptér Bluetooth.**

- **Pravděpodobně:** Počítač nemá Bluetooth nebo je v systému zakázané.
- **Co dělat:** Povolte Bluetooth nebo připojte USB adaptér Bluetooth.

Bluetooth je k dispozici jen v CLI (`hwio ble ...`). `hwio doctor` to hlásí také jako varování. Viz [Bluetooth](bluetooth.md).

<a id="ble.adapter_off"></a>
#### `ble.adapter_off`

**Bluetooth je vypnuté.**

- **Pravděpodobně:** Bluetooth je vypnuté v nastavení systému nebo hardwarovým vypínačem.
- **Co dělat:** Zapněte Bluetooth v nastavení systému.

Bluetooth je k dispozici jen v CLI (`hwio ble ...`).

<a id="ble.permission_denied"></a>
#### `ble.permission_denied`

**HARDWARIO Monitor nemá povoleno používat Bluetooth.**

- **Pravděpodobně:** Přístup k Bluetooth byl zamítnut v nastavení soukromí.
- **Co dělat:** Povolte Bluetooth pro HARDWARIO Monitor (nebo pro svůj terminál) v Nastavení systému → Soukromí a zabezpečení → Bluetooth.

Nastává v macOS, když byla žádost o oprávnění k Bluetooth odmítnuta. Když `hwio` spouštíte z terminálu, povolte Bluetooth pro aplikaci terminálu.

<a id="ble.stack_unavailable"></a>
#### `ble.stack_unavailable`

**Systémová služba Bluetooth není dostupná.**

- **Pravděpodobně:** Systémová služba Bluetooth (v Linuxu BlueZ) neběží.
- **Co dělat:** Spusťte službu Bluetooth: `sudo systemctl start bluetooth`.

V Linuxu musí běžet služba BlueZ. Chyba se zobrazí také při neznámé chybě systémového zásobníku Bluetooth během vyhledávání.

<a id="ble.no_pairing_agent"></a>
#### `ble.no_pairing_agent`

**Žádný program není připraven vyžádat si přístupový klíč Bluetooth.**

- **Pravděpodobně:** Neběží žádný párovací agent (v prostředí GNOME může být agent aktivní jen v Nastavení → Bluetooth).
- **Co dělat:** Spárujte zařízení v systémovém nastavení Bluetooth, nebo pomocí bluetoothctl: `agent KeyboardOnly`, `default-agent`, `pair <id>`.

Jen Linux: párování bylo okamžitě zrušeno, protože žádný program nemohl zobrazit dialog pro přístupový klíč. Viz [Párování](bluetooth.md#párování).

<a id="dependency.jlink_function_missing"></a>
#### `dependency.jlink_function_missing`

**Nainstalovaný software J-Link neposkytuje funkci, kterou tato operace potřebuje.**

- **Pravděpodobně:** Knihovna J-Link je starší než verze, se kterými byl HARDWARIO Monitor testován, nebo se od nich liší.
- **Co dělat:** Nainstalujte nejnovější J-Link Software and Documentation Pack.

Některé příkazy pro cílové zařízení (například registry nebo 16bitový přístup do paměti) potřebují funkce, které poskytují jen novější knihovny J-Link. Viz [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="category-probe"></a>
### Sonda (návratový kód 5)

<a id="probe.not_found"></a>
#### `probe.not_found`

**Nebyla nalezena žádná sonda J-Link.**

- **Pravděpodobně:** Kabel USB je odpojený nebo J-Link není napájený.
- **Co dělat:** Zkontrolujte kabel USB mezi J-Linkem a počítačem.

Zobrazí se, když není připojený žádný J-Link nebo když zvolené sériové číslo (`-n <serial>`) v seznamu není. Varianty: „Sonda J-Link se sériovým číslem `<serial>` nebyla nalezena.“ a pro J-Link připojený přes síť „J-Link `<serial>` je síťový J-Link; síťové J-Linky zatím nejsou pro relace podporované.“ Použijte J-Link připojený přes USB.

<a id="probe.in_use_by_other_process"></a>
#### `probe.in_use_by_other_process`

**J-Link používá jiný program.**

- **Pravděpodobně:** Používá ho jiné okno aplikace HARDWARIO Monitor nebo příkaz hwio.
- **Co dělat:** Ukončete druhý program, nebo zvolte jiný J-Link.

HARDWARIO Monitor a `hwio` J-Link během používání zamykají. Zpráva může uvádět druhý program, například „J-Link `<serial>` používá jiný program (`<app>`, PID `<pid>`).“ Pokud program není známý, doporučení radí smazat soubor zámku `<lockfile>`, když neběží žádný jiný program HARDWARIO. Poté, co desktopová aplikace J-Link uvolnila, může být příčinou také „Po uvolnění J-Linku jej otevřel jiný program.“

<a id="probe.open_failed"></a>
#### `probe.open_failed`

**Sondu J-Link se nepodařilo otevřít.**

- **Pravděpodobně:** Používá ji jiný program nebo relace, nebo chybí oprávnění k USB (pravidla udev v Linuxu).
- **Co dělat:** Ukončete ostatní programy, které J-Link používají, a zkuste to znovu.

V Linuxu jsou častou příčinou chybějící oprávnění k USB; viz [Přístup k J-Linku přes USB](install.md#přístup-k-j-linku-přes-usb). Varianta „J-Link `<serial>` už tato aplikace používá.“ znamená, že ho má otevřený jiná karta nebo relace téhož programu.

<a id="probe.firmware_outdated"></a>
#### `probe.firmware_outdated`

**Firmware J-Linku je zastaralý.**

- **Pravděpodobně:** Firmware J-Linku se liší od firmwaru dodávaného s J-Link Software.
- **Co dělat:** Spusťte `hwio jlink update-firmware`.

Jde o varování; J-Link dál funguje. Hlásí to také `hwio doctor`. Když je firmware sondy novější než knihovna, zpráva zní „Firmware J-Linku je novější než knihovna J-Link.“ V takovém případě místo toho aktualizujte J-Link Software.

<a id="probe.firmware_update_failed"></a>
#### `probe.firmware_update_failed`

**Firmware J-Linku se nepodařilo aktualizovat.**

- **Pravděpodobně:** Aktualizace selhala, nebo sonda stále hlásí jiný firmware, než obsahuje knihovna.
- **Co dělat:** Odpojte a znovu připojte J-Link a pak znovu spusťte `hwio jlink update-firmware`.

Pochází z `hwio jlink update-firmware`.

<a id="probe.io_failed"></a>
#### `probe.io_failed`

**Komunikace s J-Linkem selhala.**

- **Pravděpodobně:** J-Link neodpověděl: krátký výpadek USB, nebo byl odpojen kabel USB.
- **Co dělat:** Zkontrolujte kabel USB mezi J-Linkem a počítačem; je-li to zapnuté, konzole se znovu připojí automaticky.

Nastává, když se během běhu konzole ztratí spojení USB s J-Linkem. Automatické opětovné připojení je v desktopové aplikaci ve výchozím stavu zapnuté (**Nastavení** › **J-Link**, nebo **Pokročilé** (⋯) v liště zařízení); v CLI použijte `--auto-reconnect`.

<a id="usb.access_denied"></a>
#### `usb.access_denied`

**Tento počítač nepovoluje přístup k J-Linku (oprávnění USB).**

- **Pravděpodobně:** Chybí pravidlo udev pro zařízení SEGGER.
- **Co dělat:** Nainstalujte pravidlo udev zobrazené v průvodci nastavením a potom J-Link znovu připojte. Pravidlo dává přístup jen uživateli přihlášenému přímo u tohoto počítače; uživatelé připojení jen přes SSH potřebují vlastní pravidlo udev.

Jen Linux. Průvodce nastavením zmíněný v textu zatím není k dispozici: nainstalujte pravidlo udev podle [Přístup k J-Linku přes USB](install.md#přístup-k-j-linku-přes-usb) a potom to zkontrolujte příkazem `hwio doctor`.

<a id="probe.held_by_program"></a>
#### `probe.held_by_program`

**Běží jiný program, který může J-Link používat.**

- **Pravděpodobně:** Jiný ladicí nebo programovací nástroj drží J-Link otevřený.
- **Co dělat:** Zavřete druhý program a zkuste to znovu.

Varování z `hwio doctor` a z diagnostické zprávy. Může uvádět program, například „`<program>` (PID `<pid>`) je spuštěn a může používat J-Link.“

<a id="ble.in_use_by_other_process"></a>
#### `ble.in_use_by_other_process`

**Zařízení používá jiný program.**

- **Pravděpodobně:** Je k němu připojené jiné okno HARDWARIO Monitor nebo příkaz `hwio`.
- **Co dělat:** Zavřete druhý program.

Zařízení Bluetooth se zamykají stejně jako J-Linky. Zpráva může uvádět druhý program („Zařízení `<id>` používá jiný program (`<app>`, PID `<pid>`).“) nebo znít „Zařízení `<id>` už tato aplikace používá.“ Pokud program není známý, doporučení radí smazat soubor zámku `<lockfile>`, když neběží žádný jiný program HARDWARIO.

<a id="usb.world_accessible"></a>
#### `usb.world_accessible`

**J-Link může používat každý uživatelský účet na tomto počítači (oprávnění USB).**

- **Pravděpodobně:** Pravidlo udev, obvykle /etc/udev/rules.d/99-jlink.rules ze SEGGER J-Link Software, zpřístupňuje programátor všem účtům.
- **Co dělat:** Odstraňte toto pravidlo (nebo jeho MODE="0666"), nainstalujte pravidlo zobrazené v průvodci nastavením a potom J-Link znovu připojte. Aktualizace J-Link Software může pravidlo nainstalovat znovu.

Varování z `hwio doctor` v Linuxu. Průvodce nastavením zmíněný v textu zatím není k dispozici: nainstalujte pravidlo podle [Přístup k J-Linku přes USB](install.md#přístup-k-j-linku-přes-usb).

<a id="category-target"></a>
### Cílové zařízení (návratový kód 6)

<a id="target.no_power"></a>
#### `target.no_power`

**Zařízení nemá napájení.**

- **Pravděpodobně:** Zařízení není napájené nebo je odpojený plochý kabel.
- **Co dělat:** Zapněte napájení zařízení a zkontrolujte plochý kabel.

Zpráva může obsahovat naměřené napětí, například „Zařízení nemá napájení (`<mV>` mV).“ CHESTER potřebuje baterie nebo napájecí vstup USB-C.

<a id="target.connect_failed"></a>
#### `target.connect_failed`

**K zařízení se nepodařilo připojit.**

- **Pravděpodobně:** Plochý kabel je odpojený nebo zapojený do nesprávného konektoru, nebo je MCU v úsporném či chráněném režimu.
- **Co dělat:** Zkontrolujte plochý kabel mezi J-Linkem a zařízením.

J-Link funguje, ale nedosáhne na MCU. Varianty uvádějí MCU, například „K MCU `<device>` zařízení se nepodařilo připojit.“ nebo „Typ MCU `<target>` nelze přečíst.“ Chráněný MCU může potřebovat [obnovení (recover)](flashing.md#obnovení-recover).

<a id="target.rtt_not_found"></a>
#### `target.rtt_not_found`

**Konzole zařízení (RTT) nebyla nalezena.**

- **Pravděpodobně:** Firmware nemá konzoli RTT, ještě startuje, nebo je procesor zastavený.
- **Co dělat:** Resetujte zařízení, nebo nahrajte firmware s podporou konzole RTT.

Konzole nenašla řídicí blok RTT včas. Příčinou může být také velmi nízká rychlost J-Linku: použijte **Automatická rychlost** (**Pokročilé** (⋯) v liště zařízení, nebo **Nastavení** › **J-Link**). LTE MCU zařízení CHESTER nemá konzoli. Viz [Konzole](console.md).

<a id="target.rtt_buffer_not_found"></a>
#### `target.rtt_buffer_not_found`

**Vyrovnávací paměť RTT nebyla nalezena.**

- **Pravděpodobně:** Firmware je starší nebo jiný.
- **Co dělat:** Použijte firmware s shellem CHESTER.

Zpráva uvádí vyrovnávací paměť, například „Vyrovnávací paměť RTT "`<name>`" nebyla nalezena.“ Když chybí jen vyrovnávací paměť logu, jde o varování („…; log zařízení se nezobrazí.“) a konzole dál funguje.

<a id="target.rtt_write_timeout"></a>
#### `target.rtt_write_timeout`

**Zařízení nečte příkazy.**

- **Pravděpodobně:** Firmware se zasekl nebo má vypnutý shell.
- **Co dělat:** Resetujte zařízení.

Zařízení nepřevzalo odeslaný příkaz včas.

<a id="target.rtt_read_failed"></a>
#### `target.rtt_read_failed`

**Čtení ze zařízení selhalo.**

- **Pravděpodobně:** Byl odpojen kabel nebo bylo zařízení resetováno.
- **Co dělat:** Zkontrolujte kabely; je-li to zapnuté, konzole se znovu připojí automaticky.

Automatické opětovné připojení je v desktopové aplikaci ve výchozím stavu zapnuté (**Nastavení** › **J-Link**, nebo **Pokročilé** (⋯) v liště zařízení).

<a id="target.rtt_write_failed"></a>
#### `target.rtt_write_failed`

**Zápis do zařízení selhal.**

- **Pravděpodobně:** Byl odpojen kabel nebo bylo zařízení resetováno.
- **Co dělat:** Zkontrolujte kabely; je-li to zapnuté, konzole se znovu připojí automaticky.

Automatické opětovné připojení je v desktopové aplikaci ve výchozím stavu zapnuté (**Nastavení** › **J-Link**, nebo **Pokročilé** (⋯) v liště zařízení).

<a id="target.not_connected"></a>
#### `target.not_connected`

**Zařízení není připojené.**

- **Pravděpodobně:** Konzole je odpojená, nebo se teprve znovu připojuje.
- **Co dělat:** Připojte konzoli znovu (F4).

Odeslali jste příkaz, když byla konzole odpojená. Se zapnutým automatickým opětovným připojením zní doporučení „Počkejte, až se konzole znovu připojí.“ V CLI spusťte konzoli znovu. Pokud k tomu dojde během dávkového souboru, zpráva zní „Dávka se zastavila po `<done>` z `<total>` příkazů.“

<a id="target.wrong_family"></a>
#### `target.wrong_family`

**J-Link je připojen k jinému MCU.**

- **Pravděpodobně:** J-Link je připojen k druhému konektoru zařízení.
- **Co dělat:** Připojte J-Link ke konektoru MCU, který chcete programovat.

Plochý kabel je v konektoru druhého MCU (například v konektoru LTE, když flashujete aplikační MCU). Zpráva může znít „Očekáván MCU `<target>` (`<part>`), ale nalezen `<found>`.“

<a id="target.protected"></a>
#### `target.protected`

**MCU je chráněn proti čtení.**

- **Pravděpodobně:** Je zapnutá ochrana přístupového portu (APPROTECT).
- **Co dělat:** Obnovte MCU (tím se vymaže) a pak do něj nahrajte firmware znovu.

Obnovení (recover) ochranu odstraní, ale MCU úplně smaže, včetně bloku informací o produktu. Viz [Obnovení (recover)](flashing.md#obnovení-recover). U konkrétního MCU zní doporučení „Spusťte `hwio <profile> <target> recover` (vymaže MCU).“; v desktopové aplikaci použijte **Obnovit (recover)…** v části **Údržba** na stránce **Nahrát FW**.

<a id="target.program_failed"></a>
#### `target.program_failed`

**Programování MCU selhalo.**

- **Pravděpodobně:** Spojení bylo přerušeno, napájení je nízké nebo je MCU chráněn.
- **Co dělat:** Zkontrolujte kabely a napájení a zkuste to znovu.

Varianta příčiny „Programování bylo přerušeno; firmware může být zapsán jen zčásti.“ znamená, že firmware musíte nahrát znovu, jinak zařízení nebude fungovat.

<a id="target.erase_failed"></a>
#### `target.erase_failed`

**Mazání MCU selhalo.**

- **Pravděpodobně:** Spojení bylo přerušeno, napájení je nízké nebo je MCU chráněn.
- **Co dělat:** Zkontrolujte kabely a napájení a zkuste to znovu.

Dvě vzácné varianty se týkají bloku informací o produktu po smazání jen firmwaru: „nelze po smazání zkontrolovat“ a „Mazání paměti programu … změnilo také jeho blok informací o produktu (PIB)“. V obou případech znovu nemažte; postupujte podle doporučení a kontaktujte podporu HARDWARIO. Viz [UICR a blok informací o produktu](flashing.md#uicr-a-blok-informací-o-produktu).

<a id="target.recover_failed"></a>
#### `target.recover_failed`

**Obnovení MCU selhalo.**

- **Pravděpodobně:** Zařízení nemá napájení nebo jeho řídicí přístupový port neodpovídá.
- **Co dělat:** Odpojte a znovu připojte napájení zařízení a zkuste to znovu.

Viz [Obnovení (recover)](flashing.md#obnovení-recover).

<a id="pib.invalid"></a>
#### `pib.invalid`

**Blok informací o produktu (PIB) chybí nebo je poškozený.**

- **Pravděpodobně:** Nebyl nikdy zapsán, nebo byl vymazán úplným smazáním či obnovením.
- **Co dělat:** Pro jeho obnovení kontaktujte podporu HARDWARIO.

Blok informací o produktu obsahuje sériové číslo a verzi hardwaru. Zpráva může v závorce uvádět kontrolu, která selhala. Viz [UICR a blok informací o produktu](flashing.md#uicr-a-blok-informací-o-produktu).

<a id="ble.device_not_found"></a>
#### `ble.device_not_found`

**Zařízení Bluetooth nebylo nalezeno.**

- **Pravděpodobně:** Zařízení je mimo dosah, vypnuté nebo má vypnuté Bluetooth.
- **Co dělat:** Přibližte zařízení a ověřte, že má zapnuté Bluetooth.

Pochází z `hwio ble console` nebo `hwio ble command` s `--name` nebo `-a`. Zařízení v dosahu zobrazíte příkazem `hwio ble scan`. Viz [Bluetooth](bluetooth.md).

<a id="ble.connect_failed"></a>
#### `ble.connect_failed`

**K zařízení se nepodařilo připojit přes Bluetooth.**

- **Pravděpodobně:** Signál je slabý nebo je zařízení zaneprázdněné jiným připojením.
- **Co dělat:** Přibližte se, odpojte od zařízení jiné telefony nebo počítače a zkuste to znovu.

Varianta příčiny je „Zařízení odmítlo spustit konzoli: může být stále připojené (k tomuto nebo jinému počítači), nebo párování nebylo dokončeno.“ Potom počkejte několik sekund, nebo zařízení odpojte v systémovém nastavení Bluetooth. Viz [Párování](bluetooth.md#párování).

<a id="ble.service_not_found"></a>
#### `ble.service_not_found`

**Zařízení nenabízí službu konzole (NUS).**

- **Pravděpodobně:** Firmware nemá shell přes Bluetooth.
- **Co dělat:** Použijte firmware se zapnutou konzolí přes Bluetooth.

V zařízení neběží firmware CHESTER s konzolí přes Bluetooth.

<a id="ble.not_paired"></a>
#### `ble.not_paired`

**Zařízení není spárované s tímto počítačem.**

- **Pravděpodobně:** Párování bylo zrušeno, selhalo nebo bylo na jedné straně odstraněno.
- **Co dělat:** Spárujte zařízení: zadejte jeho přístupový klíč, až se systém zeptá.

Spusťte `hwio ble pair` (Linux a Windows); v macOS se systém na přístupový klíč zeptá při připojení. Viz [Párování](bluetooth.md#párování).

<a id="ble.pairing_failed"></a>
#### `ble.pairing_failed`

**Párování se zařízením selhalo.**

- **Pravděpodobně:** Chybný přístupový klíč, vypršení času nebo zastaralé spárování na jedné straně.
- **Co dělat:** Odeberte zařízení v systémovém nastavení Bluetooth a spárujte ho znovu.

V Linuxu může být příčinou také to, že se neobjevil dialog pro přístupový klíč, protože neběží žádný párovací agent. Viz [Párování](bluetooth.md#párování).

<a id="ble.write_failed"></a>
#### `ble.write_failed`

**Odeslání do zařízení přes Bluetooth selhalo.**

- **Pravděpodobně:** Spojení bylo během zápisu ztraceno.
- **Co dělat:** Připojte se znovu.

Spojení Bluetooth se ztratilo během odesílání příkazu.

<a id="ble.write_timeout"></a>
#### `ble.write_timeout`

**Zařízení nečte příkazy.**

- **Pravděpodobně:** Firmware je zaneprázdněný nebo spojení vázne.
- **Co dělat:** Připojte se znovu; pokud se to opakuje, restartujte zařízení.

Zařízení nepotvrdilo příkaz odeslaný přes Bluetooth včas.

<a id="ble.disconnected"></a>
#### `ble.disconnected`

**Spojení Bluetooth se zařízením bylo ztraceno.**

- **Pravděpodobně:** Zařízení je mimo dosah, bylo restartováno nebo vypnuto.
- **Co dělat:** Přibližte zařízení; konzole se znovu připojí automaticky, pokud je to zapnuté.

Zpráva může uvádět zařízení, například „Spojení Bluetooth s `<name>` bylo ztraceno.“

<a id="target.not_halted"></a>
#### `target.not_halted`

**Procesor běží.**

- **Pravděpodobně:** Registry lze číst jen při zastaveném jádru.
- **Co dělat:** Nejprve zastavte jádro.

Pochází z `hwio chester app regs` (nebo `lte regs`) a ze serveru MCP (`hwio mcp`). Nejprve zastavte jádro příkazem `hwio chester app halt`.

<a id="target.address_out_of_range"></a>
#### `target.address_out_of_range`

**Rozsah adres není pro tuto operaci povolen.**

- **Pravděpodobně:** Zápis do paměti je povolen jen v rozsazích RAM a periferií cílového MCU.
- **Co dělat:** Použijte adresu uvnitř rozsahů RAM nebo periferií cílového MCU.

Pochází z `mem read`, `mem write` a ze serveru MCP (`hwio mcp`). Příčina může také uvádět, že rozsah přesahuje konec adresního prostoru, nebo že zasahuje do systémových či ladicích registrů procesoru nebo do registrů řadiče paměti flash, do kterých se nikdy nezapisuje.

<a id="target.memory_access_failed"></a>
#### `target.memory_access_failed`

**Čtení nebo zápis paměti cílového MCU selhal.**

- **Pravděpodobně:** Adresa není namapovaná, nebo je jádro ve stavu chyby.
- **Co dělat:** Zkontrolujte adresu a zkuste to znovu po resetu.

Pochází z `mem read`, `mem write` a ze serveru MCP (`hwio mcp`).

<a id="category-firmware"></a>
### Firmware (návratový kód 7)

<a id="firmware.hex_parse_error"></a>
#### `firmware.hex_parse_error`

**Soubor s firmwarem není platný soubor HEX.**

- **Pravděpodobně:** Soubor je poškozený nebo to není soubor HEX.
- **Co dělat:** Použijte soubor .hex vytvořený sestavením nebo jej stáhněte znovu.

Zpráva může uvádět řádek, například „Soubor s firmwarem není platný soubor HEX (řádek `<n>`).“

<a id="firmware.invalid_image"></a>
#### `firmware.invalid_image`

**Soubor není podporovaný soubor s firmwarem.**

- **Pravděpodobně:** Nejde o balíček CHESTER LTE, zip s firmwarem modemu ani soubor HEX, nebo zip překračuje bezpečnostní limity.
- **Co dělat:** Použijte soubor .hex pro CHESTER nebo balíček LTE (.zip).

HARDWARIO Monitor kontroluje obsah souboru, ne příponu. Varianty: „Soubor není balíček CHESTER LTE, firmware modemu ani soubor HEX.“ a u staženého souboru „Stažený soubor není `<name>`.“

<a id="firmware.out_of_range"></a>
#### `firmware.out_of_range`

**Firmware neodpovídá MCU.**

- **Pravděpodobně:** Obraz byl sestaven pro jiný MCU.
- **Co dělat:** Zkontrolujte, že je firmware určen pro tento MCU.

Zpráva může uvádět adresu, například „Firmware neodpovídá MCU `<target>` (adresa `<addr>`).“ Varianta „Firmware zapisuje do bloku informací o produktu (PIB) (adresa `<addr>`).“ znamená, že by obraz přepsal blok informací o produktu, což není nikdy povoleno.

<a id="firmware.uicr_in_image"></a>
#### `firmware.uicr_in_image`

**Firmware zapisuje také do konfigurační oblasti UICR.**

- **Pravděpodobně:** Sestavení obsahuje nastavení UICR.
- **Co dělat:** Pokud je to záměr, spusťte příkaz znovu s --allow-uicr; blok informací o produktu (PIB) zůstane zachován.

`--allow-uicr` je volba CLI; v desktopové aplikaci zapněte **Povolit zápis UICR** v části **Možnosti** na stránce **Nahrát FW**. Viz [UICR a blok informací o produktu](flashing.md#uicr-a-blok-informací-o-produktu).

<a id="firmware.verify_failed"></a>
#### `firmware.verify_failed`

**Ověření zapsaného firmwaru selhalo.**

- **Pravděpodobně:** Spojení je nestabilní nebo je paměť flash chráněná či vadná.
- **Co dělat:** Nahrajte firmware znovu; pokud se chyba opakuje, snižte rychlost J-Linku nebo zkontrolujte kabely.

Zpráva může uvádět adresu, například „Ověření selhalo na adrese `<0xADDR>`.“ Nižší rychlost nastavíte v **Pokročilé** (⋯) v liště zařízení, v **Nastavení** › **J-Link**, nebo volbou `--jlink-speed`.

<a id="firmware.not_found_in_catalog"></a>
#### `firmware.not_found_in_catalog`

**Firmware nebyl v katalogu nalezen.**

- **Pravděpodobně:** ID firmwaru je chybné nebo byl firmware smazán.
- **Co dělat:** Zkontrolujte ID firmwaru.

Zobrazí se, když flashujete podle 32místného ID firmwaru. Zpráva může uvádět ID, například „Firmware `<id>` nebyl v katalogu nalezen.“

<a id="firmware.modem_update_failed"></a>
#### `firmware.modem_update_failed`

**Aktualizace firmwaru modemu selhala.**

- **Pravděpodobně:** Zip s firmwarem modemu je chybný, spojení bylo přerušeno nebo nRF Util ohlásil chybu (viz Podrobnosti).
- **Co dělat:** Zkontrolujte kabel a napájení a nahrajte firmware modemu znovu.

Firmware modemu LTE zapisuje nRF Util; jeho výstup je v **Podrobnosti**. Viz [Nordic nRF Util](install.md#nordic-nrf-util-jen-firmware-modemu-lte).

<a id="firmware.checksum_mismatch"></a>
#### `firmware.checksum_mismatch`

**Stažený firmware je poškozený (nesouhlasí kontrolní součet).**

- **Pravděpodobně:** Soubor byl během stahování změněn nebo poškozen.
- **Co dělat:** Zkuste to znovu; pokud se to opakuje, nahlaste to společnosti HARDWARIO.

Stažený soubor neodpovídal kontrolnímu součtu z katalogu, proto nebyl použit. Desktopová aplikace může ukázat „Stažený soubor je poškozený. Zkuste to znovu.“

<a id="category-network"></a>
### Síť (návratový kód 8)

<a id="network.download_failed"></a>
#### `network.download_failed`

**Stažení firmwaru selhalo.**

- **Pravděpodobně:** Není připojení k internetu, blokuje jej proxy nebo selhal server.
- **Co dělat:** Zkontrolujte připojení k internetu a zkuste to znovu.

Zpráva může uvádět firmware, například „Stažení firmwaru `<id>` selhalo.“ nebo „Stažení `<name>` selhalo.“ V desktopové aplikaci můžete místo toho zvolit také vlastní soubor.

<a id="update.network"></a>
#### `update.network`

**Nepodařilo se spojit s GitHubem a zjistit dostupnost aktualizací.**

- **Pravděpodobně:** Není k dispozici připojení k internetu, nebo GitHub blokuje firewall či proxy.
- **Co dělat:** Zkontrolujte připojení nebo to zkuste později.

Pochází z kontroly aktualizací (**Nastavení** › **O aplikaci**, nebo `hwio update check`).

<a id="update.rate_limited"></a>
#### `update.rate_limited`

**GitHub dočasně odmítl kontrolu aktualizací.**

- **Pravděpodobně:** GitHub omezuje, jak často se smí z jedné síťové adresy ptát; limit mohly vyčerpat jiné počítače ve stejné síti.
- **Co dělat:** Zkuste to znovu za hodinu.

Pochází z kontroly aktualizací. GitHub omezuje počet požadavků z jedné síťové adresy.

<a id="update.bad_response"></a>
#### `update.bad_response`

**Informace o aktualizacích z GitHubu se nepodařilo přečíst.**

- **Pravděpodobně:** GitHub poslal neočekávanou odpověď, nebo ji změnila proxy.
- **Co dělat:** Zkuste to později; pokud problém trvá, podívejte se na stránku vydání: https://github.com/hardwario/hio-monitor/releases

Pochází z kontroly aktualizací.

<a id="update.too_large"></a>
#### `update.too_large`

**Informace o aktualizacích z GitHubu se nepodařilo přečíst.**

- **Pravděpodobně:** GitHub poslal neočekávanou odpověď, nebo ji změnila proxy.
- **Co dělat:** Zkuste to později; pokud problém trvá, podívejte se na stránku vydání: https://github.com/hardwario/hio-monitor/releases

Pochází z kontroly aktualizací, když je odpověď z GitHubu větší, než je povoleno. Používá stejné texty jako `update.bad_response`.

<a id="category-io"></a>
### Soubory a nastavení: io (návratový kód 3)

<a id="io.file_not_found"></a>
#### `io.file_not_found`

**Soubor nebyl nalezen.**

- **Pravděpodobně:** Cesta je chybná nebo byl soubor přesunut či smazán.
- **Co dělat:** Zkontrolujte cestu k souboru (viz Podrobnosti).

Cesta je v **Podrobnosti**. U serveru MCP (`hwio mcp`) může zpráva znít "The `<what>` `<dir>` does not exist." u volby se složkou, například `--dump-dir`.

<a id="io.read_failed"></a>
#### `io.read_failed`

**Soubor nelze přečíst.**

- **Pravděpodobně:** Nemáte oprávnění ke čtení nebo disk ohlásil chybu.
- **Co dělat:** Zkontrolujte cestu k souboru a jeho oprávnění (viz Podrobnosti).

U dávkového souboru konzole v desktopové aplikaci zní zpráva „Dávkový soubor se nepodařilo přečíst.“

<a id="io.write_failed"></a>
#### `io.write_failed`

**Soubor nelze zapsat.**

- **Pravděpodobně:** Disk je plný nebo jen pro čtení, nebo nemáte oprávnění k zápisu.
- **Co dělat:** Zkontrolujte cestu, volné místo na disku a oprávnění (viz Podrobnosti).

Cesta je v **Podrobnosti**. Varianty: „Soubor se nepodařilo uložit.“ při ukládání konzole v desktopové aplikaci a „Soubor s výpisem konzole nelze zapsat; ukládání do souboru je pro tuto relaci vypnuté.“ U serveru MCP (`hwio mcp`) může zpráva znít "The `<what>` `<dir>` is not writable."

<a id="settings.newer_schema"></a>
#### `settings.newer_schema`

**Nastavení bylo uloženo novější verzí a je otevřeno jen pro čtení.**

- **Pravděpodobně:** Nyní běží starší verze aplikace HARDWARIO Monitor.
- **Co dělat:** Změny nebudou uloženy; aktualizujte HARDWARIO Monitor.

Nastává poté, co jste použili novější verzi a potom spustili starší. Desktopová aplikace ukáže banner „Nastavení vytvořila novější verze a jsou jen pro čtení.“ Soubor nastavení se nemění, takže novější verze má dál všechna svá nastavení.

<a id="diag.report_write_failed"></a>
#### `diag.report_write_failed`

**Diagnostickou zprávu se nepodařilo uložit.**

- **Pravděpodobně:** Do složky nelze zapisovat nebo je disk plný.
- **Co dělat:** Zvolte jinou složku.

Pochází z **Nápověda** › **Vytvořit diagnostickou zprávu…** nebo z `hwio diag report`. Viz [Diagnostická zpráva](privacy.md#diagnostická-zpráva).

<a id="mcp.port_in_use"></a>
#### `mcp.port_in_use`

**Port MCP je již používán.**

- **Pravděpodobně:** Naslouchá na něm jiný HARDWARIO Monitor, `hwio mcp --http` nebo jiný server MCP (například rttt).
- **Co dělat:** Ukončete druhý server nebo zvolte jiný port pomocí --port.

Server MCP (`hwio mcp --http`). Viz [MCP](../en/mcp.md).

<a id="mcp.listen_failed"></a>
#### `mcp.listen_failed`

**Server MCP se nepodařilo spustit.**

- **Pravděpodobně:** Operační systém odmítl otevřít naslouchající socket na 127.0.0.1.
- **Co dělat:** Zvolte jiný port pomocí --port.

Server MCP (`hwio mcp --http`).

<a id="mcp.token_file"></a>
#### `mcp.token_file`

**Soubor s tokenem MCP nelze přečíst ani vytvořit.**

- **Pravděpodobně:** Soubor nebo jeho složka mají nesprávná oprávnění, nebo soubor neobsahuje platný token.
- **Co dělat:** Zkontrolujte oprávnění souboru, nebo soubor smažte, aby se vytvořil nový token.

Server MCP (`hwio mcp --http`). Viz [Security](../en/mcp.md#security).

<a id="io.file_exists"></a>
#### `io.file_exists`

**Výstupní soubor již existuje.**

- **Pravděpodobně:** Tento příkaz nikdy nepřepisuje existující soubory.
- **Co dělat:** Zvolte nový název souboru, nebo soubor nejprve smažte.

Pochází z příkazů, které zapisují soubor s `-o` (například `uicr read` nebo `mem read`), a z výpisů paměti serveru MCP (`hwio mcp`). Chcete-li soubor nahradit, přidejte `--force`.

<a id="cli.file_is_symlink"></a>
#### `cli.file_is_symlink`

**Výstupní soubor je symbolický odkaz.**

- **Pravděpodobně:** Symbolické odkazy se nikdy nenásledují ani nenahrazují, ani s volbou --force.
- **Co dělat:** Zvolte jiný název souboru, nebo nejprve odkaz odstraňte.

Jen CLI, s `-o FILE --force`.

<a id="category-usage"></a>
### Použití (návratový kód 2)

<a id="usage.invalid_argument"></a>
#### `usage.invalid_argument`

**Argument nebo volba je neplatná.**

- **Pravděpodobně:** Požadavek obsahuje nepodporovanou hodnotu.
- **Co dělat:** Zkontrolujte argumenty a zkuste to znovu.

Zpráva obvykle přesně uvádí, která hodnota je chybná, například chybná volba (doporučení pak zní "Run `hwio --help`."), neznámý profil zařízení nebo cíl, rychlost J-Linku nebo latence mimo rozsah, chybná adresa nebo délka, ID firmwaru, které nemá 32 šestnáctkových číslic, nebo více zařízení Bluetooth odpovídajících `--name` (použijte `--address`). V desktopové aplikaci zahrnuje také dávkové soubory konzole, které jsou příliš velké, obsahují příliš dlouhý příkaz nebo neobsahují žádné příkazy. Viz [CLI](../en/cli.md).

<a id="usage.confirmation_required"></a>
#### `usage.confirmation_required`

**Tato operace vyžaduje potvrzení.**

- **Pravděpodobně:** Příkaz běží neinteraktivně, takže se nemůže zeptat.
- **Co dělat:** Spusťte ho znovu s volbou --yes.

Jen CLI. Příkazy, které mažou nebo zapisují (flash, erase, recover, zápisy do paměti a UICR, `jlink update-firmware`, dávka konzole), se před spuštěním ptají. Bez terminálu nebo s `--json` se zeptat nemohou.

<a id="ble.pairing_not_supported"></a>
#### `ble.pairing_not_supported`

**Tento systém páruje zařízení Bluetooth jen při připojení.**

- **Pravděpodobně:** macOS zařízení spáruje automaticky při připojení.
- **Co dělat:** Připojte se k zařízení; macOS se zeptá na přístupový klíč.

Jen macOS, z `hwio ble pair`. Viz [Párování](bluetooth.md#párování).

<a id="mcp.tool_not_allowed"></a>
#### `mcp.tool_not_allowed`

**Tento nástroj MCP server nepovoluje.**

- **Pravděpodobně:** Destruktivní nástroje je nutné povolit při spuštění serveru.
- **Co dělat:** Spusťte server s --allow a kategorií nástroje, nebo s --allow all.

Server MCP (`hwio mcp`). Zpráva může uvádět nástroj a položky, které je třeba povolit, například "Start the server with --allow `<items>` (or --allow all)." Viz [Security](../en/mcp.md#security).

<a id="mcp.device_ambiguous"></a>
#### `mcp.device_ambiguous`

**Je připojeno více zařízení a nástroj neví, které použít.**

- **Pravděpodobně:** Argument device chybí, ale otevřeno je více zařízení.
- **Co dělat:** Zadejte argument device; identifikátory ukáže list_devices.

Server MCP (`hwio mcp`). `list_devices` je nástroj MCP.

<a id="mcp.device_not_open"></a>
#### `mcp.device_not_open`

**Zařízení není připojené.**

- **Pravděpodobně:** Nebylo nikdy připojeno, bylo odpojeno, nebo byla zavřena jeho karta.
- **Co dělat:** Nejprve zavolejte connect.

Server MCP (`hwio mcp`). `connect` je nástroj MCP.

<a id="mcp.capability_missing"></a>
#### `mcp.capability_missing`

**Tento nástroj potřebuje připojení přes J-Link.**

- **Pravděpodobně:** Zařízení je připojené přes Bluetooth, které poskytuje jen konzoli.
- **Co dělat:** Připojte zařízení pomocí sondy J-Link.

Server MCP (`hwio mcp`).

<a id="mcp.to_file_not_allowed"></a>
#### `mcp.to_file_not_allowed`

**Název souboru musí být prostý název.**

- **Pravděpodobně:** Výpisy paměti se zapisují jen do složky výpisů serveru.
- **Co dělat:** Použijte prostý název souboru, například flash.bin, nebo spusťte server s volbou --dump-dir DIR.

Server MCP (`hwio mcp`). Zpráva může uvádět složku výpisů. Viz [Security](../en/mcp.md#security).

<a id="category-timeout"></a>
### Vypršení časového limitu (návratový kód 9)

<a id="timeout"></a>
#### `timeout`

**Vypršel časový limit operace.**

- **Pravděpodobně:** Druhá strana neodpověděla včas.
- **Co dělat:** Zkuste to znovu.

Obecné vypršení časového limitu: například se zařízení nepřipojilo včas („Zařízení se nepřipojilo včas.“), flashování nebo mazání trvalo příliš dlouho, nebo nRF Util nedoběhl. `hwio chester app info` používá vlastní texty, když firmware neodpoví na `info show`.

<a id="diag.check_timeout"></a>
#### `diag.check_timeout`

**Kontrola nebyla dokončena včas.**

- **Pravděpodobně:** Systém je pomalý nebo některý nástroj neodpovídá.
- **Co dělat:** Spusťte kontroly znovu.

Varování z `hwio doctor` nebo z diagnostické zprávy. Zpráva může uvádět kontrolu, například: Kontrola „`<title>`“ nebyla dokončena včas.

<a id="mcp.consent_timeout"></a>
#### `mcp.consent_timeout`

**Na žádost o povolení nikdo neodpověděl.** Vyhrazeno pro server MCP desktopové aplikace, který zatím není k dispozici.

<a id="category-cancelled"></a>
### Zrušeno (návratový kód 130)

<a id="cancelled"></a>
#### `cancelled`

**Zrušeno uživatelem.**

- **Pravděpodobně:** Operace byla zastavena před dokončením.
- **Co dělat:** V případě potřeby operaci spusťte znovu.

Zobrazí se, když stisknete Ctrl+C nebo **Zrušit**, odpovíte Ne na potvrzení, nebo zavřete zařízení. Varianta „Výstup byl uzavřen.“ znamená, že program, který čte výstup `hwio`, skončil, například `hwio ... --json | head`.

<a id="mcp.consent_denied"></a>
#### `mcp.consent_denied`

**Uživatel tuto akci nepovolil.** Vyhrazeno pro server MCP desktopové aplikace, který zatím není k dispozici.

<a id="category-internal"></a>
### Vnitřní chyby (návratový kód 1)

<a id="internal.unexpected"></a>
#### `internal.unexpected`

**Došlo k neočekávané vnitřní chybě.**

- **Pravděpodobně:** Jde o chybu v aplikaci HARDWARIO Monitor.
- **Co dělat:** Nahlaste ji prosím a přiložte soubor protokolu (viz Podrobnosti).

Vytvořte prosím [diagnostickou zprávu](privacy.md#diagnostická-zpráva) a pošlete ji společnosti HARDWARIO.

<a id="internal.queue_full"></a>
#### `internal.queue_full`

**Vnitřní vyrovnávací paměť je plná.**

- **Pravděpodobně:** Jde o chybu v aplikaci HARDWARIO Monitor nebo je počítač přetížený.
- **Co dělat:** Nahlaste ji prosím a přiložte soubor protokolu (viz Podrobnosti).

Například zařízení posílá řádky konzole rychleji, než je lze zobrazit. Vytvořte prosím [diagnostickou zprávu](privacy.md#diagnostická-zpráva).

<a id="internal.session_limit"></a>
#### `internal.session_limit`

**Je otevřeno příliš mnoho zařízení (nejvýše 8).**

- **Pravděpodobně:** HARDWARIO Monitor pracuje nejvýše s 8 zařízeními současně.
- **Co dělat:** Nejprve zavřete některé zařízení.

<a id="internal.busy"></a>
#### `internal.busy`

**Na tomto zařízení probíhá jiná operace.**

- **Pravděpodobně:** Najednou může probíhat jen jedna operace.
- **Co dělat:** Počkejte na její dokončení nebo ji zrušte.

Například jste spustili flashování, zatímco běžel dávkový soubor konzole. Pokud se tím dávkový soubor přeruší, zpráva zní „Dávka se zastavila po `<done>` z `<total>` příkazů.“

<a id="mcp.too_many_requests"></a>
#### `mcp.too_many_requests`

**Běží příliš mnoho požadavků MCP.**

- **Pravděpodobně:** Server spouští nejvýše 32 volání nástrojů současně.
- **Co dělat:** Počkejte, až běžící volání skončí, a zkuste to znovu.

Server MCP (`hwio mcp`).
