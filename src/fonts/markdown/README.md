# Optional Markdown fonts

These font files stay in the GitHub repository, separately from the application. They are not embedded in the executable or included in installers and portable packages. The app downloads only the faces needed for a chosen Markdown font, with their license notices; it never installs fonts globally in Windows.

| Family                    | Version | Upstream                                                                   | License                                                                   |
| ------------------------- | ------- | -------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Libertinus Serif and Math | 7.051   | [Release](https://github.com/alerque/libertinus/releases/tag/v7.051)       | SIL OFL 1.1                                                               |
| New Computer Modern       | 8.1.1   | [CTAN](https://ctan.org/pkg/newcomputermodern)                             | GUST Font License; exceptions listed in `new-computer-modern/License.txt` |
| Lora                      | 3.021   | [Release](https://github.com/cyrealtype/Lora-Cyrillic/releases/tag/v3.021) | SIL OFL 1.1                                                               |

Latest published versions checked on 2026-10-11. `manifest.json` records source URLs and SHA-256 checksums of every copied file. Font files retain their original names and bytes. The New Computer Modern source archive (`sfd.txz`) accompanies its fonts for redistribution.

Choose the font under Settings → Advanced → Markdown. Applying an uncached font downloads its text faces and companion math font from [the pinned GitHub snapshot](https://github.com/abelokoj/sumatrapdf-enhanced/tree/3d7870d8a8cdcd891e352e08f5c4ecc3451680f0/src/fonts/markdown). The download runs in the background before applying the change. Opening a document does not start a download; a missing font uses the interface font until chosen. A failed download leaves the previous choice unchanged. Markdown pages contain no font selector, and these settings do not change PDF document fonts.

Downloaded files stay under `markdown-fonts/3d7870d8a8cdcd891e352e08f5c4ecc3451680f0/` in the app's data directory, including custom or portable profiles. Cached fonts remain available offline. Original font releases, license files, and the New Computer Modern source archive can also be downloaded separately from the GitHub directory above.

Math fonts apply to MathML and mathematical symbols; choosing a font does not add a TeX equation renderer. Code blocks retain their monospace style.

The browser uses renamed `EnhancedWebNewCM*.woff2` derivatives in `browser/`. Some upstream CFF hinting programs fail Chromium validation; these copies remove hinting, expand subroutines, and compress the fonts without removing glyphs or changing their outlines. Original releases, licenses, and `sfd.txz` remain available. Regenerate with `generate-browser-fonts.py` and the pinned development tools listed in its header; these tools are not required by the app. `browser/manifest.json` records the derived files and their original sources.
