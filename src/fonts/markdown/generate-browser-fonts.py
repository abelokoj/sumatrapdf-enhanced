"""Generate renamed browser copies; original fonts and glyph outlines stay intact.
Requires fonttools[woff]==4.66.1 (brotli==1.2.0). Run from this directory.
"""
from pathlib import Path
import hashlib
import json
from concurrent.futures import ProcessPoolExecutor
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "browser"
OUTPUT.mkdir(exist_ok=True)
FILES = [
    "NewCM10-Regular", "NewCM10-Italic", "NewCM10-Bold", "NewCM10-BoldItalic",
    "NewCM10-Book", "NewCM10-BookItalic",
    "NewCMSans10-Regular", "NewCMSans10-Oblique", "NewCMSans10-Bold", "NewCMSans10-BoldOblique",
    "NewCMSans10-Book", "NewCMSans10-BookOblique",
    "NewCMMono10-Regular", "NewCMMono10-Italic", "NewCMMono10-Bold", "NewCMMono10-BoldOblique",
    "NewCMMono10-Book", "NewCMMono10-BookItalic",
    "NewCMUncial10-Regular", "NewCMUncial10-Bold",
    "NewCMMath-Regular", "NewCMMath-Bold", "NewCMMath-Book", "NewCMSansMath-Regular",
]
def build(stem):
    source = ROOT / "new-computer-modern" / (stem + ".otf")
    destination = OUTPUT / ("EnhancedWeb" + stem + ".woff2")
    font = TTFont(source, recalcTimestamp=False)
    glyphs = font.getGlyphOrder()
    options = subset.Options()
    options.hinting = False
    options.desubroutinize = True
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.name_legacy = True
    options.layout_features = ["*"]
    options.glyph_names = True
    options.notdef_outline = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(glyphs=glyphs)
    subsetter.subset(font)
    assert font.getGlyphOrder() == glyphs, "Do not remove or reorder glyphs"
    # Distinguish adapted fonts from the upstream releases, as requested by GFL.
    for record in font["name"].names:
        if record.nameID in (1, 3, 4, 6, 16, 18, 21):
            record.string = "EnhancedWeb" + record.toUnicode()
    cff = font["CFF "].cff
    cff.fontNames = ["EnhancedWeb" + name for name in cff.fontNames]
    top = cff.topDictIndex[0]
    for attribute in ("FamilyName", "FullName"):
        if hasattr(top, attribute):
            setattr(top, attribute, "EnhancedWeb" + getattr(top, attribute))
    font.flavor = "woff2"
    destination = OUTPUT / ("EnhancedWeb" + stem + ".woff2")
    font.save(destination)
    result = {"file": destination.name, "source": source.name, "glyphs": len(glyphs), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(), "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest()}
    print(destination.name, flush=True)
    return result

if __name__ == "__main__":
    with ProcessPoolExecutor(max_workers=4) as pool:
        manifest = list(pool.map(build, FILES))
    (OUTPUT / "manifest.json").write_text(json.dumps({"fonttools": "4.66.1", "brotli": "1.2.0", "changes": "Rename, expand CFF subroutines, remove hinting and editor tables, compress as WOFF2. Keep all glyphs, outlines and layout tables.", "files": manifest}, indent=2) + "\n", encoding="utf-8")
