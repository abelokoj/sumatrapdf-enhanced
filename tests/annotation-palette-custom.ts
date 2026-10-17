// Preset additions use the original menu; custom picking remains separate.
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlClient, ControlCommand } from "./control.ts";
import { assemblePdf, cmdId, pollUntil, runStandalone, writeAppdata } from "./util.ts";
import {
  clientToScreen,
  enumChildWindows,
  enumWindows,
  findChildWindow,
  findTopWindow,
  getClassName,
  getControlText,
  getWindowLong,
  getPopupMenuHandle,
  getMenuItemCount,
  getMenuItemText,
  packCoords,
  postMessage,
  VK_ESCAPE,
  WM_LBUTTONDOWN,
  WM_LBUTTONUP,
  getWindowPid,
  getWindowRect,
  getWindowText,
  isWindowVisible,
  sendMessage,
  sendText,
  VK_RETURN,
  WM_CLOSE,
  WM_COMMAND,
} from "./winapi.ts";
import { clickAt, killAndWait, launchControlled, pressKey, sendCommandSync, waitForExit } from "./win-automation.ts";

const MODELS = ["RGB", "HEX", "CMYK", "HSV", "HSL"];
const BORDER = "#2468ac";
const HOVER_CLASS = "SumatraToolbarHoverMenu";

function assert(condition: unknown, detail: string): asserts condition {
  if (!condition) throw new Error("annotation-palette-custom: " + detail);
}
async function dump(client: ControlClient): Promise<string> {
  const response = await client.request(ControlCommand.TestToolbarButtons, []);
  assert(response[0] === 0, "Cannot inspect toolbar state");
  return String(response[1] ?? "");
}
async function openShapes(app: Awaited<ReturnType<typeof launchControlled>>): Promise<number> {
  sendCommandSync(app.frame, cmdId("CmdAnnotationShapes"));
  return pollUntil(
    () => findTopWindow(app.proc.pid!, HOVER_CLASS),
    (window) => !!window && isWindowVisible(window),
    { error: "Shapes palette did not open" },
  );
}
async function clickPaletteItem(
  app: Awaited<ReturnType<typeof launchControlled>>,
  text: string,
  menu = false,
): Promise<void> {
  const raw = await dump(app.client);
  const item = [
    ...raw.matchAll(/^dropdown-item idx=\d+ cmd=\d+ current=\d+ rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+) text=(.*)$/gm),
  ].find((match) => match[5] === text);
  assert(item, "Missing palette item '" + text + "'\n" + raw);
  const popup = findTopWindow(app.proc.pid!, HOVER_CLASS);
  assert(popup, "Palette window is missing");
  const origin = clientToScreen(popup, 0, 0);
  const x = (+item[1]! + +item[3]!) / 2 - origin.x;
  const y = (+item[2]! + +item[4]!) / 2 - origin.y;
  if (menu) {
    postMessage(popup, WM_LBUTTONDOWN, 1, packCoords(x, y));
    postMessage(popup, WM_LBUTTONUP, 0, packCoords(x, y));
  } else await clickAt(popup, x, y, 0);
}
function pickerWindow(pid: number, title: string): number {
  let result = 0;
  enumWindows((window) => {
    if (getWindowPid(window) !== pid || !isWindowVisible(window) || getWindowText(window) !== title) return true;
    result = window;
    return false;
  });
  return result;
}
async function picker(pid: number, title: string) {
  const window = await pollUntil(
    () => pickerWindow(pid, title),
    (dialog) => !!dialog,
    { error: "'" + title + "' did not open a full color picker" },
  );
  const combo = findChildWindow(window, "ComboBox");
  assert(
    combo && Number(sendMessage(combo, 0x146, 0, 0)) === MODELS.length,
    "Picker must offer all five editable color models",
  );
  // Every model must expose its actual channel fields, including CMYK's four.
  for (const model of MODELS) {
    sendMessage(combo, 0x14e, MODELS.indexOf(model), 0);
    sendMessage(window, WM_COMMAND, (getWindowLong(combo, -12) & 0xffff) | (1 << 16), combo);
    await pollUntil(
      () => edits(window),
      (inputs) => inputs.length === (model === "HEX" ? 2 : model === "CMYK" ? 5 : 4),
      { error: "Picker did not expose " + model + " channel inputs" },
    );
  }
  sendMessage(combo, 0x14e, MODELS.indexOf("HEX"), 0);
  sendMessage(window, WM_COMMAND, (getWindowLong(combo, -12) & 0xffff) | (1 << 16), combo);
  const inputs = await pollUntil(
    () => edits(window),
    (values) => values.length === 2,
    { error: "Missing HEX/opacity inputs" },
  );
  return { window, hex: inputs[1]!, opacity: inputs[0]! };
}
function edits(dialog: number): number[] {
  const result: number[] = [];
  enumChildWindows(dialog, (window) => {
    if (getClassName(window) === "Edit" && isWindowVisible(window)) result.push(window);
    return true;
  });
  return result.sort(
    (a, b) => getWindowRect(a).top - getWindowRect(b).top || getWindowRect(a).left - getWindowRect(b).left,
  );
}
function paletteContains(settings: string, color: string): boolean {
  const palettes = [...settings.matchAll(/^\s*(?:PresetColors|.*Colors)\s*=\s*(.*)$/gm)]
    .map((match) => match[1])
    .join(" ");
  const rgb = color.slice(1);
  return new RegExp("#(?:ff)?" + rgb + "(?:\\s|$)", "i").test(palettes);
}
export async function testit(): Promise<void> {
  for (const theme of ["Sumatra Light", "Sumatra Dark"]) {
    const dir = writeAppdata(
      "annotation-palette-custom-" + theme.replaceAll(" ", "-"),
      [
        "UiLanguage = en",
        "CheckForUpdates = false",
        "RestoreSession = false",
        "ReuseInstance = false",
        "ShowStartPage = false",
        "Theme = " + theme,
        "Annotations [",
        "\tShapeTool = 2",
        "\tPresetColors = #ffff00 #00ff00 #0000ff #000000",
        "\tSquareColor = #ff0000",
        "\tSquareInteriorColor = #0000ff",
        "\tSquareInteriorOpacity = 33",
        "\tSquareBorderWidth = 2.75",
        "]",
      ].join("\n"),
    );
    const pdf = join(dir, "blank.pdf");
    writeFileSync(
      pdf,
      assemblePdf([
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>",
      ]),
      "latin1",
    );
    let app = await launchControlled(["-appdata", dir, pdf], { saveSettings: true });
    try {
      await app.client.waitForRenderIdle();
      await app.client.setNotificationsEnabled(false);
      const initial = await dump(app.client);
      assert(/annotationButtons=\d+ visible=0/.test(initial), "Annotation toolbar must begin collapsed");
      assert(
        new RegExp("^idx=\\d+ cmd=" + cmdId("CmdAnnotationShapes") + " hidden=0 rect=", "m").test(initial),
        "Shapes must remain in the main toolbar when annotation tools are collapsed",
      );

      for (const label of ["Border", "Fill"]) {
        await openShapes(app);
        await clickPaletteItem(app, label + ": Add a preset color", true);
        const menu = await pollUntil(
          () => findTopWindow(app.proc.pid!, "#32768"),
          (window) => !!window && isWindowVisible(window),
          { error: label + " plus did not open its original color menu" },
        );
        const handle = getPopupMenuHandle(menu);
        const count = getMenuItemCount(handle);
        assert(count > 0, label + " color menu is empty");
        for (let i = 0; i < count; i++) {
          assert(/^#[0-9a-f]{6,8}$/i.test(getMenuItemText(handle, i)), "Unexpected color menu command");
        }
        assert(!pickerWindow(app.proc.pid!, "Add color to tool palette"), "Plus opened the reverted full picker");
        postMessage(menu, 0x0100, VK_ESCAPE, 0);
        postMessage(app.frame, 0x001f, 0, 0);
        await pollUntil(
          () => isWindowVisible(menu),
          (visible) => !visible,
          { error: "Color menu did not dismiss" },
        );
        const popup = findTopWindow(app.proc.pid!, HOVER_CLASS);
        if (popup) sendMessage(popup, WM_CLOSE, 0, 0);
      }
      await openShapes(app);
      await clickPaletteItem(app, "Border: Edit colors");
      const ui = await picker(app.proc.pid!, "Annotation Colors");
      enumChildWindows(ui.window, (control) => {
        assert(getWindowText(control) !== "Keep color in tool palette", "Reverted Keep color checkbox remains");
        return true;
      });
      sendText(ui.hex, BORDER);
      sendText(ui.opacity, "100");
      await pressKey(ui.hex, VK_RETURN, 0);
      await pollUntil(
        () => isWindowVisible(ui.window),
        (visible) => !visible,
        { error: "Color picker did not select" },
      );
      sendMessage(app.frame, WM_CLOSE, 0, 0);
      const closed = await waitForExit(app.proc);
      if (!closed) {
        const windows: string[] = [];
        enumWindows((window) => {
          if (getWindowPid(window) === app.proc.pid && isWindowVisible(window))
            windows.push(getClassName(window) + ": " + getWindowText(window));
          return true;
        });
        console.log("Remaining test windows: " + windows.join(" | "));
      }
      assert(closed, "App did not close to save preferences");
      app.client.close();
      const saved = readFileSync(join(dir, "SumatraPDFEnhanced-settings.txt"), "utf8");
      assert(!paletteContains(saved, BORDER), "Custom picker color was automatically saved into tool palette");
      assert(/SquareColor\s*=\s*#(?:ff)?2468ac/i.test(saved), "Existing custom color selection stopped working");
      assert(/SquareInteriorOpacity\s*=\s*33/.test(saved), "Rollback changed shape fill opacity");
      console.log(theme + ": original plus menus, separate full picker, and no Keep color controls passed");
    } finally {
      app.client.close();
      await killAndWait(app.proc);
    }
  }
}
if (import.meta.main) await runStandalone(testit);
