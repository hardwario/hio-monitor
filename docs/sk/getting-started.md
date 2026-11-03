# Začíname

[Dokumentácia](index.md) › Začíname

HARDWARIO Monitor zobrazuje konzolu a log zariadení HARDWARIO CHESTER a nahráva do nich firmvér, a to cez sondu
SEGGER J-Link. Bluetooth LE a MCP server pre AI asistentov sú dostupné v príkazovom riadku (`hwio`, pozrite si
[Bluetooth LE](bluetooth.md) a [MCP server](../en/mcp.md)).

Skôr než začnete, nainštalujte aplikáciu a SEGGER J-Link Software ([Inštalácia](install.md)).

## Zariadenie

CHESTER má dva MCU, ktoré sa nazývajú **ciele** (targets):

| Cieľ | MCU | Čo môžete robiť |
|---|---|---|
| `app` | aplikačný MCU (nRF52840) | konzola a log (RTT), nahratie firmvéru, vymazanie, reset, obnovenie |
| `lte` | LTE MCU (nRF9160 alebo nRF9151) | nahratie LTE firmvéru a firmvéru modemu, vymazanie, reset, obnovenie; nemá konzolu |

Pripojte J-Link k počítaču cez USB a k programovaciemu konektoru zariadenia CHESTER a CHESTER napájajte z batérií
alebo cez jeho napájací vstup USB-C.

## Pripojenie

1. Spustite **HARDWARIO Monitor**. Prvá karta, **New device** (nové zariadenie), zobrazuje **Choose a device to
   connect** (vyberte zariadenie na pripojenie) so sondami J-Link pripojenými k tomuto počítaču. Zoznam sa obnovuje
   každé dve sekundy.
2. Vyberte sondu. Sonda, ktorá je už otvorená v inej karte, má označenie **In use (tab N)**; jej výberom sa prepnete
   do tejto karty. Sonda, ktorú drží iný program, má označenie **In use by another program** a nedá sa vybrať.
3. V časti **Device** ponechajte **CHESTER** a zvoľte cieľ: **app** alebo **lte**.
4. Stlačte **Connect** (pripojiť).

Lišta zariadenia v hornej časti karty zobrazuje sondu, cieľ a stav: **Connecting…**, **Connected · 3.30 V** (s
napätím cieľa), **Reconnecting…** alebo **Disconnected — reason**. **Connect** a **Disconnect**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>) sú vpravo a <kbd>F4</kbd> pripojí znova.

Ak sa nenájde žiadna sonda, stránka zobrazí **No J-Link probe found** a informáciu, či sa našiel J-Link Software.
Skontrolujte kábel USB a v Linuxe pravidlo udev ([Inštalácia](install.md#prístup-k-j-linku-cez-usb)). `hwio doctor`
skontroluje všetko z toho.

### Možnosti pripojenia

**⋯** (**Advanced**, rozšírené) v lište zariadenia obsahuje možnosti tejto sondy. HARDWARIO Monitor si ich pamätá podľa
sériového čísla sondy. Uplatnia sa pri ďalšom pripojení.

| Možnosť | Predvolené | Význam |
|---|---|---|
| **Automatic speed** | zapnuté | Rýchlosť zvolí J-Link. Vypnuté: **J-Link speed (kHz)**, 5 až 50000 |
| **RTT latency (ms)** | 0 (hodnota zariadenia, pre CHESTER 50 ms) | Ako často sa konzola číta |
| **Auto-reconnect** | zapnuté | Po výpadku spojenia sa pripojí znova sám |
| **Reset on connect** | vypnuté | Pri pripájaní resetuje zariadenie |
| **Save console to log file** | zapnuté | Zapisuje [súbor konzoly](console.md#súbory-konzoly) |

Sondy bez vlastných možností používajú predvolené hodnoty v **Settings → J-Link**.

## Karty

Každé zariadenie je karta; otvorených môže byť až 8. <kbd>Ctrl</kbd>+<kbd>T</kbd> alebo **+** otvorí novú kartu,
<kbd>Ctrl</kbd>+<kbd>W</kbd> ju zatvorí (rovnako aj kliknutie stredným tlačidlom na kartu), <kbd>Ctrl</kbd>+<kbd>Tab</kbd>
a <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> prepínajú. Ak v karte prebieha operácia, pred zatvorením sa aplikácia
najprv spýta.

Lišta vľavo prepína stránky karty: **Console** (konzola, <kbd>Ctrl</kbd>+<kbd>1</kbd>) a **Flash** (nahrávanie
firmvéru, <kbd>Ctrl</kbd>+<kbd>2</kbd>). **Settings** (nastavenia, <kbd>Ctrl</kbd>+<kbd>,</kbd>) sa otvorí ako samostatná
karta.

Pri ďalšom spustení sa karty vrátia, odpojené (**Settings → General → Reopen tabs at start**).

Naraz beží len jedno okno aplikácie. Jej opätovné spustenie prenesie otvorené okno do popredia.

## Stránky

- [Konzola](console.md): shell a log zariadenia, vyhľadávanie, filter, história, dávkové súbory, súbory konzoly.
- [Nahrávanie firmvéru](flashing.md): aplikačný a LTE firmvér, vymazanie, obnovenie, reset.

## Nastavenia

| Sekcia | Čo obsahuje |
|---|---|
| **General** | **Language** (System, English, Čeština), **Theme** (System, Light, Dark), **Reopen tabs at start** |
| **Console** | veľkosť písma, časové značky, zalamovanie riadkov, návrhy príkazov, história príkazov, súbory konzoly a ich priečinok |
| **J-Link** | knižnica J-Link (**Automatic** alebo **Custom path**) a predvolené možnosti pripojenia |
| **nRF Util** | program nRF Util (**Automatic** alebo **Custom path**), potrebný len pre firmvér LTE modemu |
| **About** | verzia (s tlačidlom **Copy** pre podporu), [kontrola aktualizácií](privacy.md#kontrola-aktualizácií), príkaz `hwio` tejto inštalácie, licencie, odkazy |

Zmeny sa uplatnia okamžite.

## Pomocník

**Help** (pomocník) v dolnej časti lišty:

- **Keyboard shortcuts (F1)**: všetky klávesové skratky aplikácie (aj <kbd>Ctrl</kbd>+<kbd>/</kbd>).
- **Create diagnostic report…**: súbor zip pre podporu HARDWARIO ([Súkromie](privacy.md#diagnostická-správa)).
- **Documentation ↗**: stránka HARDWARIO Monitor na docs.hardwario.com.

Keď niečo zlyhá, aplikácia ukáže, čo sa stalo, pravdepodobnú príčinu (**Probably:**) a čo robiť (**What to do:**).
**Details** zobrazí kód chyby; [Riešenie problémov](troubleshooting.md) uvádza všetky kódy.

## Príkazový riadok

Všetko, čo aplikácia robí s J-Linkom, je dostupné aj ako príkaz:

```sh
hwio jlink list                       # the connected probes
hwio chester app console              # the console of the application MCU
hwio chester app flash firmware.hex
hwio doctor                           # check the installation
```

Pozrite si [Príkazový riadok](../en/cli.md).
