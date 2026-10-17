import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export function parseEnhancedVersion(value: string): { display: string; resource: number[]; calendar: boolean } {
  const version = value.trim();
  const calendar = /^(\d{4})-(\d{2})-(\d{2})\.([1-9]\d*)$/.exec(version);
  if (calendar) {
    const [year, month, day, release] = calendar.slice(1).map(Number);
    const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
    const days = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > days[month - 1] || release > 65535) {
      throw new Error("Invalid Enhanced release date or release counter (1 to 65535)");
    }
    return { display: version, resource: [year, month, day, release], calendar: true };
  }

  // Older source snapshots still generate their original embedded version.
  const match = /^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*))?$/.exec(version);
  if (!match || match.slice(1, 4).some((part) => Number(part) > 65535)) {
    throw new Error(
      "enhanced-version.txt must contain YYYY-MM-DD.N (legacy vA.B.C source versions are also supported)",
    );
  }
  return { display: version.substring(1), resource: [...match.slice(1, 4).map(Number), 0], calendar: false };
}

export function enhancedVersionHeader(value: string): string {
  const { display, resource } = parseEnhancedVersion(value);
  return `// Generated from enhanced-version.txt by cmd/build.ts.\n#define ENHANCED_VERSION_STRA "${display}"\n#define ENHANCED_VERSION_RESOURCE ${resource.join(", ")}\n`;
}

export function generateEnhancedVersion(root = process.cwd()): void {
  const text = enhancedVersionHeader(readFileSync(join(root, "enhanced-version.txt"), "utf-8"));
  const path = join(root, "src", "EnhancedVersion.h");
  let previous = "";
  try {
    previous = readFileSync(path, "utf-8");
  } catch {}
  if (previous.replace(/\r\n/g, "\n") !== text) writeFileSync(path, text, "utf-8");
}

if (import.meta.main) {
  if (process.argv.length === 3 && process.argv[2] === "--check") {
    const parsed = parseEnhancedVersion(readFileSync("enhanced-version.txt", "utf-8"));
    if (!parsed.calendar) throw new Error("New releases require YYYY-MM-DD.N");
    console.log(parsed.display);
  } else if (process.argv.length === 2) {
    generateEnhancedVersion();
  } else {
    throw new Error("Usage: bun cmd/gen-enhanced-version.ts [--check]");
  }
}
