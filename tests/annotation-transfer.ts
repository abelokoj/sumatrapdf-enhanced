import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { writeTransferFixtures } from "./annotation-transfer-data.ts";
import { ControlClient, ControlCommand } from "./control.ts";
import { cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";

import {
  enumWindows,
  getWindowPid,
  getWindowText,
  isWindowVisible,
  isWindowEnabled,
  getWindowOwner,
  postMessage,
  captureWindowToPng,
} from "./winapi.ts";

type Row = {
  page: number;
  type: string;
  rect: number[];
  color: number[];
  fill: number[];
  opacity: number;
  ink: number[];
  contents: string;
};
type Mapping = { source: number; target: number; annotations: number; confidence: number };

function assert(condition: unknown, message: string): asserts condition {
  if (!condition) {
    throw new Error(`annotation-transfer: ${message}`);
  }
}
function hash(path: string): string {
  return createHash("sha256").update(readFileSync(path)).digest("hex");
}
function numbers(value: string): number[] {
  return value.split(/[ ,]+/).filter(Boolean).map(Number);
}
function dumpRows(raw: string): Row[] {
  return raw
    .split("\n")
    .filter((line) => /^page=/.test(line))
    .map((line) => {
      const fields = Object.fromEntries(
        line.split(";").map((part) => {
          const index = part.indexOf("=");
          return [part.slice(0, index), part.slice(index + 1)];
        }),
      );
      return {
        page: Number(fields.page),
        type: fields.type!,
        rect: numbers(fields.rect ?? ""),
        color: numbers(fields.C ?? ""),
        fill: numbers(fields.IC ?? ""),
        opacity: Number(fields.CA),
        ink: numbers(fields.ink ?? ""),
        contents: fields.contents ?? "",
      };
    });
}
function mappings(raw: string): Mapping[] {
  return [...raw.matchAll(/source=(\d+) target=(\d+) annotations=(\d+) confidence=([\d.]+)/g)].map((m) => ({
    source: +m[1]!,
    target: +m[2]!,
    annotations: +m[3]!,
    confidence: +m[4]!,
  }));
}
async function operation(client: ControlClient, action: string, arg = "", source = 0, target = 0): Promise<string> {
  const result = await client.request(ControlCommand.TestAnnotationTransfer, [action, arg, source, target]);
  const raw = String(result[1] ?? "");
  assert(result[0] === 0, `${action} failed: ${raw}`);
  return raw;
}
async function rejected(client: ControlClient, action: string, arg: string): Promise<void> {
  const result = await client.request(ControlCommand.TestAnnotationTransfer, [action, arg, 0, 0]);
  assert(result[0] !== 0, `${action} accepted invalid input ${arg}`);
  assert(String(result[1] ?? "").length > 0, `${action} failure had no explanation`);
}
async function withPdf(
  path: string,
  callback: (client: ControlClient, frame: number, pid: number) => Promise<void>,
): Promise<void> {
  const { proc, client, frame } = await launchControlled([path]);
  try {
    await client.waitForRenderIdle();
    await callback(client, frame, proc.pid!);
  } finally {
    await operation(client, "reset").catch(() => {});
    await client.quit().catch(() => {});
    client.close();
    await killAndWait(proc);
  }
}
function equivalent(actual: number[], expected: number[], label: string, tolerance = 0.01): void {
  assert(
    actual.length === expected.length &&
      actual.every((n, i) => Number.isFinite(n) && Math.abs(n - expected[i]!) <= tolerance),
    `${label} changed: ${actual} != ${expected}`,
  );
}
function row(rows: Row[], contents: string): Row {
  const value = rows.find((item) => item.contents === contents);
  assert(value, `missing annotation '${contents}'`);
  return value;
}

export async function testit(): Promise<void> {
  const dir = tmpPath("annotation-transfer");
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  const fixture = writeTransferFixtures(dir);
  const originals = Object.fromEntries(Object.entries(fixture).map(([key, path]) => [key, hash(path)]));
  const packagePath = join(dir, "observations.sannot.pdf");
  const output = join(dir, "transferred.pdf");
  let originalRows: Row[] = [];
  await withPdf(fixture.old!, async (client) => {
    originalRows = dumpRows(await operation(client, "dump", fixture.old!));
    assert(originalRows.length === 5, `fixture has ${originalRows.length} annotations, expected 5`);
    await operation(client, "export", packagePath);
    assert(existsSync(packagePath), "export created no package");
    const header = readFileSync(packagePath).subarray(0, 8).toString("latin1");
    assert(header.startsWith("%PDF-"), "package is not a native PDF annotation container");
  });
  let transferred: Row[] = [];
  await withPdf(fixture.revised!, async (client) => {
    const mapping = mappings(await operation(client, "analyze", packagePath));
    assert(mapping.length === 3, `expected 3 annotated source pages, got ${mapping.length}`);
    for (const [source, target] of [
      [1, 3],
      [2, 4],
      [3, 2],
    ]) {
      assert(
        mapping.some((page) => page.source === source && page.target === target && page.confidence > 0.9),
        `source page ${source} did not match reordered page ${target}`,
      );
    }
    await rejected(client, "import", fixture.old!);
    await rejected(client, "import", fixture.revised!);
    const result = await operation(client, "import", output);
    assert(/imported=5\b/.test(result), `expected 5 imports: ${result}`);
    transferred = dumpRows(await operation(client, "dump", output));
    assert(transferred.length === 5, `output has ${transferred.length} annotations, expected 5`);
    assert(!transferred.some((annotation) => annotation.page === 1), "inserted cover received an annotation");
    for (const original of originalRows) {
      const actual = row(transferred, original.contents);
      const target = { 1: 3, 2: 4, 3: 2 }[original.page];
      assert(
        actual.page === target && actual.type === original.type,
        `wrong destination/type for ${original.contents}`,
      );
      equivalent(actual.rect, original.rect, `${original.contents} rectangle`);
      equivalent(actual.color, original.color, `${original.contents} stroke color`);
      equivalent(actual.fill, original.fill, `${original.contents} fill color`);
      equivalent(actual.ink, original.ink, `${original.contents} ink geometry`);
      assert(Math.abs(actual.opacity - original.opacity) < 0.001, `${original.contents} opacity changed`);
    }
    await rejected(client, "analyze", fixture.old!);
    const malformed = join(dir, "malformed.sannot.pdf");
    writeFileSync(malformed, "%PDF-1.7\nnot a PDF annotation package\n");
    await rejected(client, "analyze", malformed);
    await rejected(client, "import", join(dir, "after-failed-analysis.pdf"));
    assert(!existsSync(join(dir, "after-failed-analysis.pdf")), "failed analysis left a stale importable plan");
  });
  await withPdf(output, async (client) => {
    await operation(client, "analyze", packagePath);
    const repeat = join(dir, "repeat.pdf");
    const result = await operation(client, "import", repeat);
    assert(/imported=0\b/.test(result), `re-import duplicated annotations: ${result}`);
    assert(dumpRows(await operation(client, "dump", repeat)).length === 5, "re-import changed annotation count");
  });
  await withPdf(fixture.shifted!, async (client) => {
    await operation(client, "analyze", packagePath);
    const shifted = join(dir, "shifted-result.pdf");
    await operation(client, "import", shifted);
    const actual = row(dumpRows(await operation(client, "dump", shifted)), "Preserve this highlight");
    const original = row(originalRows, "Preserve this highlight");
    assert(
      Math.abs(Math.abs(actual.rect[1]! - original.rect[1]!) - 72) < 2,
      `highlight did not follow moved text: ${actual.rect} vs ${original.rect}`,
    );
  });
  await withPdf(fixture.ambiguous!, async (client) => {
    const mapping = mappings(await operation(client, "analyze", packagePath));
    assert(mapping.find((page) => page.source === 1)?.target === 0, "duplicate identical pages were guessed silently");
    const skipped = join(dir, "ambiguous-skipped.pdf");
    await operation(client, "import", skipped);
    assert(
      dumpRows(await operation(client, "dump", skipped)).length === 3,
      "ambiguous source page was imported without review",
    );
    await operation(client, "map", "", 1, 2);
    const reviewed = join(dir, "ambiguous-reviewed.pdf");
    await operation(client, "import", reviewed);
    const rows = dumpRows(await operation(client, "dump", reviewed));
    assert(rows.length === 5 && row(rows, "Preserve this highlight").page === 2, "manual mapping was not honored");
  });
  await withPdf(fixture.old!, async (_client, frame, pid) => {
    for (const [command, title] of [
      ["CmdExportAnnotationsTransfer", "Export annotations"],
      ["CmdImportAnnotationsTransfer", "Import annotations"],
    ]) {
      sendCommandSync(frame, cmdId(command!));
      const dialog = await pollUntil(
        () => {
          let found = 0;
          enumWindows((hwnd) => {
            if (getWindowPid(hwnd) === pid && isWindowVisible(hwnd) && getWindowText(hwnd) === title) found = hwnd;
            return true;
          });
          return found;
        },
        (value) => !!value,
        { error: `missing ${title} dialog` },
      );
      assert(
        getWindowOwner(dialog) === frame && !isWindowEnabled(frame),
        "transfer dialog does not protect its source window",
      );
      assert(captureWindowToPng(dialog, join(dir, `${command}.png`)), "transfer dialog capture failed");
      postMessage(dialog, 0x10, 0, 0);
      await pollUntil(
        () => isWindowEnabled(frame),
        (value) => value,
        { error: "closing transfer dialog left the app disabled" },
      );
    }
  });
  for (const [key, path] of Object.entries(fixture)) {
    assert(hash(path) === originals[key], `${key} original PDF was overwritten`);
  }
  assert(hash(output) !== hash(fixture.revised!), "import produced no document changes");
  console.log(
    "Annotation transfer: reordered/inserted pages, text anchors, styles, deduplication, review and input safety passed",
  );
}

if (import.meta.main) {
  await runStandalone(testit);
}
