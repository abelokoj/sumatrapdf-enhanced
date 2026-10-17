import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, type ControlClient } from "./control.ts";
import { cmdId, makePdf, pollUntil, runStandalone, tmpPath } from "./util.ts";
import {
  findCanvas,
  killAndWait,
  launchControlled,
  openContextMenu,
  sendCommandSync,
  waitForContextMenu,
  waitForExit,
} from "./win-automation.ts";
import {
  clientToScreen,
  getMenuItemCount,
  getMenuItemText,
  getPopupMenuHandle,
  isWindowVisible,
  postMessage,
  sendMessage,
  setProcessDpiAware,
  VK_ESCAPE,
} from "./winapi.ts";

function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`home-completed-documents: ${message}`);
}

function field(settingsPath: string, path: string, name: string): string {
  const data = readFileSync(settingsPath, "utf8");
  const start = data.indexOf(`FilePath = ${path}`);
  assert(start >= 0, `Saved history lost ${path}`);
  const next = data.indexOf("FilePath = ", start + 11);
  const block = data.slice(start, next < 0 ? data.length : next);
  return new RegExp(`^\\s*${name}\\s*=\\s*(.*)$`, "m").exec(block)?.[1]?.trim() || "false";
}

async function selected(client: ControlClient, path?: string) {
  return pollUntil(
    () => client.homeSelection(),
    (s) => s.ready && s.entries === 3 && (!path || s.path === path),
    { error: (s) => `Home selection did not settle: ${s.raw}` },
  );
}

async function menuAction(client: ControlClient, frame: number, label: string, apply = true) {
  const selection = await selected(client);
  const canvas = findCanvas(frame);
  assert(canvas, "Home canvas missing");
  const [x, y, dx, dy] = selection.outline;
  if (dx > 0 && dy > 0) {
    const point = clientToScreen(canvas, x + Math.floor(dx / 2), y + Math.floor(dy / 2));
    openContextMenu(canvas, point.x, point.y);
  } else {
    openContextMenu(canvas, -1, -1);
  }
  const popup = await waitForContextMenu(5000);
  assert(popup, "Document context menu did not open");
  const handle = getPopupMenuHandle(popup);
  const items: string[] = [];
  let index = -1;
  for (let i = 0; i < getMenuItemCount(handle); i++) {
    const text = getMenuItemText(handle, i);
    if (text) items.push(text);
    if (text === label) index = i;
  }
  assert(index >= 0, `Expected '${label}', menu has ${items.join(" | ")}`);
  postMessage(popup, 0x0100, VK_ESCAPE, 0);
  await pollUntil(
    () => isWindowVisible(popup),
    (visible) => !visible,
    { error: "Context menu did not close" },
  );
  if (apply) {
    // Exercise the same action as the native menu without desktop mouse input.
    const result = await client.request(ControlCommand.TestHomeSelection, [
      "complete-selected",
      label.startsWith("Move") ? 1 : 0,
    ]);
    assert(result[0] === 0, `Completion action failed: ${JSON.stringify(result)}`);
  }
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const base = process.env.SUMATRA_HOME_COMPLETED_DIR || tmpPath("home-completed-documents");
  mkdirSync(base, { recursive: true });
  const first = join(base, "pinned-reader.pdf");
  const second = join(base, "recent-reader.pdf");
  const completed = join(base, "finished-reader.pdf");
  const bytes = Buffer.from(makePdf(2, 612, 0, 0), "latin1");
  for (const path of [first, second, completed]) writeFileSync(path, bytes);
  const settings = join(base, "SumatraPDFEnhanced-settings.txt");
  writeFileSync(
    settings,
    `UiLanguage = en\nCheckForUpdates = false\nReuseInstance = false\nRestoreSession = false\nRememberOpenedFiles = true\nRememberStatePerDocument = false\nShowStartPage = true\nHomePageViewMode = list\nFileStates [\n [\n  FilePath = ${first}\n  IsPinned = true\n  OpenCount = 10\n ]\n [\n  FilePath = ${second}\n  OpenCount = 9\n ]\n [\n  FilePath = ${completed}\n  OpenCount = 8\n  Completed = true\n ]\n]\n`,
  );
  let app = await launchControlled(["-appdata", base], { saveSettings: true });
  try {
    await app.client.homeSelection("list");
    sendMessage(findCanvas(app.frame), 0x0100, 0x24, 0);
    const before = await selected(app.client, first);
    await menuAction(app.client, app.frame, "Move to Completed Documents", false);
    assert(field(settings, first, "Completed") === "false", "Dismissed menu changed completion metadata");
    await menuAction(app.client, app.frame, "Move to Completed Documents");
    await selected(app.client, first);
    await pollUntil(
      () => field(settings, first, "Completed"),
      (v) => v === "true",
      { error: "Completion did not save" },
    );
    assert(field(settings, first, "IsPinned") === "true", "Completion changed the pin");
    assert(field(settings, second, "Completed") === "false", "Completion changed another recent document");
    assert(field(settings, completed, "Completed") === "true", "Completion changed another completed document");
    assert(
      (await selected(app.client, first)).sel !== before.sel,
      "Completed document did not move to its grouped section",
    );
    await menuAction(app.client, app.frame, "Return to Recent Documents", false);
    sendCommandSync(app.frame, cmdId("CmdExit"));
    assert(await waitForExit(app.proc), "First app did not exit to persist completion");
    app.client.close();
    app = await launchControlled(["-appdata", base], { saveSettings: true });
    await app.client.homeSelection("list");
    sendMessage(findCanvas(app.frame), 0x0100, 0x24, 0);
    let selection = await selected(app.client);
    for (let i = 0; i < 3 && selection.path !== first; i++) {
      sendMessage(findCanvas(app.frame), 0x0100, 0x28, 0);
      selection = await selected(app.client);
    }
    assert(selection.path === first, "Restart lost the completed document");
    await menuAction(app.client, app.frame, "Return to Recent Documents");
    await selected(app.client, first);
    await pollUntil(
      () => field(settings, first, "Completed"),
      (v) => v === "false",
      { error: "Return to Recent did not save" },
    );
    assert((await selected(app.client, first)).sel === before.sel, "Return did not restore the recent group order");
    assert(field(settings, first, "IsPinned") === "true", "Return changed the pin");
    for (const path of [first, second, completed])
      assert(readFileSync(path).equals(bytes), "Completion action changed document contents");
    writeFileSync(
      join(base, "result.txt"),
      "PASS native menu labels and cancellation; shared completion actions, selection, grouping, pins, persistence, restart, unchanged PDF content (no desktop input)\n",
    );
  } finally {
    app.client.close();
    await killAndWait(app.proc);
  }
}

if (import.meta.main) await runStandalone(testit);
