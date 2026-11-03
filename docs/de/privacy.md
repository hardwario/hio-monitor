# Datenschutz und gespeicherte Daten

[Dokumentation](index.md) › Datenschutz

HARDWARIO Monitor hat keine Telemetrie und kein Benutzerkonto. Es verbindet sich nur für die unten genannten Zwecke
mit dem Internet, und alles, was es speichert, bleibt auf Ihrem Computer.

## Netzwerkverbindungen

| Was | Wann | Wohin |
|---|---|---|
| [Update-Prüfung](#update-prüfung) | die Desktop-App, höchstens einmal am Tag (abschaltbar); `hwio update check` | `api.github.com` |
| Katalog-Firmware | wenn Sie über eine Firmware-ID flashen oder `hwio chester app catalog-info` ausführen | `firmware.hardwario.com` |
| HARDWARIO-LTE-Firmware | wenn Sie die LTE-Firmware ohne eigene Datei flashen | `docs.hardwario.com` |
| Dokumentationslinks | wenn Sie auf einen klicken | Ihr Browser |

Der MCP-Server (`hwio mcp --http`) lauscht nur auf diesem Computer (127.0.0.1) und ist aus dem Netzwerk nicht
erreichbar. SEGGER J-Link Software und nRF Util sind eigenständige Programme mit eigenem Verhalten.

## Update-Prüfung

Die Desktop-App fragt GitHub 10 Sekunden nach dem Start nach der Liste der Versionen von HARDWARIO Monitor,
höchstens einmal in 24 Stunden. Die Anfrage enthält keine Daten über Sie oder Ihre Geräte, nur die App-Version im
Header `User-Agent`. Ist eine neuere Version erschienen, meldet ein Banner das (**HARDWARIO Monitor 2.1.0 is
available (you have 2.0.0).**) mit **Download**, das die Release-Seite öffnet, und **Skip this version** (diese
Version überspringen). Die App lädt nie selbst etwas herunter oder installiert etwas.

**Settings → About → Updates** (Einstellungen → Über → Updates):

- **Check for updates automatically** (automatisch nach Updates suchen, an): schaltet die tägliche Prüfung aus.
- **Include pre-releases (beta, release candidates)** (Vorabversionen einbeziehen, aus).
- **Check now** (jetzt prüfen).

Die Kommandozeile prüft nur, wenn Sie es verlangen: `hwio update check` (`--prerelease` bezieht Vorabversionen ein).

## Diagnosebericht

Wenn Sie Hilfe vom HARDWARIO-Support benötigen, erstellen Sie einen Diagnosebericht: **Help → Create diagnostic
report…** (Hilfe → Diagnosebericht erstellen…) in der App oder `hwio diag report` (`-o FILE`). Er ist eine ZIP-Datei
mit:

- einer Zusammenfassung: die App-Version, das Betriebssystem, die gefundene J-Link Software und nRF Util, die
  angeschlossenen J-Link-Probes, die geöffneten Tabs und die Ergebnisse der Prüfungen von `hwio doctor`;
- den aktuellen Anwendungslogs (der App und von `hwio`);
- der Einstellungsdatei;
- den neuesten Absturzberichten, falls vorhanden.

Bevor er geschrieben wird, werden Ihr Benutzername, Ihr Home-Ordner und der Computername ersetzt (`<user>`, `~`,
`<host>`), und Werte, die wie Passwörter, Tokens oder Schlüssel aussehen, werden durch `<redacted>` ersetzt.
Seriennummern der Probes bleiben enthalten. Die Logs können Ausgaben Ihres Geräts enthalten, sehen Sie den Bericht
daher durch, bevor Sie ihn senden. Konsolendateien und der Befehlsverlauf sind nicht Teil des Berichts.

Der Bericht wird auf Ihrem Computer gespeichert (standardmäßig `hwio-diagnostics-YYYYMMDD-HHMMSS.zip` in Dokumente
oder bei `hwio diag report` im aktuellen Ordner); er wird nie hochgeladen. Hängen Sie ihn selbst an Ihre
Support-Anfrage an.

## Absturzberichte

Stürzt die App oder `hwio` ab, wird ein Absturzbericht in den Ordner `crashes` der Anwendungsdaten geschrieben
(höchstens 5 werden aufbewahrt). Es wird nichts gesendet. Beim nächsten Start meldet die App **HARDWARIO Monitor quit
unexpectedly last time.** (wurde zuletzt unerwartet beendet) und bietet **Create diagnostic report** an, der den
Absturzbericht enthält. `HWIO_NO_CRASH_HANDLER=1` schaltet Absturzberichte aus.

## Was wo gespeichert wird

| Daten | Windows | macOS | Linux |
|---|---|---|---|
| Einstellungen (`Monitor.ini`) | `%APPDATA%\HARDWARIO\Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` |
| Anwendungsdaten | `%LOCALAPPDATA%\HARDWARIO\Monitor` | `~/Library/Application Support/HARDWARIO/Monitor` | `~/.local/share/HARDWARIO/Monitor` |

Der Ordner der Anwendungsdaten enthält:

| Ordner | Inhalt | Grenze |
|---|---|---|
| `console` | [Konsolendateien](console.md#konsolendateien) und den Befehlsverlauf (`history-<device>-<target>.txt`) | 50 Dateien und 200 MB; 1000 Befehle pro Verlauf |
| `logs` | Anwendungslogs, eine Datei pro Lauf | 10 Dateien der App, 20 von `hwio`, je Art 50 MiB |
| `firmware-cache` | heruntergeladene Katalog-Firmware und LTE-Firmware | 100 Dateien, 500 MiB |
| `crashes` | Absturzberichte | 5 Dateien |

Die Einstellungsdatei, die Logs und die Konsolendateien werden von der App und `hwio` gemeinsam genutzt.
Konsolendateien und der Verlauf können nur von Ihrem Benutzer gelesen werden (Linux, macOS). Befehle, die Geheimnisse
enthalten, werden nicht im Verlauf gespeichert und in Konsolendateien als `<redacted>` geschrieben
([Konsole](console.md#befehlsverlauf)).

Die Deinstallation von HARDWARIO Monitor löscht diese Ordner nicht; löschen Sie sie selbst, wenn Sie möchten. Um nur
den Befehlsverlauf zu löschen: **Settings → Console → Clear history** oder `hwio console history clear`.
