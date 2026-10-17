import { chmodSync, copyFileSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ptr } from "bun:ffi";
import { cmdId, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled, sendCommand } from "./win-automation.ts";
import {
  enumChildWindows,
  captureWindowToPng,
  enumWindows,
  findChildWindow,
  getClassName,
  getControlText,
  getWindowLong,
  getWindowPid,
  getWindowText,
  isWindowVisible,
  postMessage,
  sendMessage,
} from "./winapi.ts";

function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error("markdown-edit-layout: " + message);
}
function findTop(pid: number, title: string, prefix = false): number {
  let found = 0;
  enumWindows((hwnd) => {
    if (
      getWindowPid(hwnd) === pid &&
      isWindowVisible(hwnd) &&
      (prefix ? getWindowText(hwnd).startsWith(title) : getWindowText(hwnd) === title)
    ) {
      found = hwnd;
      return false;
    }
    return true;
  });
  return found;
}
function button(window: number, text: string): number {
  let found = 0;
  enumChildWindows(window, (hwnd) => {
    if (getClassName(hwnd) === "Button" && getControlText(hwnd).replaceAll("&", "") === text) {
      found = hwnd;
      return false;
    }
    return true;
  });
  return found;
}
function setText(hwnd: number, text: string) {
  const bytes = new Uint16Array(
    [...text]
      .flatMap((c) => {
        const u = c.codePointAt(0)!;
        return u > 65535 ? [0xd800 + ((u - 65536) >> 10), 0xdc00 + ((u - 65536) & 1023)] : [u];
      })
      .concat(0),
  );
  check(sendMessage(hwnd, 0x0c, 0, ptr(bytes)) !== 0, "WM_SETTEXT failed");
}
function encoded(text: string, kind: string): Buffer {
  if (kind === "utf8") return Buffer.from(text);
  if (kind === "bom") return Buffer.concat([Buffer.from([239, 187, 191]), Buffer.from(text)]);
  const bytes = Buffer.from(text, "utf16le");
  if (kind === "be") bytes.swap16();
  return Buffer.concat([Buffer.from(kind === "be" ? [254, 255] : [255, 254]), bytes]);
}
export async function testit() {
  const directory = tmpPath("markdown-edit-layout");
  mkdirSync(directory, { recursive: true });
  const encodings = process.env.SUMATRA_MARKDOWN_ENCODINGS?.split(",") || ["utf8", "bom", "le", "be"];
  for (const kind of encodings) {
    check(["utf8", "bom", "le", "be"].includes(kind), "unknown test encoding");
    const profile = join(directory, kind);
    mkdirSync(profile, { recursive: true });
    writeFileSync(
      join(profile, "SumatraPDFEnhanced-settings.txt"),
      "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nReloadModifiedDocuments = false\nTheme = Sumatra Light\nMarkdownUI [\nUseFixedPageUI = true\n]\n",
    );
    const source = join(profile, "Source α.md");
    const captures = process.env.SUMATRA_MARKDOWN_CAPTURE_DIR;
    if (captures) mkdirSync(captures, { recursive: true });
    const beforeText = "# Original\r\nCafé 日本語\r\n";
    const before = encoded(beforeText, kind);
    writeFileSync(source, before);
    const app = await launchControlled(["-appdata", profile, source]);
    try {
      await app.client.waitForRenderIdle();
      async function open() {
        sendCommand(app.frame, cmdId("CmdEditMarkdown"));
        const window = await pollUntil(
          () => findTop(app.proc.pid!, "Edit Markdown", true),
          (v) => !!v,
          { error: "editor did not open for " + kind },
        );
        const edit = findChildWindow(window, "Edit");
        check(edit && (getWindowLong(edit, -16) & 0x800) === 0, "source is read-only");
        check((getWindowLong(window, -16) & 0x40000) !== 0, "editor cannot resize");
        return { window, edit, save: button(window, "Save") };
      }
      let ui = await open();
      check(ui.save, "accessible Save button missing");
      check(getControlText(ui.edit).includes("日本語"), "encoding decoded incorrectly");
      check(
        getControlText(ui.edit) === beforeText,
        "untouched editor differs from decoded source: " + JSON.stringify(getControlText(ui.edit)),
      );
      postMessage(ui.window, 0x10, 0, 0);
      await pollUntil(
        () => findTop(app.proc.pid!, "Edit Markdown", true),
        (v) => !v,
        { error: "unchanged editor remained open" },
      );
      check(readFileSync(source).equals(before), "no-op edit changed bytes/BOM/EOL");

      ui = await open();
      const afterText = "# Edited\r\nCafé 日本語 αβ\r\n";
      setText(ui.edit, afterText);
      postMessage(ui.save, 0xf5, 0, 0);
      await pollUntil(
        () => readFileSync(source).equals(encoded(afterText, kind)),
        (v) => v,
        { error: "save bytes differ for " + kind },
      );
      postMessage(ui.window, 0x10, 0, 0);
      await pollUntil(
        () => findTop(app.proc.pid!, "Edit Markdown", true),
        (v) => !v,
        { error: "saved editor remained open" },
      );
      await app.client.waitForRenderIdle();
      ui = await open();
      check(getControlText(ui.edit).includes("Edited"), "saved source did not reopen");
      setText(ui.edit, "# Unsaved\r\n");
      postMessage(ui.window, 0x10, 0, 0);
      let prompt = await pollUntil(
        () => findTop(app.proc.pid!, "Unsaved Markdown"),
        (v) => !!v,
        { error: "missing unsaved prompt" },
      );
      postMessage(prompt, 0x111, 2, 0); // Cancel
      await pollUntil(
        () => findTop(app.proc.pid!, "Unsaved Markdown"),
        (v) => !v,
        { error: "cancel prompt remained open" },
      );
      check(
        isWindowVisible(ui.window) && getControlText(ui.edit).includes("Unsaved"),
        "Cancel destroyed editor or edits",
      );
      postMessage(ui.window, 0x10, 0, 0);
      prompt = await pollUntil(
        () => findTop(app.proc.pid!, "Unsaved Markdown"),
        (v) => !!v,
        { error: "missing discard prompt" },
      );
      postMessage(prompt, 0x111, 7, 0); // No/discard
      await pollUntil(
        () => findTop(app.proc.pid!, "Edit Markdown", true),
        (v) => !v,
        { error: "discard did not close" },
      );
      check(readFileSync(source).equals(encoded(afterText, kind)), "discard changed source");

      ui = await open();
      setText(ui.edit, "# Local edit\r\n");
      const external = encoded("# External\r\n", kind);
      writeFileSync(source, external);
      postMessage(ui.save, 0xf5, 0, 0);
      prompt = await pollUntil(
        () => findTop(app.proc.pid!, "Markdown changed"),
        (v) => !!v,
        { error: "external change was not detected" },
      );
      postMessage(prompt, 0x111, 7, 0);
      await pollUntil(
        () => findTop(app.proc.pid!, "Markdown changed"),
        (v) => !v,
        { error: "conflict prompt remained" },
      );
      check(readFileSync(source).equals(external), "declined conflict overwrote external bytes");
      check(getControlText(ui.edit).includes("Local edit"), "conflict lost unsaved buffer");
      setText(ui.edit, getControlText(ui.edit) + " retained");
      postMessage(ui.window, 0x10, 0, 0);
      prompt = await pollUntil(
        () => findTop(app.proc.pid!, "Unsaved Markdown"),
        (v) => !!v,
        { error: "missing final discard" },
      );
      postMessage(prompt, 0x111, 7, 0);
      await pollUntil(
        () => findTop(app.proc.pid!, "Edit Markdown", true),
        (v) => !v,
        { error: "editor did not close" },
      );
      ui = await open();
      setText(ui.edit, "# Read-only edit\r\n");
      chmodSync(source, 0o444);
      try {
        postMessage(ui.save, 0xf5, 0, 0);
        prompt = await pollUntil(
          () => findTop(app.proc.pid!, "Could not save Markdown"),
          (v) => !!v,
          {
            error: "read-only save did not report failure",
          },
        );
        const ok = await pollUntil(
          () => button(prompt, "OK"),
          (v) => !!v,
          { error: "save error OK button missing" },
        );
        postMessage(ok, 0xf5, 0, 0);
        await pollUntil(
          () => findTop(app.proc.pid!, "Could not save Markdown"),
          (v) => !v,
          {
            error: "read-only error remained open",
          },
        );
        check(readFileSync(source).equals(external), "failed save changed original bytes");
        check(getControlText(ui.edit).includes("Read-only edit"), "failed save lost the unsaved buffer");
      } finally {
        chmodSync(source, 0o666);
      }
      postMessage(ui.window, 0x10, 0, 0);
      prompt = await pollUntil(
        () => findTop(app.proc.pid!, "Unsaved Markdown"),
        (v) => !!v,
        {
          error: "failed-save editor lost dirty state",
        },
      );
      postMessage(prompt, 0x111, 7, 0);
      await pollUntil(
        () => findTop(app.proc.pid!, "Edit Markdown", true),
        (v) => !v,
        {
          error: "failed-save editor did not close after discard",
        },
      );
      console.log(
        "PASS",
        kind,
        ": no-op, exact save bytes, reopen, unsaved Cancel/Discard, external-conflict and failed-save preservation",
      );
      if (captures) copyFileSync(source, join(captures, kind + "-saved.md"));
    } catch (error) {
      enumWindows((window) => {
        if (getWindowPid(window) === app.proc.pid! && isWindowVisible(window)) {
          console.log("Failure window:", getWindowText(window));
          const edit = findChildWindow(window, "Edit");
          if (edit) console.log("Failure editor text:", JSON.stringify(getControlText(edit)));
          captureWindowToPng(window, join(captures || profile, kind + "-failure-" + window + ".png"));
        }
        return true;
      });
      throw error;
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
