# Bundled Markdown fonts

These unmodified font releases are available without installing fonts in Windows.

| Family                    | Version | Upstream                                                                   | License                                                                   |
| ------------------------- | ------- | -------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Libertinus Serif and Math | 7.051   | [Release](https://github.com/alerque/libertinus/releases/tag/v7.051)       | SIL OFL 1.1                                                               |
| New Computer Modern       | 8.1.1   | [CTAN](https://ctan.org/pkg/newcomputermodern)                             | GUST Font License; exceptions listed in `new-computer-modern/License.txt` |
| Lora                      | 3.021   | [Release](https://github.com/cyrealtype/Lora-Cyrillic/releases/tag/v3.021) | SIL OFL 1.1                                                               |

Latest published versions checked on 2026-10-11. `manifest.json` records source URLs and SHA-256 checksums of every copied file. Font files retain their original names and bytes. The New Computer Modern source archive (`sfd.txz`) accompanies its fonts for redistribution.

Choose the default under Settings → Markdown, or use the Font dropdown above an open Markdown document. Math fonts apply to MathML and mathematical symbols; choosing a font does not add a TeX equation renderer. Code blocks retain their monospace style.

The browser uses renamed `EnhancedWebNewCM*.woff2` derivatives in `browser/`. Some upstream CFF hinting programs fail Chromium validation; these copies remove hinting, expand subroutines, and compress the fonts without removing glyphs or changing their outlines. Original releases, licenses, and `sfd.txz` remain available. Regenerate with `generate-browser-fonts.py` and the pinned development tools listed in its header; these tools are not required by the app. `browser/manifest.json` records the derived files and their original sources.
