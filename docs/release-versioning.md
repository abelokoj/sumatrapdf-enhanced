# Enhanced release versioning

Enhanced versions use `YYYY-MM-DD.N`, for example `2026-10-23.14`.

- The date is the actual publication date in America/New_York.
- `N` is the running published app-release count, including previews and maintenance updates. It never resets by day or year. Optional font/dictionary assets are not app releases.
- x64, ARM64, installer and portable packages of one release share its version. Local test builds do not consume a published counter.
- Explain feature and maintenance scope in release notes; do not add another numeric suffix.
- Windows file/product numeric metadata uses `YYYY.MM.DD.N`. Display strings and filenames retain hyphens in the date.
- Canonical tags are `enhanced-YYYY-MM-DD.N`; filenames are `SumatraPDF-Enhanced-YYYY-MM-DD.N-ARCH-KIND`.

`enhanced-version.txt` is the build version source. `cmd/build.ts` regenerates `src/EnhancedVersion.h`; packaging and publishing validate the same real-calendar date and 16-bit numeric component limits. The publisher checks the next counter against published app releases before any mutation.

The app shows its Enhanced version separately from its original SumatraPDF source version. Keep source commits and upstream version attribution intact; inherited upstream tags identify original source history.

## Historical migration

Four releases were relabeled without recompiling their binaries:

- `enhanced-v0.1.0-preview.3` -> `enhanced-2026-10-01.1` (Preview)
- `enhanced-v0.1.0` -> `enhanced-2026-10-01.2`
- `enhanced-v0.1.1` -> `enhanced-2026-10-01.3`
- `enhanced-v0.1.2` -> `enhanced-2026-10-11.4`

Canonical date tags point at exactly the original source commits. Old tags remain historical aliases. Release asset names and external checksums follow the canonical labels; original EXE/ZIP bytes and signatures remain unchanged. Migration metadata records both identities, and embedded historical manifests still describe the original package contents.

Older installed Enhanced updaters understand semantic tags only. Install the first calendar-version installer manually. New updaters continue to validate download hashes, architecture and embedded product identity; a relabeled old binary cannot pass as a rebuilt calendar binary.

The working version `2026-10-16.5` is an unpublished local build. Publishing it requires explicit approval, public release notes, matching source provenance, all architecture packages and successful release gates. If publication occurs on a later date, update the date while retaining the next counter.
