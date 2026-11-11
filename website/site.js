const REPOSITORY = "abelokoj/sumatrapdf-enhanced";
const RELEASES_URL = `https://github.com/${REPOSITORY}/releases`;
const LATEST_RELEASE_URL = `${RELEASES_URL}/latest`;
const PACKAGE_TYPES = ["install.exe", "portable.exe", "portable.zip"];
const ARCHITECTURES = ["x64", "arm64"];

let release = null;

function parseRelease(latest) {
  if (
    typeof latest?.tag_name !== "string" ||
    latest.draft !== false ||
    latest.prerelease !== false ||
    !Array.isArray(latest.assets)
  )
    return null;

  const match = /^enhanced-(v\d+\.\d+\.\d+|(\d{4}-\d{2}-\d{2})\.\d+)$/.exec(latest.tag_name);
  if (!match) return null;
  if (match[2]) {
    const date = Date.parse(match[2]);
    if (!Number.isFinite(date) || new Date(date).toISOString().slice(0, 10) !== match[2]) return null;
  }

  const version = match[1];
  const assets = [];
  for (const architecture of ARCHITECTURES) {
    for (const type of PACKAGE_TYPES) {
      const name = `SumatraPDF-Enhanced-${version}-${architecture}-${type}`;
      const asset = latest.assets.find(
        (item) =>
          item?.name === name &&
          item.state === "uploaded" &&
          item.browser_download_url === `${RELEASES_URL}/download/${latest.tag_name}/${name}`,
      );
      if (!asset) return null;
      assets.push(asset);
    }
  }
  return { version, tag: latest.tag_name, assets };
}

function updateDownloads() {
  const architecture = document.querySelector('input[name="architecture"]:checked').value;

  document.querySelectorAll("[data-package]").forEach((link) => {
    const type = link.dataset.package;
    const assetName = release && `SumatraPDF-Enhanced-${release.version}-${architecture}-${type}`;
    const asset = release?.assets.find((item) => item.name === assetName);
    link.href = asset?.browser_download_url ?? LATEST_RELEASE_URL;
    link.setAttribute(
      "aria-label",
      asset
        ? `Download ${link.querySelector("strong").textContent} for Windows ${architecture}, ${release.version}`
        : `View the latest stable release for ${link.querySelector("strong").textContent} for Windows ${architecture}`,
    );
  });

  document.getElementById("release-version").textContent = release?.version ?? "Latest stable";
}

async function loadRelease() {
  try {
    const response = await fetch(`https://api.github.com/repos/${REPOSITORY}/releases/latest`, {
      cache: "no-store",
      headers: { Accept: "application/vnd.github+json" },
      signal: globalThis.AbortSignal?.timeout?.(8000),
    });
    if (response.ok) release = parseRelease(await response.json());
  } catch {
    // Keep the official stable release page available if metadata cannot be loaded.
  }

  updateDownloads();
  const status = document.getElementById("release-status");
  status.replaceChildren(
    document.createTextNode(
      release ? `Latest stable release: ${release.version}. ` : "Could not check the latest stable version. ",
    ),
  );
  const notes = document.createElement("a");
  notes.href = release ? `${RELEASES_URL}/tag/${release.tag}` : LATEST_RELEASE_URL;
  notes.textContent = release ? "Read the release notes" : "View the latest stable release for downloads";
  status.append(notes, document.createTextNode("."));
}

const stage = document.querySelector(".document-stage");
const demoStatus = document.getElementById("demo-status");
const saveButton = document.getElementById("save-word");
const modeDescriptions = {
  read: "A quiet space for your next good read.",
  highlight: "Example: mark a word with your highlighter.",
  dictionary: "Example: an offline definition, beside your reading.",
};

document.querySelectorAll("[data-mode][aria-pressed]").forEach((button) => {
  button.addEventListener("click", () => {
    const mode = button.dataset.mode;
    stage.dataset.mode = mode;
    document.querySelector(".definition").hidden = mode !== "dictionary";
    document.querySelector(".pen-palette").hidden = mode !== "highlight";
    document.querySelectorAll("[data-mode][aria-pressed]").forEach((item) => {
      item.setAttribute("aria-pressed", String(item === button));
    });
    demoStatus.textContent = modeDescriptions[mode];
  });
});

saveButton.addEventListener("click", () => {
  const saved = saveButton.getAttribute("aria-pressed") !== "true";
  saveButton.setAttribute("aria-pressed", String(saved));
  saveButton.textContent = saved ? "✓ Saved in this demo" : "+ Save to vocabulary";
  demoStatus.textContent = saved ? "Example word saved for this preview session." : modeDescriptions.dictionary;
});

document.querySelectorAll('input[name="architecture"]').forEach((input) => {
  input.addEventListener("change", updateDownloads);
});

updateDownloads();
loadRelease();
