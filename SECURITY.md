# Security policy

## Supported version

Security fixes are applied to the latest revision of the default branch. Older
firmware builds and third-party forks are not maintained by this repository.

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting form under the repository's
**Security** tab. Do not include credentials, Wi-Fi passwords, access tokens or
other personal data in a public issue. Include the affected revision, hardware,
reproduction steps and impact, using redacted logs where possible.

## Deployment boundary

- Never commit `~/.codex/auth.json`, Wi-Fi credentials or captured network data.
- Run the Codex usage bridge only on a trusted LAN. Do not expose port `8766` to
  the public internet.
- The bridge sends the OAuth token only to an HTTPS-verified `chatgpt.com`
  endpoint and returns only usage percentages/reset metadata to the ESP8266.
- The ESP8266 configuration portal and EEPROM storage are convenience features,
  not hardened secret storage. Use the device only on a network whose physical
  and local access you trust.
- Weather data currently uses a third-party plaintext HTTP endpoint and must not
  be treated as authenticated or integrity-protected information.
