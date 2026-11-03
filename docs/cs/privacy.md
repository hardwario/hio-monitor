# Soukromí a uložená data

[Dokumentace](index.md) › Soukromí

HARDWARIO Monitor nemá žádnou telemetrii ani účet. K internetu se připojuje jen kvůli níže uvedeným věcem a vše, co
ukládá, zůstává ve vašem počítači.

## Síťová připojení

| Co | Kdy | Kam |
|---|---|---|
| [Kontrola aktualizací](#kontrola-aktualizací) | desktopová aplikace, nejvýše jednou denně (lze vypnout); `hwio update check` | `api.github.com` |
| Firmware z katalogu | když flashujete podle ID firmwaru nebo spustíte `hwio chester app catalog-info` | `firmware.hardwario.com` |
| LTE firmware HARDWARIO | když flashujete LTE firmware bez vlastního souboru | `docs.hardwario.com` |
| Odkazy na dokumentaci | když na některý kliknete | váš prohlížeč |

Server MCP (`hwio mcp --http`) naslouchá jen na tomto počítači (127.0.0.1) a ze sítě není dostupný. SEGGER J-Link
Software a nRF Util jsou samostatné programy s vlastním chováním.

## Kontrola aktualizací

Desktopová aplikace se 10 sekund po spuštění zeptá GitHubu na seznam vydání HARDWARIO Monitor, nejvýše jednou za
24 hodin. Požadavek nenese žádné údaje o vás ani o vašich zařízeních, jen verzi aplikace v hlavičce `User-Agent`.
Když vyjde novější verze, oznámí to banner (**Je k dispozici HARDWARIO Monitor 2.1.0 (máte 2.0.0).**) s tlačítky
**Stáhnout**, které otevře stránku vydání, a **Tuto verzi přeskočit**. Aplikace sama nikdy nic nestahuje ani
neinstaluje.

**Nastavení → O aplikaci → Aktualizace**:

- **Automaticky kontrolovat aktualizace** (zapnuto): vypne denní kontrolu.
- **Zahrnout předběžné verze (beta, kandidáti na vydání)** (vypnuto).
- **Zkontrolovat nyní**.

Příkazová řádka kontroluje, jen když o to požádáte: `hwio update check` (`--prerelease` zahrne předběžné verze).

## Diagnostická zpráva

Když potřebujete pomoc od podpory HARDWARIO, vytvořte diagnostickou zprávu: v aplikaci **Nápověda → Vytvořit
diagnostickou zprávu…**, nebo `hwio diag report` (`-o FILE`). Je to soubor zip, který obsahuje:

- souhrn: verzi aplikace, operační systém, nalezený J-Link Software a nRF Util, připojené sondy J-Link, otevřené
  karty a výsledky kontrol `hwio doctor`;
- nedávné logy aplikace (aplikace i `hwio`);
- soubor nastavení;
- poslední zprávy o pádu, pokud nějaké jsou.

Před zapsáním se vaše uživatelské jméno, domovská složka a název počítače nahradí (`<user>`, `~`, `<host>`) a hodnoty,
které vypadají jako hesla, tokeny nebo klíče, se nahradí `<redacted>`. Sériová čísla sond ve zprávě zůstávají. Logy
mohou obsahovat výstup vašeho zařízení, proto si zprávu před odesláním prohlédněte. Soubory konzole a historie
příkazů nejsou součástí zprávy.

Zpráva se uloží do vašeho počítače (ve výchozím stavu `hwio-diagnostics-YYYYMMDD-HHMMSS.zip` do složky Dokumenty,
u `hwio diag report` do aktuální složky); nikdy se nikam neodesílá. K žádosti o podporu ji přiložte sami.

## Zprávy o pádu

Pokud aplikace nebo `hwio` spadne, zapíše se zpráva o pádu do složky `crashes` v datech aplikace (uchovává se nejvýše
5 zpráv). Nic se neodesílá. Při dalším spuštění aplikace ukáže **HARDWARIO Monitor se minule neočekávaně ukončil.** a
nabídne **Vytvořit diagnostickou zprávu**, která zprávu o pádu obsahuje. `HWIO_NO_CRASH_HANDLER=1` zprávy o pádu
vypne.

## Co se kde ukládá

| Data | Windows | macOS | Linux |
|---|---|---|---|
| Nastavení (`Monitor.ini`) | `%APPDATA%\HARDWARIO\Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` |
| Data aplikace | `%LOCALAPPDATA%\HARDWARIO\Monitor` | `~/Library/Application Support/HARDWARIO/Monitor` | `~/.local/share/HARDWARIO/Monitor` |

Složka dat aplikace obsahuje:

| Složka | Obsah | Limit |
|---|---|---|
| `console` | [soubory konzole](console.md#soubory-konzole) a historii příkazů (`history-<device>-<target>.txt`) | 50 souborů a 200 MB; 1000 příkazů na historii |
| `logs` | logy aplikace, jeden soubor na každé spuštění | 10 souborů aplikace, 20 souborů `hwio`, 50 MiB každého druhu |
| `firmware-cache` | stažený firmware z katalogu a LTE firmware | 100 souborů, 500 MiB |
| `crashes` | zprávy o pádu | 5 souborů |

Soubor nastavení, logy a soubory konzole sdílí aplikace a `hwio`. Soubory konzole a historii může číst jen váš
uživatel (Linux, macOS). Příkazy, které obsahují tajné údaje, se do historie neukládají a v souborech konzole se
zapisují jako `<redacted>` ([Konzole](console.md#historie-příkazů)).

Odinstalace HARDWARIO Monitor tyto složky nesmaže; pokud chcete, smažte je sami. Jen historii příkazů smažete takto:
**Nastavení → Konzole → Vymazat historii**, nebo `hwio console history clear`.
