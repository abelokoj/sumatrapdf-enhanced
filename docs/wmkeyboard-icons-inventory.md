# WMKeyboard Icon Inventory, Vector Extraction, and Integration Proposals

This document provides a comprehensive inventory, technical specification, license catalog, vector definition library, and architectural integration proposal for the iconography and vector asset system found in **WMKeyboard** ([wasi-master/wmkeyboard](https://github.com/wasi-master/wmkeyboard)), the reference Android keyboard application accompanying the bundled vocabulary decks in SumatraPDF Enhanced.

---

## 1. Executive Summary & Architectural Overview

WMKeyboard incorporates a modern, modular vector icon architecture designed for high-density Android displays, dynamic theming (Android 13+ monochrome theming and custom palette generation), and extensible runtime icon packs.

Key characteristics of the WMKeyboard iconography system:
1. **Multi-tiered Vector Architecture**:
   - **Branding & Launcher System**: Authored on a 108dp adaptive canvas constrained within a 66dp safe-zone diameter, capable of outputting full-bleed store listings (512px), layered WebP raster mipmaps, linear gradient vector drawables, and single-path even-odd monochrome masks.
   - **Handcrafted Component Glyphs**: Unique UI vectors engineered directly in code (`KeyboardIcons.kt`) where standard icon sets lacked appropriate semantics (notably a directional-safe `ForwardDelete` ⌦ and an `Incognito` hat-and-glasses mark).
   - **Curated Google Material Symbols**: 22 domain-specific symbols extracted and generated from Material Symbols (`0 -960 960 960` coordinate space) covering specialized tools (Stickers, GIF boxes, Trackpad, Dictation waveforms, Bubble level, Indic translation).
   - **Extensive Slot-Based Tool Registry**: 66+ toolbar tool slots, 21 key action slots, 10 toolbar chrome slots, and 13 emoji category tab slots managed via a decoupled `IconSlots` and `IconDefaults` engine.
   - **Alternative Variants Engine**: Slot-level visual variants (e.g., `tool.gif@text`, `tool.voice@wave`, `tool.settings@filled`) accessible via a unified `@` key convention.
   - **Runtime Icon Pack Format**: ZIP-based packs containing raw SVGs mapped to slot IDs (`<slot-id>.svg` and `<slot-id>@<variant>.svg`) parsed by an in-tree lightweight XML/SVG parser.

All custom and extracted standalone SVG files generated from this inventory have been compiled and mirrored into:
- [`data/wmkeyboard-reference/icons/`](file:///C:/dev/sumatrapdf/data/wmkeyboard-reference/icons/)
- [`docs/wmkeyboard-icons/`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/)
- Structured machine-readable catalog: [`data/wmkeyboard-reference/inventory.json`](file:///C:/dev/sumatrapdf/data/wmkeyboard-reference/inventory.json)

---

## 2. Geometry Specifications & Design Standards

| Surface / Asset Type | Coordinate Canvas / Viewport | Scale / Safe Constraints | Stroke / Fill Parameters | Primary Characteristics |
| :--- | :--- | :--- | :--- | :--- |
| **Adaptive Launcher Icon** | `108dp x 108dp` (center at `54, 54`) | Safe-circle diameter: `66dp` (`r = 32.5dp`). Scaled uniformly by `s = 0.6972` | Linear gradient fills + soft drop-shadow (`dx=0, dy=1.6, stdDev=1.7`) | Accommodates circle, squircle, teardrop launcher masks without clipping. |
| **Themed Monochrome Mark** | `108dp x 108dp` | Scaled `s = 0.6972` around `(54, 54)` | `fillType="evenOdd"`, `#FFFFFFFF` | Single continuous compound path; even-odd winding knocks out key gaps, speech pin halo, and WM monogram. |
| **Standard UI Glyphs** | `24dp x 24dp` (viewport `24 x 24`) | Optical center at `(12, 12)` | Stroke: `1.7dp - 1.75dp`, `round` caps, `round` joins | Clean geometric outlines matching modern Material Symbols and Lucide style. |
| **Material Symbols Viewport** | `960 x 960` (`viewBox="0 -960 960 960"`) | Normalized to `24dp x 24dp` | Solid fill (`fill="currentColor"`), shifted `Y += 960` | Origin at bottom-left (font metrics convention); scalable without distortion. |
| **Media Kit Wordmark** | Width `240dp+`, variable aspect | Set in Inter (Medium/SemiBold) | Converted to vector curves | Preserves font kerning via HarfBuzz / uharfbuzz pipeline. |

---

## 3. Brand & Launcher Identity Inventory

The core mark represents a physical keyboard body overlaid with a speech-bubble pin holding the monogram **"WM"**.

### Palette Reference
- **Background Gradient**: Top `#2F3474` &rarr; Bottom `#191C42` (Deep indigo ramp)
- **Keyboard Body Gradient**: Top `#5D9BFF` &rarr; Bottom `#A78BFA` (Soft periwinkle/violet)
- **Monogram Brand Gradient**: `#4C8DF6` &rarr; `#8B5CF6` (Seed brand ramp)
- **Pin Face**: `#FFFFFF` (Solid white)
- **Drop Shadow**: `#0B0E33` with `34%` opacity, `dy=1.6`, `stdDeviation=1.7`

### Asset Catalog
| Asset Identifier | Source File in Upstream Repo | Output Standalone SVG | Description & Semantic Role |
| :--- | :--- | :--- | :--- |
| `launcher_monochrome` | `app/src/main/res/drawable/ic_launcher_monochrome.xml` | [`launcher_monochrome.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/launcher_monochrome.svg) | Android 13+ themed icon; single compound path with even-odd fill knocking out keys and monogram. |
| `launcher_background` | `app/src/main/res/drawable/ic_launcher_background.xml` | [`launcher_background.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/launcher_background.svg) | Linear gradient background layer for adaptive icon. |
| `media_kit_icon` | `docs/public/media-kit/logo/icon.svg` | [`media_kit_icon.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_icon.svg) | Full vector launcher icon with multi-stop gradients and drop-shadow filter. |
| `media_kit_icon_rounded` | `docs/public/media-kit/logo/icon-rounded.svg` | [`media_kit_icon_rounded.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_icon_rounded.svg) | Squircle-masked launcher icon for documentation and media kits. |
| `media_kit_mark_mono_black`| `docs/public/media-kit/logo/mark-mono-black.svg` | [`media_kit_mark_mono_black.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_mark_mono_black.svg) | Solid black silhouette of the keyboard mark + pin. |
| `media_kit_mark_mono_white`| `docs/public/media-kit/logo/mark-mono-white.svg` | [`media_kit_mark_mono_white.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_mark_mono_white.svg) | Solid white silhouette of the keyboard mark + pin. |
| `media_kit_mark_on_dark` | `docs/public/media-kit/logo/mark-on-dark.svg` | [`media_kit_mark_on_dark.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_mark_on_dark.svg) | Contrast-adjusted mark for dark UI headers. |
| `media_kit_mark_on_light`| `docs/public/media-kit/logo/mark-on-light.svg` | [`media_kit_mark_on_light.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_mark_on_light.svg) | Contrast-adjusted mark for light UI headers. |
| `media_kit_lockup_dark` | `docs/public/media-kit/logo/lockup-dark.svg` | [`media_kit_lockup_dark.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_lockup_dark.svg) | Horizontal logo lockup: icon + "WM Keyboard" wordmark (dark ground). |
| `media_kit_lockup_light`| `docs/public/media-kit/logo/lockup-light.svg` | [`media_kit_lockup_light.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_lockup_light.svg) | Horizontal logo lockup: icon + "WM Keyboard" wordmark (light ground). |
| `media_kit_wordmark_navy`| `docs/public/media-kit/logo/wordmark-navy.svg` | [`media_kit_wordmark_navy.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_wordmark_navy.svg) | Outlined wordmark set in Inter (deep navy). |
| `media_kit_wordmark_white`| `docs/public/media-kit/logo/wordmark-white.svg` | [`media_kit_wordmark_white.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/media_kit_wordmark_white.svg) | Outlined wordmark set in Inter (pure white). |

---

## 4. Handcrafted IME UI Glyphs

These glyphs were manually coded in `feature/ime/src/main/java/com/wasimaster/wmkeyboard/ime/ui/KeyboardIcons.kt` because standard Google Material Symbols or Feather/Lucide lacked exact design parity.

### 1. `KeyboardIcons.ForwardDelete` (⌦)
- **File**: [`keyboard_forward_delete.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/keyboard_forward_delete.svg)
- **Viewport**: `0 0 24 24`, Stroke width: `1.7`, `round` caps and joins.
- **Rationale**: Standard Material Backspace is auto-mirrored in RTL locales. Mirroring Backspace for forward delete caused RTL layouts to flip forward-delete back into backspace. The authors built a dedicated forward-facing tapered slab with an inset centered cross:
```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">
  <path d="M3.2 5.2 H15.4 L21.2 12 L15.4 18.8 H3.2 Z" />
  <path d="M6.8 9.2 L12.4 14.8 M12.4 9.2 L6.8 14.8" />
</svg>
```

### 2. `KeyboardIcons.Incognito`
- **File**: [`keyboard_incognito.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/keyboard_incognito.svg)
- **Viewport**: `0 0 24 24`, Stroke width: `1.7`.
- **Rationale**: Designed to replace the un-themeable 🕶 emoji on the privacy toolbar indicator. Features a fedora hat crown, horizontal brim, and circular glasses joined by a curved bridge:
```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24" fill="currentColor">
  <!-- Hat crown -->
  <path d="M8.1 9.4 L9.1 4.6 Q9.3 3.8 10.1 3.8 H13.9 Q14.7 3.8 14.9 4.6 L15.9 9.4 Z" />
  <!-- Brim -->
  <path d="M3.6 10.4 H20.4 V11.9 H3.6 Z" />
  <!-- Glasses -->
  <path d="M10.6 16.9 A2.55 2.55 0 1 1 10.59 16.85 M18.7 16.9 A2.55 2.55 0 1 1 18.69 16.85 M10.8 16.3 Q12 15.6 13.2 16.3" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" />
</svg>
```

---

## 5. Curated Material Symbols Inventory (`SymbolIcons.kt`)

Extracted from Material Symbols (Apache 2.0) with standard 960x960 box (`viewBox="0 -960 960 960"`). Generated into standalone SVGs in `docs/wmkeyboard-icons/`.

| Symbol Identifier | Upstream Glyphs Name | Output SVG File | UI Tool / Function | Summary of Drawing |
| :--- | :--- | :--- | :--- | :--- |
| `Sticker` | `sticker` | [`symbol_sticker.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_sticker.svg) | `ToolbarTool.STICKER` | Sheet with peel corner folded up |
| `StickerAdd` | `sticker_add` | [`symbol_sticker_add.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_sticker_add.svg) | Custom sticker pack addition | Peeling sticker with `+` badge |
| `GifBox` | `gif_box` | [`symbol_gif_box.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_gif_box.svg) | `ToolbarTool.GIF` (default) | Square outline containing bold "GIF" lettering |
| `Gif` | `gif` | [`symbol_gif.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_gif.svg) | `tool.gif@text_small` variant | Compact "GIF" lettering without outer boundary |
| `Gif2` | `gif_2` | [`symbol_gif2.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_gif2.svg) | `tool.gif@text` variant | Full-width display "GIF" glyph |
| `SmartToy` | `smart_toy` | [`symbol_smart_toy.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_smart_toy.svg) | `tool.ai@robot` variant | Retro robot head with antennae and digital smile |
| `SettingsFilled` | `settings` (fill) | [`symbol_settings_filled.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_settings_filled.svg) | `tool.settings@filled` variant | Solid filled gear with 6 rounded teeth |
| `MicFilled` | `mic` (fill) | [`symbol_mic_filled.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_mic_filled.svg) | `tool.voice@filled` variant | Solid filled capsule microphone |
| `AnimatedImages` | `animated_images` | [`symbol_animated_images.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_animated_images.svg) | Media / Motion GIF tools | Staggered frames depicting animation sequence |
| `TrackpadInput` | `trackpad_input` | [`symbol_trackpad_input.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_trackpad_input.svg) | `ToolbarTool.TRACKPAD` | Finger touching touchpad surface with gestures |
| `MobileHandLeft` | `mobile_hand_left` | [`symbol_mobile_hand_left.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_mobile_hand_left.svg) | `ToolbarTool.ONE_HANDED` | Hand gripping phone from left edge |
| `ToolsLevel` | `tools_level` | [`symbol_tools_level.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_tools_level.svg) | `ToolbarTool.LEVEL` | Horizontal carpenter's spirit bubble level |
| `Dictionary` | `dictionary` | [`symbol_dictionary.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_dictionary.svg) | `ToolbarTool.DICTIONARY` | Open hardcover dictionary with initial 'A' on page |
| `KeyboardExternalInput` | `keyboard_external_input` | [`symbol_keyboard_external_input.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_keyboard_external_input.svg) | Physical keyboard tools | Keyboard silhouette with external cable input |
| `PartlyCloudyDay`| `partly_cloudy_day` | [`symbol_partly_cloudy_day.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_partly_cloudy_day.svg) | `ToolbarTool.WEATHER` | Sun rays peeking from behind cumulus cloud |
| `ConvertToText` | `convert_to_text` | [`symbol_convert_to_text.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_convert_to_text.svg) | `ToolbarTool.OCR` | Document page transformed into live selectable lines |
| `TranslateIndic` | `translate_indic` | [`symbol_translate_indic.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_translate_indic.svg) | Indic phonetic layouts | Dual script character card (Indic 'অ' & Latin 'A') |
| `Shift` | `shift` (outline) | [`symbol_shift.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_shift.svg) | `key.shift` (off state) | Outlined upward arrow with notched shaft |
| `ShiftFilled` | `shift` (fill) | [`symbol_shift_filled.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_shift_filled.svg) | `key.shift_on` (armed state) | Solid filled upward arrow |
| `ShiftLockFilled`| `shift_lock` (fill) | [`symbol_shift_lock_filled.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_shift_lock_filled.svg) | `key.shift_lock` (locked) | Solid filled upward arrow over horizontal bar |
| `KeyboardPreviousLanguage` | `keyboard_previous_language` | [`symbol_keyboard_previous_language.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_keyboard_previous_language.svg) | Language cycling key | Miniature keyboard chassis with rotating globe |
| `KeyboardKeys` | `keyboard_keys` | [`symbol_keyboard_keys.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/symbol_keyboard_keys.svg) | Layout designer tools | Full grid of keyboard key tops with spacebar |

---

## 6. System Shortcuts & Social Branding Inventory

Extracted and converted from Android Vector Drawables in `app/src/main/res/drawable/`:

| Identifier | Source Vector Drawable | Standalone SVG | Visual Purpose |
| :--- | :--- | :--- | :--- |
| `qs_pin_keyboard` | `ic_qs_pin_keyboard.xml` | [`qs_pin_keyboard.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/qs_pin_keyboard.svg) | Android Quick Settings tile: Pin keyboard persistently on screen |
| `shortcut_ai_chat` | `ic_shortcut_ai_chat.xml` | [`shortcut_ai_chat.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/shortcut_ai_chat.svg) | Launcher deep shortcut: Direct AI Chat assistant invocation |
| `shortcut_languages`| `ic_shortcut_languages.xml` | [`shortcut_languages.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/shortcut_languages.svg) | Launcher deep shortcut: Keyboard Language & Input layout settings |
| `shortcut_themes` | `ic_shortcut_themes.xml` | [`shortcut_themes.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/shortcut_themes.svg) | Launcher deep shortcut: Color palette & Theme selection |
| `shortcut_tools` | `ic_shortcut_tools.xml` | [`shortcut_tools.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/shortcut_tools.svg) | Launcher deep shortcut: Toolbar Toolbox customizer |
| `common_notification` | `common_ic_notification.xml`| [`common_notification.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/common_notification.svg) | Android Notification Bar: Active background service badge |
| `summon_ai` | `ic_summon_ai.xml` | [`summon_ai.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/summon_ai.svg) | Floating trigger handle for assistant sidebar |
| `summon_keyboard` | `ic_summon_keyboard.xml` | [`summon_keyboard.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/summon_keyboard.svg) | Floating trigger handle to raise keyboard without text field |
| `summon_keyboard_hide` | `ic_summon_keyboard_hide.xml` | [`summon_keyboard_hide.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/summon_keyboard_hide.svg) | Floating trigger handle to dismiss persistent keyboard |
| `brand_telegram` | `ic_brand_telegram.xml` | [`brand_telegram.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/brand_telegram.svg) | Telegram paper airplane brand vector |
| `brand_instagram`| `ic_brand_instagram.xml` | [`brand_instagram.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/brand_instagram.svg) | Instagram rounded camera aperture brand vector |
| `brand_reddit` | `ic_brand_reddit.xml` | [`brand_reddit.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/brand_reddit.svg) | Reddit Snoo alien silhouette brand vector |
| `brand_tiktok` | `ic_brand_tiktok.xml` | [`brand_tiktok.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/brand_tiktok.svg) | TikTok musical note brand vector |
| `brand_x` | `ic_brand_x.xml` | [`brand_x.svg`](file:///C:/dev/sumatrapdf/docs/wmkeyboard-icons/brand_x.svg) | X (formerly Twitter) stylized lettermark |

---

## 7. Complete Toolbar Tools Slot Inventory (`ToolbarTool`)

Every tool in `ToolbarTool.entries` maps to a stable slot ID `"tool." + name.lowercase()` in `IconSlots.kt` and an exhaustive mapping in `IconDefaults.forTool`:

| Slot Identifier | Default Vector Glyph | Upstream Glyph Source | Tool Function & UI Context | Supported Visual Variants |
| :--- | :--- | :--- | :--- | :--- |
| `tool.emoji` | `EmojiEmotions` | Material Symbols Outlined | Emoji browser & recent stickers | `v:simple` (`SentimentSatisfied`) |
| `tool.clipboard` | `Assignment` | Material Symbols AutoMirrored | Clipboard history & pin management | `v:plain` (`ContentPaste`) |
| `tool.snippets` | `TextSnippet` | Material Symbols AutoMirrored | Saved reusable text snippets | None |
| `tool.text_edit` | `EditNote` | Material Symbols Outlined | Dedicated caret & selection keypad | None |
| `tool.trackpad` | `TrackpadInput` | `SymbolIcons.TrackpadInput` | Spacebar / keyboard trackpad mode | None |
| `tool.one_handed` | `MobileHandLeft` | `SymbolIcons.MobileHandLeft` | Docked one-handed layout toggle | None |
| `tool.split` | `VerticalSplit` | Material Symbols Outlined | Split keyboard layout for tablets | None |
| `tool.floating` | `PictureInPictureAlt` | Material Symbols Outlined | Free-floating draggable keyboard | None |
| `tool.persistent` | `PushPin` | Material Symbols Outlined | Pin keyboard over window without focus | None |
| `tool.resize` | `AspectRatio` | Material Symbols Outlined | Inline drag-to-resize handle | None |
| `tool.settings` | `Settings` | Material Symbols Outlined | Settings app launcher | `v:filled` (`SymbolIcons.SettingsFilled`) |
| `tool.flashlight` | `FlashlightOn` | Material Symbols Outlined | Hardware torch toggle | None |
| `tool.compass` | `Explore` | Material Symbols Outlined | Sensor-based compass tool | None |
| `tool.level` | `ToolsLevel` | `SymbolIcons.ToolsLevel` | Digital surface bubble level | None |
| `tool.undo` | `Undo` | Material Symbols AutoMirrored | Undo last text transformation | None |
| `tool.redo` | `Redo` | Material Symbols AutoMirrored | Redo undone text transformation | None |
| `tool.moon_phase` | `DarkMode` | Material Symbols Outlined | Lunar phase calculator | None |
| `tool.weather` | `PartlyCloudyDay` | `SymbolIcons.PartlyCloudyDay` | Local forecast widget | None |
| `tool.calendar` | `CalendarMonth` | Material Symbols Outlined | Calendar view & date picker | None |
| `tool.incognito` | `VisibilityOff` | Material Symbols Outlined | Disable learning & private mode | `v:mask` (`KeyboardIcons.Incognito`) |
| `tool.selection_actions` | `SmartButton` | Material Symbols Outlined | Quick action bar above text selection | None |
| `tool.phonetic_english` | `Translate` | Material Symbols Outlined | Indic phonetic script bypass toggle | Overridden to Indic glyph |
| `tool.power_saving` | `BatterySaver` | Material Symbols Outlined | Battery throttle & animation pause | None |
| `tool.themes` | `Palette` | Material Symbols Outlined | Color scheme & background changer | `v:brush` (`Brush`) |
| `tool.autocorrect` | `Spellcheck` | Material Symbols Outlined | Real-time spellchecking toggle | None |
| `tool.sound_haptics` | `Vibration` | Material Symbols Outlined | Haptic feedback & key click toggle | `v:speaker` (`VolumeUp`) |
| `tool.numpad` | `Dialpad` | Material Symbols Outlined | Dedicated 10-key numeric keypad | None |
| `tool.handwriting` | `Draw` | Material Symbols Outlined | Stylus & finger canvas recognition | None |
| `tool.camera` | `PhotoCamera` | Material Symbols Outlined | Instant camera snapshot input | None |
| `tool.dictionary` | `Dictionary` | `SymbolIcons.Dictionary` | Offline dictionary lookup | None |
| `tool.vocabulary` | `AutoStories` | Material Symbols Outlined | Word deck flashcards & spaced repetition | None |
| `tool.learn_from_text` | `School` | Material Symbols Outlined | Harvest unknown words from field | None |
| `tool.translate` | `Translate` | Material Symbols Outlined | Instant multi-language translation | None |
| `tool.gif` | `GifBox` | `SymbolIcons.GifBox` | Inline GIF search & insert | `v:text` (`Gif2`), `v:text_small` (`Gif`) |
| `tool.sticker` | `Sticker` | `SymbolIcons.Sticker` | Inline sticker picker | None |
| `tool.web_search` | `TravelExplore` | Material Symbols Outlined | Fast web engine query | `v:magnifier` (`Search`) |
| `tool.image_search` | `ImageSearch` | Material Symbols Outlined | Reverse image lookup & web images | None |
| `tool.ocr` | `ConvertToText` | `SymbolIcons.ConvertToText` | OCR camera text scanner | None |
| `tool.qr_scan` | `QrCodeScanner` | Material Symbols Outlined | QR code / barcode decoder | None |
| `tool.doc_scan` | `DocumentScanner` | Material Symbols Outlined | Multi-page document scanner | None |
| `tool.voice` | `Mic` | Material Symbols Outlined | On-device voice dictation (Whisper) | `v:filled` (`MicFilled`), `v:wave` (`GraphicEq`) |
| `tool.grammar` | `FactCheck` | Material Symbols AutoMirrored | Grammar & punctuation corrector | None |
| `tool.wikipedia` | `Public` | Material Symbols Outlined | Wikipedia summary lookup | None |
| `tool.symbols` | `Functions` | Material Symbols Outlined | Math, Greek & unicode symbols | None |
| `tool.calculator` | `Calculate` | Material Symbols Outlined | Inline math calculator | None |
| `tool.unit_convert` | `SwapHoriz` | Material Symbols Outlined | Length, mass, temp converter | None |
| `tool.currency` | `CurrencyExchange`| Material Symbols Outlined | Foreign currency rate converter | None |
| `tool.qr_gen` | `QrCode2` | Material Symbols Outlined | Generate QR code from text | None |
| `tool.password_gen` | `Password` | Material Symbols Outlined | Random password generator | None |
| `tool.ai` | `AutoAwesome` | Material Symbols Outlined | On-device LLM text rewriter | `v:robot` (`SymbolIcons.SmartToy`) |
| `tool.ai_reply` | `Quickreply` | Material Symbols Outlined | Smart reply generator for incoming chat | None |
| `tool.modes` | `Tune` | Material Symbols Outlined | Preset typing mode switcher | None |
| `tool.typing_test` | `Speed` | Material Symbols Outlined | WPM & accuracy speed tester | None |
| `tool.media_control` | `MusicNote` | Material Symbols Outlined | Play/pause/next media keys | None |
| `tool.plugins` | `Extension` | Material Symbols Outlined | Addon extension manager | None |
| `tool.app_launcher` | `Apps` | Material Symbols Outlined | Quick app switch drawer | None |
| `tool.fancy` | `TextFormat` | Material Symbols Outlined | Stylized Unicode text decorator | None |
| `tool.custom_layout` | `GridView` | Material Symbols Outlined | Secondary custom key grid | None |
| `tool.statistics` | `QueryStats` | Material Symbols Outlined | Typing heatmap & keystroke counters | None |
| `tool.find_replace` | `FindReplace` | Material Symbols Outlined | Text find and replace bar | None |
| `tool.notes` | `StickyNote2` | Material Symbols Outlined | Integrated Markdown notepad | None |
| `tool.location` | `LocationOn` | Material Symbols Outlined | Coordinates, Plus code, map link | None |
| `tool.clock` | `Schedule` | Material Symbols Outlined | World clock, timer, stopwatch | None |
| `tool.gallery` | `PhotoLibrary` | Material Symbols Outlined | Media picker & photo editor | None |
| `tool.language` | `Language` | Material Symbols Outlined | Active language toggle | None |
| `tool.kde_connect` | `Phonelink` | Material Symbols Outlined | Paired desktop remote control | None |
| `tool.cursor_left` | `KeyboardArrowLeft`| Material Symbols AutoMirrored | Caret left 1 char | None |
| `tool.cursor_right`| `KeyboardArrowRight`| Material Symbols AutoMirrored | Caret right 1 char | None |
| `tool.cursor_up` | `KeyboardArrowUp` | Material Symbols Outlined | Caret up 1 line | None |
| `tool.cursor_down` | `KeyboardArrowDown` | Material Symbols Outlined | Caret down 1 line | None |
| `tool.cursor_home` | `FirstPage` | Material Symbols Outlined | Caret to line start | None |
| `tool.cursor_end` | `LastPage` | Material Symbols AutoMirrored | Caret to line end | None |
| `tool.page_up` | `KeyboardDoubleArrowUp` | Material Symbols Outlined | Caret up 1 page | None |
| `tool.page_down` | `KeyboardDoubleArrowDown` | Material Symbols Outlined | Caret down 1 page | None |
| `tool.cursor_word_left` | `KeyboardDoubleArrowLeft` | Material Symbols Outlined | Caret left 1 word | None |
| `tool.cursor_word_right`| `KeyboardDoubleArrowRight`| Material Symbols Outlined | Caret right 1 word | None |
| `tool.select_word` | `HighlightAlt` | Material Symbols Outlined | Select current word | None |
| `tool.select_line` | `ViewHeadline` | Material Symbols Outlined | Select current line | None |
| `tool.select_all` | `FormatShapes` | Material Symbols Outlined | Select all text in field | None |
| `tool.select_mode` | `SelectAll` | Material Symbols Outlined | Toggle persistent selection mode | None |
| `tool.copy` | `ContentCopy` | Material Symbols Outlined | Copy selected text | None |
| `tool.cut` | `ContentCut` | Material Symbols Outlined | Cut selected text | None |
| `tool.paste` | `ContentPaste` | Material Symbols Outlined | Paste clipboard text | None |
| `tool.hide_keyboard`| `KeyboardHide` | Material Symbols Outlined | Dismiss keyboard window | `v:chevron` (`KeyboardArrowDown`) |

---

## 8. Key, Chrome, and Emoji Tab Slots

### Key Slots (`IconSlots.keySlots`)
| Slot ID | Default Vector | Purpose & Trigger |
| :--- | :--- | :--- |
| `key.shift` | `SymbolIcons.Shift` | Shift key (lowercase / unshifted state) |
| `key.shift_on` | `SymbolIcons.ShiftFilled` | Shift key (armed for next capital character) |
| `key.shift_lock` | `SymbolIcons.ShiftLockFilled` | Shift key (caps lock active) |
| `key.caps_lock` | `KeyboardCapslock` | Dedicated tablet caps lock key |
| `key.backspace` | `Backspace` (AutoMirrored) | Delete character before cursor |
| `key.forward_delete`| `KeyboardIcons.ForwardDelete` | Delete character after cursor (⌦) |
| `key.globe` | `Language` | Switch input language |
| `key.input_method_picker`| `Keyboard` | Android IME system switch dialog |
| `key.emoji` | `EmojiEmotions` | Open emoji / symbols drawer (`v:simple` available) |
| `key.arrow_left` | `KeyboardArrowLeft` (AutoMirrored) | Arrow row left |
| `key.arrow_up` | `KeyboardArrowUp` | Arrow row up |
| `key.arrow_down` | `KeyboardArrowDown` | Arrow row down |
| `key.arrow_right` | `KeyboardArrowRight` (AutoMirrored) | Arrow row right |
| `key.tab` | `KeyboardTab` (AutoMirrored) | Send tab character |
| `key.enter` | `KeyboardReturn` (AutoMirrored) | Standard newline / submit |
| `key.enter_search` | `Search` | Enter action: search query submission |
| `key.enter_send` | `Send` (AutoMirrored) | Enter action: messaging send |
| `key.enter_go` | `ArrowForward` (AutoMirrored) | Enter action: navigation URL submit |
| `key.enter_next` | `KeyboardTab` (AutoMirrored) | Enter action: jump to next form input |
| `key.enter_previous`| `ArrowBack` (AutoMirrored) | Enter action: jump to previous form input |
| `key.enter_done` | `Check` | Enter action: finish editing form |

### Chrome Slots (`IconSlots.chromeSlots`)
| Slot ID | Default Vector | Purpose |
| :--- | :--- | :--- |
| `chrome.toolbox` | `GridView` | Open toolbar tool customizer panel |
| `chrome.panel_back` | `ChevronLeft` | Back button inside secondary tool panels |
| `chrome.suggestions_expand` | `ChevronRight` | Expand suggestion strip into multi-row grid |
| `chrome.emoji_shortcut` | `EmojiEmotions` | Direct emoji button in suggestion strip |
| `chrome.search_close` | `Close` | Clear search field or close search panel |
| `chrome.incognito` | `KeyboardIcons.Incognito` | Private browsing mode status indicator |
| `chrome.power_saving` | `BatterySaver` | Battery saving mode indicator |
| `chrome.show_keys` | `Keyboard` | Reveal on-screen keys when physical keyboard docked |
| `chrome.hide_keys` | `KeyboardHide` | Collapse on-screen keys to toolbar only |
| `chrome.toolbox_search` | `Search` | Filter tools inside toolbox panel |

### Emoji Tab Slots (`IconSlots.emojiTabSlots`)
| Slot ID | Default Vector | Category Name |
| :--- | :--- | :--- |
| `emoji_tab.search` | `Search` | Emoji keyword search |
| `emoji_tab.recent` | `Schedule` | Recently used emoji |
| `emoji_tab.most_used` | `BarChart` | Most frequently typed emoji |
| `emoji_tab.smileys` | `EmojiEmotions` | Smileys & emotion |
| `emoji_tab.people` | `EmojiPeople` | People & body |
| `emoji_tab.animals` | `Pets` | Animals & nature |
| `emoji_tab.nature` | `EmojiNature` | Plants & flowers |
| `emoji_tab.food` | `Fastfood` | Food & drink |
| `emoji_tab.travel` | `DirectionsCar` | Travel & places |
| `emoji_tab.activities`| `SportsSoccer` | Activities & sports |
| `emoji_tab.objects` | `EmojiObjects` | Objects & tools |
| `emoji_tab.symbols` | `EmojiSymbols` | Symbols & shapes |
| `emoji_tab.flags` | `EmojiFlags` | Country & regional flags |

---

## 9. Key Layout Names & Aliases Engine (`KeyIcons.kt`)

Key layout definition files store icon references as strings rather than hard-coded enums. The `KeyIcons` module maps canonical names and common aliases to vector instances:

```text
delete      -> backspace
return      -> enter
caps / capslock -> caps_lock
shift_filled -> shift_on
up / down / left / right -> arrow_*
done        -> check
clear       -> close
plus / minus -> add / remove
clipboard   -> paste
globe       -> language
voice       -> mic
photo / picture -> image
email / mail -> at
favorite    -> heart
hide        -> keyboard_hide
gear        -> settings
bulb        -> highlight
```

---

## 10. Icon Pack Format Specification

WMKeyboard implements a decoupled icon pack ecosystem (`IconPackFile.kt` and `SvgParser.kt`):

```text
<pack-name>.zip
├── pack.json                     # Metadata (name, author, version, description)
└── icons/                        # Flat folder of vector SVGs
    ├── tool.vocabulary.svg       # Base slot override
    ├── tool.gif.svg              # GIF base slot override
    ├── tool.gif@text.svg         # Variant override using '@' separator
    ├── key.backspace.svg         # Key slot override
    └── chrome.toolbox.svg        # Chrome slot override
```

### SVG Parser Grammar Requirements
1. **Root element**: `<svg ... viewBox="minX minY width height">`.
2. **Supported tags**: `<path>`, `<rect>`, `<circle>`, `<ellipse>`, `<line>`, `<polyline>`, `<polygon>`, `<g>`.
3. **Supported path commands**: `M/m`, `L/l`, `H/h`, `V/v`, `C/c`, `S/s`, `Q/q`, `T/t`, `A/a`, `Z/z`.
4. **Color handling**: `currentColor` inherits surrounding theme button tint; explicit hex `#RRGGBB` or `#AARRGGBB` overrides tint.
5. **Fill rule**: defaults to `nonZero`; supports `fill-rule="evenodd"`.

---

## 11. Licenses, Rights & Attribution Catalog

| Asset Group | Original Source / Author | Governing License | Compliance Requirements |
| :--- | :--- | :--- | :--- |
| **WMKeyboard Application & Custom Artwork** | [Wasi Master / wmkeyboard](https://github.com/wasi-master/wmkeyboard), Copyright (c) 2026 | **MIT License** | Full license text in `docs/licenses/` and `data/vocabulary/LICENSE`. Attribution must accompany redistributions. |
| **Google Material Symbols & Icons** | [Google LLC](https://github.com/google/material-design-icons) | **Apache License 2.0** | Permissive vector reuse; allows modification, distribution, and commercial inclusion with notice. |
| **Inter Typography (Wordmark Outlines)** | Rasmus Andersson | **SIL Open Font License 1.1** | Only vector outlines are embedded; raw font files are not redistributed. |
| **Amaz Obitaem Ostrov V.2 (Monogram)** | Victor Kharyk | **Freeware** | Font binary is excluded; only converted vector curve coordinates are included in source tools. |
| **Lucide Native Toolbar Icons** | [Lucide Icons Project](https://github.com/lucide-icons/lucide) | **ISC License / MIT** | Preserved in `docs/icon-attribution.md` and `docs/licenses/lucide-LICENSE.txt`. |

---

## 12. Architectural Integration Proposals for SumatraPDF Enhanced

Based on this inventory, the following technical integration proposals are submitted for SumatraPDF Enhanced:

### Proposal 1: Unified Deck Learning & Dictionary Iconography
SumatraPDF Enhanced already bundles WM vocabulary decks (`data/vocabulary/*.wmvocab.json.gz`) and an offline dictionary lookup system (`src/VocabularyMeanings.h`). The learning tools should adopt the exact vector iconography established in WMKeyboard:
- `symbol_dictionary.svg` for the Dictionary lookup panel and definition popup.
- `symbol_convert_to_text.svg` for OCR selection and document text extraction.
- `symbol_tools_level.svg` and `symbol_shift.svg` for reading precision tools.
- `keyboard_incognito.svg` for the reading privacy indicator (disabling recent files and reading telemetry).

### Proposal 2: Win32 Native SVG Path Integration
Following SumatraPDF's zero-dependency policy, external graphic rendering libraries (such as Skia or Flutter) should not be introduced. The vector definitions in this inventory are fully compatible with SumatraPDF's native GDI+ / Direct2D SVG rendering pipeline in `src/SvgIcons.cpp`:
- Paths should be declared as compact C string literals using `currentColor` support.
- Icons should adopt the standard 1.75px stroke width and rounded caps to maintain consistency with `src/EnhancedIcons.h`.

### Proposal 3: Extensible Desktop Theme Packs
Align the WMKeyboard ZIP-based icon pack convention (`pack.json` + `icons/<slot>.svg`) with SumatraPDF Enhanced's file icon packs (`docs/file-icons/`). This allows advanced users to customize reading deck buttons, toolbar symbols, and annotation badges through local theme folders without recompiling the executable.

---

## 13. Summary of Extracted SVG Assets

The following 50 SVG files have been generated, validated, and placed in `docs/wmkeyboard-icons/` and `data/wmkeyboard-reference/icons/`:

```text
docs/wmkeyboard-icons/
├── brand_instagram.svg
├── brand_reddit.svg
├── brand_telegram.svg
├── brand_tiktok.svg
├── brand_x.svg
├── common_notification.svg
├── keyboard_forward_delete.svg
├── keyboard_incognito.svg
├── launcher_background.svg
├── launcher_monochrome.svg
├── media_kit_icon.svg
├── media_kit_icon_rounded.svg
├── media_kit_lockup_dark.svg
├── media_kit_lockup_light.svg
├── media_kit_mark_mono_black.svg
├── media_kit_mark_mono_white.svg
├── media_kit_mark_on_dark.svg
├── media_kit_mark_on_light.svg
├── media_kit_wordmark_navy.svg
├── media_kit_wordmark_white.svg
├── qs_pin_keyboard.svg
├── shortcut_ai_chat.svg
├── shortcut_languages.svg
├── shortcut_themes.svg
├── shortcut_tools.svg
├── summon_ai.svg
├── summon_keyboard.svg
├── summon_keyboard_hide.svg
├── symbol_animated_images.svg
├── symbol_convert_to_text.svg
├── symbol_dictionary.svg
├── symbol_gif.svg
├── symbol_gif2.svg
├── symbol_gif_box.svg
├── symbol_keyboard_external_input.svg
├── symbol_keyboard_keys.svg
├── symbol_keyboard_previous_language.svg
├── symbol_mic_filled.svg
├── symbol_mobile_hand_left.svg
├── symbol_partly_cloudy_day.svg
├── symbol_settings_filled.svg
├── symbol_shift.svg
├── symbol_shift_filled.svg
├── symbol_shift_lock_filled.svg
├── symbol_smart_toy.svg
├── symbol_sticker.svg
├── symbol_sticker_add.svg
├── symbol_tools_level.svg
├── symbol_trackpad_input.svg
└── symbol_translate_indic.svg
```
