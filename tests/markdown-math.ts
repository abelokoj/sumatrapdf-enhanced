import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { ControlCommand } from "./control.ts";
import { pollUntil, runStandalone, tmpPath } from "./util.ts";
import { killAndWait, launchControlled } from "./win-automation.ts";
import { Page, browserEnv, browserPort, restoreBrowserPolicy } from "./markdown-display-fonts.ts";
function check(ok: unknown, message: string): asserts ok {
  if (!ok) throw new Error("markdown-math: " + message);
}
export async function testit() {
  const dir = tmpPath("markdown-math");
  mkdirSync(dir, { recursive: true });
  const tick = String.fromCharCode(96);
  const markdown = [
    "# Offline mathematics",
    "Inline $x^2+y^2=z^2$ and slash notation \\(a+b\\).",
    "$$\\frac{1}{2}$$",
    "$$\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}$$",
    "$$\\begin{aligned}a&=b\\\\c&=d\\end{aligned}$$",
    "Unsupported $\\DefinitelyNotACommand{x}$",
    "Expansion limit $\\def\\loop{\\loop}\\loop$",
    "Untrusted $\\href{javascript:alert(1)}{click}$",
    "Price $5.00 and $10.00. Escaped \\$5.00.",
    "Code " + tick + "$literal$" + tick + ".",
    tick.repeat(3) + "tex\n$fenced$\n" + tick.repeat(3),
    "    $indented$",
    "<script>window.untrustedMarkdownExecuted = true;</script>",
    ...Array.from({ length: 500 }, (_, i) => "Equation " + i + ": $x_{" + i + "}+1$."),
    "## End\nLast paragraph.",
  ].join("\n\n");
  for (const [theme, scale] of [
    ["Light", 100],
    ["Dark", 200],
  ] as const) {
    const profile = join(dir, theme);
    mkdirSync(profile, { recursive: true });
    const source = join(profile, "Math.md");
    writeFileSync(source, markdown);
    const original = readFileSync(source);
    writeFileSync(
      join(profile, "SumatraPDFEnhanced-settings.txt"),
      "UiLanguage = en\nCheckForUpdates = false\nRestoreSession = false\nDocumentColorsFollowTheme = smart\nTheme = Sumatra " +
        theme +
        "\nInterfaceScale = " +
        scale +
        "\nMarkdownUI [\nUseFixedPageUI = false\n]\n",
    );
    const browser = join(profile, "browser");
    mkdirSync(browser, { recursive: true });
    const app = await launchControlled(["-appdata", profile, source], { env: browserEnv(browser) });
    let page: Page | undefined;
    try {
      page = await Page.connect(await browserPort(browser));
      await page.command("Log.enable");
      await page.until("document.documentElement && document.documentElement.dataset.mathReady === '1'");
      const first = await page.eval("window.enhancedMathState");
      check(
        (await page.eval("getComputedStyle(document.body).backgroundColor")) ===
          (theme === "Dark" ? "rgb(30, 30, 30)" : "rgb(255, 255, 255)"),
        "document theme did not apply",
      );
      check(first.total === 508, "equation recognition count: " + JSON.stringify(first));
      check(
        first.complete === first.total && first.errors >= 2,
        "malformed equations not isolated: " + JSON.stringify(first),
      );
      check(
        await page.eval("document.querySelectorAll('[data-rendered=\"1\"] .katex-mathml math').length>=505"),
        "valid math lacks accessible MathML",
      );
      check(
        await page.eval(
          "Array.from(document.querySelectorAll('code')).map(e=>e.textContent).join('|').includes('$literal$') && Array.from(document.querySelectorAll('code')).some(e=>e.textContent.includes('$fenced$')) && Array.from(document.querySelectorAll('code')).some(e=>e.textContent.includes('$indented$'))",
        ),
        "literal code was typeset",
      );
      check(
        await page.eval("document.body.textContent.includes('Price $5.00 and $10.00. Escaped $5.00.')"),
        "currency changed",
      );
      check(
        await page.eval(
          "Array.from(document.querySelectorAll('.enhanced-math-error')).some(e=>e.textContent.includes('DefinitelyNotACommand'))",
        ),
        "unsupported source not readable",
      );
      check(
        await page.eval("!window.untrustedMarkdownExecuted && !document.querySelector('a[href^=\"javascript:\"]')"),
        "untrusted Markdown/math became active content",
      );
      await page.command("Network.enable");
      await page.command("Network.clearBrowserCache");
      await page.command("Network.setCacheDisabled", { cacheDisabled: true });
      await page.command("Network.emulateNetworkConditions", {
        offline: true,
        latency: 0,
        downloadThroughput: 0,
        uploadThroughput: 0,
      });
      await page.command("Page.addScriptToEvaluateOnNewDocument", {
        source:
          "window.mathTickIntervals=[];window.mathTickLast=performance.now();window.mathTickTimer=setInterval(function(){var n=performance.now();window.mathTickIntervals.push(n-window.mathTickLast);window.mathTickLast=n;},10);",
      });
      const started = Date.now();
      await page.command("Page.enable");
      const oldLoader = (await page.command("Page.getFrameTree")).frameTree.frame.loaderId;
      await page.command("Page.reload", { ignoreCache: true });
      await pollUntil(
        async () => (await page!.command("Page.getFrameTree")).frameTree.frame.loaderId,
        (loader) => loader !== oldLoader,
        { error: "math reload did not start a new document" },
      );
      await page.until(
        "document.documentElement && document.documentElement.dataset.mathReady === '1' && window.mathTickIntervals && document.querySelector('main') && document.querySelector('main').textContent.includes('Last paragraph.')",
      );
      await page.eval("document.fonts.ready");
      const elapsed = Date.now() - started;
      const state = await page.eval("window.enhancedMathState");
      check(state.complete === 508, "offline reload did not render all equations");
      const resources = await page.eval("performance.getEntriesByType('resource').map(e=>e.name)");
      check(
        resources.some((s: string) => s.endsWith("/katex/katex.min.js")) &&
          resources.some((s: string) => s.includes("/katex/fonts/")),
        "local math runtime/fonts were not requested",
      );
      check(
        resources.every((s: string) => s.startsWith("https://sumatrapdf.markdown/")),
        "Markdown required public resources while offline",
      );
      const timing = await page.eval(
        "(()=>{clearInterval(window.mathTickTimer);var a=window.mathTickIntervals;return {ticks:a.length,max:a.length?Math.max(...a):0,mean:a.reduce((s,x)=>s+x,0)/Math.max(1,a.length)};})()",
      );
      check(timing.ticks > 0 && timing.max < 5000, "large math prevented timer progress: " + JSON.stringify(timing));
      const pingStart = Date.now();
      await app.client.request(ControlCommand.Ping, []);
      const nativePing = Date.now() - pingStart;
      await page.command("Emulation.setDeviceMetricsOverride", {
        width: 640,
        height: 800,
        deviceScaleFactor: 1,
        mobile: false,
      });
      await page.eval("window.scrollTo(0,document.body.scrollHeight)");
      await pollUntil(
        async () => await page!.eval("window.scrollY"),
        (v) => v > 0,
        { error: "large math document did not scroll" },
      );
      check(
        await page.eval("document.documentElement.scrollWidth<=document.documentElement.clientWidth+1"),
        "math overflows the narrow page",
      );
      check(readFileSync(source).equals(original), "viewing math changed source bytes");
      const captures = process.env.SUMATRA_MARKDOWN_CAPTURE_DIR;
      if (captures) {
        mkdirSync(captures, { recursive: true });
        await page.capture(join(captures, "math-" + theme + ".png"));
        await page.eval("window.scrollTo(0,0);new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))");
        await page.capture(join(captures, "math-top-" + theme + ".png"));
      }
      console.log(
        JSON.stringify({
          theme,
          scale,
          equations: state.total,
          errors: state.errors,
          offlineReloadMs: elapsed,
          timing,
          nativePingMs: nativePing,
        }),
      );
    } catch (error) {
      if (page) {
        console.log(
          "Math failure state:",
          JSON.stringify(
            await page.eval(
              "({ready:document.readyState,katex:typeof katex,state:window.enhancedMathState,math:document.querySelectorAll('.enhanced-math').length,scripts:Array.from(document.scripts).map(s=>s.src),resources:performance.getEntriesByType('resource').map(e=>({name:e.name,size:e.decodedBodySize})),content:document.querySelector('main')?.innerHTML.slice(0,1500)})",
            ),
          ),
        );
        console.log("Browser log:", JSON.stringify(page.logs));
        const captures = process.env.SUMATRA_MARKDOWN_CAPTURE_DIR;
        if (captures) {
          mkdirSync(captures, { recursive: true });
          await page.capture(join(captures, "math-failure-" + theme + ".png"));
        }
      }
      throw error;
    } finally {
      page?.close();
      app.client.close();
      await killAndWait(app.proc);
      restoreBrowserPolicy();
    }
  }
}
if (import.meta.main) await runStandalone(testit);
