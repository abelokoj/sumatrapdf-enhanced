// Check rendered label edges and wrapped value positions, not just property text.
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { assemblePdf, cmdId, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowToPng,
  enumWindows,
  findChildWindow,
  getClassName,
  getClientRect,
  getControlText,
  getWindowPid,
  getWindowText,
  sendMessage,
  setWindowPos,
} from "./winapi.ts";

const WM_VSCROLL = 0x115;
const SB_TOP = 6;
const SB_BOTTOM = 7;
const EM_GETFIRSTVISIBLELINE = 0xce;
const EM_POSFROMCHAR = 0xd6;
const EM_LINEFROMCHAR = 0xc9;
const EM_LINEINDEX = 0xbb;
const EM_SETSEL = 0xb1;
const EM_SCROLLCARET = 0xb7;

function position(edit: number, index: number) {
  const xy = Number(sendMessage(edit, EM_POSFROMCHAR, index, 0));
  return { x: (xy << 16) >> 16, y: xy >> 16 };
}

export async function testit(): Promise<void> {
  const dir = tmpPath(`properties-layout-${process.pid}`);
  mkdirSync(dir, { recursive: true });
  const pdf = join(dir, "document-with-a-long-name-for-checking-property-value-wrapping.pdf");
  const title =
    "A long document title for checking column alignment and wrapping: " +
    "the continuation must stay inside the value column. ".repeat(5);
  writeFileSync(
    pdf,
    assemblePdf(
      [
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
        `<< /Title (${title}) /Author (Column layout test author) /Subject (First subject line\\nSecond subject line) >>`,
      ],
      { trailerExtras: "/Info 4 0 R" },
    ),
    "latin1",
  );
  const failures: string[] = [];
  for (const [theme, scale, fontSize] of [
    ["Sumatra Light", 100, 0],
    ["Sumatra Dark", 100, 25],
    ["Sumatra Dark", 200, 25],
  ] as const) {
    writeFileSync(
      join(dir, "SumatraPDFEnhanced-settings.txt"),
      `UiLanguage = en\nRestoreSession = false\nCheckForUpdates = false\nTheme = ${theme}\nInterfaceScale = ${scale}\nUIFontSize = ${fontSize}\n`,
    );
    const app = await launchControlled(["-appdata", dir, pdf]);
    try {
      await app.client.waitForRenderIdle();
      sendCommandSync(app.frame, cmdId("CmdProperties"));
      let properties = 0,
        edit = 0,
        raw = "";
      for (let i = 0; i < 160; i++) {
        enumWindows((hwnd) => {
          if (getWindowPid(hwnd) === app.proc.pid && getWindowText(hwnd) === "Document Properties") properties = hwnd;
          return true;
        });
        edit = properties ? findChildWindow(properties, "RICHEDIT50W") || findChildWindow(properties, "Edit") : 0;
        raw = edit ? getControlText(edit) : "";
        if (raw.includes(title) && !raw.includes("Getting font information")) break;
        await Bun.sleep(50);
      }
      if (!raw.includes(title)) throw Error("Properties layout: complete fixture not ready");
      const rich = getClassName(edit).toLowerCase() === "richedit50w";
      const text = rich ? raw.replaceAll("\r\n", "\r") : raw;
      for (const width of [1300, 750, 1100]) {
        setWindowPos(properties, 30, 40, width, 800);
        sendMessage(edit, EM_SETSEL, 0, 0);
        sendMessage(edit, EM_SCROLLCARET, 0, 0);
        await Bun.sleep(80);
        const rows = ["File:", "File Size:", "File Type:", "Title:", "Author:"];
        const ends: number[] = [],
          starts: number[] = [];
        for (const label of rows) {
          const at = text.indexOf(label);
          if (at < 0) throw Error(`Properties layout: missing ${label}`);
          let value = at + label.length;
          while (/\s/.test(text[value] ?? "") && value < text.length) value++;
          sendMessage(edit, EM_SETSEL, at, at);
          sendMessage(edit, EM_SCROLLCARET, 0, 0);
          ends.push(position(edit, at + label.length - 1).x);
          starts.push(position(edit, value).x);
        }
        if (Math.max(...ends) - Math.min(...ends) > 2)
          failures.push(`${theme} ${scale}% width=${width}: label right edges ${ends}`);
        if (Math.max(...starts) - Math.min(...starts) > 2)
          failures.push(`${theme} ${scale}% width=${width}: value left edges ${starts}`);
        for (const label of ["File:", "Title:"]) {
          const at = text.indexOf(label);
          let value = at + label.length;
          while (/\s/.test(text[value] ?? "") && value < text.length) value++;
          sendMessage(edit, EM_SETSEL, value, value);
          sendMessage(edit, EM_SCROLLCARET, 0, 0);
          const line = Number(sendMessage(edit, EM_LINEFROMCHAR, value, 0));
          const next = Number(sendMessage(edit, EM_LINEINDEX, line + 1, 0));
          const end = text.indexOf(rich ? "\r" : "\r\n", value);
          if (next <= value || next >= end) continue;
          const first = position(edit, value),
            continuation = position(edit, next);
          if (Math.abs(first.x - continuation.x) > 2)
            failures.push(
              `${theme} ${scale}% width=${width}: ${label} continuation x=${continuation.x}, value x=${first.x}`,
            );
        }
        const secondSubject = text.indexOf("Second subject line");
        if (secondSubject < 0 || Math.abs(position(edit, secondSubject).x - starts[0]!) > 2)
          failures.push(`${theme} ${scale}% width=${width}: multiline subject escaped the value column`);
        const captureDir = process.env.SUMATRA_PROPERTIES_CAPTURE_DIR;
        if (captureDir && width === 750) {
          mkdirSync(captureDir, { recursive: true });
          sendMessage(edit, EM_SETSEL, 0, 0);
          sendMessage(edit, EM_SCROLLCARET, 0, 0);
          sendMessage(edit, WM_VSCROLL, SB_TOP, 0);
          captureWindowToPng(
            properties,
            join(captureDir, `columns-${theme.includes("Dark") ? "dark" : "light"}-${scale}.png`),
          );
        }
        sendMessage(edit, WM_VSCROLL, SB_TOP, 0);
        const needsScroll = position(edit, text.length - 1).y >= getClientRect(edit).bottom;
        sendMessage(edit, WM_VSCROLL, SB_BOTTOM, 0);
        if (needsScroll && Number(sendMessage(edit, EM_GETFIRSTVISIBLELINE, 0, 0)) <= 0)
          failures.push(`${theme} ${scale}% width=${width}: properties do not scroll to the end`);
        sendMessage(edit, WM_VSCROLL, SB_TOP, 0);
        console.log(`${theme} ${scale}% width=${width}: labels=${ends}; values=${starts}`);
      }
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
  if (failures.length) throw Error("Properties column layout:\n" + failures.join("\n"));
}

if (import.meta.main) await runStandalone(testit);
