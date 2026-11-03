# Konzola

[Dokumentácia](index.md) › Konzola

Stránka **Console** (konzola, <kbd>Ctrl</kbd>+<kbd>1</kbd>) zobrazuje, čo aplikačný MCU zariadenia CHESTER posiela cez
J-Link (SEGGER RTT), v dvoch paneloch:

- **Shell**: príkazový shell zariadenia. Príkazy píšete pod ním a ich odpovede sa zobrazujú tu. Odoslané príkazy sú
  zobrazené ako `> command`.
- **Log**: správy logu zariadenia so stĺpcom úrovne: **D** ladenie (debug), **I** informácia, **W** varovanie,
  **E** chyba a **H** pre riadky hexdump.

LTE MCU nemá konzolu; pri cieli **lte** stránka zobrazí **This target has no console**.

Riadky medzi riadkami zo zariadenia označujú, čo sa stalo so spojením: **— connected —**, **— disconnected —**,
**— paused: … —** počas nahrávania firmvéru, **— resumed —**.

## Rozloženie

Veľkosť panelov zmeníte potiahnutím úchytu medzi nimi (alebo naň presuňte fokus a použite klávesy so šípkami). Pri
šírke okna pod 1000 pixelov sú panely pod sebou; **More** (viac) v paneli nástrojov obsahuje **Stacked layout** a
**Swap panes**, ako aj **Show timestamps** a **Wrap lines**. <kbd>Ctrl</kbd>+<kbd>=</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd>
a <kbd>Ctrl</kbd>+<kbd>0</kbd> menia veľkosť písma.

Zobrazenie sleduje nové riadky, ako prichádzajú. Ak sa posuniete nahor, zostane tam, kde ste; tlačidlo
**▼ N new lines** vás vráti späť, rovnako ako <kbd>End</kbd> alebo **Follow**. **Pause** (<kbd>Ctrl</kbd>+<kbd>P</kbd>)
zmrazí zobrazenie, zatiaľ čo riadky ďalej prichádzajú; **Resume** ich zobrazí. Každý panel uchováva posledných
100 000 riadkov.

## Panel nástrojov

| Tlačidlo | Funkcia |
|---|---|
| **Reset** | Resetuje zariadenie, alebo pri **Reset and halt** ho resetuje a ponechá CPU zastavený |
| **Reconnect** | Znova sa pripojí ku konzole (<kbd>F4</kbd>) |
| **Batch…** | Spustí [dávkový súbor](#dávkové-súbory) (<kbd>Ctrl</kbd>+<kbd>B</kbd>); ⓘ vedľa neho otvorí dokumentáciu |
| **Clear** | Vymaže **Shell**, **Log** alebo **Both** (oba; <kbd>Ctrl</kbd>+<kbd>K</kbd> vymaže panel s fokusom) |
| **Save view…** | Uloží panel s fokusom do textového súboru (<kbd>Ctrl</kbd>+<kbd>S</kbd>) |
| **Follow**, **Pause** | Pozrite vyššie |
| **More** | **Open log file**, **Open log folder** ([súbory konzoly](#súbory-konzoly)) a možnosti rozloženia |

## Odosielanie príkazov

Kliknite na vstupné pole pod shellom (alebo stlačte <kbd>Ctrl</kbd>+<kbd>E</kbd>), napíšte príkaz a stlačte
<kbd>Enter</kbd>. Keď je vstupné pole vypnuté, jeho zástupný text uvádza dôvod (**Not connected**, **Connecting…**
alebo prebiehajúca operácia, napríklad nahrávanie firmvéru).

- <kbd>↑</kbd> a <kbd>↓</kbd> prechádzajú históriou príkazov.
- <kbd>Ctrl</kbd>+<kbd>R</kbd> vyhľadáva v histórii: napíšte časť príkazu, <kbd>Enter</kbd> ho vloží do vstupného
  poľa.
- Počas písania zoznam navrhuje vlastné príkazy zariadenia (s ich textom pomocníka) a zodpovedajúce položky histórie.
  <kbd>↑</kbd>/<kbd>↓</kbd> vyberajú, <kbd>Tab</kbd> alebo <kbd>→</kbd> potvrdí, <kbd>Esc</kbd> zoznam zatvorí.
- Sivý text za kurzorom dopĺňa riadok; <kbd>→</kbd> alebo <kbd>End</kbd> ho potvrdí.
- <kbd>Tab</kbd> doplní príkaz. Nikdy neopustí vstupné pole; na presun ďalej použite <kbd>Shift</kbd>+<kbd>Tab</kbd>.

Príkazy zariadenia sa načítajú príkazom `help`, keď sa konzola pripojí. Vypnete to v **Settings → Console → Read
the command list from the device on connect (sends “help”)** a návrhy pomocou **Suggest commands while typing**.

### História príkazov

História uchováva posledných 1000 príkazov pre každý typ zariadenia a cieľ (napríklad CHESTER app). Zdieľajú ju všetky
karty aj príkazový riadok (`hwio chester app console`). Príkazy obsahujúce tajné údaje (napríklad
`lte config password`, `lrw config appkey` alebo `ble config passkey`) sa nikdy neukladajú, a rovnako ani príkaz, ktorý
začnete medzerou. **Settings → Console** vypne ukladanie (**Remember commands**) a vymaže históriu (**Clear history**).

## Vyhľadávanie a filter

Lišta pod logom má dva režimy:

- **Search** (hľadať, <kbd>Ctrl</kbd>+<kbd>F</kbd>): zvýrazní zhody a zobrazí **3 / 17**. <kbd>Enter</kbd> alebo
  <kbd>F3</kbd> prejde na ďalšiu zhodu, <kbd>Shift</kbd>+<kbd>Enter</kbd> alebo <kbd>Shift</kbd>+<kbd>F3</kbd> na
  predchádzajúcu; v logu fungujú aj <kbd>N</kbd> a <kbd>Shift</kbd>+<kbd>N</kbd>.
- **Filter** (<kbd>Ctrl</kbd>+<kbd>L</kbd>): zobrazí len zodpovedajúce riadky (**Showing 40 of 2 000 lines**).

**Aa** rozlišuje veľké a malé písmená, **.\*** považuje text za regulárny výraz. Tlačidlá úrovní **D**, **I**, **W**,
**E** a **H** zobrazujú alebo skrývajú úrovne; riadky hexdump (**H**) sú skryté, kým ich nezapnete. <kbd>Esc</kbd>
zatvorí vyhľadávanie, druhé <kbd>Esc</kbd> zruší filter.

Shell má vlastné vyhľadávanie: stlačte <kbd>Ctrl</kbd>+<kbd>F</kbd>, keď má shell fokus.

## Výber a kopírovanie

Kliknutím na riadok ho vyberiete, <kbd>Shift</kbd>+kliknutím alebo ťahaním vyberiete viac, <kbd>Ctrl</kbd>+<kbd>A</kbd>
vyberie všetko. <kbd>Ctrl</kbd>+<kbd>C</kbd> alebo ponuka po kliknutí pravým tlačidlom skopíruje riadky
("Copied to clipboard."); naraz sa skopíruje najviac 100 000 riadkov.

## Uloženie zobrazenia

**Save view…** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) uloží panel s fokusom tak, ako je zobrazený, s časovými značkami, ak sú
zapnuté. Názov je predvyplnený, napríklad `chester-app-clime-v3.2.0-20261103-101500-log.txt`, a priečinok je ten,
ktorý ste použili naposledy (pri prvom použití Dokumenty).

## Dávkové súbory

Dávkový súbor je textový súbor v UTF-8 s jedným príkazom shellu na riadok. **Batch…** (<kbd>Ctrl</kbd>+<kbd>B</kbd>)
odošle riadky jeden po druhom, jeden každých 200 ms. Prázdne riadky a riadky začínajúce znakom `#` sa preskočia.

```text
# what the device is and how it is configured
info show
app config show
```

Celý súbor sa skontroluje pred odoslaním prvého riadka: najviac 10 000 príkazov, 1 MiB na súbor a 4096 bajtov na
riadok. Počas behu panel nástrojov zobrazuje **Batch 3 / 4** s tlačidlom na zastavenie. Dávka sa zastaví, keď sa
zariadenie odpojí alebo sa spustí iná operácia (napríklad nahrávanie firmvéru).

## Súbory konzoly

Ak to nevypnete, každé pripojenie sa zapisuje do súboru konzoly: shell, log, odoslané príkazy a udalosti spojenia,
s časovými značkami. Súbory sú v priečinku **Log folder**, ktorý je uvedený v **Settings → Console**:

| Systém | Priečinok |
|---|---|
| Windows | `%LOCALAPPDATA%\HARDWARIO\Monitor\console` |
| macOS | `~/Library/Application Support/HARDWARIO/Monitor/console` |
| Linux | `~/.local/share/HARDWARIO/Monitor/console` |

Súbor je pomenovaný podľa zariadenia, firmvéru, času (UTC) a sondy, napríklad
`chester-app-clime-v3.2.0-20261103-101500-<serial>.log`. Súbor väčší ako 10 MiB pokračuje v `….part2.log`. Priečinok
uchováva 50 najnovších súborov, spolu najviac 200 MB. Tajné údaje sa zapisujú ako `<redacted>` (rovnaké príkazy ako
v histórii a claim token a BLE passkey z výstupu `info show`). V Linuxe a macOS môže súbory čítať len váš používateľ.

Súbory konzoly vypnete v **Settings → Console → Save console to log files** alebo pre jednotlivú sondu v **⋯**
(**Advanced**).

## Opätovné pripojenie

Keď spojenie vypadne (zariadenie bolo resetované, stratilo napájanie alebo bol vytiahnutý kábel), konzola sa sama
pripojí znova a lišta zariadenia zobrazí **Reconnecting…** s dôvodom. Vypnete to pomocou **Auto-reconnect** v **⋯**
(**Advanced**) alebo v **Settings → J-Link**; potom sa znova pripojíte pomocou <kbd>F4</kbd> alebo **Reconnect**.

Ak zariadenie posiela rýchlejšie, ako ich aplikácia stihne zobraziť, pruh s upozornením uvedie, koľko riadkov bolo
zahodených.

## Príkazový riadok

Tá istá konzola v termináli:

```sh
hwio chester app console                   # interactive: F4 reconnects, Ctrl+C on an empty line or Ctrl+D quits
hwio chester app command "info show"       # one command, print the answer
echo "app config show" | hwio chester app console
```

Pozrite si [Príkazový riadok](../en/cli.md#console).
