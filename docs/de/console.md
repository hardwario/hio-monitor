# Konsole

[Dokumentation](index.md) › Konsole

Die Seite **Console** (Konsole, <kbd>Ctrl</kbd>+<kbd>1</kbd>) zeigt, was die Anwendungs-MCU des CHESTER über den
J-Link (SEGGER RTT) sendet, in zwei Bereichen:

- **Shell**: die Befehls-Shell des Geräts. Sie geben darunter Befehle ein, und ihre Antworten erscheinen hier.
  Gesendete Befehle werden als `> command` angezeigt.
- **Log**: die Log-Meldungen des Geräts, mit einer Spalte für die Stufe: **D** Debug, **I** Info, **W** Warnung,
  **E** Fehler und **H** für Hexdump-Zeilen.

Die LTE-MCU hat keine Konsole; beim Target **lte** zeigt die Seite **This target has no console** (dieses Target hat
keine Konsole).

Zeilen zwischen den Gerätezeilen markieren, was mit der Verbindung geschah: **— connected —** (verbunden),
**— disconnected —** (getrennt), **— paused: … —** (angehalten) während eines Flash-Vorgangs, **— resumed —**
(fortgesetzt).

## Layout

Ziehen Sie den Griff zwischen den Bereichen, um ihre Größe zu ändern (oder fokussieren Sie ihn und verwenden Sie die
Pfeiltasten). Unter 1000 Pixeln Fensterbreite werden die Bereiche untereinander angeordnet; **More** (Mehr) in der
Werkzeugleiste enthält **Stacked layout** (gestapeltes Layout) und **Swap panes** (Bereiche tauschen) sowie
**Show timestamps** (Zeitstempel anzeigen) und **Wrap lines** (Zeilen umbrechen). <kbd>Ctrl</kbd>+<kbd>=</kbd>,
<kbd>Ctrl</kbd>+<kbd>-</kbd> und <kbd>Ctrl</kbd>+<kbd>0</kbd> ändern die Schriftgröße.

Eine Ansicht folgt neuen Zeilen, sobald sie eintreffen. Scrollen Sie nach oben, bleibt sie, wo Sie sind; eine
Schaltfläche **▼ N new lines** (N neue Zeilen) bringt Sie zurück, ebenso <kbd>End</kbd> oder **Follow** (Folgen).
**Pause** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) friert die Ansicht ein, während weiter Zeilen eintreffen; **Resume**
(Fortsetzen) zeigt sie an. Jeder Bereich behält die letzten 100 000 Zeilen.

## Werkzeugleiste

| Schaltfläche | Funktion |
|---|---|
| **Reset** | Setzt das Gerät zurück, oder mit **Reset and halt** setzt es zurück und hält die CPU angehalten |
| **Reconnect** | Verbindet sich erneut mit der Konsole (<kbd>F4</kbd>) |
| **Batch…** | Führt eine [Batch-Datei](#batch-dateien) aus (<kbd>Ctrl</kbd>+<kbd>B</kbd>); ⓘ daneben öffnet die Dokumentation |
| **Clear** | Leert die **Shell**, das **Log** oder **Both** (beide) (<kbd>Ctrl</kbd>+<kbd>K</kbd> leert den fokussierten Bereich) |
| **Save view…** | Speichert den fokussierten Bereich in eine Textdatei (<kbd>Ctrl</kbd>+<kbd>S</kbd>) |
| **Follow**, **Pause** | Siehe oben |
| **More** | **Open log file**, **Open log folder** (die [Konsolendateien](#konsolendateien)) und die Layout-Optionen |

## Befehle senden

Klicken Sie in das Eingabefeld unter der Shell (oder drücken Sie <kbd>Ctrl</kbd>+<kbd>E</kbd>), geben Sie einen
Befehl ein und drücken Sie <kbd>Enter</kbd>. Ist das Eingabefeld deaktiviert, sagt sein Platzhaltertext, warum
(**Not connected**, **Connecting…** oder ein laufender Vorgang wie ein Flash-Vorgang).

- <kbd>↑</kbd> und <kbd>↓</kbd> blättern durch den Befehlsverlauf.
- <kbd>Ctrl</kbd>+<kbd>R</kbd> durchsucht den Verlauf: Geben Sie einen Teil eines Befehls ein, <kbd>Enter</kbd>
  übernimmt ihn in das Eingabefeld.
- Während Sie tippen, schlägt eine Liste die eigenen Befehle des Geräts (mit ihrem Hilfetext) und passende
  Verlaufseinträge vor. <kbd>↑</kbd>/<kbd>↓</kbd> wählen, <kbd>Tab</kbd> oder <kbd>→</kbd> übernimmt,
  <kbd>Esc</kbd> schließt die Liste.
- Grauer Text nach dem Cursor vervollständigt die Zeile; <kbd>→</kbd> oder <kbd>End</kbd> übernimmt ihn.
- <kbd>Tab</kbd> vervollständigt den Befehl. Er verlässt das Eingabefeld nie; verwenden Sie
  <kbd>Shift</kbd>+<kbd>Tab</kbd>, um weiterzugehen.

Die Befehle des Geräts werden beim Verbinden der Konsole mit `help` gelesen. Schalten Sie das in **Settings →
Console → Read the command list from the device on connect (sends “help”)** aus, und die Vorschläge mit **Suggest
commands while typing**.

### Befehlsverlauf

Der Verlauf behält die letzten 1000 Befehle pro Gerätetyp und Target (zum Beispiel CHESTER app). Er wird von allen
Tabs und von der Kommandozeile (`hwio chester app console`) gemeinsam genutzt. Befehle, die Geheimnisse enthalten
(wie `lte config password`, `lrw config appkey` oder `ble config passkey`), werden nie gespeichert, ebenso wenig ein
Befehl, den Sie mit einem Leerzeichen beginnen. **Settings → Console** schaltet das Speichern aus (**Remember
commands**) und löscht den Verlauf (**Clear history**).

## Suchen und Filtern

Die Leiste unter dem Log hat zwei Modi:

- **Search** (Suchen, <kbd>Ctrl</kbd>+<kbd>F</kbd>): hebt die Treffer hervor und zeigt **3 / 17**. <kbd>Enter</kbd>
  oder <kbd>F3</kbd> springt zum nächsten Treffer, <kbd>Shift</kbd>+<kbd>Enter</kbd> oder
  <kbd>Shift</kbd>+<kbd>F3</kbd> zum vorherigen; im Log funktionieren auch <kbd>N</kbd> und
  <kbd>Shift</kbd>+<kbd>N</kbd>.
- **Filter** (<kbd>Ctrl</kbd>+<kbd>L</kbd>): zeigt nur die passenden Zeilen (**Showing 40 of 2 000 lines**).

**Aa** beachtet die Groß-/Kleinschreibung, **.\*** behandelt den Text als regulären Ausdruck. Die Stufen-Schaltflächen
**D**, **I**, **W**, **E** und **H** blenden die Stufen ein oder aus; Hexdump-Zeilen (**H**) sind ausgeblendet, bis
Sie sie einschalten. <kbd>Esc</kbd> schließt die Suche, ein zweites <kbd>Esc</kbd> löscht den Filter.

Die Shell hat eine eigene Suche: Drücken Sie <kbd>Ctrl</kbd>+<kbd>F</kbd>, während die Shell den Fokus hat.

## Auswählen und Kopieren

Klicken Sie auf eine Zeile, um sie auszuwählen; <kbd>Shift</kbd>+Klick oder Ziehen wählt mehr aus,
<kbd>Ctrl</kbd>+<kbd>A</kbd> wählt alles aus. <kbd>Ctrl</kbd>+<kbd>C</kbd> oder das Kontextmenü (Rechtsklick) kopiert
die Zeilen („Copied to clipboard.“); höchstens 100 000 Zeilen werden auf einmal kopiert.

## Ansicht speichern

**Save view…** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) speichert den fokussierten Bereich so, wie er angezeigt wird, mit
Zeitstempeln, wenn sie eingeschaltet sind. Der Name ist vorausgefüllt, zum Beispiel
`chester-app-clime-v3.2.0-20261103-101500-log.txt`, und der Ordner ist der zuletzt verwendete (beim ersten Mal
Dokumente).

## Batch-Dateien

Eine Batch-Datei ist eine UTF-8-Textdatei mit einem Shell-Befehl pro Zeile. **Batch…** (<kbd>Ctrl</kbd>+<kbd>B</kbd>)
sendet die Zeilen nacheinander, eine alle 200 ms. Leere Zeilen und Zeilen, die mit `#` beginnen, werden übersprungen.

```text
# what the device is and how it is configured
info show
app config show
```

Die ganze Datei wird geprüft, bevor die erste Zeile gesendet wird: höchstens 10 000 Befehle, 1 MiB pro Datei und
4096 Bytes pro Zeile. Während sie läuft, zeigt die Werkzeugleiste **Batch 3 / 4** mit einer Stopp-Schaltfläche. Ein
Batch stoppt, wenn das Gerät getrennt wird oder ein anderer Vorgang (wie ein Flash-Vorgang) beginnt.

## Konsolendateien

Sofern Sie es nicht ausschalten, wird jede Verbindung in eine Konsolendatei geschrieben: die Shell, das Log, die
gesendeten Befehle und die Verbindungsereignisse, mit Zeitstempeln. Die Dateien liegen im **Log folder**
(Log-Ordner), der in **Settings → Console** angezeigt wird:

| System | Ordner |
|---|---|
| Windows | `%LOCALAPPDATA%\HARDWARIO\Monitor\console` |
| macOS | `~/Library/Application Support/HARDWARIO/Monitor/console` |
| Linux | `~/.local/share/HARDWARIO/Monitor/console` |

Eine Datei ist nach dem Gerät, der Firmware, der Zeit (UTC) und der Probe benannt, zum Beispiel
`chester-app-clime-v3.2.0-20261103-101500-<serial>.log`. Eine Datei über 10 MiB wird in `….part2.log` fortgesetzt.
Der Ordner behält die neuesten 50 Dateien, zusammen höchstens 200 MB. Geheimnisse werden als `<redacted>` geschrieben
(dieselben Befehle wie im Verlauf sowie der Claim-Token und der BLE-Passkey von `info show`). Unter Linux und macOS
kann nur Ihr Benutzer die Dateien lesen.

Schalten Sie Konsolendateien in **Settings → Console → Save console to log files** aus, oder pro Probe in **⋯**
(**Advanced**).

## Neu verbinden

Wenn die Verbindung abbricht (das Gerät wurde zurückgesetzt, verlor die Stromversorgung oder das Kabel wurde
gezogen), verbindet sich die Konsole selbstständig neu, und die Geräteleiste zeigt **Reconnecting…** mit dem Grund.
Schalten Sie das mit **Auto-reconnect** in **⋯** (**Advanced**) oder **Settings → J-Link** aus; dann verbinden
<kbd>F4</kbd> oder **Reconnect** erneut.

Sendet das Gerät schneller, als die App anzeigen kann, meldet ein Banner, wie viele Zeilen verworfen wurden.

## Kommandozeile

Dieselbe Konsole in einem Terminal:

```sh
hwio chester app console                   # interactive: F4 reconnects, Ctrl+C on an empty line or Ctrl+D quits
hwio chester app command "info show"       # one command, print the answer
echo "app config show" | hwio chester app console
```

Siehe [Kommandozeile](../en/cli.md#console).
