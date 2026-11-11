# SumatraPDF Enhanced website

Static project website for GitHub Pages. The site has no build step and serves
local branding and licensed Manrope fonts. The hero feature illustration is an
example; the screenshot gallery shows the actual application. Gallery images
use lazy-loaded WebP previews and link to the original full-size PNGs.

## Preview

From the repository root, run `python -m http.server 4173 --directory website`
and open `http://localhost:4173`.

## Publish

In the repository's **Settings → Pages**, select **GitHub Actions** as the
publishing source. Merge the website and `.github/workflows/website.yml` onto
`master`. The workflow deploys only `website/`; subsequent website changes
publish automatically. It can also be run manually from Actions.

Expected project URL: https://abelokoj.github.io/sumatrapdf-enhanced/

The download selector checks GitHub's latest stable release and accepts both
date-based and legacy version tags. It uses uploaded assets only when the
complete x64 and ARM64 installer, portable EXE and portable ZIP set is present.
If the API is unavailable or the release is incomplete, the links open
GitHub's latest stable release page instead of guessing a file URL. Prereleases
and drafts are excluded. Without JavaScript, the same release-page links work.
All local paths are relative for project Pages hosting.

Manrope is included under the license in `assets/Manrope-OFL.txt`. Application
logos retain the repository's existing licensing.
