# Bluetooth LE

[Dokumentace](index.md) › Bluetooth LE

CHESTER nabízí svůj příkazový shell přes Bluetooth LE (Nordic UART Service). HARDWARIO Monitor se k němu připojuje
z příkazové řádky příkazem `hwio ble`; J-Link ani kabel nejsou potřeba. Desktopová aplikace Bluetooth zatím nemá.

Shell přes Bluetooth funguje stejně jako konzole přes J-Link: odesíláte příkazy a čtete jejich odpovědi a zprávy
logu, které firmware posílá, se zobrazují jako řádky logu.

## Vyhledání zařízení

```sh
hwio ble scan
```

Vyhledávání trvá 5 sekund (`--timeout S`, 1 až 60) a vypíše zařízení CHESTER v okolí: ta, jejichž inzerovaný název
obsahuje `CHESTER`. `--all` vypíše všechna zařízení Bluetooth.

Každé zařízení má **ID**:

- Linux a Windows: jeho adresa Bluetooth, například `F1:22:33:44:55:66`.
- macOS: UUID, které zařízení přidělí macOS. Není to adresa Bluetooth a platí jen na daném Macu.

## Připojení

Zařízení zvolte podle jeho ID (`-a`) nebo podle části názvu (`--name`, nerozlišuje velikost písmen; musí odpovídat
právě jedno zařízení nalezené pětisekundovým vyhledáváním):

```sh
hwio ble console -a F1:22:33:44:55:66      # interactive console
hwio ble console --name "CHESTER Clime"
hwio ble command -a F1:22:33:44:55:66 "info show"
```

Připojení může trvat až 30 sekund; neúspěšné pokusy se v této době opakují. `ble command` vypíše odpověď a skončí
u výzvy shellu, nebo po `-t` sekundách (výchozí 3).

V interaktivní konzoli se klávesou <kbd>F4</kbd> připojíte znovu a <kbd>Ctrl</kbd>+<kbd>C</kbd> na prázdném řádku
nebo <kbd>Ctrl</kbd>+<kbd>D</kbd> konzoli ukončí. S `--auto-reconnect` se konzole po přerušení spojení znovu připojí,
dokud je zařízení v dosahu. Konzole zapisuje soubor konzole a uchovává historii příkazů stejně jako konzole přes
J-Link ([Konzole](console.md#soubory-konzole)).

K zařízení může být připojený jen jeden program. Pokud je k němu připojený jiný `hwio`, dostanete chybu
[ble.in_use_by_other_process](troubleshooting.md#ble.in_use_by_other_process).

## Párování

CHESTER chrání svůj shell přes Bluetooth přístupovým klíčem, proto se s ním počítač musí jednou spárovat (bond).
Přístupový klíč je 123456, pokud nebyl změněn při konfiguraci zařízení. HARDWARIO Monitor přístupový klíč nikdy
neukládá a nikdy ho za vás nezadává: zeptá se na něj operační systém.

| Systém | Jak |
|---|---|
| Linux | Při prvním připojení, nebo příkazem `hwio ble pair -a ID`. Na přístupový klíč se zeptá nastavení Bluetooth v desktopovém prostředí. |
| Windows | Při prvním připojení, nebo příkazem `hwio ble pair -a ID`. Na přístupový klíč se zeptá Windows. |
| macOS | Jen při prvním připojení; na přístupový klíč se zeptá macOS. `hwio ble pair` v macOS není k dispozici. |

Když zařízení čeká na přístupový klíč, čeká připojení až 60 sekund.

**Linux bez párovacího agenta.** Některá desktopová prostředí (GNOME) se na přístupový klíč ptají, jen když je otevřené
jejich nastavení Bluetooth. Pokud se nic nezeptá, dostanete chybu
[ble.no_pairing_agent](troubleshooting.md#ble.no_pairing_agent). Spárujte zařízení v systémovém nastavení Bluetooth,
nebo pomocí `bluetoothctl`:

```text
$ bluetoothctl
[bluetooth]# agent KeyboardOnly
[bluetooth]# default-agent
[bluetooth]# pair F1:22:33:44:55:66
```

Pokud párování selže poté, co byl přístupový klíč v zařízení změněn, odeberte zařízení v systémovém nastavení
Bluetooth a spárujte ho znovu ([ble.pairing_failed](troubleshooting.md#ble.pairing_failed)).

## Oprávnění a adaptér

- **Linux:** Bluetooth používá BlueZ. Pokud neběží, spusťte ho příkazem `sudo systemctl start bluetooth`.
- **macOS:** při prvním vyhledávání se macOS zeptá na přístup k Bluetooth pro aplikaci, ze které `hwio` spouštíte
  (obvykle Terminál). Pokud jste přístup odmítli, povolte ho v **Nastavení systému → Soukromí a zabezpečení →
  Bluetooth**.
- **Všechny systémy:** zapněte adaptér Bluetooth. `hwio doctor` ukáže, zda je připravený.

## JSON

S `--json` vypíše `ble scan` zařízení a `ble console` vypíše jeden řádek JSON na každý řádek konzole, stejně jako
konzole přes J-Link ([Příkazová řádka](../en/cli.md#json-output)).
