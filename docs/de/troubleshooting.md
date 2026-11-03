# Fehlerbehebung

[Dokumentation](index.md) › Fehlerbehebung

Diese Seite hilft Ihnen, wenn etwas nicht funktioniert. Sie beginnt mit den häufigsten Problemen und listet dann alle Fehlercodes auf.

## Wie Fehler angezeigt werden

Jeder Fehler hat eine kurze Meldung, eine wahrscheinliche Ursache und einen Vorschlag.

- **Desktop-App (`hwio-monitor`):** Der Fehlerbereich zeigt die Meldung, **Probably:** (die Ursache) und **What to do:** (den Vorschlag), mit einer Schaltfläche für den nächsten Schritt, etwa **Reconnect**, **Open Settings** oder **Retry**. **Details** zeigt den Fehlercode (zum Beispiel `probe.not_found`) und technische Details; **Copy** kopiert sie.
- **CLI (`hwio`):** Der Fehler wird nach stderr geschrieben:

  ```text
  Error: No J-Link probe found.
  Cause: The USB cable is disconnected or the J-Link is not powered.
  What to do: Check the USB cable between the J-Link and the computer.
  Details: <technical details>
  ```

  Der Befehl endet mit dem [Exit-Code](../en/cli.md#exit-codes) der Fehlerkategorie.
- **CLI mit `--json`:** Die Ausgabe ist eine Zeile mit `"ok":false` und einem Objekt `error`. Seine Felder sind `code` (der Fehlercode), `category`, `message`, `cause`, `suggestion` und `detail`. Siehe [JSON-Ausgabe](../en/cli.md#json-output).

Jeder Fehlercode unten hat einen Anker, der dem Code entspricht, sodass Sie direkt darauf verlinken können, zum Beispiel `troubleshooting.md#probe.not_found`.

**Bevor Sie um Hilfe bitten:**

- Führen Sie `hwio doctor` aus. Es prüft die J-Link Software, nRF Util, den USB-Zugriff auf den J-Link (Linux), andere Programme, die den J-Link verwenden, die angeschlossenen Probes und ihre Firmware, die Einstellungs- und Log-Ordner sowie Bluetooth.
- Erstellen Sie einen Diagnosebericht: Wählen Sie in der Desktop-App **Help** › **Create diagnostic report…**, oder führen Sie `hwio diag report` aus. Der Bericht ist eine ZIP-Datei mit einer Zusammenfassung, aktuellen Logs und den Einstellungen. Was er enthält, steht unter [Diagnosebericht](privacy.md#diagnosebericht).

## Häufige Probleme

### Kein J-Link gefunden

- Prüfen Sie das USB-Kabel zwischen dem J-Link und dem Computer. Versuchen Sie einen anderen USB-Anschluss oder ein anderes Kabel.
- Führen Sie `hwio jlink list` aus, um die Probes zu sehen. Wenn Sie eine Seriennummer angegeben haben (`-n <serial>`), prüfen Sie sie.
- Prüfen Sie unter Linux die [USB-Berechtigungen](#usb-berechtigungen-unter-linux).

Fehlercodes: [`probe.not_found`](#probe.not_found), [`probe.open_failed`](#probe.open_failed), [`probe.io_failed`](#probe.io_failed).

### J-Link Software nicht gefunden oder zu alt

HARDWARIO Monitor enthält die SEGGER J-Link Software nicht. Installieren Sie Version 8.00 oder neuer; siehe [SEGGER J-Link Software](install.md#segger-j-link-software). Ist sie an einem ungewöhnlichen Ort installiert, legen Sie die Bibliothek in **Settings** › **J-Link** › **Library** fest, oder verwenden Sie in der CLI `--jlink-library PATH` (oder `$HWIO_JLINK_LIBRARY`). Führen Sie `hwio doctor` zur Prüfung aus.

Fehlercodes: [`dependency.jlink_not_found`](#dependency.jlink_not_found), [`dependency.jlink_load_failed`](#dependency.jlink_load_failed), [`dependency.jlink_too_old`](#dependency.jlink_too_old), [`dependency.jlink_function_missing`](#dependency.jlink_function_missing).

### USB-Berechtigungen unter Linux

Unter Linux braucht Ihr Benutzerkonto Zugriff auf den J-Link über USB. Das `.deb`-Paket installiert dafür eine udev-Regel; bei anderen Installationen installieren Sie sie selbst, wie unter [USB-Zugriff auf den J-Link](install.md#usb-zugriff-auf-den-j-link) beschrieben. Ziehen Sie dann den J-Link ab, schließen Sie ihn wieder an und führen Sie `hwio doctor` aus. Manche Fehlertexte erwähnen einen Einrichtungsassistenten; er ist noch nicht verfügbar, folgen Sie daher stattdessen der Installationsseite.

Fehlercodes: [`usb.access_denied`](#usb.access_denied), [`usb.world_accessible`](#usb.world_accessible).

### Der J-Link wird von einem anderen Programm verwendet

Nur ein Programm kann einen J-Link zur gleichen Zeit verwenden. Schließen Sie das andere Fenster von HARDWARIO Monitor, den anderen `hwio`-Befehl oder andere Debugging-Werkzeuge (zum Beispiel die SEGGER-Werkzeuge). In der Desktop-App ist eine Probe, die ein anderes Programm verwendet, in der Geräteliste mit **In use by another program** markiert. `hwio doctor` listet Programme auf, die die Probe belegen könnten.

Fehlercodes: [`probe.in_use_by_other_process`](#probe.in_use_by_other_process), [`probe.held_by_program`](#probe.held_by_program), [`probe.open_failed`](#probe.open_failed).

### Das Gerät hat keinen Strom oder antwortet nicht

- CHESTER braucht eine eigene Stromversorgung: Batterien oder den USB-C-Stromeingang.
- Prüfen Sie das Flachbandkabel zwischen dem J-Link und CHESTER, und dass es im Anschluss der gewünschten MCU steckt (app oder LTE).
- Ist die MCU gegen Auslesen geschützt, siehe [Flashen schlägt fehl](#flashen-schlägt-fehl-oder-die-mcu-ist-geschützt).

Fehlercodes: [`target.no_power`](#target.no_power), [`target.connect_failed`](#target.connect_failed), [`target.wrong_family`](#target.wrong_family).

### Keine Konsole (RTT) gefunden

Die Konsole braucht Firmware mit einer RTT-Konsole. Setzen Sie das Gerät zurück oder flashen Sie solche Firmware. Auch eine sehr niedrige J-Link-Geschwindigkeit kann die Ursache sein: Verwenden Sie **Automatic speed**. Die LTE-MCU des CHESTER hat keine Konsole; verwenden Sie die app-MCU. Siehe [Konsole](console.md).

Fehlercodes: [`target.rtt_not_found`](#target.rtt_not_found), [`target.rtt_buffer_not_found`](#target.rtt_buffer_not_found), [`target.rtt_write_timeout`](#target.rtt_write_timeout), [`target.not_connected`](#target.not_connected).

### Flashen schlägt fehl oder die MCU ist geschützt

- Prüfen Sie die Kabel und die Stromversorgung und flashen Sie dann erneut.
- Schlägt die Verifizierung wiederholt fehl, stellen Sie eine niedrigere J-Link-Geschwindigkeit ein.
- Ist die MCU gegen Auslesen geschützt, führen Sie einen Recover aus. Recover löscht die ganze MCU, einschließlich des UICR und des Product Information Block. Siehe [Recover](flashing.md#recover).
- Schreibt die Firmware auch den UICR, erlauben Sie es bewusst: **Allow writing UICR** auf der Seite **Flash** oder `--allow-uicr` in der CLI. Siehe [UICR und der Product Information Block](flashing.md#uicr-und-der-product-information-block).

Fehlercodes: [`target.protected`](#target.protected), [`target.program_failed`](#target.program_failed), [`firmware.verify_failed`](#firmware.verify_failed), [`firmware.uicr_in_image`](#firmware.uicr_in_image), [`firmware.out_of_range`](#firmware.out_of_range), [`target.recover_failed`](#target.recover_failed).

### Die LTE-Modem-Firmware braucht nRF Util

Das Aktualisieren der LTE-Modem-Firmware verwendet Nordic nRF Util mit seinem Befehl `device`. Nichts anderes braucht es. Installieren Sie es wie unter [Nordic nRF Util](install.md#nordic-nrf-util-nur-für-die-lte-modem-firmware) beschrieben, oder legen Sie seinen Pfad in **Settings** › **nRF Util** fest, oder mit `--nrfutil PATH` in der CLI.

Fehlercodes: [`dependency.nrfutil_not_found`](#dependency.nrfutil_not_found), [`dependency.nrfutil_device_missing`](#dependency.nrfutil_device_missing), [`dependency.nrfutil_too_old`](#dependency.nrfutil_too_old), [`firmware.modem_update_failed`](#firmware.modem_update_failed).

### Bluetooth-Gerät nicht gefunden oder Kopplung schlägt fehl

Bluetooth ist nur in der CLI verfügbar (`hwio ble ...`).

- Schalten Sie Bluetooth ein und führen Sie `hwio ble scan` aus. Es werden nur Geräte angezeigt, deren Name `CHESTER` enthält; `--all` zeigt jedes Gerät.
- Bringen Sie das Gerät näher und trennen Sie es von anderen Telefonen oder Computern.
- Schlägt die Kopplung fehl, entfernen Sie das Gerät in den Bluetooth-Einstellungen des Systems und koppeln Sie erneut. Unter Linux muss ein Pairing-Agent laufen, der nach dem Passkey fragt. Siehe [Kopplung](bluetooth.md#kopplung).

Fehlercodes: [`ble.adapter_off`](#ble.adapter_off), [`ble.device_not_found`](#ble.device_not_found), [`ble.connect_failed`](#ble.connect_failed), [`ble.not_paired`](#ble.not_paired), [`ble.pairing_failed`](#ble.pairing_failed), [`ble.no_pairing_agent`](#ble.no_pairing_agent).

### Die Einstellungen sind schreibgeschützt

Haben Sie eine neuere Version von HARDWARIO Monitor verwendet und starten dann eine ältere, öffnet die ältere Version die Einstellungen schreibgeschützt und zeigt das Banner „Settings were created by a newer version and are read-only.“ Änderungen werden nicht gespeichert. Aktualisieren Sie HARDWARIO Monitor auf die neuere Version.

Fehlercode: [`settings.newer_schema`](#settings.newer_schema).

## Fehlercodes

Die Fehlercodes sind nach Kategorie gruppiert. Die Kategorie bestimmt den [Exit-Code](../en/cli.md#exit-codes) von `hwio`:

| Kategorie | Bedeutung | Exit-Code |
|---|---|---|
| [`dependency`](#category-dependency) | Ein benötigtes Programm oder ein Systemdienst fehlt: J-Link Software, nRF Util, Bluetooth | 4 |
| [`probe`](#category-probe) | Die J-Link-Probe oder ihre USB-Verbindung | 5 |
| [`target`](#category-target) | Das Gerät: Stromversorgung, Kabel, MCU, Konsole, Bluetooth-Verbindung | 6 |
| [`firmware`](#category-firmware) | Die Firmware-Datei oder ihr Download | 7 |
| [`network`](#category-network) | Internetverbindung: Firmware-Katalog, Update-Prüfung | 8 |
| [`io`](#category-io) | Dateien, Ordner und Einstellungen | 3 |
| [`usage`](#category-usage) | Ein falsches Argument, eine falsche Option oder Eingabe | 2 |
| [`timeout`](#category-timeout) | Etwas hat nicht rechtzeitig geantwortet | 9 |
| [`cancelled`](#category-cancelled) | Der Vorgang wurde abgebrochen | 130 |
| [`internal`](#category-internal) | Ein internes Problem von HARDWARIO Monitor | 1 |

Ein erfolgreicher Befehl endet mit Exit-Code 0. Die Texte unten sind die englischen Standardtexte. Viele Stellen zeigen eine genauere Meldung, zum Beispiel eine, die die MCU oder die Datei nennt; solche Varianten sind unter jedem Code beschrieben. Werte in spitzen Klammern, wie `<serial>`, werden durch echte Werte ersetzt.

<a id="category-dependency"></a>
### Abhängigkeit (Exit-Code 4)

<a id="dependency.jlink_not_found"></a>
#### `dependency.jlink_not_found`

**SEGGER J-Link Software was not found.**

- **Probably:** It is not installed, or it is installed in a non-standard location.
- **What to do:** Install J-Link Software 8.00 or newer, or set its path with --jlink-library.

Erscheint, wenn HARDWARIO Monitor die J-Link-Bibliothek nicht finden kann, zum Beispiel direkt nach der Installation. Legen Sie in der Desktop-App den Pfad in **Settings** › **J-Link** › **Library** (**Custom path**) fest; in der CLI verwenden Sie `--jlink-library PATH` oder `$HWIO_JLINK_LIBRARY`. Siehe [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.jlink_load_failed"></a>
#### `dependency.jlink_load_failed`

**The J-Link library could not be loaded.**

- **Probably:** The installation is damaged, is for another processor architecture, or lacks a required function.
- **What to do:** Reinstall J-Link Software.

Die Bibliotheksdatei wurde gefunden, konnte aber nicht verwendet werden, zum Beispiel eine 32-Bit- oder x86-Bibliothek auf einem ARM-Computer. Prüfen Sie den Pfad in **Settings** › **J-Link** › **Library** oder `--jlink-library`.

<a id="dependency.jlink_too_old"></a>
#### `dependency.jlink_too_old`

**The J-Link Software is too old.**

- **Probably:** HARDWARIO Monitor needs J-Link Software 8.00 or newer.
- **What to do:** Install the current J-Link Software.

Die Meldung kann die Version nennen, etwa „J-Link Software `<version>` is too old.“ Siehe [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="dependency.nrfutil_not_found"></a>
#### `dependency.nrfutil_not_found`

**nRF Util was not found.**

- **Probably:** It is not installed; it is needed only for the modem firmware.
- **What to do:** Install nRF Util and its device command, or set its path with --nrfutil.

nRF Util wird nur zum Aktualisieren der LTE-Modem-Firmware benötigt. Legen Sie in der Desktop-App den Pfad in **Settings** › **nRF Util** › **Path** (**Custom path**) fest; in der CLI verwenden Sie `--nrfutil PATH`. Siehe [Nordic nRF Util](install.md#nordic-nrf-util-nur-für-die-lte-modem-firmware).

<a id="dependency.nrfutil_device_missing"></a>
#### `dependency.nrfutil_device_missing`

**The nRF Util device command is not installed.**

- **Probably:** nRF Util is installed without its device command.
- **What to do:** Run `nrfutil install device`.

Siehe [Nordic nRF Util](install.md#nordic-nrf-util-nur-für-die-lte-modem-firmware).

<a id="dependency.nrfutil_too_old"></a>
#### `dependency.nrfutil_too_old`

**nRF Util is too old.**

- **Probably:** HARDWARIO Monitor needs a newer nRF Util or device command.
- **What to do:** Run `nrfutil self-upgrade` and `nrfutil upgrade device`.

Die Meldung kann die Version nennen, etwa „nRF Util `<version>` is too old.“

<a id="ble.adapter_not_found"></a>
#### `ble.adapter_not_found`

**No Bluetooth adapter was found.**

- **Probably:** The computer has no Bluetooth or it is disabled in the system.
- **What to do:** Enable Bluetooth or connect a USB Bluetooth adapter.

Bluetooth ist nur in der CLI verfügbar (`hwio ble ...`). `hwio doctor` meldet es ebenfalls als Warnung. Siehe [Bluetooth](bluetooth.md).

<a id="ble.adapter_off"></a>
#### `ble.adapter_off`

**Bluetooth is turned off.**

- **Probably:** Bluetooth is turned off in the system settings or with a hardware switch.
- **What to do:** Turn Bluetooth on in the system settings.

Bluetooth ist nur in der CLI verfügbar (`hwio ble ...`).

<a id="ble.permission_denied"></a>
#### `ble.permission_denied`

**HARDWARIO Monitor is not allowed to use Bluetooth.**

- **Probably:** Bluetooth access was denied in the privacy settings.
- **What to do:** Allow Bluetooth for HARDWARIO Monitor (or your terminal) in System Settings → Privacy & Security → Bluetooth.

Tritt unter macOS auf, wenn die Abfrage der Bluetooth-Berechtigung abgelehnt wurde. Wenn Sie `hwio` aus einem Terminal starten, erlauben Sie Bluetooth für die Terminal-App.

<a id="ble.stack_unavailable"></a>
#### `ble.stack_unavailable`

**The Bluetooth service of the system is not available.**

- **Probably:** The Bluetooth service of the system (BlueZ on Linux) is not running.
- **What to do:** Start the Bluetooth service: `sudo systemctl start bluetooth`.

Unter Linux muss der Dienst BlueZ laufen. Der Fehler erscheint auch bei einem unbekannten Fehler des Bluetooth-Stacks des Systems während eines Scans.

<a id="ble.no_pairing_agent"></a>
#### `ble.no_pairing_agent`

**No program is ready to ask for the Bluetooth passkey.**

- **Probably:** No pairing agent is running (on GNOME the agent may only be active in Settings → Bluetooth).
- **What to do:** Pair the device from the system Bluetooth settings, or with bluetoothctl: `agent KeyboardOnly`, `default-agent`, `pair <id>`.

Nur Linux: Die Kopplung wurde sofort abgebrochen, weil kein Programm den Passkey-Dialog anzeigen konnte. Siehe [Kopplung](bluetooth.md#kopplung).

<a id="dependency.jlink_function_missing"></a>
#### `dependency.jlink_function_missing`

**The installed J-Link software does not provide a function this operation needs.**

- **Probably:** The J-Link library is older than, or different from, the versions HARDWARIO Monitor was tested with.
- **What to do:** Install the latest J-Link Software and Documentation Pack.

Manche Target-Befehle (zum Beispiel Register oder 16-Bit-Speicherzugriff) brauchen Funktionen, die nur neuere J-Link-Bibliotheken bieten. Siehe [SEGGER J-Link Software](install.md#segger-j-link-software).

<a id="category-probe"></a>
### Probe (Exit-Code 5)

<a id="probe.not_found"></a>
#### `probe.not_found`

**No J-Link probe found.**

- **Probably:** The USB cable is disconnected or the J-Link is not powered.
- **What to do:** Check the USB cable between the J-Link and the computer.

Erscheint, wenn kein J-Link angeschlossen ist oder wenn die gewählte Seriennummer (`-n <serial>`) nicht in der Liste ist. Varianten: „No J-Link probe with serial number `<serial>` found.“ und, für einen J-Link, der über das Netzwerk verbunden ist, „The J-Link `<serial>` is a network J-Link; network J-Links are not supported for sessions yet.“ Verwenden Sie einen über USB angeschlossenen J-Link.

<a id="probe.in_use_by_other_process"></a>
#### `probe.in_use_by_other_process`

**The J-Link is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or hwio command is using it.
- **What to do:** Close the other program, or choose another J-Link.

HARDWARIO Monitor und `hwio` sperren einen J-Link, während sie ihn verwenden. Die Meldung kann das andere Programm nennen, etwa „The J-Link `<serial>` is used by another program (`<app>`, PID `<pid>`).“ Ist das Programm unbekannt, rät der Vorschlag, die Sperrdatei `<lockfile>` zu löschen, wenn kein anderes HARDWARIO-Programm läuft. Nachdem die Desktop-App den J-Link freigegeben hat, kann die Ursache auch „Another program opened the J-Link after it was released.“ lauten.

<a id="probe.open_failed"></a>
#### `probe.open_failed`

**The J-Link probe could not be opened.**

- **Probably:** It is used by another program or session, or the USB permissions are missing (Linux udev rules).
- **What to do:** Close other programs using the J-Link and try again.

Unter Linux sind fehlende USB-Berechtigungen eine häufige Ursache; siehe [USB-Zugriff auf den J-Link](install.md#usb-zugriff-auf-den-j-link). Die Variante „The J-Link `<serial>` is already used by this application.“ bedeutet, dass ein anderer Tab oder eine andere Sitzung desselben Programms ihn geöffnet hat.

<a id="probe.firmware_outdated"></a>
#### `probe.firmware_outdated`

**The J-Link firmware is outdated.**

- **Probably:** The J-Link firmware differs from the firmware bundled with the J-Link Software.
- **What to do:** Run `hwio jlink update-firmware`.

Dies ist eine Warnung; der J-Link funktioniert weiter. `hwio doctor` meldet es ebenfalls. Ist die Firmware der Probe neuer als die Bibliothek, lautet die Meldung „The J-Link firmware is newer than the J-Link library.“ Aktualisieren Sie in diesem Fall stattdessen die J-Link Software.

<a id="probe.firmware_update_failed"></a>
#### `probe.firmware_update_failed`

**The J-Link firmware could not be updated.**

- **Probably:** The update failed or the probe still reports a firmware different from the library's.
- **What to do:** Disconnect and reconnect the J-Link, then run `hwio jlink update-firmware` again.

Kommt von `hwio jlink update-firmware`.

<a id="probe.io_failed"></a>
#### `probe.io_failed`

**Communication with the J-Link failed.**

- **Probably:** The J-Link did not answer: a USB glitch, or the USB cable was disconnected.
- **What to do:** Check the USB cable between the J-Link and the computer; the console reconnects automatically if enabled.

Tritt auf, wenn die USB-Verbindung zum J-Link verloren geht, während die Konsole läuft. Auto-Reconnect ist in der Desktop-App standardmäßig eingeschaltet (**Settings** › **J-Link** oder **Advanced** (⋯) in der Geräteleiste); in der CLI verwenden Sie `--auto-reconnect`.

<a id="usb.access_denied"></a>
#### `usb.access_denied`

**This computer does not allow access to the J-Link (USB permissions).**

- **Probably:** The udev rule for SEGGER devices is missing.
- **What to do:** Install the udev rule shown in the setup wizard, then reconnect the J-Link. The rule gives access only to the user logged in at this computer; users connected only over SSH need their own udev rule.

Nur Linux. Der im Text erwähnte Einrichtungsassistent ist noch nicht verfügbar: Installieren Sie die udev-Regel wie unter [USB-Zugriff auf den J-Link](install.md#usb-zugriff-auf-den-j-link) beschrieben, und führen Sie dann `hwio doctor` zur Prüfung aus.

<a id="probe.held_by_program"></a>
#### `probe.held_by_program`

**Another program is running and may be using the J-Link.**

- **Probably:** Another debugging or programming tool keeps the probe open.
- **What to do:** Close the other program and try again.

Eine Warnung von `hwio doctor` und im Diagnosebericht. Sie kann das Programm nennen, etwa „`<program>` (PID `<pid>`) is running and may be using the J-Link.“

<a id="ble.in_use_by_other_process"></a>
#### `ble.in_use_by_other_process`

**The device is used by another program.**

- **Probably:** Another HARDWARIO Monitor window or `hwio` command is connected to it.
- **What to do:** Close the other program.

Bluetooth-Geräte werden wie J-Links gesperrt. Die Meldung kann das andere Programm nennen („The device `<id>` is used by another program (`<app>`, PID `<pid>`).“) oder „The device `<id>` is already used by this application.“ lauten. Ist das Programm unbekannt, rät der Vorschlag, die Sperrdatei `<lockfile>` zu löschen, wenn kein anderes HARDWARIO-Programm läuft.

<a id="usb.world_accessible"></a>
#### `usb.world_accessible`

**Every user account on this computer can use the J-Link (USB permissions).**

- **Probably:** A udev rule, usually /etc/udev/rules.d/99-jlink.rules from the SEGGER J-Link Software, makes the probe accessible to all accounts.
- **What to do:** Remove that rule (or its MODE="0666"), install the rule shown in the setup wizard, then reconnect the J-Link. A J-Link Software update may install the rule again.

Eine Linux-Warnung von `hwio doctor`. Der im Text erwähnte Einrichtungsassistent ist noch nicht verfügbar: Installieren Sie die Regel wie unter [USB-Zugriff auf den J-Link](install.md#usb-zugriff-auf-den-j-link) beschrieben.

<a id="category-target"></a>
### Target (Exit-Code 6)

<a id="target.no_power"></a>
#### `target.no_power`

**The device has no power.**

- **Probably:** The device is not powered or the flat cable is disconnected.
- **What to do:** Power the device and check the flat cable.

Die Meldung kann die gemessene Spannung enthalten, etwa „The device has no power (`<mV>` mV).“ CHESTER braucht Batterien oder den USB-C-Stromeingang.

<a id="target.connect_failed"></a>
#### `target.connect_failed`

**Could not connect to the device.**

- **Probably:** The flat cable is disconnected or in the wrong connector, or the MCU is in a low-power or protected state.
- **What to do:** Check the flat cable between the J-Link and the device.

Der J-Link funktioniert, erreicht aber die MCU nicht. Varianten nennen die MCU, zum Beispiel „Could not connect to the device's `<device>` MCU.“ oder „The type of the `<target>` MCU could not be read.“ Eine geschützte MCU kann einen [Recover](flashing.md#recover) benötigen.

<a id="target.rtt_not_found"></a>
#### `target.rtt_not_found`

**The device's console (RTT) was not found.**

- **Probably:** The firmware has no RTT console, is still booting, or the CPU is halted.
- **What to do:** Reset the device or flash firmware with RTT console support.

Die Konsole konnte den RTT-Kontrollblock nicht rechtzeitig finden. Auch eine sehr niedrige J-Link-Geschwindigkeit kann die Ursache sein: Verwenden Sie **Automatic speed** (**Advanced** (⋯) in der Geräteleiste oder **Settings** › **J-Link**). Die LTE-MCU des CHESTER hat keine Konsole. Siehe [Konsole](console.md).

<a id="target.rtt_buffer_not_found"></a>
#### `target.rtt_buffer_not_found`

**An RTT buffer was not found.**

- **Probably:** The firmware is older or different.
- **What to do:** Use firmware with the CHESTER shell.

Die Meldung nennt den Puffer, etwa „RTT buffer "`<name>`" not found.“ Fehlt nur der Log-Puffer, ist es eine Warnung („…; the device log is not shown.“), und die Konsole funktioniert weiterhin.

<a id="target.rtt_write_timeout"></a>
#### `target.rtt_write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is hung or its shell is disabled.
- **What to do:** Reset the device.

Das Gerät hat einen gesendeten Befehl nicht rechtzeitig angenommen.

<a id="target.rtt_read_failed"></a>
#### `target.rtt_read_failed`

**Reading from the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

Auto-Reconnect ist in der Desktop-App standardmäßig eingeschaltet (**Settings** › **J-Link** oder **Advanced** (⋯) in der Geräteleiste).

<a id="target.rtt_write_failed"></a>
#### `target.rtt_write_failed`

**Writing to the device failed.**

- **Probably:** A cable was disconnected or the device was reset.
- **What to do:** Check the cables; the console reconnects automatically if enabled.

Auto-Reconnect ist in der Desktop-App standardmäßig eingeschaltet (**Settings** › **J-Link** oder **Advanced** (⋯) in der Geräteleiste).

<a id="target.not_connected"></a>
#### `target.not_connected`

**The device is not connected.**

- **Probably:** The console is disconnected or still reconnecting.
- **What to do:** Reconnect the console (F4).

Sie haben einen Befehl gesendet, während die Konsole getrennt war. Bei eingeschaltetem Auto-Reconnect lautet der Vorschlag „Wait for the console to reconnect.“ Starten Sie in der CLI die Konsole erneut. Tritt es während einer Batch-Datei auf, lautet die Meldung „The batch stopped after `<done>` of `<total>` commands.“

<a id="target.wrong_family"></a>
#### `target.wrong_family`

**The J-Link is connected to another MCU.**

- **Probably:** The J-Link is connected to the other connector of the device.
- **What to do:** Connect the J-Link to the connector of the MCU you want to program.

Das Flachbandkabel steckt im Anschluss der anderen MCU (zum Beispiel im LTE-Anschluss, wenn Sie die app-MCU flashen). Die Meldung kann „Expected the `<target>` MCU (`<part>`) but found `<found>`.“ lauten.

<a id="target.protected"></a>
#### `target.protected`

**The MCU is read-back protected.**

- **Probably:** Access port protection (APPROTECT) is enabled.
- **What to do:** Recover the MCU (this erases it), then flash it again.

Recover entfernt den Schutz, löscht aber die MCU vollständig, einschließlich des Product Information Block. Siehe [Recover](flashing.md#recover). Für eine benannte MCU lautet der Vorschlag „Run `hwio <profile> <target> recover` (erases the MCU).“; in der Desktop-App verwenden Sie **Recover…** unter **Maintenance** auf der Seite **Flash**.

<a id="target.program_failed"></a>
#### `target.program_failed`

**Programming the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Die Variante der Ursache „Programming was interrupted; the firmware may be partly written.“ bedeutet, dass Sie erneut flashen müssen, bevor das Gerät funktioniert.

<a id="target.erase_failed"></a>
#### `target.erase_failed`

**Erasing the MCU failed.**

- **Probably:** The connection was lost, the power is low or the MCU is protected.
- **What to do:** Check the cables and power, then try again.

Zwei seltene Varianten betreffen den Product Information Block, nachdem nur die Firmware gelöscht wurde: „could not be checked after the erase“ und „Erasing the code flash … also changed its Product Information Block“. Löschen Sie in beiden Fällen nicht erneut; folgen Sie dem Vorschlag und wenden Sie sich an den HARDWARIO-Support. Siehe [UICR und der Product Information Block](flashing.md#uicr-und-der-product-information-block).

<a id="target.recover_failed"></a>
#### `target.recover_failed`

**Recovering the MCU failed.**

- **Probably:** The device has no power or its control access port does not answer.
- **What to do:** Power-cycle the device and try again.

Siehe [Recover](flashing.md#recover).

<a id="pib.invalid"></a>
#### `pib.invalid`

**The Product Information Block is missing or damaged.**

- **Probably:** It was never written, or it was erased by a full erase or a recover.
- **What to do:** Contact HARDWARIO support to restore it.

Der Product Information Block enthält die Seriennummer und die Hardware-Version. Die Meldung kann die fehlgeschlagene Prüfung in Klammern nennen. Siehe [UICR und der Product Information Block](flashing.md#uicr-und-der-product-information-block).

<a id="ble.device_not_found"></a>
#### `ble.device_not_found`

**The Bluetooth device was not found.**

- **Probably:** The device is out of range, turned off or its Bluetooth is disabled.
- **What to do:** Bring the device closer and make sure its Bluetooth is on.

Kommt von `hwio ble console` oder `hwio ble command` mit `--name` oder `-a`. Führen Sie `hwio ble scan` aus, um die Geräte in Reichweite zu sehen. Siehe [Bluetooth](bluetooth.md).

<a id="ble.connect_failed"></a>
#### `ble.connect_failed`

**Could not connect to the device over Bluetooth.**

- **Probably:** The signal is weak or the device is busy with another connection.
- **What to do:** Move closer, disconnect other phones or computers from the device and try again.

Eine Variante der Ursache lautet „The device refused to start the console: it may still be connected (to this or another computer) or the pairing was not completed.“ Warten Sie dann einige Sekunden, oder trennen Sie das Gerät in den Bluetooth-Einstellungen des Systems. Siehe [Kopplung](bluetooth.md#kopplung).

<a id="ble.service_not_found"></a>
#### `ble.service_not_found`

**The device does not offer the console service (NUS).**

- **Probably:** The firmware has no Bluetooth shell.
- **What to do:** Use firmware with the Bluetooth console enabled.

Auf dem Gerät läuft keine CHESTER-Firmware mit Bluetooth-Konsole.

<a id="ble.not_paired"></a>
#### `ble.not_paired`

**The device is not paired with this computer.**

- **Probably:** Pairing was cancelled, failed or was removed on one side.
- **What to do:** Pair the device: enter its passkey when the system asks.

Führen Sie `hwio ble pair` aus (Linux und Windows); unter macOS fragt das System beim Verbinden nach dem Passkey. Siehe [Kopplung](bluetooth.md#kopplung).

<a id="ble.pairing_failed"></a>
#### `ble.pairing_failed`

**Pairing with the device failed.**

- **Probably:** A wrong passkey, a timeout or a stale bond on one side.
- **What to do:** Remove the device in the system Bluetooth settings and pair again.

Unter Linux kann die Ursache auch sein, dass kein Passkey-Dialog erschien, weil kein Pairing-Agent läuft. Siehe [Kopplung](bluetooth.md#kopplung).

<a id="ble.write_failed"></a>
#### `ble.write_failed`

**Sending to the device over Bluetooth failed.**

- **Probably:** The link was lost during the write.
- **What to do:** Reconnect.

Die Bluetooth-Verbindung ging verloren, während ein Befehl gesendet wurde.

<a id="ble.write_timeout"></a>
#### `ble.write_timeout`

**The device is not reading commands.**

- **Probably:** The firmware is busy or the link is stalled.
- **What to do:** Reconnect; if it repeats, restart the device.

Das Gerät hat einen über Bluetooth gesendeten Befehl nicht rechtzeitig bestätigt.

<a id="ble.disconnected"></a>
#### `ble.disconnected`

**The Bluetooth connection to the device was lost.**

- **Probably:** The device is out of range, was restarted or turned off.
- **What to do:** Bring the device closer; the console reconnects automatically if enabled.

Die Meldung kann das Gerät nennen, etwa „The Bluetooth connection to `<name>` was lost.“

<a id="target.not_halted"></a>
#### `target.not_halted`

**The CPU is running.**

- **Probably:** Registers can be read only while the core is halted.
- **What to do:** Halt the core first.

Kommt von `hwio chester app regs` (oder `lte regs`) und dem MCP-Server (`hwio mcp`). Halten Sie den Kern zuerst mit `hwio chester app halt` an.

<a id="target.address_out_of_range"></a>
#### `target.address_out_of_range`

**The address range is not allowed for this operation.**

- **Probably:** Memory writes are allowed only in the RAM and peripheral ranges of the target.
- **What to do:** Use an address inside the RAM or peripheral ranges of the target.

Kommt von `mem read`, `mem write` und dem MCP-Server (`hwio mcp`). Die Ursache kann auch besagen, dass der Bereich über das Ende des Adressraums hinausreicht oder dass er sich mit CPU-System- oder Debug-Registern oder den Registern des Flash-Controllers überschneidet, die nie beschrieben werden.

<a id="target.memory_access_failed"></a>
#### `target.memory_access_failed`

**Reading or writing target memory failed.**

- **Probably:** The address is not mapped, or the core is in a fault state.
- **What to do:** Check the address and try again after a reset.

Kommt von `mem read`, `mem write` und dem MCP-Server (`hwio mcp`).

<a id="category-firmware"></a>
### Firmware (Exit-Code 7)

<a id="firmware.hex_parse_error"></a>
#### `firmware.hex_parse_error`

**The firmware file is not a valid HEX file.**

- **Probably:** The file is damaged or it is not a HEX file.
- **What to do:** Use the .hex file produced by the build or download it again.

Die Meldung kann die Zeile nennen, etwa „The firmware file is not a valid HEX file (line `<n>`).“

<a id="firmware.invalid_image"></a>
#### `firmware.invalid_image`

**The file is not a supported firmware file.**

- **Probably:** It is not a CHESTER LTE bundle, a modem firmware zip or a HEX file, or the zip exceeds the safety limits.
- **What to do:** Use a CHESTER .hex file or LTE bundle (.zip).

HARDWARIO Monitor prüft den Inhalt der Datei, nicht die Endung. Varianten: „The file is not a CHESTER LTE bundle, modem firmware or HEX file.“ und, bei einem Download, „The downloaded file is not `<name>`.“

<a id="firmware.out_of_range"></a>
#### `firmware.out_of_range`

**The firmware does not fit the MCU.**

- **Probably:** The image was built for another MCU.
- **What to do:** Check that the firmware is for this MCU.

Die Meldung kann die Adresse angeben, etwa „The firmware does not fit the `<target>` MCU (address `<addr>`).“ Die Variante „The firmware writes into the Product Information Block (address `<addr>`).“ bedeutet, dass das Image den Product Information Block überschreiben würde, was nie erlaubt ist.

<a id="firmware.uicr_in_image"></a>
#### `firmware.uicr_in_image`

**The firmware also writes the UICR configuration area.**

- **Probably:** The build includes UICR settings.
- **What to do:** Re-run with --allow-uicr if this is intended; the Product Information Block is kept.

`--allow-uicr` ist die CLI-Option; in der Desktop-App schalten Sie **Allow writing UICR** unter **Options** auf der Seite **Flash** ein. Siehe [UICR und der Product Information Block](flashing.md#uicr-und-der-product-information-block).

<a id="firmware.verify_failed"></a>
#### `firmware.verify_failed`

**Verification of the written firmware failed.**

- **Probably:** The connection is unstable, or the flash is protected or faulty.
- **What to do:** Flash again; if it repeats, lower the J-Link speed or check the cables.

Die Meldung kann die Adresse angeben, etwa „Verification failed at address `<0xADDR>`.“ Stellen Sie eine niedrigere Geschwindigkeit unter **Advanced** (⋯) in der Geräteleiste, in **Settings** › **J-Link** oder mit `--jlink-speed` ein.

<a id="firmware.not_found_in_catalog"></a>
#### `firmware.not_found_in_catalog`

**The firmware was not found in the catalog.**

- **Probably:** The firmware ID is wrong or it was deleted.
- **What to do:** Check the firmware ID.

Erscheint, wenn Sie über eine 32-stellige Firmware-ID flashen. Die Meldung kann die ID nennen, etwa „Firmware `<id>` was not found in the catalog.“

<a id="firmware.modem_update_failed"></a>
#### `firmware.modem_update_failed`

**The modem firmware update failed.**

- **Probably:** The modem firmware zip is wrong, the connection was lost, or nRF Util reported an error (see Details).
- **What to do:** Check the cable and power, then flash the modem firmware again.

Die LTE-Modem-Firmware wird von nRF Util geschrieben; seine Ausgabe steht in **Details**. Siehe [Nordic nRF Util](install.md#nordic-nrf-util-nur-für-die-lte-modem-firmware).

<a id="firmware.checksum_mismatch"></a>
#### `firmware.checksum_mismatch`

**The downloaded firmware is damaged (checksum mismatch).**

- **Probably:** The file was changed or damaged during the download.
- **What to do:** Try again; if it repeats, report it to HARDWARIO.

Die heruntergeladene Datei stimmte nicht mit der Prüfsumme aus dem Katalog überein und wurde daher nicht verwendet. Die Desktop-App kann „The downloaded file is damaged. Try again.“ anzeigen.

<a id="category-network"></a>
### Netzwerk (Exit-Code 8)

<a id="network.download_failed"></a>
#### `network.download_failed`

**Downloading the firmware failed.**

- **Probably:** There is no internet connection, a proxy blocks it, or the server failed.
- **What to do:** Check the internet connection and try again.

Die Meldung kann die Firmware nennen, etwa „Downloading firmware `<id>` failed.“ oder „Downloading `<name>` failed.“ In der Desktop-App können Sie stattdessen auch eine eigene Datei wählen.

<a id="update.network"></a>
#### `update.network`

**Could not reach GitHub to check for updates.**

- **Probably:** There is no internet connection, or a firewall or proxy blocks GitHub.
- **What to do:** Check your connection or try again later.

Kommt von der Update-Prüfung (**Settings** › **About** oder `hwio update check`).

<a id="update.rate_limited"></a>
#### `update.rate_limited`

**GitHub temporarily refused the update check.**

- **Probably:** GitHub limits how often a network address may ask; other computers on the same network may have used up the limit.
- **What to do:** Try again in an hour.

Kommt von der Update-Prüfung. GitHub begrenzt die Anzahl der Anfragen pro Netzwerkadresse.

<a id="update.bad_response"></a>
#### `update.bad_response`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

Kommt von der Update-Prüfung.

<a id="update.too_large"></a>
#### `update.too_large`

**The update information from GitHub could not be read.**

- **Probably:** GitHub sent an unexpected answer, or a proxy changed it.
- **What to do:** Try again later; if it persists, check the releases page: https://github.com/hardwario/hio-monitor/releases

Kommt von der Update-Prüfung, wenn die Antwort von GitHub größer als erlaubt ist. Verwendet dieselben Texte wie `update.bad_response`.

<a id="category-io"></a>
### Dateien und Einstellungen: io (Exit-Code 3)

<a id="io.file_not_found"></a>
#### `io.file_not_found`

**The file was not found.**

- **Probably:** The path is wrong or the file was moved or deleted.
- **What to do:** Check the file path (see Details).

Der Pfad steht in **Details**. Beim MCP-Server (`hwio mcp`) kann die Meldung für eine Ordneroption wie `--dump-dir` „The `<what>` `<dir>` does not exist.“ lauten.

<a id="io.read_failed"></a>
#### `io.read_failed`

**The file could not be read.**

- **Probably:** You do not have permission to read it or the disk reported an error.
- **What to do:** Check the file path and its permissions (see Details).

Bei einer Konsolen-Batch-Datei in der Desktop-App lautet die Meldung „The batch file could not be read.“

<a id="io.write_failed"></a>
#### `io.write_failed`

**The file could not be written.**

- **Probably:** The disk is full or read-only, or you do not have permission to write.
- **What to do:** Check the path, the free disk space and the permissions (see Details).

Der Pfad steht in **Details**. Varianten: „The file could not be saved.“ beim Speichern der Konsole in der Desktop-App und „The console log file could not be written; logging to file is off for this session.“ Beim MCP-Server (`hwio mcp`) kann die Meldung „The `<what>` `<dir>` is not writable.“ lauten.

<a id="settings.newer_schema"></a>
#### `settings.newer_schema`

**Settings were written by a newer version and are opened read-only.**

- **Probably:** An older version of HARDWARIO Monitor is running now.
- **What to do:** Changes will not be saved; update HARDWARIO Monitor.

Tritt auf, nachdem Sie eine neuere Version verwendet und dann eine ältere gestartet haben. Die Desktop-App zeigt das Banner „Settings were created by a newer version and are read-only.“ Die Einstellungsdatei wird nicht verändert, sodass die neuere Version weiterhin alle ihre Einstellungen hat.

<a id="diag.report_write_failed"></a>
#### `diag.report_write_failed`

**The diagnostic report could not be saved.**

- **Probably:** The folder is not writable or the disk is full.
- **What to do:** Choose another folder.

Kommt von **Help** › **Create diagnostic report…** oder `hwio diag report`. Siehe [Diagnosebericht](privacy.md#diagnosebericht).

<a id="mcp.port_in_use"></a>
#### `mcp.port_in_use`

**The MCP port is already in use.**

- **Probably:** Another HARDWARIO Monitor, `hwio mcp --http` or another MCP server (for example rttt) is listening on it.
- **What to do:** Stop the other server or choose another port with --port.

MCP-Server (`hwio mcp --http`). Siehe [MCP](../en/mcp.md).

<a id="mcp.listen_failed"></a>
#### `mcp.listen_failed`

**The MCP server could not start.**

- **Probably:** The operating system refused to open the listening socket on 127.0.0.1.
- **What to do:** Choose another port with --port.

MCP-Server (`hwio mcp --http`).

<a id="mcp.token_file"></a>
#### `mcp.token_file`

**The MCP token file cannot be read or created.**

- **Probably:** The file or its folder has the wrong permissions, or the file does not contain a valid token.
- **What to do:** Check the file permissions, or delete the file to create a new token.

MCP-Server (`hwio mcp --http`). Siehe [Sicherheit](../en/mcp.md#security).

<a id="io.file_exists"></a>
#### `io.file_exists`

**The output file already exists.**

- **Probably:** Existing files are never overwritten by this command.
- **What to do:** Choose a new file name or delete the file first.

Kommt von Befehlen, die mit `-o` eine Datei schreiben (zum Beispiel `uicr read` oder `mem read`), und von Speicherabzügen des MCP-Servers (`hwio mcp`). Fügen Sie `--force` hinzu, um die Datei zu ersetzen.

<a id="cli.file_is_symlink"></a>
#### `cli.file_is_symlink`

**The output file is a symbolic link.**

- **Probably:** Symbolic links are never followed or replaced, even with --force.
- **What to do:** Choose another file name or remove the link first.

Nur CLI, mit `-o FILE --force`.

<a id="category-usage"></a>
### Verwendung (Exit-Code 2)

<a id="usage.invalid_argument"></a>
#### `usage.invalid_argument`

**An argument or option is invalid.**

- **Probably:** The request contains a value that is not supported.
- **What to do:** Check the arguments and try again.

Die Meldung sagt meist genau, welcher Wert falsch ist, zum Beispiel eine ungültige Option (der Vorschlag lautet dann „Run `hwio --help`.“), ein unbekanntes Geräteprofil oder Target, eine J-Link-Geschwindigkeit oder Latenz außerhalb des Bereichs, eine ungültige Adresse oder Länge, eine Firmware-ID, die nicht 32 Hexadezimalziffern hat, oder mehrere Bluetooth-Geräte, die zu `--name` passen (verwenden Sie `--address`). In der Desktop-App deckt er auch Konsolen-Batch-Dateien ab, die zu groß sind, einen zu langen Befehl oder keine Befehle enthalten. Siehe [CLI](../en/cli.md).

<a id="usage.confirmation_required"></a>
#### `usage.confirmation_required`

**This operation needs confirmation.**

- **Probably:** The command runs non-interactively, so it cannot ask.
- **What to do:** Re-run with --yes.

Nur CLI. Befehle, die löschen oder schreiben (Flashen, Löschen, Recover, Speicher- und UICR-Schreibzugriffe, `jlink update-firmware`, Konsolen-Batch), fragen vor dem Start nach. Ohne Terminal oder mit `--json` können sie nicht fragen.

<a id="ble.pairing_not_supported"></a>
#### `ble.pairing_not_supported`

**This system pairs Bluetooth devices only when connecting.**

- **Probably:** macOS pairs a device automatically when it connects.
- **What to do:** Connect to the device; macOS asks for the passkey.

Nur macOS, von `hwio ble pair`. Siehe [Kopplung](bluetooth.md#kopplung).

<a id="mcp.tool_not_allowed"></a>
#### `mcp.tool_not_allowed`

**This MCP tool is not allowed by the server.**

- **Probably:** Destructive tools must be allowed when the server starts.
- **What to do:** Start the server with --allow and the tool's category, or with --allow all.

MCP-Server (`hwio mcp`). Die Meldung kann das Werkzeug und die zu erlaubenden Einträge nennen, etwa „Start the server with --allow `<items>` (or --allow all).“ Siehe [Sicherheit](../en/mcp.md#security).

<a id="mcp.device_ambiguous"></a>
#### `mcp.device_ambiguous`

**Several devices are connected and the tool does not know which one to use.**

- **Probably:** The device argument was left out while more than one device is open.
- **What to do:** Pass the device argument; list_devices shows the ids.

MCP-Server (`hwio mcp`). `list_devices` ist ein MCP-Werkzeug.

<a id="mcp.device_not_open"></a>
#### `mcp.device_not_open`

**The device is not connected.**

- **Probably:** It was never connected, it was disconnected, or its tab was closed.
- **What to do:** Call connect first.

MCP-Server (`hwio mcp`). `connect` ist ein MCP-Werkzeug.

<a id="mcp.capability_missing"></a>
#### `mcp.capability_missing`

**This tool needs a J-Link connection.**

- **Probably:** The device is connected over Bluetooth, which provides only the console.
- **What to do:** Connect the device with a J-Link probe.

MCP-Server (`hwio mcp`).

<a id="mcp.to_file_not_allowed"></a>
#### `mcp.to_file_not_allowed`

**The file name must be a plain name.**

- **Probably:** Memory dumps are written only into the server's dump folder.
- **What to do:** Use a plain file name such as flash.bin, or start the server with --dump-dir DIR.

MCP-Server (`hwio mcp`). Die Meldung kann den Ordner für Speicherabzüge nennen. Siehe [Sicherheit](../en/mcp.md#security).

<a id="category-timeout"></a>
### Zeitüberschreitung (Exit-Code 9)

<a id="timeout"></a>
#### `timeout`

**The operation timed out.**

- **Probably:** The other side did not respond in time.
- **What to do:** Try it again.

Eine allgemeine Zeitüberschreitung: zum Beispiel hat sich das Gerät nicht rechtzeitig verbunden („The device did not connect in time.“), Flashen oder Löschen dauerte zu lange, oder nRF Util wurde nicht fertig. `hwio chester app info` verwendet eigene Texte, wenn die Firmware nicht auf `info show` antwortet.

<a id="diag.check_timeout"></a>
#### `diag.check_timeout`

**A check did not finish in time.**

- **Probably:** The system is slow or a tool does not answer.
- **What to do:** Run the checks again.

Eine Warnung von `hwio doctor` oder im Diagnosebericht. Die Meldung kann die Prüfung nennen, etwa „The check "`<title>`" did not finish in time.“

<a id="mcp.consent_timeout"></a>
#### `mcp.consent_timeout`

**Nobody answered the permission request.** Reserviert für den MCP-Server der Desktop-App, der noch nicht verfügbar ist.

<a id="category-cancelled"></a>
### Abgebrochen (Exit-Code 130)

<a id="cancelled"></a>
#### `cancelled`

**Cancelled by user.**

- **Probably:** The operation was stopped before it finished.
- **What to do:** Run the operation again if needed.

Erscheint, wenn Sie Ctrl+C oder **Cancel** drücken, eine Bestätigung mit Nein beantworten oder das Gerät schließen. Die Variante „The output was closed.“ bedeutet, dass das Programm, das die Ausgabe von `hwio` liest, beendet wurde, zum Beispiel `hwio ... --json | head`.

<a id="mcp.consent_denied"></a>
#### `mcp.consent_denied`

**The user did not allow this action.** Reserviert für den MCP-Server der Desktop-App, der noch nicht verfügbar ist.

<a id="category-internal"></a>
### Intern (Exit-Code 1)

<a id="internal.unexpected"></a>
#### `internal.unexpected`

**An unexpected internal error occurred.**

- **Probably:** This is a bug in HARDWARIO Monitor.
- **What to do:** Please report it with the log file (see Details).

Bitte erstellen Sie einen [Diagnosebericht](privacy.md#diagnosebericht) und senden Sie ihn an HARDWARIO.

<a id="internal.queue_full"></a>
#### `internal.queue_full`

**An internal buffer is full.**

- **Probably:** This is a bug in HARDWARIO Monitor or the computer is overloaded.
- **What to do:** Please report it with the log file (see Details).

Zum Beispiel sendet das Gerät Konsolenzeilen schneller, als sie angezeigt werden können. Bitte erstellen Sie einen [Diagnosebericht](privacy.md#diagnosebericht).

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

Zum Beispiel haben Sie einen Flash-Vorgang gestartet, während eine Konsolen-Batch-Datei lief. Wird eine Batch-Datei auf diese Weise unterbrochen, lautet die Meldung „The batch stopped after `<done>` of `<total>` commands.“

<a id="mcp.too_many_requests"></a>
#### `mcp.too_many_requests`

**Too many MCP requests are running.**

- **Probably:** The server runs at most 32 tool calls at the same time.
- **What to do:** Wait for running calls to finish and try again.

MCP-Server (`hwio mcp`).
