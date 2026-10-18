const { chromium } = require("playwright");
const { resolve } = require("node:path");

let browser;
(async () => {
  const output = resolve(__dirname, "../../build");
  browser = await chromium.launch({ headless: true });
  const page = await browser.newPage({ viewport: { width: 420, height: 680 } });

  await page.goto("http://127.0.0.1:4173/", { waitUntil: "networkidle" });
  await page.waitForSelector(".skill-row");
  await page.evaluate(() => document.fonts.ready);
  const resizeHandles = await page.locator("[data-resize]").evaluateAll((handles) =>
    handles.map((handle) => handle.dataset.resize));
  const expectedResizeHandles = [
    "top", "right", "bottom", "left",
    "top-left", "top-right", "bottom-left", "bottom-right",
  ];
  if (resizeHandles.length !== expectedResizeHandles.length ||
      expectedResizeHandles.some((direction) => !resizeHandles.includes(direction))) {
    throw new Error(`resize handles are incomplete: ${resizeHandles.join(",")}`);
  }
  await page.keyboard.press("Control+K");
  const shortcutFocus = await page.evaluate(() => document.activeElement?.id);
  if (shortcutFocus !== "search-input") {
    throw new Error(`Ctrl+K did not focus search: ${shortcutFocus}`);
  }
  const unavailableWarning = await page.locator("#warning-text").textContent();
  if (!unavailableWarning?.includes("不可用")) {
    throw new Error("unavailable path state is not visible");
  }
  if ((await page.locator(".warning-indicator").count()) < 1) {
    throw new Error("metadata warning state is not visible");
  }
  await page.screenshot({ path: resolve(output, "preview-catalog-420x680.png") });

  const layout = await page.evaluate(() => {
    const shell = document.querySelector(".app-shell").getBoundingClientRect();
    return {
      viewportWidth: window.innerWidth,
      viewportHeight: window.innerHeight,
      shellWidth: shell.width,
      shellHeight: shell.height,
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
    };
  });
  if (layout.horizontalOverflow ||
      layout.shellWidth > layout.viewportWidth ||
      layout.shellHeight > layout.viewportHeight) {
    throw new Error(`catalog overflows viewport: ${JSON.stringify(layout)}`);
  }

  await page.locator("#search-input").fill("前端");
  await page.waitForTimeout(150);
  const filteredRows = await page.locator(".skill-row").count();
  if (filteredRows < 1) {
    throw new Error("search did not return visible rows");
  }
  await page.screenshot({ path: resolve(output, "preview-search-420x680.png") });
  await page.locator("#clear-search").click();

  await page.locator("#collapse-all").click();
  await page.waitForTimeout(100);
  const collapsedCategories = await page.locator(".category.is-collapsed").count();
  const totalCategories = await page.locator(".category").count();
  if (collapsedCategories !== totalCategories ||
      (await page.locator("#collapse-all").textContent())?.trim() !== "展开全部") {
    throw new Error("collapse all did not collapse every category");
  }
  await page.locator("#collapse-all").click();
  await page.waitForTimeout(100);
  if ((await page.locator(".category.is-collapsed").count()) !== 0 ||
      (await page.locator("#collapse-all").textContent())?.trim() !== "折叠全部") {
    throw new Error("expand all did not clear collapsed state");
  }

  const firstRow = page.locator(".skill-row").first();
  const firstPath = await firstRow.getAttribute("data-path");

  await page.locator(".category-header").first().click();
  await page.waitForTimeout(300);
  const collapsed = await page.locator(".category").first().evaluate((node) =>
    node.classList.contains("is-collapsed"));
  if (!collapsed) {
    throw new Error("category collapse did not update the tree");
  }
  await page.locator(".category-header").first().click();
  await page.waitForTimeout(300);

  const sourceRow = page.locator(".skill-row").nth(0);
  await sourceRow.dragTo(page.locator('.category[data-category="backend"]'));
  await page.waitForTimeout(150);
  const movedByDrag = await page
    .locator('.category[data-category="backend"] .skill-row')
    .evaluateAll((rows, path) => rows.some((row) => row.dataset.path === path), firstPath);
  if (!movedByDrag) {
    throw new Error("drag and drop did not move the Skill");
  }

  await page.locator(".skill-row").first().click({ button: "right" });
  if (await page.locator("#context-menu").isHidden()) {
    throw new Error("context menu did not open");
  }
  await page.keyboard.press("Escape");

  await page.locator('.category[data-category="favorite"] .category-header').dblclick();
  const renameInput = page.locator('.category[data-category="favorite"] .category-name-input');
  await renameInput.fill("高频");
  await renameInput.press("Enter");
  await page.waitForFunction(() =>
    document.querySelector('.category[data-category="favorite"] .category-label')
      ?.textContent === "高频");

  await page.locator(".add-category-button").click();
  await page.locator(".add-category-form .category-name-input").fill("设计");
  await page.locator(".add-category-form").press("Enter");
  await page.waitForFunction(() =>
    [...document.querySelectorAll(".category-label")]
      .some((node) => node.textContent === "设计"));

  await page.locator(".skill-row").first().click();
  await page.waitForTimeout(100);
  const toastText = await page.locator("#toast-text").textContent();
  if (!toastText?.includes("/")) {
    throw new Error("copy feedback did not render");
  }

  await page.evaluate(async () => {
    window.SkillBoxPreviewControl.setMonitorDegraded(true);
    window.SkillBoxPreviewControl.catalogChanged();
  });
  await page.waitForFunction(() =>
    document.querySelector("#warning-text")?.textContent?.includes("周期扫描"));
  const degradedWarning = await page.locator("#warning-text").textContent();
  if (!degradedWarning?.includes("周期扫描")) {
    throw new Error("watcher degraded state is not visible");
  }
  await page.evaluate(async () => {
    window.SkillBoxPreviewControl.setMonitorDegraded(false);
    window.SkillBoxPreviewControl.catalogChanged();
  });

  await page.locator("#open-settings").click();
  await page.waitForTimeout(100);
  const settingsFocus = await page.evaluate(() => document.activeElement?.id);
  if (settingsFocus !== "path-input") {
    throw new Error(`settings focus did not move to path input: ${settingsFocus}`);
  }
  await page.screenshot({ path: resolve(output, "preview-settings-420x680.png") });

  await page.locator("#path-input").fill("C:\\Skills");
  await page.locator("#add-path-form button[type=submit]").click();
  await page.waitForFunction(() =>
    document.querySelector("#toast-text")?.textContent?.includes("已存在"));
  await page.locator("#path-input").fill("C:\\New Skills");
  await page.locator("#add-path-form button[type=submit]").click();
  await page.waitForFunction(() => document.querySelectorAll(".path-item").length === 3);
  await page.locator(".path-item").last().locator(".remove-path").click();
  await page.waitForFunction(() => document.querySelectorAll(".path-item").length === 2);

  await page.locator("#tab-api").click();
  await page.locator("#api-key").fill("preview-key");
  await page.locator("#fetch-models").click();
  await page.waitForFunction(() =>
    document.querySelector("#api-status")?.textContent?.includes("获取"));
  await page.locator("#test-model").click();
  await page.waitForFunction(() =>
    document.querySelector("#api-status")?.textContent?.includes("正常"));
  await page.screenshot({ path: resolve(output, "preview-api-420x680.png") });
  await page.setViewportSize({ width: 380, height: 560 });
  await page.waitForTimeout(100);
  await page.screenshot({ path: resolve(output, "preview-api-380x560.png") });
  await page.setViewportSize({ width: 420, height: 680 });
  await page.keyboard.press("Escape");
  if (await page.locator("#settings-view").isVisible()) {
    throw new Error("Escape did not close settings");
  }

  await page.locator("#translate-all").click();
  await page.waitForTimeout(100);
  const batchToast = await page.locator("#toast-text").textContent();
  if (!batchToast?.includes("失败")) {
    throw new Error("batch translation failure summary did not render");
  }

  await page.locator(".skill-row").first().click({ button: "right" });
  if (await page.locator("#retranslate-skill").isHidden()) {
    throw new Error("retranslate action is not available for a translated Skill");
  }
  await page.locator("#retranslate-skill").click();
  await page.waitForTimeout(100);

  await page.setViewportSize({ width: 380, height: 560 });
  await page.waitForTimeout(100);
  const minLayout = await page.evaluate(() => {
    const shell = document.querySelector(".app-shell").getBoundingClientRect();
    return {
      viewportWidth: window.innerWidth,
      viewportHeight: window.innerHeight,
      shellWidth: shell.width,
      shellHeight: shell.height,
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth,
    };
  });
  if (minLayout.horizontalOverflow ||
      minLayout.shellWidth > minLayout.viewportWidth ||
      minLayout.shellHeight > minLayout.viewportHeight) {
    throw new Error(`minimum layout overflows viewport: ${JSON.stringify(minLayout)}`);
  }

  await page.reload({ waitUntil: "networkidle" });
  await page.waitForSelector(".skill-row");
  await page.evaluate(() => document.fonts.ready);
  await page.setViewportSize({ width: 420, height: 680 });
  await page.waitForTimeout(100);
  await page.screenshot({ path: resolve(output, "preview-catalog-420x680.png") });

  await page.setViewportSize({ width: 380, height: 560 });
  await page.waitForTimeout(100);
  await page.screenshot({ path: resolve(output, "preview-catalog-380x560.png") });

  console.log("Preview screenshots and interaction checks passed.");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
}).finally(async () => {
  if (browser) {
    await browser.close();
  }
});
