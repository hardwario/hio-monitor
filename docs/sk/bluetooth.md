# Bluetooth LE

[Dokumentácia](index.md) › Bluetooth LE

CHESTER sprístupňuje svoj príkazový shell cez Bluetooth LE (Nordic UART Service). HARDWARIO Monitor sa k nemu dostane
z príkazového riadka pomocou `hwio ble`; nepotrebujete J-Link ani kábel. Desktopová aplikácia zatiaľ Bluetooth nemá.

Shell cez Bluetooth funguje ako konzola cez J-Link: odosielate príkazy a čítate ich odpovede a správy logu, ktoré
firmvér posiela, sa zobrazujú ako riadky logu.

## Vyhľadanie zariadenia

```sh
hwio ble scan
```

Vyhľadávanie trvá 5 sekúnd (`--timeout S`, 1 až 60) a uvedie zariadenia CHESTER v okolí: tie, ktorých vysielaný názov
obsahuje `CHESTER`. `--all` uvedie všetky zariadenia Bluetooth.

Každé zariadenie má **ID**:

- Linux a Windows: jeho adresa Bluetooth, napríklad `F1:22:33:44:55:66`.
- macOS: UUID, ktoré zariadeniu pridelí macOS. Nie je to adresa Bluetooth a platí len na danom Macu.

## Pripojenie

Zariadenie vyberte podľa jeho ID (`-a`) alebo podľa časti jeho názvu (`--name`, nerozlišuje veľké a malé písmená;
musí mu zodpovedať práve jedno zariadenie nájdené 5-sekundovým vyhľadávaním):

```sh
hwio ble console -a F1:22:33:44:55:66      # interactive console
hwio ble console --name "CHESTER Clime"
hwio ble command -a F1:22:33:44:55:66 "info show"
```

Pripojenie môže trvať až 30 sekúnd; neúspešné pokusy sa v tomto čase opakujú. `ble command` vypíše odpoveď a skončí
pri výzve shellu alebo po `-t` sekundách (predvolene 3).

V interaktívnej konzole <kbd>F4</kbd> pripojí znova a <kbd>Ctrl</kbd>+<kbd>C</kbd> na prázdnom riadku alebo
<kbd>Ctrl</kbd>+<kbd>D</kbd> ju ukončí. S `--auto-reconnect` sa konzola po výpadku spojenia znova pripojí, kým je
zariadenie v dosahu. Konzola zapisuje súbor konzoly a uchováva históriu príkazov, rovnako ako konzola cez J-Link
([Konzola](console.md#súbory-konzoly)).

K zariadeniu môže byť pripojený len jeden program. Ak je k nemu pripojený iný `hwio`, dostanete chybu
[ble.in_use_by_other_process](troubleshooting.md#ble.in_use_by_other_process).

## Párovanie

CHESTER chráni svoj Bluetooth shell prístupovým kľúčom (passkey), preto sa s ním počítač musí raz spárovať (bond).
Prístupový kľúč je 123456, ak nebol zmenený pri konfigurácii zariadenia. HARDWARIO Monitor prístupový kľúč nikdy
neukladá ani ho za vás nezadáva: pýta si ho operačný systém.

| Systém | Ako |
|---|---|
| Linux | Pri prvom pripojení alebo príkazom `hwio ble pair -a ID`. Na prístupový kľúč sa spýtajú nastavenia Bluetooth pracovného prostredia. |
| Windows | Pri prvom pripojení alebo príkazom `hwio ble pair -a ID`. Na prístupový kľúč sa spýta Windows. |
| macOS | Len pri prvom pripojení; na prístupový kľúč sa spýta macOS. `hwio ble pair` nie je v macOS dostupný. |

Keď zariadenie čaká na prístupový kľúč, pripojenie čaká až 60 sekúnd.

**Linux bez párovacieho agenta.** Niektoré pracovné prostredia (GNOME) sa pýtajú na prístupový kľúč len vtedy, keď sú
otvorené ich nastavenia Bluetooth. Ak sa nič nespýta, dostanete chybu
[ble.no_pairing_agent](troubleshooting.md#ble.no_pairing_agent). Spárujte zariadenie v systémových nastaveniach
Bluetooth alebo pomocou `bluetoothctl`:

```text
$ bluetoothctl
[bluetooth]# agent KeyboardOnly
[bluetooth]# default-agent
[bluetooth]# pair F1:22:33:44:55:66
```

Ak párovanie zlyhá po zmene prístupového kľúča v zariadení, odstráňte zariadenie v systémových nastaveniach Bluetooth
a spárujte ho znova ([ble.pairing_failed](troubleshooting.md#ble.pairing_failed)).

## Oprávnenia a adaptér

- **Linux:** Bluetooth používa BlueZ. Ak nebeží, spustite ho príkazom `sudo systemctl start bluetooth`.
- **macOS:** pri prvom vyhľadávaní sa macOS spýta na prístup k Bluetooth pre aplikáciu, z ktorej `hwio` spúšťate
  (zvyčajne Terminál). Ak ste ho odmietli, povoľte ho v **System Settings → Privacy & Security → Bluetooth**.
- **Všetky systémy:** zapnite adaptér Bluetooth. `hwio doctor` ukáže, či je pripravený.

## JSON

S `--json` vypíše `ble scan` zariadenia a `ble console` vypíše jeden riadok JSON na každý riadok konzoly, rovnako ako
konzola cez J-Link ([Príkazový riadok](../en/cli.md#json-output)).
