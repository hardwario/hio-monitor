# Erste Schritte

[Dokumentation](index.md) › Erste Schritte

HARDWARIO Monitor zeigt die Konsole und das Log von HARDWARIO-CHESTER-Geräten und flasht ihre Firmware, über eine
SEGGER-J-Link-Probe. Bluetooth LE und der MCP-Server für KI-Assistenten sind auf der Kommandozeile verfügbar
(`hwio`, siehe [Bluetooth LE](bluetooth.md) und [MCP-Server](../en/mcp.md)).

Installieren Sie vor dem Start die App und die SEGGER J-Link Software ([Installation](install.md)).

## Das Gerät

CHESTER hat zwei MCUs, die **Targets** (Ziele) genannt werden:

| Target | MCU | Was Sie tun können |
|---|---|---|
| `app` | die Anwendungs-MCU (nRF52840) | Konsole und Log (RTT), Flashen, Löschen, Reset, Recover |
| `lte` | die LTE-MCU (nRF9160 oder nRF9151) | LTE-Firmware und Modem-Firmware flashen, Löschen, Reset, Recover; sie hat keine Konsole |

Verbinden Sie den J-Link über USB mit dem Computer und mit dem Programmieranschluss des CHESTER, und versorgen Sie den
CHESTER mit Batterien oder über seinen USB-C-Stromeingang.

## Verbinden

1. Starten Sie **HARDWARIO Monitor**. Der erste Tab, **New device** (Neues Gerät), zeigt **Choose a device to
   connect** (Gerät zum Verbinden auswählen) mit den J-Link-Probes, die an diesen Computer angeschlossen sind. Die
   Liste wird alle zwei Sekunden aktualisiert.
2. Wählen Sie die Probe. Eine Probe, die bereits in einem anderen Tab geöffnet ist, zeigt **In use (tab N)** (in
   Verwendung, Tab N); wenn Sie sie wählen, wechseln Sie zu diesem Tab. Eine Probe, die von einem anderen Programm
   belegt ist, zeigt **In use by another program** (von einem anderen Programm verwendet) und kann nicht gewählt
   werden.
3. Lassen Sie unter **Device** (Gerät) **CHESTER** eingestellt und wählen Sie das Target: **app** oder **lte**.
4. Drücken Sie **Connect** (Verbinden).

Die Geräteleiste oben im Tab zeigt die Probe, das Target und den Zustand: **Connecting…**, **Connected ·
3.30 V** (mit der Target-Spannung), **Reconnecting…** oder **Disconnected — reason** (Verbindung wird
hergestellt…, Verbunden, Verbindung wird wiederhergestellt…, Getrennt — Grund). **Connect** und **Disconnect** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>)
befinden sich rechts, und <kbd>F4</kbd> verbindet neu.

Wird keine Probe gefunden, zeigt die Seite **No J-Link probe found** (keine J-Link-Probe gefunden) und ob die J-Link
Software gefunden wurde. Prüfen Sie das USB-Kabel und unter Linux die udev-Regel
([Installation](install.md#usb-zugriff-auf-den-j-link)). `hwio doctor` prüft all das.

### Verbindungsoptionen

**⋯** (**Advanced**, Erweitert) in der Geräteleiste enthält die Optionen dieser Probe. HARDWARIO Monitor merkt sie
sich pro Seriennummer der Probe. Sie gelten ab dem nächsten Verbinden.

| Option | Standard | Bedeutung |
|---|---|---|
| **Automatic speed** | an | Der J-Link wählt die Geschwindigkeit. Aus: **J-Link speed (kHz)**, 5 bis 50000 |
| **RTT latency (ms)** | 0 (der Wert des Geräts, 50 ms für CHESTER) | Wie oft die Konsole gelesen wird |
| **Auto-reconnect** | an | Nach einem Verbindungsabbruch selbstständig neu verbinden |
| **Reset on connect** | aus | Das Gerät beim Verbinden zurücksetzen |
| **Save console to log file** | an | Eine [Konsolendatei](console.md#konsolendateien) schreiben |

Probes ohne eigene Optionen verwenden die Standardwerte in **Settings → J-Link** (Einstellungen → J-Link).

## Tabs

Jedes Gerät ist ein Tab; bis zu 8 können geöffnet sein. <kbd>Ctrl</kbd>+<kbd>T</kbd> oder **+** öffnet einen neuen
Tab, <kbd>Ctrl</kbd>+<kbd>W</kbd> schließt ihn (ebenso ein Mittelklick auf einen Tab), <kbd>Ctrl</kbd>+<kbd>Tab</kbd>
und <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> wechseln. Läuft im Tab ein Vorgang, fragt das Schließen zuerst
nach.

Die Leiste links wechselt die Seiten des Tabs: **Console** (Konsole, <kbd>Ctrl</kbd>+<kbd>1</kbd>) und **Flash**
(Flashen, <kbd>Ctrl</kbd>+<kbd>2</kbd>). **Settings** (Einstellungen, <kbd>Ctrl</kbd>+<kbd>,</kbd>) öffnet sich als
eigener Tab.

Beim nächsten Start kommen die Tabs zurück, getrennt (**Settings → General → Reopen tabs at start**).

Es läuft jeweils nur ein Fenster der App. Ein erneuter Start holt das geöffnete Fenster in den Vordergrund.

## Seiten

- [Konsole](console.md): Shell und Log des Geräts, Suche, Filter, Verlauf, Batch-Dateien, Konsolendateien.
- [Flashen](flashing.md): Anwendungs- und LTE-Firmware, Löschen, Recover, Reset.

## Einstellungen

| Abschnitt | Inhalt |
|---|---|
| **General** | **Language** (System, English, Čeština), **Theme** (System, Light, Dark), **Reopen tabs at start** |
| **Console** | Schriftgröße, Zeitstempel, Zeilenumbruch, Befehlsvorschläge, Befehlsverlauf, Konsolendateien und ihr Ordner |
| **J-Link** | die J-Link-Bibliothek (**Automatic** oder **Custom path**) und die Standard-Verbindungsoptionen |
| **nRF Util** | das Programm nRF Util (**Automatic** oder **Custom path**), nur für die LTE-Modem-Firmware nötig |
| **About** | die Version (mit **Copy** für den Support), die [Update-Prüfung](privacy.md#update-prüfung), der `hwio`-Befehl dieser Installation, Lizenzen, Links |

Änderungen gelten sofort.

## Hilfe

**Help** (Hilfe) unten in der Leiste:

- **Keyboard shortcuts (F1)**: alle Tastenkürzel der App (auch <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Create diagnostic report…**: eine ZIP-Datei für den HARDWARIO-Support
  ([Datenschutz](privacy.md#diagnosebericht)).
- **Documentation ↗**: die Seite zu HARDWARIO Monitor auf docs.hardwario.com.

Wenn etwas fehlschlägt, zeigt die App, was passiert ist, die wahrscheinliche Ursache (**Probably:**) und was zu tun
ist (**What to do:**). **Details** zeigt den Fehlercode; [Fehlerbehebung](troubleshooting.md) listet alle Codes auf.

## Kommandozeile

Alles, was die App mit einem J-Link tut, ist auch ein Befehl:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

Siehe [Kommandozeile](../en/cli.md).
