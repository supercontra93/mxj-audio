# Contributing to MXJ AUDIO

Thank you for helping. MXJ AUDIO is free software under AGPLv3.
*Türkçe özet aşağıda.*

## Before you start
- Read `CLAUDE.md`: layers, real-time rules, commands, translations, theme.
- Open an issue first for anything larger than a small fix, so work is not duplicated.
- Work follows the phases in `docs/phases/`. Ideas outside the current phase go to `docs/later.md`.

## Pull requests
- One goal per pull request, with tests where possible.
- CI must be green (Windows build and tests, layer rule, Linux portability check).
- New dependencies need a decision record in `docs/adr/` and a verified free licence in `THIRD_PARTY.md`.
- Sign off every commit (`git commit -s`). This is the
  [Developer Certificate of Origin](https://developercertificate.org/): you confirm you have the
  right to submit the work under the project licence. No separate contributor agreement is needed.

## Name and logo
The code is AGPLv3. The name "MXJ AUDIO" and its logo are not covered by the code licence;
forks must use a different name.

---

## Türkçe özet
- Önce `CLAUDE.md` dosyasını oku. Büyük işler için önce bir konu (issue) aç.
- Her katkı isteği tek bir amaca hizmet etsin, CI yeşil olsun.
- Yeni kütüphane eklemek için `docs/adr/` altına karar kaydı yaz ve lisansın ücretsiz olduğunu doğrula.
- Her commit'i `git commit -s` ile imzala (DCO).
