# Signs Windows files with Azure Trusted Signing (plan/features/0015 E2, ADR-0120).
#
#   packaging/windows/sign.ps1 -Files <file>... [-DryRun]
#
# The release workflow signs with the Trusted Signing GitHub action (pinned by SHA, OIDC login
# through azure/login); this script is the same step for local dry runs and manual signing. It uses
# the TrustedSigning PowerShell module (Invoke-TrustedSigning) with the Azure login of the session
# (az login / azure/login), SHA-256 file digest and an RFC 3161 timestamp, then checks the result
# with `signtool verify /pa`. Configuration (environment, see packaging/README.md):
#   TRUSTED_SIGNING_ENDPOINT   e.g. https://weu.codesigning.azure.net/
#   TRUSTED_SIGNING_ACCOUNT    the Trusted Signing account name
#   TRUSTED_SIGNING_PROFILE    the certificate profile name
# -DryRun prints what would be signed and needs no secrets (PR CI runs it).
param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'

foreach ($file in $Files) {
    if (-not (Test-Path -LiteralPath $file)) { throw "Not found: $file" }
}

if ($DryRun) {
    foreach ($file in $Files) {
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash
        Write-Host "[dry run] would sign $file (SHA-256 $hash) with Trusted Signing" `
            "(file digest SHA256, timestamp http://timestamp.acs.microsoft.com)"
    }
    exit 0
}

foreach ($name in 'TRUSTED_SIGNING_ENDPOINT', 'TRUSTED_SIGNING_ACCOUNT', 'TRUSTED_SIGNING_PROFILE') {
    if (-not [Environment]::GetEnvironmentVariable($name)) { throw "$name is not set" }
}
if (-not (Get-Module -ListAvailable -Name TrustedSigning)) {
    Install-Module -Name TrustedSigning -RequiredVersion 0.5.0 -Force -Scope CurrentUser
}
Invoke-TrustedSigning `
    -Endpoint $env:TRUSTED_SIGNING_ENDPOINT `
    -CodeSigningAccountName $env:TRUSTED_SIGNING_ACCOUNT `
    -CertificateProfileName $env:TRUSTED_SIGNING_PROFILE `
    -Files ($Files -join ',') `
    -FileDigest SHA256 `
    -TimestampRfc3161 'http://timestamp.acs.microsoft.com' `
    -TimestampDigest SHA256

foreach ($file in $Files) {
    & signtool verify /pa /v $file
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed: $file" }
}
