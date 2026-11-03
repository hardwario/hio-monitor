# Konzole

[Dokumentace](index.md) › Konzole

Stránka **Konzole** (<kbd>Ctrl</kbd>+<kbd>1</kbd>) ukazuje, co aplikační MCU zařízení CHESTER posílá přes J-Link
(SEGGER RTT), ve dvou panelech:

- **Shell**: příkazový shell zařízení. Příkazy píšete pod ním a jejich odpovědi se objevují zde. Odeslané příkazy
  se zobrazují jako `> command`.
- **Log**: zprávy logu zařízení se sloupcem úrovně: **D** ladění (debug), **I** informace, **W** varování, **E** chyba
  a **H** pro řádky hexdumpu.

LTE MCU nemá konzoli; s cílem **lte** stránka ukáže **Tento cíl nemá konzoli**.

Řádky mezi řádky ze zařízení označují, co se stalo se spojením: **— připojeno —**, **— odpojeno —**,
**— pozastaveno: … —** během flashování, **— obnoveno —**.

## Rozložení

Velikost panelů změníte přetažením úchytu mezi nimi (nebo na něj přejděte a použijte šipky). Při šířce okna pod 1000
pixelů jsou panely pod sebou; **Další** v panelu nástrojů obsahuje **Panely pod sebou** a **Prohodit panely** a také
**Zobrazit časové značky** a **Zalamovat řádky**. <kbd>Ctrl</kbd>+<kbd>=</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd> a
<kbd>Ctrl</kbd>+<kbd>0</kbd> mění velikost písma.

Výpis sleduje nové řádky, jak přicházejí. Když se posunete nahoru, zůstane tam, kde jste; zpět vás vrátí tlačítko
**▼ N nových řádků**, klávesa <kbd>End</kbd> nebo **Sledovat**. **Pozastavit** (<kbd>Ctrl</kbd>+<kbd>P</kbd>)
zmrazí výpis, zatímco řádky dál přicházejí; **Pokračovat** je zobrazí. Každý panel uchovává posledních 100 000 řádků.

## Panel nástrojů

| Tlačítko | Funkce |
|---|---|
| **Reset** | Resetuje zařízení, nebo s volbou **Reset a zastavit** ho resetuje a ponechá procesor zastavený |
| **Znovu připojit** | Znovu se připojí ke konzoli (<kbd>F4</kbd>) |
| **Dávka…** | Spustí [dávkový soubor](#dávkové-soubory) (<kbd>Ctrl</kbd>+<kbd>B</kbd>); ⓘ vedle něj otevře dokumentaci |
| **Vymazat** | Vymaže **Shell**, **Log** nebo **Obojí** (<kbd>Ctrl</kbd>+<kbd>K</kbd> vymaže panel, který má fokus) |
| **Uložit výpis…** | Uloží panel, který má fokus, do textového souboru (<kbd>Ctrl</kbd>+<kbd>S</kbd>) |
| **Sledovat**, **Pozastavit** | Viz výše |
| **Další** | **Otevřít soubor s logem**, **Otevřít složku s logy** ([soubory konzole](#soubory-konzole)) a možnosti rozložení |

## Odesílání příkazů

Klikněte do vstupního pole pod shellem (nebo stiskněte <kbd>Ctrl</kbd>+<kbd>E</kbd>), napište příkaz a stiskněte
<kbd>Enter</kbd>. Když je vstupní pole zakázané, jeho zástupný text uvádí proč (**Nepřipojeno**, **Připojování…** nebo
probíhající operace, například flashování).

- <kbd>↑</kbd> a <kbd>↓</kbd> procházejí historii příkazů.
- <kbd>Ctrl</kbd>+<kbd>R</kbd> prohledává historii: napište část příkazu, <kbd>Enter</kbd> ho vloží do vstupního
  pole.
- Během psaní nabízí seznam vlastní příkazy zařízení (s jejich nápovědou) a odpovídající položky historie.
  <kbd>↑</kbd>/<kbd>↓</kbd> vybírají, <kbd>Tab</kbd> nebo <kbd>→</kbd> přijme, <kbd>Esc</kbd> seznam zavře.
- Šedý text za kurzorem doplňuje řádek; <kbd>→</kbd> nebo <kbd>End</kbd> ho přijme.
- <kbd>Tab</kbd> doplní příkaz. Vstupní pole nikdy neopustí; pro přechod dál použijte <kbd>Shift</kbd>+<kbd>Tab</kbd>.

Příkazy zařízení se při připojení konzole načtou příkazem `help`. Vypnete to v **Nastavení → Konzole → Po připojení
načíst seznam příkazů ze zařízení (odešle „help“)** a návrhy v **Navrhovat příkazy při psaní**.

### Historie příkazů

Historie uchovává posledních 1000 příkazů pro každý typ zařízení a cíl (například CHESTER app). Sdílejí ji všechny
karty i příkazová řádka (`hwio chester app console`). Příkazy, které obsahují tajné údaje (například
`lte config password`, `lrw config appkey` nebo `ble config passkey`), se nikdy neukládají, stejně jako příkaz, který
začnete mezerou. V **Nastavení → Konzole** ukládání vypnete (**Pamatovat si příkazy**) a historii smažete
(**Vymazat historii**).

## Hledání a filtr

Lišta pod logem má dva režimy:

- **Hledat** (<kbd>Ctrl</kbd>+<kbd>F</kbd>): zvýrazní shody a ukáže **3 / 17**. <kbd>Enter</kbd> nebo
  <kbd>F3</kbd> přejde na další shodu, <kbd>Shift</kbd>+<kbd>Enter</kbd> nebo <kbd>Shift</kbd>+<kbd>F3</kbd> na
  předchozí; v logu fungují také <kbd>N</kbd> a <kbd>Shift</kbd>+<kbd>N</kbd>.
- **Filtr** (<kbd>Ctrl</kbd>+<kbd>L</kbd>): ukáže jen odpovídající řádky (**Zobrazeno 40 z 2 000 řádků**).

**Aa** rozlišuje velikost písmen, **.\*** chápe text jako regulární výraz. Tlačítka úrovní **D**, **I**, **W**, **E**
a **H** úrovně zobrazují nebo skrývají; řádky hexdumpu (**H**) jsou skryté, dokud je nezapnete. <kbd>Esc</kbd>
zavře hledání, druhé <kbd>Esc</kbd> zruší filtr.

Shell má vlastní hledání: stiskněte <kbd>Ctrl</kbd>+<kbd>F</kbd>, když má fokus shell.

## Výběr a kopírování

Kliknutím na řádek ho vyberete, <kbd>Shift</kbd>+kliknutím nebo tažením vyberete více řádků, <kbd>Ctrl</kbd>+<kbd>A</kbd>
vybere vše. <kbd>Ctrl</kbd>+<kbd>C</kbd> nebo místní nabídka (pravé tlačítko) řádky zkopíruje („Zkopírováno do
schránky.“); najednou se zkopíruje nejvýše 100 000 řádků.

## Uložení výpisu

**Uložit výpis…** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) uloží panel, který má fokus, tak, jak je zobrazený, včetně časových
značek, pokud jsou zapnuté. Název je předvyplněný, například `chester-app-clime-v3.2.0-20261103-101500-log.txt`, a
složka je ta, kterou jste použili naposledy (poprvé Dokumenty).

## Dávkové soubory

Dávkový soubor je textový soubor v UTF-8 s jedním příkazem shellu na řádek. **Dávka…** (<kbd>Ctrl</kbd>+<kbd>B</kbd>)
odesílá řádky jeden po druhém, jeden každých 200 ms. Prázdné řádky a řádky začínající `#` se přeskakují.

```text
# what the device is and how it is configured
info show
app config show
```

Celý soubor se zkontroluje před odesláním prvního řádku: nejvýše 10 000 příkazů, 1 MiB na soubor a 4096 bajtů na
řádek. Během běhu ukazuje panel nástrojů **Dávka 3 / 4** s tlačítkem pro zastavení. Dávka se zastaví, když se zařízení
odpojí nebo začne jiná operace (například flashování).

## Soubory konzole

Pokud to nevypnete, zapisuje se každé připojení do souboru konzole: shell, log, odeslané příkazy a události spojení,
s časovými značkami. Soubory jsou ve složce, kterou ukazuje **Složka s logy** v **Nastavení → Konzole**:

| Systém | Složka |
|---|---|
| Windows | `%LOCALAPPDATA%\HARDWARIO\Monitor\console` |
| macOS | `~/Library/Application Support/HARDWARIO/Monitor/console` |
| Linux | `~/.local/share/HARDWARIO/Monitor/console` |

Soubor je pojmenovaný podle zařízení, firmwaru, času (UTC) a sondy, například
`chester-app-clime-v3.2.0-20261103-101500-<serial>.log`. Soubor větší než 10 MiB pokračuje v `….part2.log`. Složka
uchovává 50 nejnovějších souborů, dohromady nejvýše 200 MB. Tajné údaje se zapisují jako `<redacted>` (tytéž příkazy
jako v historii a navíc claim token a přístupový klíč BLE z `info show`). V Linuxu a macOS může soubory číst jen váš
uživatel.

Soubory konzole vypnete v **Nastavení → Konzole → Ukládat konzoli do souborů** nebo pro jednotlivou sondu v **⋯**
(**Pokročilé**).

## Opětovné připojení

Když se spojení přeruší (zařízení bylo resetováno, ztratilo napájení nebo byl vytažen kabel), konzole se znovu připojí
sama a lišta zařízení ukáže **Opětovné připojování…** s důvodem. Vypnete to volbou **Automaticky znovu připojit** v
**⋯** (**Pokročilé**) nebo v **Nastavení → J-Link**; potom se znovu připojíte klávesou <kbd>F4</kbd> nebo tlačítkem
**Znovu připojit**.

Pokud zařízení posílá data rychleji, než je aplikace stíhá zobrazovat, banner ukáže, kolik řádků bylo zahozeno.

## Příkazová řádka

Stejná konzole v terminálu:

```sh
hwio chester app console                   # interactive: F4 reconnects, Ctrl+C on an empty line or Ctrl+D quits
hwio chester app command "info show"       # one command, print the answer
echo "app config show" | hwio chester app console
```

Viz [Příkazová řádka](../en/cli.md#console).
