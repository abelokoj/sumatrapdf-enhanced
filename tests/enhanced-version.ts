import assert from "node:assert/strict";
import { mkdtempSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { enhancedVersionHeader, generateEnhancedVersion, parseEnhancedVersion } from "../cmd/gen-enhanced-version.ts";

export async function testit(): Promise<void> {
  for (const [value, resource] of [
    ["2026-10-16.5", [2026, 10, 16, 5]],
    ["2024-02-29.65535", [2024, 2, 29, 65535]],
    ["2000-01-01.1", [2000, 1, 1, 1]],
  ] as const) {
    const parsed = parseEnhancedVersion(value);
    assert.equal(parsed.display, value);
    assert.equal(parsed.calendar, true);
    assert.deepEqual(parsed.resource, [...resource]);
    assert.equal(
      enhancedVersionHeader(value),
      `// Generated from enhanced-version.txt by cmd/build.ts.\n#define ENHANCED_VERSION_STRA "${value}"\n#define ENHANCED_VERSION_RESOURCE ${resource.join(", ")}\n`,
    );
  }
  assert.deepEqual(parseEnhancedVersion("v0.1.2"), {
    display: "0.1.2",
    resource: [0, 1, 2, 0],
    calendar: false,
  });
  assert.equal(parseEnhancedVersion("v0.1.0-preview.1").display, "0.1.0-preview.1");
  for (const invalid of [
    "2026-02-29.5",
    "2100-02-29.1",
    "2026-04-31.5",
    "2026-00-16.5",
    "2026-13-16.5",
    "2026-10-00.5",
    "2026-10-16.0",
    "2026-10-16.65536",
    "2026-10-16.05",
    "0000-01-01.1",
    "1999-12-31.1",
    "2026-1-16.5",
    "2026-10-16.5-preview",
    "../2026-10-16.5",
    "v65536.1.1",
    "v01.1.1",
    "v0.1.2\n#define MALICIOUS 1",
  ]) {
    assert.throws(() => enhancedVersionHeader(invalid), undefined, invalid);
  }

  const root = mkdtempSync(join(tmpdir(), "sumatra-version-test-"));
  try {
    mkdirSync(join(root, "src"));
    writeFileSync(join(root, "enhanced-version.txt"), "2026-10-16.5\r\n");
    generateEnhancedVersion(root);
    const path = join(root, "src", "EnhancedVersion.h");
    assert.equal(readFileSync(path, "utf8"), enhancedVersionHeader("2026-10-16.5"));
    const modified = statSync(path).mtimeMs;
    generateEnhancedVersion(root);
    assert.equal(statSync(path).mtimeMs, modified, "unchanged generation must not force a rebuild");
  } finally {
    rmSync(root, { recursive: true });
  }
  console.log("PASS: Enhanced calendar version dates, resources, legacy generation, and stable output");
}

if (import.meta.main) await testit();
