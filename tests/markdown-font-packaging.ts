import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";

export async function testit(): Promise<void> {
  const root = join(import.meta.dir, "..");
  const resource = readFileSync(join(root, "src/SumatraPDF.rc"), "utf8");
  assert.doesNotMatch(resource, /ENHANCED_MD_FONT_|fonts[\\/]+markdown/i, "Markdown fonts must not be embedded");
  assert.match(resource, /ENHANCED_MARKDOWN_LIGHT_CSS\s+RCDATA/);
  assert.match(resource, /ENHANCED_MARKDOWN_DARK_CSS\s+RCDATA/);
  const archive = readFileSync(join(root, "cmd/pack-embedded-prebuild.cmd"), "utf8");
  assert.doesNotMatch(archive, /copy[^\r\n]*src\\fonts\\markdown/i, "Optional fonts must not enter the archive");
  const installer = readFileSync(join(root, "src/Installer.cpp"), "utf8");
  assert.doesNotMatch(installer, /StrL\("docs\/licenses\/markdown\//, "Optional font payload is not installed");
  console.log("Markdown fonts are excluded from executable and installer resources; both stylesheets remain.");
}

if (import.meta.main) {
  await testit();
}
