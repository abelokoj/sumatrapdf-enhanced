// Plain text must not open citation previews unless the pointer is on a real citation.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, type ControlClient } from "./control.ts";
import { assemblePdf, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, killAndWait, launchControlled } from "./win-automation.ts";
import { captureWindowToPng, captureWindowPixels, packCoords } from "./winapi.ts";

function textStream(lines: [number, number, string][]): string {
  return lines.map(([x, y, text]) => `BT /F1 11 Tf ${x} ${y} Td (${text.replace(/[\\()]/g, "\\$&")}) Tj ET`).join("\n");
}

function fixture(): string {
  const first = textStream([
    [24, 744, "Revision: 15 October 2026, 03:30:04PM (America/New_York)"],
    [24, 680, "Before words"],
    [160, 680, "(Smith, 2020)"],
    [290, 680, "after words."],
    [24, 616, "See (Jones, 2019)."],
    [24, 570, "Also (Jones, 2019)."],
    [24, 500, "Jones, 2019. Genuine same-page bibliography entry."],
    [24, 440, "[7] Isolated numbered reference entry."],
  ]);
  const second = textStream([[24, 744, "Smith, 2020. Genuine bibliography on the next page."]]);
  return assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 6 0 R >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 7 0 R >>",
    "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    `<< /Length ${first.length} >>\nstream\n${first}\nendstream`,
    `<< /Length ${second.length} >>\nstream\n${second}\nendstream`,
  ]);
}

function pagePoint(canvas: number): (x: number, y: number) => number {
  const pixels = captureWindowPixels(canvas);
  if (!pixels) throw new Error("Reference hover: canvas capture failed");
  let left = pixels.w,
    right = -1,
    top = pixels.h,
    bottom = -1;
  for (let y = 0; y < pixels.h; y++) {
    for (let x = 0; x < pixels.w; x++) {
      const i = (y * pixels.w + x) * 4;
      if (pixels.data[i] < 250 || pixels.data[i + 1] < 250 || pixels.data[i + 2] < 250) continue;
      left = Math.min(left, x);
      right = Math.max(right, x);
      top = Math.min(top, y);
      bottom = Math.max(bottom, y);
    }
  }
  if (process.env.SUMATRA_HOVER_CAPTURE) captureWindowToPng(canvas, process.env.SUMATRA_HOVER_CAPTURE);
  if (right <= left || bottom <= top) throw new Error("Reference hover: rendered page missing");
  return (x, y) =>
    packCoords(Math.round(left + (x * (right - left + 1)) / 612), Math.round(top + (y * (bottom - top + 1)) / 792));
}

async function state(client: ControlClient): Promise<string> {
  const response = await client.request(ControlCommand.TestRefHover, []);
  if (response[0] !== 0) throw new Error(`Reference hover: state failed: ${response[1]}`);
  return String(response[1]);
}

export async function testit(): Promise<void> {
  const dir = tmpPath("ref-hover-false-positive");
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "plain-citations.pdf");
  writeFileSync(pdf, fixture(), "latin1");
  writeFileSync(
    join(dir, "SumatraPDFEnhanced-settings.txt"),
    "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nShowToc = false\nShowFavorites = false\nTheme = Sumatra Dark\nCitationHoverDelay = 100\n",
  );
  const { proc, client, frame } = await launchControlled([
    "-appdata",
    dir,
    "-view",
    "single page",
    "-zoom",
    "fit page",
    pdf,
  ]);
  try {
    await client.waitForRenderIdle();
    await client.setNotificationsEnabled(false);
    const canvas = findCanvas(frame);
    const point = pagePoint(canvas);
    const checks: [string, number, number, number][] = [
      ["plain revision date", 100, 44, 0],
      ["ordinary words before a citation", 50, 108, 0],
      ["ordinary words after a citation", 315, 108, 0],
      ["isolated numbered reference entry", 30, 348, 0],
      ["unlinked citation", 180, 108, 2],
      ["same-page citation", 70, 172, 1],
      ["repeated same-page citation", 70, 218, 1],
      ["bibliography entry itself", 35, 288, 0],
    ];
    const failures: string[] = [];
    for (const [name, x, y, destPage] of checks) {
      const coords = point(x, y);
      // The control action uses the real detector without physical mouse-leave cancellation.
      const shown = await client.request(ControlCommand.TestRefHover, ["show-text", coords & 0xffff, coords >>> 16]);
      if (shown[0] !== 0) throw new Error(`Reference hover: lookup failed: ${shown[1]}`);
      let raw = await state(client);
      const deadline = Date.now() + 1200;
      while (!raw.includes("visible=1") && Date.now() < deadline) {
        await Bun.sleep(25);
        raw = await state(client);
      }
      const expected = destPage ? `visible=1` : "visible=0";
      if (!raw.includes(expected) || (destPage && !raw.includes(`page=${destPage} `))) {
        failures.push(`${name}: expected ${destPage ? `destination page ${destPage}` : "no popup"}; ${raw}`);
      }
      console.log(`${name}: ${raw}`);
    }
    if (failures.length) throw new Error(`Reference hover:\n${failures.join("\n")}`);
  } finally {
    client.close();
    await killAndWait(proc);
  }
}

if (import.meta.main) await runStandalone(testit);
