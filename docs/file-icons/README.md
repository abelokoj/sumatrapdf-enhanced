# File icons

SumatraPDF Enhanced uses **Outlined pages** by default. **Folded pages** and **Color covers** are optional downloads. All three use the approved app-logo badge with 20% extending outside the page's left edge, leaving the folded top-right corner visible.

Each icon has native 16, 20, 24, 32, 40, 48, 64, 96, 128, and 256 pixel images. Every size is rendered directly from the SVG, with stronger detail at 16–24 pixels. Windows can choose the closest image for the display scale without enlarging a small bitmap. The original app-logo artwork is preserved.

These icons belong to this app's file associations. They do not change the default opener for a file type or replace another app's icons. Windows thumbnails may still appear instead of file icons when thumbnail view is enabled.

## Choose a style

Choose a file-icon style in the app's appearance settings. The default style is included with the app. Before downloading either alternative, the app shows the style name, source, download size, and confirmation. Once downloaded, the alternative remains available offline.

The downloadable packs live in this repository:

- [Folded pages](packs/folded-pages.sfeicons), about 1.26 MiB.
- [Color covers](packs/color-covers.sfeicons), about 1.54 MiB.

The optional packs are not included in the application executable or installer. Their individual `.ico` files and SVG sources are in the matching subfolders under `packs/`. Default SVG sources are in `sources/outlined-pages/`; default compiled icons are in `src/gfx/filetypes/` at the repository root.

## Regenerate assets

Asset development requires Node.js, Sharp 0.35.5, and Arial Bold. These are development tools, not application dependencies. SVG files share `app-logo-badge.png`; generation embeds this image before rendering. Label typography uses Arial, so font and renderer versions must match for byte-identical output.

Run from the repository root with Sharp installed in an external development directory:

```text
node docs/file-icons/generate.mjs --sharp-module=<absolute-path-to-sharp-package>
```

Without that argument, the script resolves `sharp` through Node's normal module lookup. The generator writes icons, both packs, `manifest.json`, `src/FileTypeIconPacks.h`, and `src/gfx/filetype-icons.rcinc`. It does not modify settings, associations, or the main resource script.

Resource IDs 1000–1038 follow the extension order in `gSupportedExts` in `src/RegistryInstaller.cpp`. Preserve this order when adding formats, or update the runtime resource map together with the generator. The manifest records the exact extension order, resource IDs, renderer versions, sizes, and SHA-256 checksums.

## Pack format

The `.sfeicons` format contains only icon images, without executable code or archive paths. Numeric fields are unsigned little-endian integers.

```text
8 bytes    ASCII "SFEICON1"
u32        entry count (39)
repeated entry:
  u16      extension name byte count
  u32      ICO byte count
  bytes    ASCII extension without the dot
  bytes    complete multi-resolution ICO
```

Entries follow the registered extension order. The entire download and each icon have SHA-256 checksums in the generated manifest and runtime header. Packs are limited to 8 MiB and individual icons to 512 KiB. Future incompatible format changes require a new magic/version.

## License

The SVG designs, generated icons, and generator are distributed under the repository's GPLv3 license; see `COPYING` at the repository root. The badge reproduces the existing app logo from `src/gfx/SumatraPDF-smaller.ico` and retains its project attribution in `AUTHORS`. Arial is used only during asset generation and is not redistributed here.
