# HARDWARIO Monitor installieren

[Dokumentation](index.md) › Installation

HARDWARIO Monitor ist ein Paket pro Betriebssystem. Jedes Paket enthält die Desktop-App (`hwio-monitor`) und das
Kommandozeilenwerkzeug (`hwio`). Außerdem benötigen Sie **SEGGER J-Link Software** für die J-Link-Probe und, nur für
die LTE-Modem-Firmware, **Nordic nRF Util**. Keines davon ist in den Paketen enthalten; HARDWARIO Monitor verwendet
sie aus Ihrer Installation.

## Download

Laden Sie die neueste Version von der [Release-Seite][releases] herunter.

| System | Datei |
|---|---|
| Windows 10 22H2 oder neuer, 64 Bit | `hwio-monitor-<version>-windows-x64.msi` |
| macOS 13.3 oder neuer, Apple Silicon und Intel | `hwio-monitor-<version>-macos-universal.dmg` |
| Ubuntu 24.04 oder neuer, Debian 13 oder neuer | `hardwario-monitor_<version>_amd64.deb` |
| Ubuntu 22.04 oder neuer und andere Linux-Distributionen mit glibc 2.35 oder neuer, x86-64 | `hwio-monitor-<version>-x86_64.AppImage` |

Jede Version enthält außerdem eine Datei `SHA256SUMS`. So prüfen Sie einen Download:

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Windows

1. Führen Sie die MSI-Datei aus. Sie installiert für alle Benutzer nach `C:\Program Files\HARDWARIO\Monitor` und
   fragt nach Administratorrechten.
2. In der Liste der Funktionen:
   - **Add hwio to PATH** (hwio zum PATH hinzufügen, an): Der Befehl `hwio` funktioniert in neuen Terminals.
   - **Desktop shortcut** (Desktopverknüpfung, aus): eine Verknüpfung auf dem Desktop.
3. Starten Sie **HARDWARIO Monitor** aus dem Startmenü.

Das Installationsprogramm ist von HARDWARIO a.s. signiert. Solange eine neue Version Microsoft SmartScreen noch
unbekannt ist, kann Windows vor dem Start warnen: Wählen Sie **More info → Run anyway** (Weitere Informationen →
Trotzdem ausführen).

Die Installation einer neueren Version ersetzt die ältere; die Einstellungen bleiben erhalten. Eine ältere Version
kann nicht über eine neuere installiert werden („A newer version of HARDWARIO Monitor is already installed.“):
Deinstallieren Sie zuerst. Deinstallieren Sie über **Settings → Apps** (Einstellungen → Apps).

Stille Installation, zum Beispiel mit Desktopverknüpfung:

```bat
msiexec /i hwio-monitor-<version>-windows-x64.msi /qn ADDDESKTOPSHORTCUT=1
```

## macOS

1. Öffnen Sie die DMG-Datei und ziehen Sie **HARDWARIO Monitor** nach **Applications** (Programme).
2. Starten Sie die App aus dem Ordner Programme. Die App ist von HARDWARIO signiert und von Apple notarisiert, daher
   öffnet sie sich ohne Warnung.

Der Befehl `hwio` befindet sich im App-Bundle und ist nicht im `PATH`. **Settings → About → Command line**
(Einstellungen → Über → Kommandozeile) zeigt seinen Pfad und einen Befehl, der einen Link anlegt; Sie können ihn
kopieren:

```sh
sudo ln -s '/Applications/HARDWARIO Monitor.app/Contents/MacOS/hwio' /usr/local/bin/hwio
```

**Bluetooth:** Beim ersten `hwio ble`-Befehl fragt macOS nach dem Zugriff auf Bluetooth. macOS erteilt die
Berechtigung der App, aus der Sie `hwio` starten, meist dem Terminal. Wenn Sie abgelehnt haben, erlauben Sie es in
**System Settings → Privacy & Security → Bluetooth** (Systemeinstellungen → Datenschutz & Sicherheit → Bluetooth).

Zum Aktualisieren ersetzen Sie die App im Ordner Programme durch die neue. Zum Deinstallieren ziehen Sie sie in den
Papierkorb.

## Linux

### .deb (Ubuntu 24.04+, Debian 13+)

```sh
sudo apt install ./hardwario-monitor_<version>_amd64.deb
```

Das Paket installiert nach `/opt/hardwario-monitor`, legt `hwio-monitor` und `hwio` in `/usr/bin` ab, fügt die App
dem Desktop-Menü hinzu und installiert die udev-Regel für J-Link-Probes
(`/usr/lib/udev/rules.d/60-hardwario-jlink.rules`). Für Bluetooth hängt es von `bluez` ab. Entfernen Sie es mit
`sudo apt remove hardwario-monitor`.

### AppImage (Ubuntu 22.04+ und andere Distributionen)

```sh
chmod +x hwio-monitor-<version>-x86_64.AppImage
./hwio-monitor-<version>-x86_64.AppImage
```

Das AppImage enthält beide Programme. Es startet das Kommandozeilenwerkzeug, wenn `hwio` das erste Argument ist oder
wenn es über einen Link namens `hwio` gestartet wird:

```sh
./hwio-monitor-<version>-x86_64.AppImage hwio jlink list
ln -s "$PWD/hwio-monitor-<version>-x86_64.AppImage" ~/.local/bin/hwio
```

Ohne FUSE (manche Container und minimale Systeme) fügen Sie `--appimage-extract-and-run` hinzu oder setzen
`APPIMAGE_EXTRACT_AND_RUN=1`. Das AppImage aktualisiert sich nicht selbst und installiert die udev-Regel nicht: siehe
den nächsten Abschnitt.

### USB-Zugriff auf den J-Link

Das .deb-Paket installiert diese Regel. Beim AppImage installieren Sie sie einmalig:

```sh
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="1366", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/60-hardwario-jlink.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Ziehen Sie dann den J-Link ab und stecken Sie ihn wieder ein. Die Regel gibt die Probe dem Benutzer, der am Computer
angemeldet ist, nicht jedem Konto. Ein Benutzer, der nur über SSH verbunden ist, hat keine solche Anmeldung und
braucht eine eigene Regel.

Das Installationsprogramm von SEGGER kann `/etc/udev/rules.d/99-jlink.rules` mit `MODE="0666"` anlegen, wodurch jedes
Konto auf dem Computer die Probe verwenden kann. `hwio doctor` warnt davor
([usb.world_accessible](troubleshooting.md#usb.world_accessible)). Entfernen Sie diese Regel oder ihr `MODE`, wenn Sie
das nicht wünschen; ein Update der J-Link Software kann sie erneut installieren.

### Bluetooth

Bluetooth verwendet BlueZ. Wenn es nicht läuft: `sudo systemctl start bluetooth`. Für die Kopplung wird ein Programm
benötigt, das nach dem Passkey fragt (ein Pairing-Agent), meist die Bluetooth-Einstellungen des Desktops. Siehe
[Bluetooth LE](bluetooth.md#kopplung).

## SEGGER J-Link Software

Installieren Sie **J-Link Software and Documentation Pack 8.00 oder neuer** von der SEGGER-Website. HARDWARIO Monitor
findet es an den Standardorten:

| System | Durchsucht |
|---|---|
| Windows | der Installationspfad in der Registry (`SOFTWARE\SEGGER\J-Link`), `C:\Program Files\SEGGER\JLink*` |
| macOS | `/Applications/SEGGER/JLink`, `/Applications/SEGGER/JLink_V*` |
| Linux | `/opt/SEGGER/JLink`, `/opt/SEGGER/JLink_V*` |

Sind mehrere Versionen installiert, wird die neueste verwendet. Um eine andere Kopie zu verwenden, wählen Sie
**Custom path** (Eigener Pfad) in **Settings → J-Link → Library** (Einstellungen → J-Link → Bibliothek), oder auf der
Kommandozeile übergeben Sie `--jlink-library PATH` bzw. setzen `HWIO_JLINK_LIBRARY`. Die Reihenfolge ist:
`--jlink-library`, dann `HWIO_JLINK_LIBRARY`, dann die Einstellung, dann die Standardorte. Ein Pfad, der nicht
existiert, ist ein Fehler; HARDWARIO Monitor weicht dann nicht auf die Standardorte aus.

## Nordic nRF Util (nur für die LTE-Modem-Firmware)

nRF Util aktualisiert die Modem-Firmware der LTE-MCU. Alles andere funktioniert ohne es. Installieren Sie `nrfutil`
von Nordic Semiconductor und danach seinen Befehl `device`:

```sh
nrfutil install device
```

HARDWARIO Monitor sucht es in **Settings → nRF Util → Path** (Einstellungen → nRF Util → Pfad) (oder
`--nrfutil PATH`, oder `HWIO_NRFUTIL`), dann im `PATH`, dann in `~/.nrfutil/bin` (`%USERPROFILE%\.nrfutil\bin` unter
Windows) und `/usr/local/bin`. Wenn es zu alt ist: `nrfutil self-upgrade` und `nrfutil upgrade device`.

## Installation prüfen

```sh
hwio doctor
```

`hwio doctor` prüft die J-Link Software, nRF Util, den USB-Zugriff auf den J-Link (Linux), andere Programme, die eine
Probe belegen, die angeschlossenen Probes und ihre Firmware, die Einstellungs- und Log-Ordner sowie Bluetooth. Zu
jedem Problem wird angegeben, was zu tun ist. In der App sind dieselben Prüfungen Teil des Diagnoseberichts
(**Help → Create diagnostic report…**, Hilfe → Diagnosebericht erstellen…), und **Settings → J-Link** sowie
**Settings → nRF Util** zeigen, was gefunden wurde.

Weiter: [Erste Schritte](getting-started.md).

[releases]: https://github.com/hardwario/monitor/releases/latest
