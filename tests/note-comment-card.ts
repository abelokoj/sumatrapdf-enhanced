import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { assemblePdf, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, killAndWait, launchControlled } from "./win-automation.ts";
import {
  captureWindowToPng,
  enumWindows,
  findChildWindow,
  getClassName,
  getControlText,
  getWindowLong,
  getWindowPid,
  getWindowRect,
  isWindowVisible,
  packCoords,
  sendMessage,
} from "./winapi.ts";
function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw Error("note-comment-card: " + message);
}
export async function testit() {
  const dir = tmpPath("note-comment-card");
  mkdirSync(dir, { recursive: true });
  const body = "Pinned note body. Select and copy this text.";
  const pdf = assemblePdf([
    "<< /Type /Catalog /Pages 2 0 R >>",
    "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Annots [4 0 R] >>",
    "<< /Type /Annot /Subtype /Text /Name /Note /Rect [72 700 92 720] /C [1 0.94 0.67] /T (Author) /Contents (" +
      body +
      ") >>",
  ]);
  for (const [theme, scale] of [
    ["Light", 100],
    ["Dark", 200],
  ] as const) {
    const profile = join(dir, theme);
    mkdirSync(profile, { recursive: true });
    const file = join(profile, "Note.pdf");
    writeFileSync(file, pdf, "latin1");
    const original = readFileSync(file);
    writeFileSync(
      join(profile, "SumatraPDFEnhanced-settings.txt"),
      "UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nTheme = Sumatra " +
        theme +
        "\nInterfaceScale = " +
        scale +
        "\n",
    );
    const app = await launchControlled(["-appdata", profile, "-view", "single page", "-zoom", "fit page", file]);
    try {
      await app.client.waitForRenderIdle();
      const canvas = findCanvas(app.frame);
      const layout = String((await app.client.request(ControlCommand.TestLayout, []))[1]);
      const page = /page n=1 shown=1 .*screen=(-?\d+),(-?\d+),(\d+),(\d+)/.exec(layout);
      check(page, "native page geometry missing: " + layout);
      const [left, top, width, height] = page.slice(1).map(Number);
      const point = packCoords(Math.round(left! + (82 * width!) / 612), Math.round(top! + (82 * height!) / 792));
      sendMessage(canvas, 0x200, 0, point);
      sendMessage(canvas, 0x201, 1, point);
      sendMessage(canvas, 0x202, 0, point);
      const card = await pollUntil(
        () => {
          let found = 0;
          enumWindows((h) => {
            if (getWindowPid(h) === app.proc.pid && getClassName(h) === "SumatraAnnotTextPopup" && isWindowVisible(h))
              found = h;
            return true;
          });
          return found;
        },
        (v) => !!v,
        { error: "note card did not open" },
      );
      const edit = findChildWindow(card, "Edit");
      check(edit, "selectable text control missing");
      check((getWindowLong(edit, -16) & 0x800) !== 0, "note card is editable");
      check(getControlText(edit) === body, "annotation body changed");
      sendMessage(edit, 0x102, 88, 0);
      check(getControlText(edit) === body, "read-only typing altered contents");
      const bounds = getWindowRect(card),
        parent = getWindowRect(canvas);
      check(
        bounds.left >= parent.left &&
          bounds.right <= parent.right &&
          bounds.top >= parent.top &&
          bounds.bottom <= parent.bottom,
        "card escaped canvas",
      );
      const capture = process.env.SUMATRA_NOTE_CAPTURE_DIR;
      if (capture) {
        mkdirSync(capture, { recursive: true });
        captureWindowToPng(card, join(capture, "note-card-" + theme + ".png"));
      }
      sendMessage(edit, 0x100, 27, 0);
      await pollUntil(
        () => isWindowVisible(card),
        (v) => !v,
        { error: "Escape did not dismiss card" },
      );
      check(readFileSync(file).equals(original), "reading a note modified the PDF");
      console.log("PASS", theme, scale, JSON.stringify(bounds));
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
