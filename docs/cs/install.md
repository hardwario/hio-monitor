# Instalace HARDWARIO Monitor

[Dokumentace](index.md) › Instalace

HARDWARIO Monitor je pro každý operační systém jeden balíček. Každý balíček obsahuje desktopovou aplikaci
(`hwio-monitor`) i nástroj pro příkazovou řádku (`hwio`). Pro sondu J-Link potřebujete také **SEGGER J-Link
Software** a jen pro firmware modemu LTE **Nordic nRF Util**. Ani jeden z nich není součástí balíčků; HARDWARIO
Monitor je používá z vaší instalace.

## Stažení

Nejnovější vydání stáhněte ze [stránky vydání][releases].

| Systém | Soubor |
|---|---|
| Windows 10 22H2 nebo novější, 64bitový | `hwio-monitor-<version>-windows-x64.msi` |
| macOS 13.3 nebo novější, Apple silicon i Intel | `hwio-monitor-<version>-macos-universal.dmg` |
| Ubuntu 24.04 nebo novější, Debian 13 nebo novější | `hardwario-monitor_<version>_amd64.deb` |
| Ubuntu 22.04 nebo novější a další linuxové distribuce s glibc 2.35 nebo novější, x86-64 | `hwio-monitor-<version>-x86_64.AppImage` |

Každé vydání obsahuje také soubor `SHA256SUMS`. Stažený soubor ověříte takto:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Windows

1. Spusťte MSI. Instaluje se pro všechny uživatele do `C:\Program Files\HARDWARIO\Monitor` a vyžaduje oprávnění
   správce.
2. V seznamu funkcí:
   - **Add hwio to PATH** (zapnuto): příkaz `hwio` funguje v nových terminálech.
   - **Desktop shortcut** (vypnuto): zástupce na ploše.
3. Spusťte **HARDWARIO Monitor** z nabídky Start.

Instalátor je podepsaný společností HARDWARIO a.s. Dokud služba Microsoft SmartScreen nové vydání nezná, může Windows
před spuštěním zobrazit varování: zvolte **Další informace → Přesto spustit**.

Instalace novější verze nahradí starší; nastavení zůstane zachováno. Starší verzi nelze nainstalovat přes novější
("A newer version of HARDWARIO Monitor is already installed."): nejprve novější verzi odinstalujte. Odinstalace se
provádí v **Nastavení → Aplikace**.

Tichá instalace, například se zástupcem na ploše:

```bat
msiexec /i hwio-monitor-<version>-windows-x64.msi /qn ADDDESKTOPSHORTCUT=1
```

## macOS

1. Otevřete DMG a přetáhněte **HARDWARIO Monitor** do složky **Aplikace**.
2. Spusťte ho ze složky Aplikace. Aplikace je podepsaná společností HARDWARIO a notarizovaná společností Apple, takže
   se otevře bez varování.

Příkaz `hwio` je uvnitř balíčku aplikace a není v cestě `PATH`. **Nastavení → O aplikaci → Příkazový řádek** ukazuje
jeho cestu a příkaz, který vytvoří odkaz; ten můžete zkopírovat:

```sh
sudo ln -s '/Applications/HARDWARIO Monitor.app/Contents/MacOS/hwio' /usr/local/bin/hwio
```

**Bluetooth:** při prvním příkazu `hwio ble` se macOS zeptá na přístup k Bluetooth. macOS udělí oprávnění aplikaci,
ze které `hwio` spouštíte, obvykle Terminálu. Pokud jste přístup odmítli, povolte ho v **Nastavení systému →
Soukromí a zabezpečení → Bluetooth**.

Při aktualizaci nahraďte aplikaci ve složce Aplikace novou verzí. Při odinstalaci ji přesuňte do Koše.

## Linux

### .deb (Ubuntu 24.04+, Debian 13+)

```sh
sudo apt install ./hardwario-monitor_<version>_amd64.deb
```

Balíček se instaluje do `/opt/hardwario-monitor`, umístí `hwio-monitor` a `hwio` do `/usr/bin`, přidá aplikaci do
nabídky prostředí a nainstaluje pravidlo udev pro sondy J-Link (`/usr/lib/udev/rules.d/60-hardwario-jlink.rules`).
Pro Bluetooth závisí na balíčku `bluez`. Odeberete ho příkazem `sudo apt remove hardwario-monitor`.

### AppImage (Ubuntu 22.04+ a další distribuce)

```sh
chmod +x hwio-monitor-<version>-x86_64.AppImage
./hwio-monitor-<version>-x86_64.AppImage
```

AppImage obsahuje oba programy. Nástroj pro příkazovou řádku spustí, když je `hwio` prvním argumentem nebo když je
spuštěn přes odkaz s názvem `hwio`:

```sh
./hwio-monitor-<version>-x86_64.AppImage hwio jlink list
ln -s "$PWD/hwio-monitor-<version>-x86_64.AppImage" ~/.local/bin/hwio
```

Bez FUSE (některé kontejnery a minimální systémy) přidejte `--appimage-extract-and-run` nebo nastavte
`APPIMAGE_EXTRACT_AND_RUN=1`. AppImage se sám neaktualizuje a neinstaluje pravidlo udev: viz další oddíl.

### Přístup k J-Linku přes USB

Balíček .deb toto pravidlo instaluje. U AppImage ho nainstalujte jednou ručně:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-hardwario-jlink.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Potom J-Link odpojte a znovu připojte. Pravidlo zpřístupní sondu uživateli přihlášenému přímo u počítače, ne všem
účtům. Uživatel připojený jen přes SSH takové přihlášení nemá a potřebuje vlastní pravidlo.

Instalátor SEGGER může přidat `/etc/udev/rules.d/99-jlink.rules` s `MODE="0666"`, díky čemuž může sondu používat
každý účet na počítači. `hwio doctor` na to upozorní ([usb.world_accessible](troubleshooting.md#usb.world_accessible)).
Pokud to nechcete, odstraňte toto pravidlo nebo jeho `MODE`; aktualizace J-Link Software ho může nainstalovat znovu.

### Bluetooth

Bluetooth používá BlueZ. Pokud neběží: `sudo systemctl start bluetooth`. Párování potřebuje program, který se zeptá
na přístupový klíč (párovací agent), obvykle nastavení Bluetooth v desktopovém prostředí. Viz
[Bluetooth LE](bluetooth.md#párování).

## SEGGER J-Link Software

Z webu SEGGER nainstalujte **J-Link Software and Documentation Pack 8.00 nebo novější**. HARDWARIO Monitor ho najde
ve výchozích umístěních:

| Systém | Prohledává se |
|---|---|
| Windows | instalační cesta v registru (`SOFTWARE\SEGGER\J-Link`), `C:\Program Files\SEGGER\JLink*` |
| macOS | `/Applications/SEGGER/JLink`, `/Applications/SEGGER/JLink_V*` |
| Linux | `/opt/SEGGER/JLink`, `/opt/SEGGER/JLink_V*` |

Je-li nainstalováno více verzí, použije se nejnovější. Chcete-li použít jinou kopii, zvolte **Vlastní cesta** v
**Nastavení → J-Link → Knihovna**, nebo v příkazové řádce zadejte `--jlink-library PATH` či nastavte
`HWIO_JLINK_LIBRARY`. Pořadí je: `--jlink-library`, potom `HWIO_JLINK_LIBRARY`, potom nastavení a nakonec výchozí
umístění. Cesta, která neexistuje, je chyba; HARDWARIO Monitor se pak k výchozím umístěním nevrací.

## Nordic nRF Util (jen firmware modemu LTE)

nRF Util aktualizuje firmware modemu v LTE MCU. Vše ostatní funguje bez něj. Nainstalujte `nrfutil` od Nordic
Semiconductor a potom jeho příkaz `device`:

```sh
nrfutil install device
```

HARDWARIO Monitor ho hledá v **Nastavení → nRF Util → Cesta** (nebo `--nrfutil PATH`, nebo `HWIO_NRFUTIL`), potom
v cestě `PATH`, potom v `~/.nrfutil/bin` (ve Windows `%USERPROFILE%\.nrfutil\bin`) a v `/usr/local/bin`. Je-li příliš
starý: `nrfutil self-upgrade` a `nrfutil upgrade device`.

## Kontrola instalace

```sh
hwio doctor
```

`hwio doctor` zkontroluje J-Link Software, nRF Util, přístup k J-Linku přes USB (Linux), jiné programy, které drží
sondu, připojené sondy a jejich firmware, složky nastavení a logů a Bluetooth. U každého problému uvede, co dělat.
V aplikaci jsou tytéž kontroly součástí diagnostické zprávy (**Nápověda → Vytvořit diagnostickou zprávu…**) a
**Nastavení → J-Link** a **Nastavení → nRF Util** ukazují, co bylo nalezeno.

Další krok: [Začínáme](getting-started.md).

[releases]: https://github.com/hardwario/monitor/releases/latest
