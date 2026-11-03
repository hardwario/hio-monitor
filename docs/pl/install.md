# Instalacja HARDWARIO Monitor

[Dokumentacja](index.md) › Instalacja

HARDWARIO Monitor to jeden pakiet dla każdego systemu operacyjnego. Każdy pakiet zawiera aplikację desktopową
(`hwio-monitor`) i narzędzie wiersza poleceń (`hwio`). Potrzebne jest też oprogramowanie **SEGGER J-Link Software**
dla sondy J-Link, a tylko dla firmware modemu LTE także **Nordic nRF Util**. Żadne z nich nie jest dołączone do
pakietów; HARDWARIO Monitor korzysta z nich z Twojej instalacji.

## Pobieranie

Najnowsze wydanie można pobrać ze [strony wydań][releases].

| System | Plik |
|---|---|
| Windows 10 22H2 lub nowszy, 64-bitowy | `hwio-monitor-<version>-windows-x64.msi` |
| macOS 13.3 lub nowszy, Apple silicon i Intel | `hwio-monitor-<version>-macos-universal.dmg` |
| Ubuntu 24.04 lub nowszy, Debian 13 lub nowszy | `hardwario-monitor_<version>_amd64.deb` |
| Ubuntu 22.04 lub nowszy i inne dystrybucje Linuksa z glibc 2.35 lub nowszą, x86-64 | `hwio-monitor-<version>-x86_64.AppImage` |

Każde wydanie zawiera też plik `SHA256SUMS`. Aby sprawdzić pobrany plik:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Windows

1. Uruchom plik MSI. Instaluje on aplikację dla wszystkich użytkowników w `C:\Program Files\HARDWARIO\Monitor` i
   wymaga uprawnień administratora.
2. Na liście funkcji:
   - **Add hwio to PATH** (włączone): polecenie `hwio` działa w nowo otwartych terminalach.
   - **Desktop shortcut** (wyłączone): skrót na pulpicie.
3. Uruchom **HARDWARIO Monitor** z menu Start.

Instalator jest podpisany przez HARDWARIO a.s. Dopóki nowe wydanie jest jeszcze nieznane usłudze Microsoft
SmartScreen, Windows może wyświetlić ostrzeżenie przed jego uruchomieniem: wybierz **More info → Run anyway**
(**Więcej informacji → Uruchom mimo to**).

Instalacja nowszej wersji zastępuje starszą; ustawienia zostają zachowane. Starszej wersji nie można zainstalować
na nowszą („A newer version of HARDWARIO Monitor is already installed.”): najpierw odinstaluj nowszą. Odinstalowanie
odbywa się w **Settings → Apps** (**Ustawienia → Aplikacje**).

Instalacja cicha, na przykład ze skrótem na pulpicie:

```bat
msiexec /i hwio-monitor-<version>-windows-x64.msi /qn ADDDESKTOPSHORTCUT=1
```

## macOS

1. Otwórz plik DMG i przeciągnij **HARDWARIO Monitor** do **Applications** (**Programy**).
2. Uruchom aplikację z folderu Programy. Aplikacja jest podpisana przez HARDWARIO i notaryzowana przez Apple, więc
   otwiera się bez ostrzeżenia.

Polecenie `hwio` znajduje się wewnątrz pakietu aplikacji i nie jest w `PATH`. **Settings → About → Command line**
pokazuje jego ścieżkę i polecenie tworzące dowiązanie, które można skopiować:

```sh
sudo ln -s '/Applications/HARDWARIO Monitor.app/Contents/MacOS/hwio' /usr/local/bin/hwio
```

**Bluetooth:** przy pierwszym poleceniu `hwio ble` macOS poprosi o dostęp do Bluetooth. macOS przyznaje uprawnienie
aplikacji, z której uruchamiasz `hwio`, zwykle Terminalowi. Jeśli odmówiono, zezwól na dostęp w **System Settings →
Privacy & Security → Bluetooth** (**Ustawienia systemowe → Prywatność i ochrona → Bluetooth**).

Aby zaktualizować, zastąp aplikację w folderze Programy nową wersją. Aby odinstalować, przenieś ją do Kosza.

## Linux

### .deb (Ubuntu 24.04+, Debian 13+)

```sh
sudo apt install ./hardwario-monitor_<version>_amd64.deb
```

Pakiet instaluje się w `/opt/hardwario-monitor`, umieszcza `hwio-monitor` i `hwio` w `/usr/bin`, dodaje aplikację
do menu pulpitu i instaluje regułę udev dla sond J-Link (`/usr/lib/udev/rules.d/60-hardwario-jlink.rules`).
Do obsługi Bluetooth wymaga pakietu `bluez`. Usuwa się go poleceniem `sudo apt remove hardwario-monitor`.

### AppImage (Ubuntu 22.04+ i inne dystrybucje)

```sh
chmod +x hwio-monitor-<version>-x86_64.AppImage
./hwio-monitor-<version>-x86_64.AppImage
```

AppImage zawiera oba programy. Uruchamia narzędzie wiersza poleceń, gdy `hwio` jest pierwszym argumentem albo gdy
zostanie uruchomiony przez dowiązanie o nazwie `hwio`:

```sh
./hwio-monitor-<version>-x86_64.AppImage hwio jlink list
ln -s "$PWD/hwio-monitor-<version>-x86_64.AppImage" ~/.local/bin/hwio
```

Bez FUSE (w niektórych kontenerach i minimalnych systemach) dodaj `--appimage-extract-and-run` lub ustaw
`APPIMAGE_EXTRACT_AND_RUN=1`. AppImage nie aktualizuje się samoczynnie i nie instaluje reguły udev: zobacz następną
sekcję.

### Dostęp USB do J-Link

Pakiet .deb instaluje tę regułę. W przypadku AppImage zainstaluj ją jednorazowo:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-hardwario-jlink.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Następnie odłącz J-Link i podłącz go ponownie. Reguła udostępnia sondę użytkownikowi zalogowanemu przy komputerze,
a nie każdemu kontu. Użytkownik połączony tylko przez SSH nie ma takiego logowania i potrzebuje własnej reguły.

Instalator SEGGER może dodać `/etc/udev/rules.d/99-jlink.rules` z `MODE="0666"`, co pozwala każdemu kontu na
komputerze używać sondy. `hwio doctor` ostrzega o tym ([usb.world_accessible](troubleshooting.md#usb.world_accessible)).
Jeśli tego nie chcesz, usuń tę regułę lub jej `MODE`; aktualizacja J-Link Software może zainstalować ją ponownie.

### Bluetooth

Bluetooth korzysta z BlueZ. Jeśli usługa nie działa: `sudo systemctl start bluetooth`. Parowanie wymaga programu,
który zapyta o klucz dostępu (passkey) – agenta parowania; zwykle są to ustawienia Bluetooth środowiska graficznego.
Zobacz [Bluetooth LE](bluetooth.md#parowanie).

## SEGGER J-Link Software

Zainstaluj **J-Link Software and Documentation Pack 8.00 lub nowszy** ze strony SEGGER. HARDWARIO Monitor znajdzie
go w domyślnych lokalizacjach:

| System | Przeszukiwane |
|---|---|
| Windows | ścieżka instalacji w rejestrze (`SOFTWARE\SEGGER\J-Link`), `C:\Program Files\SEGGER\JLink*` |
| macOS | `/Applications/SEGGER/JLink`, `/Applications/SEGGER/JLink_V*` |
| Linux | `/opt/SEGGER/JLink`, `/opt/SEGGER/JLink_V*` |

Gdy zainstalowanych jest kilka wersji, używana jest najnowsza. Aby użyć innej kopii, wybierz **Custom path**
(własna ścieżka) w **Settings → J-Link → Library**, a w wierszu poleceń podaj `--jlink-library PATH` lub ustaw
`HWIO_JLINK_LIBRARY`. Kolejność jest następująca: `--jlink-library`, potem `HWIO_JLINK_LIBRARY`, potem ustawienie,
potem domyślne lokalizacje. Ścieżka, która nie istnieje, jest błędem; HARDWARIO Monitor nie wraca wtedy do
domyślnych lokalizacji.

## Nordic nRF Util (tylko firmware modemu LTE)

nRF Util aktualizuje firmware modemu w MCU LTE. Wszystko inne działa bez niego. Zainstaluj `nrfutil` od Nordic
Semiconductor, a następnie jego polecenie `device`:

```sh
nrfutil install device
```

HARDWARIO Monitor szuka go w **Settings → nRF Util → Path** (lub `--nrfutil PATH`, lub `HWIO_NRFUTIL`), następnie
w `PATH`, a potem w `~/.nrfutil/bin` (`%USERPROFILE%\.nrfutil\bin` w systemie Windows) i `/usr/local/bin`. Jeśli
jest zbyt stary: `nrfutil self-upgrade` i `nrfutil upgrade device`.

## Sprawdzenie instalacji

```sh
hwio doctor
```

`hwio doctor` sprawdza J-Link Software, nRF Util, dostęp USB do J-Link (Linux), inne programy blokujące sondę,
podłączone sondy i ich firmware, foldery ustawień i logów oraz Bluetooth. Każdy problem jest opisany wraz z tym, co
należy zrobić. W aplikacji te same testy są częścią raportu diagnostycznego (**Help → Create diagnostic report…**),
a **Settings → J-Link** i **Settings → nRF Util** pokazują, co zostało znalezione.

Dalej: [Pierwsze kroki](getting-started.md).

[releases]: https://github.com/hardwario/monitor/releases/latest
