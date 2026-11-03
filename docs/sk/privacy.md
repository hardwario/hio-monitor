# Súkromie a uložené údaje

[Dokumentácia](index.md) › Súkromie

HARDWARIO Monitor nemá telemetriu ani používateľský účet. K internetu sa pripája len kvôli veciam uvedeným nižšie a
všetko, čo ukladá, zostáva vo vašom počítači.

## Sieťové pripojenia

| Čo | Kedy | Kam |
|---|---|---|
| [Kontrola aktualizácií](#kontrola-aktualizácií) | desktopová aplikácia, najviac raz denne (dá sa vypnúť); `hwio update check` | `api.github.com` |
| Firmvér z katalógu | keď nahrávate firmvér podľa ID alebo spustíte `hwio chester app catalog-info` | `firmware.hardwario.com` |
| LTE firmvér HARDWARIO | keď nahrávate LTE firmvér bez vlastného súboru | `docs.hardwario.com` |
| Odkazy na dokumentáciu | keď na niektorý kliknete | váš prehliadač |

MCP server (`hwio mcp --http`) počúva len na tomto počítači (127.0.0.1) a zo siete nie je dostupný. SEGGER J-Link
Software a nRF Util sú samostatné programy s vlastným správaním.

## Kontrola aktualizácií

Desktopová aplikácia sa 10 sekúnd po spustení spýta GitHubu na zoznam vydaní HARDWARIO Monitor, najviac raz za
24 hodín. Požiadavka neobsahuje žiadne údaje o vás ani o vašich zariadeniach, len verziu aplikácie v hlavičke
`User-Agent`. Keď je k dispozícii novšia verzia, upozorní na to pruh (**HARDWARIO Monitor 2.1.0 is available (you have
2.0.0).**) s tlačidlom **Download**, ktoré otvorí stránku vydania, a **Skip this version** (preskočiť túto verziu).
Aplikácia sama nikdy nič nesťahuje ani neinštaluje.

**Settings → About → Updates**:

- **Check for updates automatically** (zapnuté): vypne dennú kontrolu.
- **Include pre-releases (beta, release candidates)** (vypnuté).
- **Check now** (skontrolovať teraz).

Príkazový riadok kontroluje len vtedy, keď o to požiadate: `hwio update check` (`--prerelease` zahrnie predbežné
vydania).

## Diagnostická správa

Keď potrebujete pomoc od podpory HARDWARIO, vytvorte diagnostickú správu: **Help → Create diagnostic report…**
v aplikácii alebo `hwio diag report` (`-o FILE`). Je to súbor zip, ktorý obsahuje:

- súhrn: verziu aplikácie, operačný systém, nájdený J-Link Software a nRF Util, pripojené sondy J-Link, otvorené karty
  a výsledky kontrol `hwio doctor`;
- nedávne logy aplikácie (desktopovej aplikácie aj `hwio`);
- súbor nastavení;
- najnovšie správy o páde, ak nejaké sú.

Pred zapísaním sa vaše používateľské meno, domovský priečinok a názov počítača nahradia (`<user>`, `~`, `<host>`) a
hodnoty, ktoré vyzerajú ako heslá, tokeny alebo kľúče, sa nahradia `<redacted>`. Sériové čísla sond v nej zostanú. Logy
môžu obsahovať výstup vášho zariadenia, preto si správu pred odoslaním prezrite. Súbory konzoly a história príkazov
nie sú súčasťou správy.

Správa sa uloží vo vašom počítači (predvolene `hwio-diagnostics-YYYYMMDD-HHMMSS.zip` v priečinku Dokumenty, pri
`hwio diag report` v aktuálnom priečinku); nikdy sa neodosiela. K žiadosti o podporu ju priložte sami.

## Správy o páde

Ak aplikácia alebo `hwio` spadne, do priečinka `crashes` v údajoch aplikácie sa zapíše správa o páde (uchovávajú sa
najviac 5). Nič sa neodosiela. Pri ďalšom spustení aplikácia oznámi **HARDWARIO Monitor quit unexpectedly
last time.** a ponúkne **Create diagnostic report**, ktorá správu o páde zahŕňa. `HWIO_NO_CRASH_HANDLER=1` správy
o páde vypne.

## Čo je uložené kde

| Údaje | Windows | macOS | Linux |
|---|---|---|---|
| Nastavenia (`Monitor.ini`) | `%APPDATA%\HARDWARIO\Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` | `~/.config/HARDWARIO/Monitor.ini` |
| Údaje aplikácie | `%LOCALAPPDATA%\HARDWARIO\Monitor` | `~/Library/Application Support/HARDWARIO/Monitor` | `~/.local/share/HARDWARIO/Monitor` |

Priečinok údajov aplikácie obsahuje:

| Priečinok | Obsah | Limit |
|---|---|---|
| `console` | [súbory konzoly](console.md#súbory-konzoly) a história príkazov (`history-<device>-<target>.txt`) | 50 súborov a 200 MB; 1000 príkazov na históriu |
| `logs` | logy aplikácie, jeden súbor na každé spustenie | 10 súborov desktopovej aplikácie, 20 súborov `hwio`, 50 MiB pre každý druh |
| `firmware-cache` | stiahnutý firmvér z katalógu a LTE firmvér | 100 súborov, 500 MiB |
| `crashes` | správy o páde | 5 súborov |

Súbor nastavení, logy a súbory konzoly zdieľa desktopová aplikácia aj `hwio`. Súbory konzoly a históriu môže čítať
len váš používateľ (Linux, macOS). Príkazy obsahujúce tajné údaje sa do histórie neukladajú a v súboroch konzoly sa
zapisujú ako `<redacted>` ([Konzola](console.md#história-príkazov)).

Odinštalovanie HARDWARIO Monitor tieto priečinky nevymaže; ak chcete, vymažte ich sami. Ak chcete vymazať len históriu
príkazov: **Settings → Console → Clear history** alebo `hwio console history clear`.
