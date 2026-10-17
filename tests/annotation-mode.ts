import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand, type LayoutInfo, type ControlClient } from "./control.ts";
import { cmdId, makePdf, pollUntil, runStandalone, tmpPath } from "./util.ts";
import { findCanvas, findChildByClass, killAndWait, launchControlled, sendCommandSync } from "./win-automation.ts";
import {
  captureWindowToPng,
  clientToScreen,
  getClientRect,
  getScrollInfo,
  getWindowLong,
  getWindowRect,
  GWL_STYLE,
  isZoomed,
  moveWindow,
  packCoords,
  postMessage,
  sendMessage,
  setProcessDpiAware,
  VK_ESCAPE,
  WS_CAPTION,
} from "./winapi.ts";

function assert(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error(`annotation-mode: ${message}`);
}

async function toolbarState(client: ControlClient) {
  const response = await client.request(ControlCommand.TestToolbarButtons, []);
  const raw = String(response[1] ?? "");
  const mode = /^annotationMode=(\d) dock=(\d) vertical=(\d)$/m.exec(raw);
  const button = /^modeButton cmd=\d+ hidden=(\d) rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)$/m.exec(raw);
  const columns = /^annotationColumns=(\d) scroll=(\d+),(\d+) viewport=(\d+)$/m.exec(raw);
  const native = /^toolbarHwnd=(\S+)/m.exec(raw);
  assert(mode && button && columns, "Toolbar state did not report Annotation Mode");
  return {
    active: mode[1] === "1",
    dock: Number(mode[2]),
    vertical: mode[3] === "1",
    columns: Number(columns[1]),
    hwnd: native ? Number.parseInt(native[1]!, 16) : 0,
    scroll: Number(columns[2]),
    maxScroll: Number(columns[3]),
    buttonVisible: button[1] === "0",
    button: { x: Number(button[2]), y: Number(button[3]), right: Number(button[4]), bottom: Number(button[5]) },
    raw,
  };
}

async function waitMode(client: ControlClient, active: boolean, dock?: number) {
  return pollUntil(
    () => toolbarState(client),
    (state) => state.active === active && (dock === undefined || state.dock === dock),
    { error: (state) => `Mode/dock did not settle: ${state.raw}` },
  );
}

function sameRect(a: unknown, b: unknown) {
  return JSON.stringify(a) === JSON.stringify(b);
}

function assertDock(layout: LayoutInfo, position: number) {
  const toolbar = layout.items.toolbar;
  const canvas = layout.items.canvas;
  assert(toolbar?.visible && canvas?.visible, "Docked toolbar or document canvas is hidden");
  const t = toolbar.rect;
  const c = canvas.rect;
  assert(t.dx > 0 && t.dy > 0 && c.dx > 0 && c.dy > 0, "Dock collapsed the toolbar or document");
  if (position === 0) assert(t.y + t.dy <= c.y, "Top toolbar overlaps the document");
  if (position === 1) assert(t.y >= c.y + c.dy, "Bottom toolbar overlaps the document");
  if (position === 2) assert(t.x + t.dx <= c.x, "Left toolbar overlaps the document");
  if (position === 3) assert(t.x >= c.x + c.dx, "Right toolbar overlaps the document");
  if (position >= 2) assert(t.dy > t.dx, "Side dock did not use a vertical toolbar");
  assert(!layout.items.tabs?.visible, "Filename tabs remain visible in Annotation Mode");
  const caption = layout.nodes.find((node) => node.path === "caption");
  assert(caption && !caption.visible, "Title bar remains visible in Annotation Mode");
  assert(
    layout.nodes.filter((node) => node.kind === "captionBtn").every((node) => !node.visible),
    "Hidden title-bar buttons remain exposed in the control tree",
  );
  assert(
    !layout.items.menu?.visible,
    `Menu bar remains visible in Annotation Mode: ${JSON.stringify(layout.items.menu)}`,
  );
  const top = position === 0 ? t.y : c.y;
  assert(top <= 1, "Hidden title bar still reserves a row above the tools/document");
}

function dragDock(frame: number, toolbar: number, position: number) {
  const bounds = getClientRect(toolbar);
  const vertical = bounds.bottom - bounds.top > bounds.right - bounds.left;
  const x = vertical ? Math.floor((bounds.right - bounds.left) / 2) : 7;
  const y = vertical ? 7 : Math.floor((bounds.bottom - bounds.top) / 2);
  const client = getClientRect(frame);
  const width = client.right - client.left;
  const height = client.bottom - client.top;
  const targets = [
    { x: Math.floor(width / 2), y: 1 },
    { x: Math.floor(width / 2), y: height - 1 },
    { x: 1, y: Math.floor(height / 2) },
    { x: width - 1, y: Math.floor(height / 2) },
  ];
  const screen = clientToScreen(frame, targets[position]!.x, targets[position]!.y);
  const origin = clientToScreen(toolbar, 0, 0);
  const target = { x: screen.x - origin.x, y: screen.y - origin.y };
  sendMessage(toolbar, 0x0201, 1, packCoords(x, y));
  sendMessage(toolbar, 0x0200, 1, packCoords(target.x, target.y));
  sendMessage(toolbar, 0x0202, 0, packCoords(target.x, target.y));
}

export async function testit(): Promise<void> {
  setProcessDpiAware();
  const base = process.env.SUMATRA_ANNOTATION_MODE_DIR || tmpPath("annotation-mode");
  mkdirSync(base, { recursive: true });
  const pdf = join(base, "markup.pdf");
  const secondPdf = join(base, "second.pdf");
  writeFileSync(pdf, makePdf(96, 612, 0, 0), "latin1");
  writeFileSync(secondPdf, makePdf(24, 612, 0, 0), "latin1");
  const configurations = [
    {
      name: "light-top",
      position: "top",
      mode: "show",
      titlebar: true,
      custom: "ShowMenubar = true\nShowMenubarWithTabs = true\n",
      theme: "Sumatra Light",
      columns: 1,
    },
    {
      name: "dark-bottom-hidden",
      position: "bottom",
      mode: "hide",
      titlebar: false,
      custom:
        "ToolbarCustomLayout = CmdOpenFile CmdZoomIn PageInfo CmdCreateAnnotInk\nToolbarHiddenItems = CmdCreateAnnotInk\nShowMenubar = true\nShowMenubarWithTabs = true\n",
      theme: "Sumatra Dark",
      columns: 2,
    },
    {
      name: "light-overlay",
      position: "top",
      mode: "overlay",
      titlebar: true,
      custom: "",
      theme: "Sumatra Light",
      columns: 3,
    },
  ];
  for (const config of configurations) {
    const directory = join(base, config.name);
    mkdirSync(directory, { recursive: true });
    writeFileSync(
      join(directory, "SumatraPDFEnhanced-settings.txt"),
      `UiLanguage = en\nReuseInstance = false\nRestoreSession = false\nCheckForUpdates = false\nUseTabs = true\nTabsInTitlebar = ${config.titlebar}\nTheme = ${config.theme}\nToolbar = ${config.mode}\nToolbarPosition = ${config.position}\nAnnotationToolbarColumns = ${config.columns}\nFullscreen [\n Toolbar = hide\n]\n${config.custom}`,
    );
    const app = await launchControlled(["-appdata", directory, pdf, secondPdf]);
    const { client, frame } = app;
    try {
      await client.waitForRenderIdle();
      sendCommandSync(frame, cmdId("CmdZoomFitWidthAndContinuous"));
      await client.waitForRenderIdle();
      const before = await client.layout();
      const originalFrame = getWindowRect(frame);
      const originalStyle = getWindowLong(frame, GWL_STYLE);
      const originalOrigin = clientToScreen(frame, 0, 0);
      let toolbar = findChildByClass(frame, "SUMATRA_VIRT_TOOLBAR");
      const canvas = findCanvas(frame);
      assert(toolbar && canvas, "Native toolbar or canvas is missing");
      assert(!(await toolbarState(client)).active, "Mode was active at startup");
      assert(before.items.tabs?.visible, "Fixture did not start with filename tabs");
      const displayBefore = await client.request(ControlCommand.TestDisplayMode, []);
      sendCommandSync(frame, cmdId("CmdAnnotationMode"));
      const entered = await waitMode(client, true, config.position === "bottom" ? 1 : 0);
      await client.waitForRenderIdle();
      toolbar = findChildByClass(frame, "SUMATRA_VIRT_TOOLBAR");
      assert(entered.buttonVisible, "Mode exit icon is hidden");
      assert(!(getWindowLong(frame, GWL_STYLE) & WS_CAPTION), "Native title-bar style remains enabled");
      if (config.custom.includes("ToolbarHiddenItems")) {
        const hiddenPen = new RegExp(`^idx=\\d+ cmd=${cmdId("CmdCreateAnnotInk")} hidden=1 `, "m");
        assert(hiddenPen.test(entered.raw), "Entering mode made a user-hidden pen visible");
      }
      assert(sameRect(getWindowRect(frame), originalFrame), "Entering mode resized or moved the window");
      const displayAfter = await client.request(ControlCommand.TestDisplayMode, []);
      assert(sameRect(displayAfter, displayBefore), "Entering mode changed document layout or zoom");
      assertDock(await client.layout(), entered.dock);
      sendCommandSync(frame, cmdId("CmdToggleMenuBar"));
      assertDock(await client.layout(), entered.dock);
      const origin = clientToScreen(frame, 0, 0);
      assert(
        origin.y - originalFrame.top <= Math.max(1, origin.x - originalFrame.left),
        "Native title bar still reserves non-client height",
      );
      for (const position of [2, 3, 1, 0]) {
        toolbar = (await toolbarState(client)).hwnd || findChildByClass(frame, "SUMATRA_VIRT_TOOLBAR");
        dragDock(frame, toolbar, position);
        const state = await waitMode(client, true, position);
        await pollUntil(
          () => client.layout(),
          (layout) => {
            try {
              assertDock(layout, position);
              return true;
            } catch {
              return false;
            }
          },
          { error: "Toolbar edge did not settle" },
        );
        assert(state.buttonVisible, "Docking hid the exit icon");
        toolbar = state.hwnd || findChildByClass(frame, "SUMATRA_VIRT_TOOLBAR");
        assert(state.vertical === position >= 2, "Dock orientation is wrong");
        assert(state.columns === config.columns, "Advanced Settings column count was not applied");
        if (position >= 2) {
          const fixed = state.button;
          const canvasScroll = getScrollInfo(canvas).pos;
          for (let step = 0; step < 90; step++) {
            sendMessage(toolbar, 0x020a, (0xff88 << 16) >>> 0, 0);
          }
          const end = await toolbarState(client);
          assert(
            end.scroll === end.maxScroll,
            `Vertical tools cannot scroll to the end: ${end.scroll}/${end.maxScroll}\n${end.raw}`,
          );
          assert(sameRect(end.button, fixed), "Scrolling moved the pinned exit button");
          assert(getScrollInfo(canvas).pos === canvasScroll, "Toolbar wheel input scrolled the document");
          for (let step = 0; step < 90; step++) sendMessage(toolbar, 0x020a, 120 << 16, 0);
          const top = await toolbarState(client);
          assert(top.scroll === 0, "Vertical tools cannot scroll back to the beginning");
          const rows = new Map<number, number>();
          for (const cell of top.raw.matchAll(/^toolCell cmd=\d+ rect=(-?\d+),(-?\d+),(\d+),(\d+)$/gm)) {
            const center = Number(cell[2]) + Number(cell[4]) / 2;
            rows.set(center, (rows.get(center) ?? 0) + 1);
          }
          assert(
            [...rows.values()].every((count) => count <= config.columns),
            "Tools exceeded the selected column count",
          );
        }
        assert(sameRect(getWindowRect(frame), originalFrame), "Docking changed window placement");
        const toolbarBounds = getClientRect(toolbar);
        assert(
          state.button.x >= 0 &&
            state.button.y >= 0 &&
            state.button.right <= toolbarBounds.right &&
            state.button.bottom <= toolbarBounds.bottom,
          "Exit icon is clipped",
        );
        sendMessage(canvas, 0x0115, 7, 0);
        await client.waitForRenderIdle();
        const scroll = getScrollInfo(canvas);
        assert(
          scroll.pos === Math.max(scroll.min, scroll.max - scroll.page + 1),
          "Cannot scroll to the document end while docked",
        );
        if (process.env.SUMATRA_ANNOTATION_CAPTURE_DIR) {
          mkdirSync(process.env.SUMATRA_ANNOTATION_CAPTURE_DIR, { recursive: true });
          assert(
            captureWindowToPng(
              frame,
              join(process.env.SUMATRA_ANNOTATION_CAPTURE_DIR, `${config.name}-${position}.png`),
            ),
            "Screenshot failed",
          );
        }
      }
      toolbar = findChildByClass(frame, "SUMATRA_VIRT_TOOLBAR");
      dragDock(frame, toolbar, 2);
      await waitMode(client, true, 2);
      moveWindow(frame, originalFrame.left, originalFrame.top, 560, 420);
      await pollUntil(
        () => client.layout(),
        (layout) => layout.items.frame?.rect.dx === 560,
        { error: "Narrow window resize did not settle" },
      );
      assertDock(await client.layout(), 2);
      assert((await toolbarState(client)).buttonVisible, "Narrow window hid the exit icon");
      moveWindow(
        frame,
        originalFrame.left,
        originalFrame.top,
        originalFrame.right - originalFrame.left,
        originalFrame.bottom - originalFrame.top,
      );
      await pollUntil(
        () => getWindowRect(frame),
        (rect) => sameRect(rect, originalFrame),
        { error: "Window rectangle did not restore after resize" },
      );
      postMessage(frame, 0x0100, VK_ESCAPE, 0);
      await waitMode(client, false, config.position === "bottom" ? 1 : 0);
      await client.waitForRenderIdle();
      const restored = await client.layout();
      assert(restored.items.tabs?.visible, "Escape did not restore filename tabs");
      const captionBefore = before.nodes.find((node) => node.path === "caption");
      const captionRestored = restored.nodes.find((node) => node.path === "caption");
      assert(
        captionBefore && captionRestored && captionBefore.visibility === captionRestored.visibility,
        "Escape did not restore title-bar visibility",
      );
      assert(
        sameRect(restored.items.canvas?.rect, before.items.canvas?.rect),
        "Escape did not restore document canvas geometry",
      );
      if (before.items.toolbar?.visible)
        assert(
          sameRect(restored.items.toolbar?.rect, before.items.toolbar?.rect),
          "Escape did not restore the original toolbar position",
        );
      assert(
        restored.items.toolbar?.visible === before.items.toolbar?.visible,
        "Escape did not restore toolbar visibility",
      );
      assert(sameRect(getWindowRect(frame), originalFrame), "Escape changed window placement");
      assert(getWindowLong(frame, GWL_STYLE) === originalStyle, "Escape did not restore window style");
      assert(sameRect(clientToScreen(frame, 0, 0), originalOrigin), "Escape did not restore native title-bar geometry");
      sendCommandSync(frame, cmdId("CmdAnnotationMode"));
      const state = await waitMode(client, true);
      toolbar = findChildByClass(frame, "SUMATRA_VIRT_TOOLBAR");
      const x = Math.floor((state.button.x + state.button.right) / 2);
      const y = Math.floor((state.button.y + state.button.bottom) / 2);
      sendMessage(toolbar, 0x0201, 1, packCoords(x, y));
      sendMessage(toolbar, 0x0202, 0, packCoords(x, y));
      await waitMode(client, false);
      if (config.name === "light-top") {
        sendCommandSync(frame, cmdId("CmdAnnotationMode"));
        await waitMode(client, true);
        sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
        await pollUntil(
          () => getWindowRect(frame),
          (rect) => !sameRect(rect, originalFrame),
          { error: "Annotation Mode fixture did not enter fullscreen" },
        );
        assertDock(await client.layout(), 0);
        sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
        await pollUntil(
          () => getWindowRect(frame),
          (rect) => sameRect(rect, originalFrame),
          { error: "Leaving fullscreen did not restore the Annotation Mode window" },
        );
        assertDock(await client.layout(), 0);
        assert(!(getWindowLong(frame, GWL_STYLE) & WS_CAPTION), "Leaving fullscreen restored a hidden caption");
        postMessage(toolbar, 0x0100, VK_ESCAPE, 0);
        await waitMode(client, false);
        assert(getWindowLong(frame, GWL_STYLE) === originalStyle, "Nested fullscreen lost the original caption style");
        sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
        await pollUntil(
          () => getWindowRect(frame),
          (rect) => !sameRect(rect, originalFrame),
          { error: "Fullscreen fixture did not enter" },
        );
        const fullscreen = getWindowRect(frame);
        sendCommandSync(frame, cmdId("CmdAnnotationMode"));
        await waitMode(client, true);
        assert(sameRect(getWindowRect(frame), fullscreen), "Annotation Mode moved fullscreen window");
        postMessage(toolbar, 0x0100, VK_ESCAPE, 0);
        await waitMode(client, false);
        assert(sameRect(getWindowRect(frame), fullscreen), "Mode exit left normal fullscreen");
        sendCommandSync(frame, cmdId("CmdToggleFullscreen"));
        await pollUntil(
          () => getWindowRect(frame),
          (rect) => sameRect(rect, originalFrame),
          { error: "Fullscreen did not restore the normal window" },
        );
        sendMessage(frame, 0x0112, 0xf030, 0);
        await pollUntil(() => isZoomed(frame), Boolean, { error: "Maximized fixture did not enter" });
        const maximized = getWindowRect(frame);
        const maximizedStyle = getWindowLong(frame, GWL_STYLE);
        sendCommandSync(frame, cmdId("CmdAnnotationMode"));
        await waitMode(client, true);
        assertDock(await client.layout(), 0);
        assert(isZoomed(frame), "Entering Annotation Mode restored a maximized window");
        assert(sameRect(getWindowRect(frame), maximized), "Entering mode moved the maximized window");
        postMessage(toolbar, 0x0100, VK_ESCAPE, 0);
        await waitMode(client, false);
        assert(isZoomed(frame), "Exiting Annotation Mode restored a maximized window");
        assert(sameRect(getWindowRect(frame), maximized), "Exiting mode moved the maximized window");
        assert(getWindowLong(frame, GWL_STYLE) === maximizedStyle, "Mode exit lost the maximized caption style");
        sendMessage(frame, 0x0112, 0xf120, 0);
      }
      writeFileSync(
        join(directory, "result.txt"),
        `PASS ${config.name}: complete title-bar hiding, tabs, window placement, dock edges, Escape, icon exit, scrolling and preference restore\n`,
      );
    } finally {
      client.close();
      await killAndWait(app.proc);
    }
  }
  console.log(
    "Annotation Mode: all dock edges, tab hiding, both exits, responsive layout, scrolling and preference restore passed",
  );
}

if (import.meta.main) await runStandalone(testit);
