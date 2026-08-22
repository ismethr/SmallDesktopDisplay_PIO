# Contributing

Thank you for improving SmallDesktopDisplay. Keep changes focused and describe
their user-visible effect, hardware assumptions and verification steps.

Before opening a pull request:

1. Do not commit credentials, local absolute paths, `.pio`, build products or
   device/network captures.
2. Run the Python, native and relevant firmware/simulator tests documented in
   `README.md`.
3. Mark modified upstream code clearly and update user documentation when
   behaviour, wiring, network access or stored configuration changes.
4. Only add code and assets you have the right to distribute. Include licence,
   copyright, provenance and trademark notices where applicable.
5. By contributing, you agree that your contribution is distributed under
   GNU AGPL-3.0 and that you have authority to submit it under those terms.

Security reports belong in GitHub's private vulnerability reporting flow, not
in public issues or pull requests; see `SECURITY.md`.

## AI-assisted changes

Human contributors remain responsible for the code they submit, including its
review, testing, licensing and security. When OpenAI Codex materially assists
with a commit, disclose that assistance with a Git trailer after a blank line:

```text
Assisted-by: OpenAI Codex
```

Do not use `Co-authored-by` for an AI tool unless its provider publishes an
official GitHub account-linked identity and explicitly authorizes that use.
Do not invent an email address or reuse another product's bot identity.
