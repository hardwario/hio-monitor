# Začínáme

[Dokumentace](index.md) › Začínáme

HARDWARIO Monitor zobrazuje konzoli a log zařízení HARDWARIO CHESTER a flashuje jejich firmware přes sondu SEGGER
J-Link. Bluetooth LE a server MCP pro asistenty AI jsou k dispozici v příkazové řádce (`hwio`, viz
[Bluetooth LE](bluetooth.md) a [Server MCP](../en/mcp.md)).

Než začnete, nainstalujte aplikaci a SEGGER J-Link Software ([Instalace](install.md)).

## Zařízení

CHESTER má dva MCU, kterým se říká **cíle**:

| Cíl | MCU | Co s ním můžete dělat |
|---|---|---|
| `app` | aplikační MCU (nRF52840) | konzole a log (RTT), flashování, mazání, reset, obnovení (recover) |
| `lte` | LTE MCU (nRF9160 nebo nRF9151) | flashování LTE firmwaru a firmwaru modemu, mazání, reset, obnovení (recover); nemá konzoli |

Připojte J-Link k počítači přes USB a k programovacímu konektoru zařízení CHESTER a napájejte CHESTER z baterií nebo
přes jeho napájecí vstup USB-C.

## Připojení

1. Spusťte **HARDWARIO Monitor**. První karta, **Nové zařízení**, zobrazí **Vyberte zařízení k připojení** se
   sondami J-Link připojenými k tomuto počítači. Seznam se obnovuje každé dvě sekundy.
2. Vyberte sondu. U sondy, která je už otevřená v jiné kartě, je **Používá se (karta N)**; jejím výběrem se na tu
   kartu přepnete. U sondy, kterou drží jiný program, je **Používá jiný program** a nelze ji vybrat.
3. V části **Zařízení** ponechte **CHESTER** a zvolte cíl: **app** nebo **lte**.
4. Stiskněte **Připojit**.

Lišta zařízení v horní části karty ukazuje sondu, cíl a stav: **Připojování…**, **Připojeno · 3.30 V** (s napětím
cílového zařízení), **Opětovné připojování…** nebo **Odpojeno — důvod**. Tlačítka **Připojit** a **Odpojit**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>) jsou vpravo a klávesou <kbd>F4</kbd> se připojíte znovu.

Pokud se nenajde žádná sonda, stránka ukáže **Nenalezen žádný programátor J-Link** a informaci, zda byl nalezen
J-Link Software. Zkontrolujte kabel USB a v Linuxu pravidlo udev ([Instalace](install.md#přístup-k-j-linku-přes-usb)).
`hwio doctor` zkontroluje všechno.

### Možnosti připojení

**⋯** (**Pokročilé**) v liště zařízení obsahuje možnosti této sondy. HARDWARIO Monitor si je pamatuje podle sériového
čísla sondy. Projeví se při dalším připojení.

| Možnost | Výchozí | Význam |
|---|---|---|
| **Automatická rychlost** | zapnuto | Rychlost zvolí J-Link. Vypnuto: **Rychlost J-Link (kHz)**, 5 až 50000 |
| **Latence RTT (ms)** | 0 (hodnota zařízení, pro CHESTER 50 ms) | Jak často se čte konzole |
| **Automaticky znovu připojit** | zapnuto | Po přerušení spojení se znovu připojí sám |
| **Reset při připojení** | vypnuto | Při připojení resetovat zařízení |
| **Ukládat konzoli do souboru** | zapnuto | Zapisovat [soubor konzole](console.md#soubory-konzole) |

Sondy bez vlastních možností používají výchozí hodnoty z **Nastavení → J-Link**.

## Karty

Každé zařízení je jedna karta; otevřených jich může být až 8. <kbd>Ctrl</kbd>+<kbd>T</kbd> nebo **+** otevře novou
kartu, <kbd>Ctrl</kbd>+<kbd>W</kbd> ji zavře (stejně jako kliknutí prostředním tlačítkem na kartu),
<kbd>Ctrl</kbd>+<kbd>Tab</kbd> a <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> mezi kartami přepínají. Pokud v kartě
probíhá operace, zeptá se aplikace před zavřením.

Lišta vlevo přepíná stránky karty: **Konzole** (<kbd>Ctrl</kbd>+<kbd>1</kbd>) a **Nahrát FW**
(<kbd>Ctrl</kbd>+<kbd>2</kbd>). **Nastavení** (<kbd>Ctrl</kbd>+<kbd>,</kbd>) se otevře ve vlastní kartě.

Při dalším spuštění se karty obnoví, odpojené (**Nastavení → Obecné → Při spuštění obnovit karty**).

Současně běží jen jedno okno aplikace. Když ji spustíte znovu, přenese se otevřené okno do popředí.

## Stránky

- [Konzole](console.md): shell a log zařízení, hledání, filtr, historie, dávkové soubory, soubory konzole.
- [Flashování](flashing.md): aplikační a LTE firmware, mazání, obnovení (recover), reset.

## Nastavení

| Oddíl | Co obsahuje |
|---|---|
| **Obecné** | **Jazyk** (Podle systému, English, Čeština), **Vzhled** (Podle systému, Světlý, Tmavý), **Při spuštění obnovit karty** |
| **Konzole** | velikost písma, časové značky, zalamování řádků, návrhy příkazů, historie příkazů, soubory konzole a jejich složka |
| **J-Link** | knihovna J-Link (**Automaticky** nebo **Vlastní cesta**) a výchozí možnosti připojení |
| **nRF Util** | program nRF Util (**Automaticky** nebo **Vlastní cesta**), potřebný jen pro firmware modemu LTE |
| **O aplikaci** | verze (s tlačítkem **Kopírovat** pro podporu), [kontrola aktualizací](privacy.md#kontrola-aktualizací), příkaz `hwio` této instalace, licence, odkazy |

Změny se projeví okamžitě.

## Nápověda

**Nápověda** ve spodní části lišty:

- **Klávesové zkratky (F1)**: všechny klávesové zkratky aplikace (také <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Vytvořit diagnostickou zprávu…**: soubor zip pro podporu HARDWARIO ([Soukromí](privacy.md#diagnostická-zpráva)).
- **Dokumentace ↗**: stránka HARDWARIO Monitor na docs.hardwario.com.

Když něco selže, aplikace ukáže, co se stalo, pravděpodobnou příčinu (**Pravděpodobně:**) a co dělat
(**Co dělat:**). **Podrobnosti** ukážou kód chyby; [Řešení problémů](troubleshooting.md) uvádí všechny kódy.

## Příkazová řádka

Vše, co aplikace dělá s J-Linkem, je k dispozici také jako příkaz:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

Viz [Příkazová řádka](../en/cli.md).
