# Nahrávanie firmvéru

[Dokumentácia](index.md) › Nahrávanie firmvéru

Stránka **Flash** (nahrávanie firmvéru, <kbd>Ctrl</kbd>+<kbd>2</kbd>) zapisuje firmvér do zariadenia CHESTER cez J-Link
a vymazáva, obnovuje a resetuje jeho MCU. Postup má tri kroky a spúšťa ho tlačidlo **Flash** v dolnej časti
(<kbd>Ctrl</kbd>+<kbd>Enter</kbd>). Keď je tlačidlo vypnuté, text vedľa neho uvádza dôvod, napríklad **Choose a
firmware file or enter a catalog ID**.

Počas nahrávania je konzola karty pozastavená a potom sa sama obnoví.

## 1. Čo chcete nahrať?

| Voľba | MCU | Firmvér |
|---|---|---|
| **Application firmware** | aplikačný MCU | súbor `.hex` alebo ID firmvéru z katalógu |
| **LTE firmware** | LTE MCU a jeho modem | LTE firmvér HARDWARIO alebo váš vlastný `.hex` / `.zip` |

Karta zobrazuje firmvér, ktorý je teraz v zariadení, ak ho zariadenie hlási.

Na zariadení CHESTER-M má každý MCU vlastný programovací konektor. Ak cieľ karty nezodpovedá tomu, čo chcete nahrať,
stránka vás vyzve, aby ste najprv pripojili J-Link k druhému konektoru a prepli cieľ karty.

## 2. Firmvér

### Aplikačný firmvér

- Pustite súbor `.hex` kamkoľvek na stránku alebo stlačte **Browse…** (prehľadávať, <kbd>Ctrl</kbd>+<kbd>O</kbd>).
- Alebo zadajte **ID firmvéru z katalógu** (32 znakov, `0`–`9` a `a`–`f`) a stlačte **Download** (stiahnuť).
  Stránka zobrazí názov firmvéru, verziu a dátum nahratia (alebo **Unknown firmware — it can still be flashed.**).
  **Find firmware IDs ↗** otvorí zoznam katalógových aplikácií CHESTER na docs.hardwario.com.

Stiahnutý firmvér sa uchováva vo vyrovnávacej pamäti a znova sa nesťahuje. **Recent** (nedávne) uvádza posledných 10
súborov a ID.

### LTE firmvér

- **HARDWARIO LTE firmware 1.7.0 (recommended)**: LTE aplikácia a firmvér modemu, ktoré HARDWARIO otestovalo so
  zariadením CHESTER. Sťahuje sa z docs.hardwario.com a overuje sa jeho kontrolný súčet.
- **Use your own file…** (použiť vlastný súbor): aplikačný `.hex`, balík CHESTER LTE `.zip` (firmvér modemu a LTE
  aplikácia) alebo `.zip` s firmvérom modemu od spoločnosti Nordic. HARDWARIO Monitor sa pozerá na obsah súboru, nie
  na jeho názov, a odmietne zip, ktorý nie je ani jedným z nich.

Firmvér modemu potrebuje **nRF Util** ([Inštalácia](install.md#nordic-nrf-util-len-firmvér-lte-modemu)). Aktualizácia
modemu trvá 1–3 minúty a konzola je počas nej odpojená.

Ak zvolíte súbor druhého druhu, stránka ponúkne prepnutie (**This is LTE firmware — switch to LTE firmware?**).

## 3. Možnosti

- **Halt the CPU after flashing** (zastaviť CPU po nahratí): CPU nespustí nový firmvér, kým ho neresetujete alebo
  nenecháte bežať.
- **Allow writing UICR** (povoliť zápis UICR): zobrazí sa len vtedy, keď obraz obsahuje záznamy UICR; pozrite nižšie.

### UICR a blok informácií o produkte

UICR je malá oblasť flash pamäte s konfiguráciou zariadenia. Na zariadení CHESTER obsahuje aj **Product Information
Block** (PIB, blok informácií o produkte): sériové číslo, variant a revíziu hardvéru, claim token a BLE passkey.

- Nahrávanie firmvéru nikdy nezapisuje UICR, ak to nepovolíte (**Allow writing UICR** alebo `--allow-uicr`
  v príkazovom riadku), a nikdy nemení PIB.
- **Erase application** zachová UICR aj PIB.
- **Erase everything** a **Recover** vymažú aj PIB. HARDWARIO Monitor ho nedokáže zapísať späť, preto sa najprv
  spýtajú.

`hwio chester app pib read` zobrazí PIB.

## Priebeh a výsledok

Karta priebehu zobrazuje krok (**Connecting**, **Erasing**, **Programming**, **Verifying**, **Resetting** a pri modeme
**Updating the modem**), bajty a čas. **Cancel** (zrušiť) ho zastaví; zrušené alebo prerušené nahrávanie zanechá
zariadenie bez funkčného firmvéru, kým ho znova nenahráte.

Na konci stránka zobrazí napríklad **Firmware flashed and verified in 12 s.** s tlačidlami **Open console** a
**Flash again**.

## Údržba

Časť **Maintenance — application MCU** (alebo **LTE MCU**) v dolnej časti stránky je predvolene zbalená:

| Akcia | Funkcia | Pýta sa najprv |
|---|---|---|
| **Reset** | Resetuje MCU | nie |
| **Reset and halt** | Resetuje MCU a ponechá CPU zastavený | nie |
| **Erase application** | Vymaže firmvér; zachová UICR aj PIB (len aplikačný MCU) | nie |
| **Erase everything…** | Vymaže flash pamäť, UICR aj PIB | áno |
| **Recover…** | Pozrite nižšie | áno |

### Obnovenie

Keď je MCU chránený proti spätnému čítaniu (approtect), J-Link ho nemôže čítať ani zapisovať a nahrávanie zlyhá
s chybou [target.protected](troubleshooting.md#target.protected). **Recover…** (obnoviť) odstráni ochranu úplným
vymazaním MCU vrátane UICR a PIB. Na LTE MCU potom nahrá malý obraz, ktorý ponechá MCU dostupný pre ladenie. Po
obnovení firmvér znova nahrajte. Obnovenie sa nikdy nespustí samo.

## Príkazový riadok

```sh
hwio chester app flash firmware.hex
hwio chester app flash 0123456789abcdef0123456789abcdef   # a catalog ID
hwio chester app flash                                    # the build output in build/ or build/zephyr/
hwio chester lte flash                                    # the HARDWARIO LTE firmware
hwio chester lte flash mfw_nrf91x1_2.0.2.zip              # a modem firmware zip
hwio chester app erase --all
hwio chester app recover
```

Pozrite si [Príkazový riadok](../en/cli.md#flashing).
