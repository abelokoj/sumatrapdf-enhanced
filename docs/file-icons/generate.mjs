// Copyright 2026 the SumatraPDF Enhanced project authors. License: GPLv3.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createRequire } from "node:module";
import { createHash } from "node:crypto";

const base = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(base, "../..");
const require = createRequire(import.meta.url);
const modulePath = process.argv.find((arg) => arg.startsWith("--sharp-module="));
const sharp = require(modulePath ? modulePath.slice("--sharp-module=".length) : "sharp");
const sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256];
const styles = [
  { id: 1, name: "folded-pages", label: "Folded pages" },
  { id: 2, name: "color-covers", label: "Color covers" },
  { id: 3, name: "outlined-pages", label: "Outlined pages" },
];
const registry = fs.readFileSync(path.join(root, "src/RegistryInstaller.cpp"), "utf8");
const start = registry.indexOf("static SeqStrings gSupportedExts");
const list = registry.slice(start, registry.indexOf(";", start) + 1);
const extensions = [...list.matchAll(/\.([a-z0-9]+)\\0/g)].map((match) => match[1]);
if (extensions.length !== 39 || new Set(extensions).size !== 39) {
  throw new Error("Registered extensions changed; update the icon sources before generating.");
}
const logo = fs.readFileSync(path.join(base, "app-logo-badge.png")).toString("base64");
const hash = (buffer) => createHash("sha256").update(buffer).digest("hex");
const packs = [];
const assets = [];
const defaults = [];
const write = (name, buffer) => {
  fs.mkdirSync(path.dirname(name), { recursive: true });
  fs.writeFileSync(name, buffer);
};

function makeIco(pngs) {
  const header = Buffer.alloc(6 + sizes.length * 16);
  header.writeUInt16LE(1, 2);
  header.writeUInt16LE(sizes.length, 4);
  let offset = header.length;
  pngs.forEach((png, index) => {
    const at = 6 + index * 16;
    header[at] = sizes[index] === 256 ? 0 : sizes[index];
    header[at + 1] = header[at];
    header.writeUInt16LE(1, at + 4);
    header.writeUInt16LE(32, at + 6);
    header.writeUInt32LE(png.length, at + 8);
    header.writeUInt32LE(offset, at + 12);
    offset += png.length;
  });
  return Buffer.concat([header, ...pngs]);
}

function makePack(entries) {
  const header = Buffer.alloc(12);
  header.write("SFEICON1", 0, "ascii");
  header.writeUInt32LE(entries.length, 8);
  const parts = [header];
  for (const { ext, ico } of entries) {
    const name = Buffer.from(ext, "ascii");
    const entry = Buffer.alloc(6);
    entry.writeUInt16LE(name.length);
    entry.writeUInt32LE(ico.length, 2);
    parts.push(entry, name, ico);
  }
  return Buffer.concat(parts);
}

for (const style of styles) {
  const source =
    style.id === 3 ? path.join(base, "sources", style.name) : path.join(base, "packs", style.name, "sources");
  const destination = style.id === 3 ? path.join(root, "src/gfx/filetypes") : path.join(base, "packs", style.name);
  const entries = [];
  for (const [index, ext] of extensions.entries()) {
    const normal = fs.readFileSync(path.join(source, `${ext}.svg`), "utf8");
    const small = fs.readFileSync(path.join(source, `${ext}-small.svg`), "utf8");
    const pngs = [];
    for (const size of sizes) {
      // Set the SVG viewport before loading: every PNG is rasterized from the vector at its native size.
      const svg = (size <= 24 ? small : normal)
        .replace("<svg ", `<svg width="${size}" height="${size}" `)
        .replace(/href="[^\"]*app-logo-badge\.png"/, `href="data:image/png;base64,${logo}"`);
      const png = await sharp(Buffer.from(svg), { density: 72 })
        .png({ compressionLevel: 9, adaptiveFiltering: true })
        .toBuffer();
      if (png.readUInt32BE(16) !== size || png.readUInt32BE(20) !== size) {
        throw new Error(`Incorrect dimensions for ${style.name}/${ext}/${size}`);
      }
      pngs.push(png);
    }
    const ico = makeIco(pngs);
    if (ico.length > 512 * 1024) throw new Error(`Icon too large: ${ext}`);
    write(path.join(destination, `${ext}.ico`), ico);
    entries.push({ ext, ico });
    const entry = { style: style.id, extension: ext, bytes: ico.length, sha256: hash(ico) };
    if (style.id === 3) defaults.push({ ...entry, resourceId: 1000 + index });
    else assets.push(entry);
  }
  if (style.id === 3) continue;
  const pack = makePack(entries);
  if (pack.length > 8 * 1024 * 1024) throw new Error(`Pack too large: ${style.name}`);
  const filename = `${style.name}.sfeicons`;
  write(path.join(base, "packs", filename), pack);
  packs.push({
    style: style.id,
    name: style.name,
    bytes: pack.length,
    sha256: hash(pack),
    url: `https://raw.githubusercontent.com/abelokoj/sumatrapdf-enhanced/master/docs/file-icons/packs/${filename}`,
  });
}

const manifest = {
  version: 1,
  badgeOutsidePercent: 20,
  defaultStyle: 3,
  sizes,
  extensions,
  renderer: sharp.versions,
  defaults,
  packs,
  assets,
};
write(path.join(base, "manifest.json"), `${JSON.stringify(manifest, null, 2)}\n`);
const comment = "// Generated by docs/file-icons/generate.mjs.\n";
write(
  path.join(root, "src/gfx/filetype-icons.rcinc"),
  comment +
    defaults.map((entry) => `${entry.resourceId} ICON "gfx\\\\filetypes\\\\${entry.extension}.ico"`).join("\n") +
    "\n",
);
write(
  path.join(root, "src/FileTypeIconPacks.h"),
  comment +
    "static const FileTypeIconPack kFileTypeIconPacks[] = {\n" +
    packs
      .map((entry) => `    {${entry.style}, "${entry.name}", "${entry.url}", ${entry.bytes}, "${entry.sha256}"},`)
      .join("\n") +
    "\n};\n\nstatic const FileTypeIconAsset kFileTypeIconAssets[] = {\n" +
    assets.map((entry) => `    {${entry.style}, "${entry.extension}", ${entry.bytes}, "${entry.sha256}"},`).join("\n") +
    "\n};\n",
);
console.log(JSON.stringify({ defaultBytes: defaults.reduce((sum, entry) => sum + entry.bytes, 0), packs }, null, 2));
