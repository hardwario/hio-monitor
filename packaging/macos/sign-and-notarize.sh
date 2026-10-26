#!/bin/bash
# Signs HARDWARIO Monitor.app inside-out, builds the DMG and (release only) notarizes and staples
# it (plan/features/0015 D1-D2, ADR-0120, ADR-0126).
#
#   packaging/macos/sign-and-notarize.sh sign   <app> [identity]   sign the bundle (default "-":
#                                                                   ad-hoc, PR CI) and verify it
#   packaging/macos/sign-and-notarize.sh verify <app>              codesign --verify --strict +
#                                                                   the entitlements of both binaries
#   packaging/macos/sign-and-notarize.sh dmg    <app> <out.dmg> [identity]
#                                                                   DMG (create-dmg, pinned) and its
#                                                                   signature (not for ad-hoc)
#   packaging/macos/sign-and-notarize.sh notarize <dmg>           notarytool submit --wait, stapler
#                                                                   staple, spctl (release job only)
#
# Signing order (inside-out): every Mach-O in Contents/Frameworks, Contents/PlugIns and
# Contents/Resources (Qt frameworks, plugins and QML plugins, re-signed with our identity), then
# Contents/MacOS/hio, then the bundle (whose main executable is hio-monitor). Executables get the
# hardened runtime and packaging/macos/entitlements.plist; everything gets --timestamp unless ad-hoc.
#
# Notarization uses an App Store Connect API key (ADR-0120): MACOS_NOTARY_KEY_P8 (the .p8 content),
# MACOS_NOTARY_KEY_ID, MACOS_NOTARY_ISSUER. The signing identity comes from a temporary keychain set
# up by the release workflow (MACOS_CERT_P12_BASE64, MACOS_CERT_PASSWORD).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
entitlements="$here/entitlements.plist"
command="${1:?usage: sign-and-notarize.sh sign|verify|dmg|notarize ...}"

timestamp_option() {
    if [ "$1" = "-" ]; then echo "--timestamp=none"; else echo "--timestamp"; fi
}

sign_bundle() {
    local app="$1" identity="${2:--}"
    local ts
    ts="$(timestamp_option "$identity")"
    # 1. Frameworks, plugins, QML plugins and any other Mach-O inside (deepest first).
    while IFS= read -r -d '' file; do
        if file -b "$file" | grep -q "Mach-O"; then
            codesign --force --sign "$identity" --options runtime "$ts" "$file"
        fi
    done < <(find "$app/Contents/Frameworks" "$app/Contents/PlugIns" "$app/Contents/Resources" \
                -type f -print0 2>/dev/null | sort -rz)
    while IFS= read -r -d '' framework; do
        codesign --force --sign "$identity" --options runtime "$ts" "$framework"
    done < <(find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0 2>/dev/null)
    # 2. The CLI (keeps its embedded __info_plist, ADR-0086), 3. the bundle.
    codesign --force --sign "$identity" --options runtime "$ts" \
        --entitlements "$entitlements" "$app/Contents/MacOS/hio"
    codesign --force --sign "$identity" --options runtime "$ts" \
        --entitlements "$entitlements" "$app"
}

verify_bundle() {
    local app="$1"
    codesign --verify --deep --strict --verbose=2 "$app"
    for binary in "$app" "$app/Contents/MacOS/hio"; do
        local ent
        ent="$(codesign -d --entitlements - --xml "$binary" 2>/dev/null || true)"
        if ! grep -q "com.apple.security.cs.disable-library-validation" <<<"$ent"; then
            echo "Missing disable-library-validation entitlement: $binary" >&2
            exit 1
        fi
        if grep -q "allow-jit\|allow-unsigned-executable-memory\|allow-dyld-environment-variables" <<<"$ent"; then
            echo "Unexpected entitlement in $binary (ADR-0126 allows only disable-library-validation)" >&2
            exit 1
        fi
        if ! codesign -d --verbose=2 "$binary" 2>&1 | grep -q "flags=.*runtime"; then
            echo "Hardened runtime not enabled: $binary" >&2
            exit 1
        fi
    done
    # The Bluetooth usage description must survive the re-signing (ADR-0086).
    otool -s __TEXT __info_plist "$app/Contents/MacOS/hio" >/dev/null
    plutil -extract NSBluetoothAlwaysUsageDescription raw "$app/Contents/Info.plist" >/dev/null
    echo "Signature and entitlements OK: $app"
}

make_dmg() {
    local app="$1" dmg="$2" identity="${3:--}"
    local staging
    staging="$(mktemp -d)"
    cp -R "$app" "$staging/"
    rm -f "$dmg"
    # create-dmg (pinned, installed by the workflow): window with the app, an Applications link and
    # the background. --skip-jenkins leaves out the Finder (AppleScript) layout step when no
    # Finder session is available (HIO_DMG_NO_FINDER=1).
    local finder=()
    if [ -n "${HIO_DMG_NO_FINDER:-}" ]; then finder=(--skip-jenkins); fi
    create-dmg \
        --volname "HARDWARIO Monitor" \
        --background "$here/dmg/background.png" \
        --window-pos 200 120 --window-size 660 400 --icon-size 128 \
        --icon "$(basename "$app")" 165 190 --hide-extension "$(basename "$app")" \
        --app-drop-link 495 190 \
        --no-internet-enable "${finder[@]}" \
        "$dmg" "$staging"
    rm -rf "$staging"
    if [ "$identity" != "-" ]; then
        codesign --force --sign "$identity" --timestamp "$dmg"
    fi
    ls -l "$dmg"
}

notarize_dmg() {
    local dmg="$1"
    local key
    key="$(mktemp -d)/AuthKey_${MACOS_NOTARY_KEY_ID:?}.p8"
    printf '%s' "${MACOS_NOTARY_KEY_P8:?}" > "$key"
    xcrun notarytool submit "$dmg" --key "$key" --key-id "$MACOS_NOTARY_KEY_ID" \
        --issuer "${MACOS_NOTARY_ISSUER:?}" --wait --timeout 60m | tee notarytool.log
    rm -f "$key"
    if ! grep -q "status: Accepted" notarytool.log; then
        echo "notarytool: not accepted - see notarytool.log (xcrun notarytool log <id>)" >&2
        exit 1
    fi
    xcrun stapler staple "$dmg"
    spctl -a -t open --context context:primary-signature -v "$dmg"
}

case "$command" in
    sign) sign_bundle "${2:?app}" "${3:--}"; verify_bundle "$2" ;;
    verify) verify_bundle "${2:?app}" ;;
    dmg) make_dmg "${2:?app}" "${3:?dmg}" "${4:--}" ;;
    notarize) notarize_dmg "${2:?dmg}" ;;
    *) echo "unknown command: $command" >&2; exit 2 ;;
esac
