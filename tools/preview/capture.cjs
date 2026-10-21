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
  if (await page.locator(".category-icon").count()) {
    throw new Error("category icons are still visible");
  }
  const descriptionClamp = await page.locator(".skill-description").first()
    .evaluate((node) => getComputedStyle(node).webkitLineClamp);
  if (descriptionClamp !== "none") {
    throw new Error(`Skill descriptions are still clamped: ${descriptionClamp}`);
  }
  const descriptionFlow = await page.evaluate(() => {
    for (const content of document.querySelectorAll(".skill-content")) {
      const description = content.querySelector(".skill-description");
      const range = document.createRange();
      range.selectNodeContents(description);
      const lineRects = [...range.getClientRects()];
      if (lineRects.length < 3) {
        continue;
      }
      const contentLeft = content.getBoundingClientRect().left;
      return {
        contentLeft,
        laterLines: lineRects.slice(1).map((rect) => Math.round(rect.left)),
      };
    }
    return null;
  });
  if (!descriptionFlow ||
      descriptionFlow.laterLines.some((left) => left > descriptionFlow.contentLeft + 2)) {
    throw new Error(
      `wrapped Skill descriptions do not use the left edge: ${JSON.stringify(descriptionFlow)}`,
    );
  }
  const motionAudit = await page.evaluate(() => {
    const issues = [];
    const nodes = document.querySelectorAll(
      "button, .skill-row, .context-menu, .toast, .provider-card, .settings-view, .provider-dialog",
    );
    for (const node of nodes) {
      const style = getComputedStyle(node);
      const properties = style.transitionProperty.split(",").map((value) => value.trim());
      const durations = style.transitionDuration
        .split(",")
        .map((value) => parseFloat(value) * 1000);
      if (properties.includes("all")) {
        issues.push(`${node.className || node.tagName}: transition all`);
      }
      if (durations.some((duration) => duration > 300)) {
        issues.push(`${node.className || node.tagName}: duration over 300ms`);
      }
    }
    return issues;
  });
  if (motionAudit.length) {
    throw new Error(`motion audit failed: ${motionAudit.join("; ")}`);
  }
  const commandLabels = await page.locator(".skill-command").allTextContents();
  if (commandLabels.some((label) => label.startsWith("/"))) {
    throw new Error("skill command labels still expose the hidden slash");
  }
  const languageOptions = await page.locator(".language-option").allTextContents();
  if (languageOptions.slice(0, 2).join("") !== "英中") {
    throw new Error(`language switch labels are wrong: ${JSON.stringify(languageOptions)}`);
  }
  const languageButton = await page.locator(".language-option").first().boundingBox();
  if (!languageButton || languageButton.width < 22 || languageButton.height < 22) {
    throw new Error(`language button is too small: ${JSON.stringify(languageButton)}`);
  }
  const settingsIcon = await page.locator("#open-settings svg").boundingBox();
  if (!settingsIcon || settingsIcon.width < 18 || settingsIcon.height < 18) {
    throw new Error(`settings icon is too small: ${JSON.stringify(settingsIcon)}`);
  }
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
  const categoryChevron = await page.locator(".category-chevron").first().boundingBox();
  if (!categoryChevron || categoryChevron.width < 17 || categoryChevron.height < 17) {
    throw new Error(`category chevron is too small: ${JSON.stringify(categoryChevron)}`);
  }
  await page.screenshot({ path: resolve(output, "preview-catalog-420x680.png") });

  await page.locator("#theme-toggle").click();
  await page.waitForFunction(() => document.documentElement.dataset.theme === "dark");
  await page.waitForTimeout(2000);
  const darkColors = await page.evaluate(() => {
    const body = getComputedStyle(document.body);
    const row = getComputedStyle(document.querySelector(".skill-row"));
    return { body: body.backgroundColor, row: row.backgroundColor };
  });
  if (darkColors.body === darkColors.row) {
    throw new Error(`dark theme surfaces do not separate: ${JSON.stringify(darkColors)}`);
  }
  await page.screenshot({
    path: resolve(output, "preview-catalog-dark-420x680.png"),
  });
  await page.setViewportSize({ width: 380, height: 560 });
  await page.waitForTimeout(100);
  await page.screenshot({
    path: resolve(output, "preview-catalog-dark-380x560.png"),
  });
  await page.setViewportSize({ width: 420, height: 680 });
  await page.locator("#open-settings").click();
  await page.waitForTimeout(180);
  await page.screenshot({
    path: resolve(output, "preview-settings-dark-420x680.png"),
  });
  await page.locator("#tab-api").click();
  await page.locator("#add-provider-button").click();
  await page.waitForTimeout(180);
  await page.screenshot({
    path: resolve(output, "preview-provider-dialog-dark-420x680.png"),
  });
  await page.locator("#close-provider-dialog").click();
  await page.locator("#close-settings").click();
  await page.locator("#theme-toggle").click();
  await page.waitForFunction(() => document.documentElement.dataset.theme === "light");
  await page.waitForTimeout(2000);

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

  await page.locator("#search-input").fill("ui-review");
  await page.waitForTimeout(100);
  if ((await page.locator(".skill-row").count()) !== 1) {
    throw new Error("search without a leading slash did not match the Skill command");
  }
  await page.locator("#clear-search").click();

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

  await page.locator(".category-label").first().click();
  await page.waitForTimeout(80);
  if (await page.locator(".category").first().evaluate((node) =>
    node.classList.contains("is-collapsed"))) {
    throw new Error("clicking category text expanded or collapsed the category");
  }
  await page.locator(".category-toggle").first().click();
  await page.waitForTimeout(50);
  const collapsed = await page.locator(".category").first().evaluate((node) =>
    node.classList.contains("is-collapsed"));
  if (!collapsed) {
    throw new Error("category chevron did not collapse immediately");
  }
  if (await page.locator(".category-name-input").count()) {
    throw new Error("category chevron triggered inline rename");
  }
  await page.locator(".category-toggle").first().click();
  await page.waitForTimeout(50);

  const sourceRow = page.locator(".skill-row").nth(0);
  await page.evaluate((path) => {
    const row = [...document.querySelectorAll(".skill-row")]
      .find((item) => item.dataset.path === path);
    const target = document.querySelector('.category[data-category="backend"]');
    const data = new DataTransfer();
    row.dispatchEvent(new DragEvent("dragstart", { bubbles: true, dataTransfer: data }));
    target.dispatchEvent(new DragEvent("dragover", { bubbles: true, cancelable: true, dataTransfer: data }));
    target.dispatchEvent(new DragEvent("drop", { bubbles: true, cancelable: true, dataTransfer: data }));
    row.dispatchEvent(new DragEvent("dragend", { bubbles: true, dataTransfer: data }));
  }, firstPath);
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
  await page.waitForFunction(() =>
    document.querySelector("#context-menu")?.classList.contains("is-open"));
  const menuOrigin = await page.locator("#context-menu").evaluate((node) => ({
    x: node.style.getPropertyValue("--menu-origin-x"),
    y: node.style.getPropertyValue("--menu-origin-y"),
  }));
  if (!menuOrigin.x || !menuOrigin.y) {
    throw new Error(`context menu origin is missing: ${JSON.stringify(menuOrigin)}`);
  }
  await page.waitForTimeout(140);
  await page.screenshot({ path: resolve(output, "preview-context-menu-420x680.png") });
  await page.keyboard.press("Escape");

  await page.locator('.category[data-category="favorite"] .category-label').dblclick();
  const renameInput = page.locator('.category[data-category="favorite"] .category-name-input');
  const renameOrder = await page.locator('.category[data-category="favorite"] .category-header')
    .evaluate((header) => {
      const input = header.querySelector(".category-name-input");
      const count = header.querySelector(".category-count");
      return {
        inputBeforeCount: Boolean(input.compareDocumentPosition(count) &
          Node.DOCUMENT_POSITION_FOLLOWING),
        inputLeft: input.getBoundingClientRect().left,
        countLeft: count.getBoundingClientRect().left,
      };
    });
  if (!renameOrder.inputBeforeCount || renameOrder.inputLeft >= renameOrder.countLeft) {
    throw new Error(`rename input moved after the Skill count: ${JSON.stringify(renameOrder)}`);
  }
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
  await page.waitForTimeout(180);
  if (await page.locator("#settings-save-state, #settings-done, .settings-footer").count()) {
    throw new Error("settings footer still shows auto-save or done controls");
  }
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
  await page.waitForTimeout(2000);

  await page.locator("#tab-api").click();
  await page.waitForSelector(".provider-empty-state");
  const providerCount = await page.locator(".provider-card").count();
  if (providerCount !== 0) {
    throw new Error(`unexpected initial provider count: ${providerCount}`);
  }
  await page.screenshot({
    path: resolve(output, "preview-provider-empty-420x680.png"),
  });

  await page.locator("#add-provider-button").click();
  if (!(await page.locator("#provider-dialog").isVisible())) {
    throw new Error("provider picker dialog did not open");
  }
  const presetOptions = await page.locator(".provider-preset-option").count();
  if (presetOptions < 8) {
    throw new Error(`provider picker presets are incomplete: ${presetOptions}`);
  }
  if (await page.locator('.provider-preset-option[data-provider="anthropic"]').count()) {
    throw new Error("Anthropic Messages is still offered in the provider picker");
  }
  if (await page.locator(".provider-preset-option span").count()) {
    throw new Error("provider picker still exposes model subtitles");
  }
  await page.waitForTimeout(180);
  await page.screenshot({
    path: resolve(output, "preview-provider-dialog-420x680.png"),
  });
  await page.locator('.provider-preset-option[data-provider="deepseek"]').click();
  await page.waitForFunction(() =>
    document.querySelector('.provider-card[data-provider="deepseek"]'));
  const deepSeekCard = page.locator('.provider-card[data-provider="deepseek"]');
  if (await deepSeekCard.evaluate((node) => node.classList.contains("is-current"))) {
    throw new Error("a newly added provider became current automatically");
  }
  if (!(await deepSeekCard.evaluate((node) => node.classList.contains("is-expanded")))) {
    await deepSeekCard.locator(".provider-toggle").click();
  }
  if (await deepSeekCard.locator(".provider-summary").count()) {
    throw new Error("provider header still exposes a model subtitle");
  }
  const providerBodyText = await deepSeekCard.locator(".provider-body").textContent();
  if (!providerBodyText?.includes("API 协议")) {
    throw new Error("provider details lost the API protocol selector");
  }
  const protocolOptions = await deepSeekCard
    .locator(".provider-protocol option")
    .allTextContents();
  if (protocolOptions.length !== 3) {
    throw new Error(`provider protocol selector lost options: ${protocolOptions.length}`);
  }
  await deepSeekCard.locator(".provider-api-key").fill("preview-deepseek-key");
  await page.waitForTimeout(800);
  if (!(await deepSeekCard.locator(".provider-status").textContent())?.includes("已保存")) {
    throw new Error("provider API key did not auto-save after typing");
  }
  await deepSeekCard.locator(".model-row .secondary-button").click();
  await page.waitForFunction(() =>
    document.querySelector('[data-provider-status="deepseek"]')
      ?.textContent?.includes("获取"));
  await deepSeekCard.locator(".provider-actions .secondary-button").click();
  await page.waitForFunction(() =>
    document.querySelector('[data-provider-status="deepseek"]')
      ?.textContent?.includes("126 ms"));
  await page.screenshot({
    path: resolve(output, "preview-provider-added-420x680.png"),
  });
  if (!(await deepSeekCard.evaluate((node) => node.classList.contains("is-current")))) {
    await deepSeekCard.locator(".provider-current-button").click();
  }
  await page.waitForFunction(() =>
    document.querySelector('.provider-card[data-provider="deepseek"]')
      ?.classList.contains("is-current"));
  await deepSeekCard.scrollIntoViewIfNeeded();
  await page.screenshot({
    path: resolve(output, "preview-provider-test-420x680.png"),
  });

  await page.locator("#add-provider-button").click();
  await page.locator('.provider-preset-option[data-provider="custom"]').click();
  const customCard = page.locator('.provider-card[data-provider="custom"]');
  if ((await customCard.locator(".provider-name").textContent())?.trim() !== "自定义") {
    throw new Error("custom provider does not use the 自定义 default name");
  }
  await customCard.locator(".provider-rename-button").click();
  const providerNameInput = customCard.locator(".provider-name-input");
  await providerNameInput.fill("我的供应商");
  await providerNameInput.press("Enter");
  await page.waitForFunction(() =>
    document.querySelector('.provider-card[data-provider="custom"] .provider-name')
      ?.textContent === "我的供应商");
  await customCard.scrollIntoViewIfNeeded();
  await page.screenshot({
    path: resolve(output, "preview-provider-rename-420x680.png"),
  });

  await page.locator(".settings-scroll").evaluate((node) => {
    node.scrollTop = 0;
  });
  await page.waitForTimeout(2000);
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

  await page.emulateMedia({ reducedMotion: "reduce" });
  const reducedMotionDuration = await page.locator(".skill-row").first()
    .evaluate((node) => getComputedStyle(node).transitionDuration);
  if (parseFloat(reducedMotionDuration) > 0.02) {
    throw new Error(`reduced motion is not honored: ${reducedMotionDuration}`);
  }
  await page.emulateMedia({ reducedMotion: "no-preference" });

  console.log("Preview screenshots and interaction checks passed.");
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
}).finally(async () => {
  if (browser) {
    await browser.close();
  }
});
