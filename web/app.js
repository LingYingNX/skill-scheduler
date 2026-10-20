const nativeBridge = window.chrome?.webview;
const pending = new Map();
let sequence = 0;

const DEFAULT_CATEGORY_DEFINITIONS = [
  { id: "favorite", name: "常用" },
  { id: "uncommon", name: "不常用" },
  { id: "frontend", name: "前端" },
  { id: "backend", name: "后端" },
  { id: "other", name: "其他" },
];

const API_PROVIDER_PRESETS = [
  {
    id: "openai-responses",
    name: "OpenAI Responses",
    protocol: "openai-responses",
    baseUrl: "https://api.openai.com/v1",
    model: "gpt-4o-mini",
  },
  {
    id: "openai-chat",
    name: "OpenAI Chat Completions",
    protocol: "openai",
    baseUrl: "https://api.openai.com/v1",
    model: "gpt-4o-mini",
  },
  {
    id: "zhipu",
    name: "智谱 GLM",
    protocol: "openai",
    baseUrl: "https://open.bigmodel.cn/api/paas/v4",
    model: "glm-4-flash",
  },
  {
    id: "deepseek",
    name: "DeepSeek",
    protocol: "openai",
    baseUrl: "https://api.deepseek.com/v1",
    model: "deepseek-chat",
  },
  {
    id: "baidu",
    name: "百度千帆（免费额度）",
    protocol: "openai",
    baseUrl: "https://qianfan.baidubce.com/v2",
    model: "ernie-speed-128k",
  },
  {
    id: "doubao",
    name: "豆包",
    protocol: "openai",
    baseUrl: "https://ark.cn-beijing.volces.com/api/v3",
    model: "doubao-1-5-lite-32k-250115",
  },
  {
    id: "siliconflow",
    name: "硅基流动",
    protocol: "openai",
    baseUrl: "https://api.siliconflow.cn/v1",
    model: "Qwen/Qwen2.5-7B-Instruct",
  },
  {
    id: "modelscope",
    name: "魔搭社区",
    protocol: "openai",
    baseUrl: "https://api-inference.modelscope.cn/v1",
    model: "Qwen/Qwen2.5-7B-Instruct",
  },
  {
    id: "custom",
    name: "自定义",
    protocol: "openai-responses",
    baseUrl: "",
    model: "",
  },
];

const state = {
  skills: [],
  roots: [],
  paths: [],
  categoryDefinitions: [...DEFAULT_CATEGORY_DEFINITIONS],
  providers: [],
  enabledProviderIds: [],
  ui: {
    pinned: false,
    allCollapsed: false,
    collapsedCategories: [],
    theme: "light",
  },
  warning: "",
  monitorDegraded: false,
};

const selected = {
  query: "",
  activePath: "",
  contextPath: "",
  draggingPath: "",
  originalPaths: new Set(),
  settingsOpen: false,
  activeTab: "paths",
  currentProviderId: "",
};

const dom = {
  search: document.querySelector("#search-input"),
  clearSearch: document.querySelector("#clear-search"),
  warning: document.querySelector("#warning-banner"),
  warningText: document.querySelector("#warning-text"),
  skillCount: document.querySelector("#skill-count"),
  rootStatus: document.querySelector("#root-status"),
  collapseAll: document.querySelector("#collapse-all"),
  loading: document.querySelector("#loading-state"),
  categoryList: document.querySelector("#category-list"),
  empty: document.querySelector("#empty-state"),
  emptyTitle: document.querySelector("#empty-title"),
  emptyDescription: document.querySelector("#empty-description"),
  footerStatus: document.querySelector("#footer-status"),
  catalogView: document.querySelector("#catalog-view"),
  settingsView: document.querySelector("#settings-view"),
  settingsButton: document.querySelector("#open-settings"),
  themeToggle: document.querySelector("#theme-toggle"),
  closeSettings: document.querySelector("#close-settings"),
  tabPaths: document.querySelector("#tab-paths"),
  tabApi: document.querySelector("#tab-api"),
  pathsPanel: document.querySelector("#paths-panel"),
  apiPanel: document.querySelector("#api-panel"),
  pathsList: document.querySelector("#paths-list"),
  pathsCount: document.querySelector("#paths-count"),
  addPathForm: document.querySelector("#add-path-form"),
  pathInput: document.querySelector("#path-input"),
  browsePath: document.querySelector("#browse-path"),
  providerList: document.querySelector("#provider-list"),
  addProviderButton: document.querySelector("#add-provider-button"),
  providerDialog: document.querySelector("#provider-dialog"),
  providerPresetList: document.querySelector("#provider-preset-list"),
  closeProviderDialog: document.querySelector("#close-provider-dialog"),
  translateAll: document.querySelector("#translate-all"),
  contextMenu: document.querySelector("#context-menu"),
  toast: document.querySelector("#toast"),
  toastText: document.querySelector("#toast-text"),
  retranslateSkill: document.querySelector("#retranslate-skill"),
  contextCategoryActions: document.querySelector("#context-category-actions"),
};

let toastTimer = 0;
const batchWaiters = [];
const providerRuntime = new Map();
const providerKeyTimers = new Map();
const expandedProviders = new Set();

function callNative(command, payload = {}) {
  if (!nativeBridge) {
    return Promise.reject(new Error("native bridge unavailable"));
  }
  const id = `web-${++sequence}`;
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
    nativeBridge.postMessage({ id, command, payload });
  });
}

function icon(id) {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  const use = document.createElementNS("http://www.w3.org/2000/svg", "use");
  use.setAttribute("href", `#${id}`);
  svg.append(use);
  return svg;
}

function providerById(id) {
  const saved = state.providers.find((provider) => provider.id === id);
  const preset = API_PROVIDER_PRESETS.find((provider) => provider.id === id);
  if (preset) {
    return { ...preset, ...saved };
  }
  return saved;
}

function currentProvider() {
  return providerById(selected.currentProviderId);
}

function applyTheme(theme) {
  const normalized = theme === "dark" ? "dark" : "light";
  state.ui.theme = normalized;
  document.documentElement.dataset.theme = normalized;
  dom.themeToggle.setAttribute("aria-checked", String(normalized === "dark"));
  dom.themeToggle.setAttribute(
    "aria-label",
    normalized === "dark" ? "切换到亮色主题" : "切换到深色主题",
  );
  dom.themeToggle.title =
    normalized === "dark" ? "切换到亮色主题" : "切换到深色主题";
}

function categoryById(id) {
  return categories().find((item) => item.id === id) || categoryFromDefinition({
    id,
    name: id,
  });
}

function categoryFromDefinition(definition) {
  return {
    ...definition,
    label: definition.name,
  };
}

function categories() {
  const definitions =
    state.categoryDefinitions?.length
      ? state.categoryDefinitions
      : DEFAULT_CATEGORY_DEFINITIONS;
  return definitions.map(categoryFromDefinition);
}

function normalizedQuery() {
  return selected.query
    .trim()
    .replace(/^\/+/, "")
    .toLocaleLowerCase("zh-CN");
}

function skillMatches(skill, query) {
  if (!query) {
    return true;
  }
  return [
    skill.command,
    skill.name,
    skill.description,
    skill.translation,
    categoryById(skill.category).label,
  ].some((value) => String(value || "").toLocaleLowerCase("zh-CN").includes(query));
}

function visibleSkills() {
  const query = normalizedQuery();
  return state.skills.filter((skill) => skillMatches(skill, query));
}

function showToast(message, kind = "success") {
  dom.toastText.textContent = message;
  dom.toast.querySelector(".toast-dot").style.background =
    kind === "error" ? "#ff8a80" : kind === "warning" ? "#ffd36e" : "#67d89b";
  dom.toast.classList.add("is-visible");
  clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => {
    dom.toast.classList.remove("is-visible");
  }, 1900);
}

function createSkillRow(skill) {
  const row = document.createElement("div");
  row.className = "skill-row";
  row.tabIndex = 0;
  row.draggable = true;
  row.dataset.path = skill.path;
  row.setAttribute("role", "button");
  row.setAttribute("aria-label", `复制 ${skill.command}`);
  if (skill.path === selected.activePath) {
    row.classList.add("is-active");
  }

  const command = document.createElement("span");
  command.className = "skill-command";
  command.textContent = skill.command.replace(/^\/+/, "");

  const description = document.createElement("span");
  description.className = "skill-description";
  const showingOriginal = selected.originalPaths.has(skill.path);
  description.textContent =
    skill.translation && !showingOriginal ? skill.translation : skill.description || "无描述";

  const content = document.createElement("span");
  content.className = "skill-content";
  content.append(command, description);

  const actions = document.createElement("span");
  actions.className = "skill-actions";

  if (skill.warning) {
    const warning = document.createElement("span");
    warning.className = "warning-indicator";
    warning.title = skill.warning;
    warning.append(icon("i-alert"));
    actions.append(warning);
  }

  const translation = document.createElement("button");
  translation.type = "button";
  translation.className = "translation-toggle";
  if (skill.translation) {
    translation.classList.add("is-translated");
    translation.append(icon("i-languages"));
    translation.title = showingOriginal ? "显示中文译文" : "显示英文原文";
    translation.addEventListener("click", (event) => {
      event.stopPropagation();
      if (selected.originalPaths.has(skill.path)) {
        selected.originalPaths.delete(skill.path);
      } else {
        selected.originalPaths.add(skill.path);
      }
      renderCategories();
    });
  } else {
    translation.textContent = "译";
    translation.title = "翻译描述";
    translation.addEventListener("click", (event) => {
      event.stopPropagation();
      translateSkill(skill);
    });
  }
  actions.append(translation);

  const copy = document.createElement("span");
  copy.className = "copy-indicator";
  copy.append(icon("i-copy"));
  actions.append(copy);

  row.append(content, actions);
  row.addEventListener("click", () => copySkill(skill));
  row.addEventListener("focus", () => setActivePath(skill.path));
  row.addEventListener("keydown", (event) => {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      copySkill(skill);
    }
  });
  row.addEventListener("contextmenu", (event) => {
    event.preventDefault();
    openContextMenu(event, skill.path);
  });
  row.addEventListener("dragstart", (event) => {
    selected.draggingPath = skill.path;
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", skill.path);
    row.classList.add("is-dragging");
  });
  row.addEventListener("dragend", () => {
    selected.draggingPath = "";
    row.classList.remove("is-dragging");
    document.querySelectorAll(".is-drop-target").forEach((node) => {
      node.classList.remove("is-drop-target");
    });
  });
  return row;
}

function renderCategories() {
  const skills = visibleSkills();
  const query = normalizedQuery();
  dom.categoryList.replaceChildren();

  for (const category of categories()) {
    const items = skills.filter((skill) => skill.category === category.id);
    if (query && items.length === 0) {
      continue;
    }

    const section = document.createElement("section");
    const collapsed =
      state.ui.allCollapsed || state.ui.collapsedCategories.includes(category.id);
    section.className = `category${collapsed ? " is-collapsed" : ""}`;
    section.dataset.category = category.id;

    const header = document.createElement("div");
    header.className = "category-header";
    const chevron = icon("i-chevron-down");
    chevron.classList.add("category-chevron");
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "category-toggle";
    toggle.setAttribute("aria-label", `${category.name} 展开或折叠`);
    toggle.setAttribute("aria-expanded", String(!collapsed));
    toggle.append(chevron);
    const label = document.createElement("span");
    label.className = "category-label";
    label.textContent = category.name;
    const count = document.createElement("span");
    count.className = "category-count";
    count.textContent = String(items.length);
    header.append(toggle, label, count);
    toggle.addEventListener("click", (event) => {
      event.stopPropagation();
      toggleCategory(category.id);
    });
    label.addEventListener("dblclick", (event) => {
      event.preventDefault();
      event.stopPropagation();
      beginCategoryRename(section, label, category);
    });

    const body = document.createElement("div");
    body.className = "category-body";
    for (const skill of items) {
      body.append(createSkillRow(skill));
    }

    section.append(header, body);
    section.addEventListener("dragover", (event) => {
      event.preventDefault();
      event.dataTransfer.dropEffect = "move";
      section.classList.add("is-drop-target");
    });
    section.addEventListener("dragleave", (event) => {
      if (!section.contains(event.relatedTarget)) {
        section.classList.remove("is-drop-target");
      }
    });
    section.addEventListener("drop", (event) => {
      event.preventDefault();
      section.classList.remove("is-drop-target");
      const path =
        event.dataTransfer.getData("text/plain") || selected.draggingPath;
      if (path) {
        moveSkillToCategory(path, category.id);
      }
    });
    dom.categoryList.append(section);
  }

  if (!query) {
    const addRow = document.createElement("div");
    addRow.className = "add-category-row";
    const addButton = document.createElement("button");
    addButton.type = "button";
    addButton.className = "add-category-button";
    addButton.append(icon("i-plus"), document.createTextNode("新分类"));
    addButton.addEventListener("click", () => beginAddCategory(addRow));
    addRow.append(addButton);
    dom.categoryList.append(addRow);
  }

  const hasAnySkills = state.skills.length > 0;
  const hasVisibleSkills = skills.length > 0;
  dom.loading.hidden = true;
  dom.categoryList.hidden = Boolean(query) && !hasVisibleSkills;
  dom.empty.hidden = !query || hasVisibleSkills;
  if (query && !hasVisibleSkills) {
    dom.emptyTitle.textContent = hasAnySkills ? "没有匹配的 Skills" : "还没有发现 Skills";
    dom.emptyDescription.textContent = hasAnySkills
      ? "尝试缩短关键词。"
      : "在设置中添加包含 SKILL.md 的文件夹。";
  }

  const unavailableRoots = state.roots.filter((root) => !root.available);
  dom.skillCount.textContent = `${skills.length} 个 Skills`;
  dom.rootStatus.textContent = unavailableRoots.length
    ? `${unavailableRoots.length} 个路径不可用`
    : state.roots.length
      ? "监控中"
      : "";
  dom.rootStatus.classList.toggle("is-warning", unavailableRoots.length > 0);
  dom.collapseAll.textContent = state.ui.allCollapsed ? "展开全部" : "折叠全部";
  dom.footerStatus.textContent = state.warning ? "配置文件已恢复" : "config.json 已同步";
  renderWarning();
  updateActiveRow();
}

function renderWarning() {
  const unavailable = state.roots.filter((root) => !root.available);
  const warningText = state.warning
    ? `配置已恢复：${state.warning}`
    : state.monitorDegraded
      ? "实时监听不可用，已切换到周期扫描"
    : unavailable.length
      ? `${unavailable[0].path} 当前不可用；路径仍保留在配置中`
      : "";
  dom.warning.hidden = !warningText;
  dom.warningText.textContent = warningText;
}

function setActivePath(path) {
  selected.activePath = path;
  updateActiveRow();
}

function updateActiveRow() {
  document.querySelectorAll(".skill-row").forEach((row) => {
    row.classList.toggle("is-active", row.dataset.path === selected.activePath);
  });
}

async function copySkill(skill) {
  setActivePath(skill.path);
  try {
    await callNative("clipboard.copy", { text: skill.command });
    showToast(`已复制 ${skill.command}`);
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function translateSkill(skill) {
  const provider = currentProvider();
  if (!provider?.keySaved) {
    showToast("先选择供应商并保存 API Key", "warning");
    openSettings("api");
    return;
  }
  showToast("正在翻译描述");
  try {
    await callNative("translation.one", {
      path: skill.path,
      providerId: provider.id,
    });
    await loadState();
    showToast("翻译已保存到 config.json");
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function translateAll() {
  const provider = currentProvider();
  if (!provider?.keySaved) {
    showToast("先选择供应商并保存 API Key", "warning");
    openSettings("api");
    return;
  }
  dom.translateAll.disabled = true;
  dom.translateAll.querySelector("span").textContent = "翻译中";
  try {
    const resultPromise = new Promise((resolve, reject) => {
      const timeout = window.setTimeout(() => {
        const index = batchWaiters.findIndex((waiter) => waiter.resolve === resolve);
        if (index >= 0) {
          batchWaiters.splice(index, 1);
        }
        reject(new Error("批量翻译超时"));
      }, 10 * 60 * 1000);
      batchWaiters.push({
        resolve: (value) => {
          clearTimeout(timeout);
          resolve(value);
        },
      });
    });
    const accepted = await callNative("translation.batch", {
      providerId: provider.id,
    });
    const result = accepted?.accepted ? await resultPromise : accepted;
    await loadState();
    const failed = Number(result?.failed || 0);
    showToast(failed ? `已翻译，失败 ${failed} 条` : "翻译完成", failed ? "warning" : "success");
  } catch (error) {
    showToast(error.message, "error");
  } finally {
    dom.translateAll.disabled = false;
    dom.translateAll.querySelector("span").textContent = "翻译";
  }
}

async function toggleCategory(category) {
  const previousAllCollapsed = state.ui.allCollapsed;
  const previousCollapsed = [...state.ui.collapsedCategories];
  if (state.ui.allCollapsed) {
    state.ui.allCollapsed = false;
    state.ui.collapsedCategories = categories()
      .map((item) => item.id)
      .filter((id) => id !== category);
  } else {
    const collapsed = !state.ui.collapsedCategories.includes(category);
    state.ui.collapsedCategories = collapsed
      ? [...state.ui.collapsedCategories, category]
      : state.ui.collapsedCategories.filter((item) => item !== category);
  }
  updateCategoryCollapseDom();
  try {
    await callNative("ui.setCollapsed", {
      allCollapsed: state.ui.allCollapsed,
      collapsedCategories: state.ui.collapsedCategories,
    });
  } catch (error) {
    state.ui.allCollapsed = previousAllCollapsed;
    state.ui.collapsedCategories = previousCollapsed;
    updateCategoryCollapseDom();
    showToast(error.message, "error");
  }
}

async function toggleAllCategories() {
  const previousAllCollapsed = state.ui.allCollapsed;
  const previousCollapsed = [...state.ui.collapsedCategories];
  state.ui.allCollapsed = !state.ui.allCollapsed;
  state.ui.collapsedCategories = [];
  updateCategoryCollapseDom();
  try {
    await callNative("ui.setCollapsed", {
      allCollapsed: state.ui.allCollapsed,
      collapsedCategories: state.ui.collapsedCategories,
    });
  } catch (error) {
    state.ui.allCollapsed = previousAllCollapsed;
    state.ui.collapsedCategories = previousCollapsed;
    updateCategoryCollapseDom();
    showToast(error.message, "error");
  }
}

function updateCategoryCollapseDom() {
  document.querySelectorAll(".category").forEach((section) => {
    const collapsed =
      state.ui.allCollapsed ||
      state.ui.collapsedCategories.includes(section.dataset.category);
    section.classList.toggle("is-collapsed", collapsed);
    section
      .querySelector(".category-toggle")
      ?.setAttribute("aria-expanded", String(!collapsed));
  });
  dom.collapseAll.textContent = state.ui.allCollapsed ? "展开全部" : "折叠全部";
}

function openContextMenu(event, path) {
  selected.contextPath = path;
  const skill = state.skills.find((item) => item.path === path);
  dom.retranslateSkill.hidden = !skill?.translation;
  dom.contextCategoryActions.replaceChildren();
  for (const category of categories()) {
    const button = document.createElement("button");
    button.type = "button";
    button.setAttribute("role", "menuitem");
    const label = document.createElement("span");
    label.textContent = category.name;
    button.append(label);
    button.addEventListener("click", () => moveContextSkill(category.id));
    dom.contextCategoryActions.append(button);
  }
  dom.contextMenu.classList.remove("is-open");
  dom.contextMenu.hidden = false;
  const width = 148;
  const height = dom.contextMenu.offsetHeight;
  const left = Math.max(6, Math.min(event.clientX, window.innerWidth - width - 6));
  const top = Math.max(44, Math.min(event.clientY, window.innerHeight - height - 6));
  dom.contextMenu.style.left = `${left}px`;
  dom.contextMenu.style.top = `${top}px`;
  dom.contextMenu.style.setProperty(
    "--menu-origin-x",
    `${Math.min(width, Math.max(0, event.clientX - left))}px`,
  );
  dom.contextMenu.style.setProperty(
    "--menu-origin-y",
    `${Math.min(height, Math.max(0, event.clientY - top))}px`,
  );
  requestAnimationFrame(() => dom.contextMenu.classList.add("is-open"));
}

function closeContextMenu() {
  selected.contextPath = "";
  dom.contextMenu.classList.remove("is-open");
  dom.contextMenu.hidden = true;
}

async function moveContextSkill(category) {
  const path = selected.contextPath;
  closeContextMenu();
  if (!path) {
    return;
  }
  await moveSkillToCategory(path, category);
}

async function moveSkillToCategory(path, category) {
  const skill = state.skills.find((item) => item.path === path);
  if (!skill || skill.category === category) {
    return;
  }
  try {
    const updated = await callNative("category.set", { path, category });
    applyState(updated);
    showToast(`已移动到${categoryById(category).name}`);
  } catch (error) {
    showToast(error.message, "error");
  }
}

function beginCategoryRename(section, label, category) {
  if (section.querySelector(".category-name-input")) {
    return;
  }
  const input = document.createElement("input");
  input.className = "category-name-input";
  input.value = category.name;
  input.maxLength = 24;
  label.replaceWith(input);
  input.focus();
  input.select();
  let finished = false;
  const finish = async (save) => {
    if (finished) {
      return;
    }
    finished = true;
    const name = input.value.trim();
    input.replaceWith(label);
    if (!save || !name || name === category.name) {
      return;
    }
    try {
      const updated = await callNative("category.rename", {
        id: category.id,
        name,
      });
      applyState(updated);
      showToast("分类已重命名");
    } catch (error) {
      showToast(error.message, "error");
    }
  };
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      finish(true);
    } else if (event.key === "Escape") {
      event.preventDefault();
      finish(false);
    }
  });
  input.addEventListener("click", (event) => event.stopPropagation());
  input.addEventListener("dblclick", (event) => event.stopPropagation());
  input.addEventListener("blur", () => finish(true));
}

function beginAddCategory(container) {
  if (container.querySelector("form")) {
    return;
  }
  container.replaceChildren();
  const form = document.createElement("form");
  form.className = "add-category-form";
  const input = document.createElement("input");
  input.className = "category-name-input";
  input.maxLength = 24;
  input.placeholder = "分类名称";
  const add = document.createElement("button");
  add.type = "submit";
  add.className = "primary-button";
  add.textContent = "添加";
  const cancel = document.createElement("button");
  cancel.type = "button";
  cancel.className = "secondary-button";
  cancel.textContent = "取消";
  form.append(input, add, cancel);
  container.append(form);
  input.focus();

  const finish = async (save) => {
    const name = input.value.trim();
    if (!save) {
      renderCategories();
      return;
    }
    if (!name) {
      showToast("分类名称不能为空", "warning");
      input.focus();
      return;
    }
    try {
      const updated = await callNative("category.add", { name });
      applyState(updated);
      showToast("新分类已添加");
    } catch (error) {
      showToast(error.message, "error");
      input.focus();
    }
  };
  form.addEventListener("submit", (event) => {
    event.preventDefault();
    finish(true);
  });
  cancel.addEventListener("click", () => finish(false));
  input.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      event.preventDefault();
      finish(false);
    }
  });
}

async function retranslateContextSkill() {
  const path = selected.contextPath;
  closeContextMenu();
  if (!path) {
    return;
  }
  const provider = currentProvider();
  if (!provider?.keySaved) {
    showToast("先选择供应商并保存 API Key", "warning");
    openSettings("api");
    return;
  }
  showToast("正在重新翻译");
  try {
    await callNative("translation.one", {
      path,
      providerId: provider.id,
      force: true,
    });
    await loadState();
    showToast("译文已更新");
  } catch (error) {
    showToast(error.message, "error");
  }
}

function applyState(next) {
  state.skills = Array.isArray(next.skills) ? next.skills : [];
  state.roots = Array.isArray(next.roots) ? next.roots : [];
  state.paths = Array.isArray(next.paths) ? next.paths : [];
  state.categoryDefinitions =
    Array.isArray(next.categoryDefinitions) && next.categoryDefinitions.length
      ? next.categoryDefinitions
      : [...DEFAULT_CATEGORY_DEFINITIONS];
  state.providers = Array.isArray(next.providers) ? next.providers : state.providers;
  state.enabledProviderIds = Array.isArray(next.enabledProviderIds)
    ? next.enabledProviderIds
    : state.enabledProviderIds;
  if (!state.enabledProviderIds.includes(selected.currentProviderId)) {
    selected.currentProviderId = "";
  }
  state.ui = next.ui || state.ui;
  applyTheme(state.ui.theme);
  state.warning = next.warning || "";
  state.monitorDegraded = Boolean(next.monitorDegraded);
  document
    .querySelector('[data-command="window.pin"]')
    ?.classList.toggle("is-active", Boolean(state.ui.pinned));
  if (!state.skills.some((skill) => skill.path === selected.activePath)) {
    selected.activePath = "";
  }
  renderCategories();
  if (selected.settingsOpen) {
    renderPaths();
    renderApiSettings();
  }
}

async function loadState() {
  try {
    const next = await callNative("catalog.getState");
    applyState(next);
  } catch (error) {
    dom.loading.hidden = true;
    dom.empty.hidden = false;
    dom.emptyTitle.textContent = "无法载入 Skills";
    dom.emptyDescription.textContent = error.message;
  }
}

function handleSearch() {
  selected.query = dom.search.value;
  dom.clearSearch.hidden = !selected.query;
  selected.activePath = "";
  renderCategories();
}

function clearSearch() {
  dom.search.value = "";
  selected.query = "";
  dom.clearSearch.hidden = true;
  selected.activePath = "";
  renderCategories();
  dom.search.focus();
}

function focusSearch() {
  if (selected.settingsOpen) {
    closeSettingsView();
  }
  dom.search.focus();
  dom.search.select();
}

function moveActive(direction) {
  const rows = [...document.querySelectorAll(".skill-row")];
  if (!rows.length) {
    return;
  }
  const current = rows.findIndex((row) => row.dataset.path === selected.activePath);
  const nextIndex = current < 0
    ? direction > 0 ? 0 : rows.length - 1
    : (current + direction + rows.length) % rows.length;
  rows[nextIndex].focus();
  rows[nextIndex].scrollIntoView({ block: "nearest" });
}

function openSettings(tab = "paths") {
  selected.settingsOpen = true;
  dom.catalogView.hidden = true;
  dom.settingsView.hidden = false;
  switchSettingsTab(tab);
  renderPaths();
  renderApiSettings();
  window.setTimeout(() => {
    if (tab === "paths") {
      dom.pathInput.focus();
    } else {
      const keyInput = selected.currentProviderId
        ? document.querySelector(
            `.provider-card[data-provider="${selected.currentProviderId}"] .provider-api-key`,
          )
        : null;
      (keyInput || dom.addProviderButton).focus();
    }
  }, 50);
}

function closeSettingsView() {
  selected.settingsOpen = false;
  dom.settingsView.hidden = true;
  dom.catalogView.hidden = false;
  dom.settingsButton.focus();
}

function switchSettingsTab(tab) {
  selected.activeTab = tab;
  const paths = tab === "paths";
  dom.pathsPanel.hidden = !paths;
  dom.apiPanel.hidden = paths;
  dom.tabPaths.classList.toggle("active", paths);
  dom.tabApi.classList.toggle("active", !paths);
  dom.tabPaths.setAttribute("aria-selected", String(paths));
  dom.tabApi.setAttribute("aria-selected", String(!paths));
}

function renderPaths() {
  dom.pathsList.replaceChildren();
  dom.pathsCount.textContent = `${state.paths.length} 个路径`;
  state.paths.forEach((path) => {
    const root = state.roots.find((item) => item.path === path);
    const item = document.createElement("div");
    item.className = "path-item";

    const indicator = document.createElement("span");
    indicator.className = `path-state${root && !root.available ? " unavailable" : ""}`;

    const copy = document.createElement("div");
    copy.className = "path-copy";
    const value = document.createElement("div");
    value.className = "path-value";
    value.textContent = path;
    value.title = path;
    copy.append(value);
    if (root && !root.available) {
      const error = document.createElement("div");
      error.className = "path-error";
      error.textContent = root.error || "路径不可用";
      copy.append(error);
    }

    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "remove-path";
    remove.title = "移除路径";
    remove.setAttribute("aria-label", "移除路径");
    remove.append(icon("i-x"));
    remove.addEventListener("click", () => removePath(path));

    item.append(indicator, copy, remove);
    dom.pathsList.append(item);
  });
}

function renderApiSettings() {
  dom.providerList.replaceChildren();
  const enabled = new Set(state.enabledProviderIds);
  if (!enabled.size) {
    const empty = document.createElement("div");
    empty.className = "provider-empty-state";
    empty.textContent = "尚未添加模型供应商。";
    dom.providerList.append(empty);
    return;
  }
  for (const preset of API_PROVIDER_PRESETS.filter((provider) => enabled.has(provider.id))) {
    const provider = { ...preset, ...providerById(preset.id) };
    const expanded = expandedProviders.has(provider.id);
    const isCurrent = provider.id === selected.currentProviderId;
    const runtime = providerRuntime.get(provider.id) || {};

    const section = document.createElement("section");
    section.className =
      `provider-card${expanded ? " is-expanded" : ""}${isCurrent ? " is-current" : ""}`;
    section.dataset.provider = provider.id;

    const header = document.createElement("div");
    header.className = "provider-header";
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "provider-toggle";
    toggle.setAttribute("aria-expanded", String(expanded));
    toggle.setAttribute("aria-label", `${provider.name} 展开或折叠`);
    const chevron = icon("i-chevron-down");
    chevron.classList.add("provider-chevron");
    toggle.append(chevron);
    const status = document.createElement("span");
    status.className = `provider-key-state${provider.keySaved ? " is-saved" : ""}`;
    status.setAttribute("aria-label", provider.keySaved ? "密钥已保存" : "密钥未保存");
    const name = document.createElement("span");
    name.className = "provider-name";
    name.textContent = provider.name;
    const rename = document.createElement("button");
    rename.type = "button";
    rename.className = "provider-rename-button";
    rename.title = "重命名供应商";
    rename.setAttribute("aria-label", "重命名供应商");
    rename.append(icon("i-pencil"));
    const currentBadge = document.createElement("span");
    currentBadge.className = "provider-current-badge";
    currentBadge.textContent = "当前";
    currentBadge.hidden = !isCurrent;
    header.append(toggle, name, rename, currentBadge, status);
    toggle.addEventListener("click", () => {
      if (expandedProviders.has(provider.id)) {
        expandedProviders.delete(provider.id);
      } else {
        expandedProviders.add(provider.id);
      }
      renderApiSettings();
    });
    rename.addEventListener("click", () => {
      beginProviderRename(section, name, rename, provider);
    });

    const body = document.createElement("div");
    body.className = "provider-body";
    body.hidden = !expanded;

    const baseField = document.createElement("label");
    baseField.className = "field provider-field";
    const baseLabel = document.createElement("span");
    baseLabel.textContent = "Base URL";
    const baseInput = document.createElement("input");
    baseInput.className = "provider-base-url";
    baseInput.type = "url";
    baseInput.autocomplete = "off";
    baseInput.spellcheck = false;
    baseInput.value = provider.baseUrl || "";
    baseInput.addEventListener("change", () => updateProvider(provider.id).catch(() => {}));
    baseField.append(baseLabel, baseInput);

    const keyField = document.createElement("label");
    keyField.className = "field provider-field";
    const keyLabel = document.createElement("span");
    keyLabel.textContent = "API Key";
    const keyRow = document.createElement("div");
    keyRow.className = "password-field";
    const keyInput = document.createElement("input");
    keyInput.className = "provider-api-key";
    keyInput.type = "password";
    keyInput.autocomplete = "new-password";
    keyInput.placeholder = provider.keySaved
      ? "已保存，粘贴新密钥以替换"
      : "粘贴 API Key";
    keyInput.addEventListener("input", () => {
      clearTimeout(providerKeyTimers.get(provider.id));
      const timer = window.setTimeout(() => {
        updateProvider(provider.id, {}, { preserveKey: true }).catch(() => {});
      }, 700);
      providerKeyTimers.set(provider.id, timer);
    });
    keyInput.addEventListener("change", () => {
      clearTimeout(providerKeyTimers.get(provider.id));
      updateProvider(provider.id).catch(() => {});
    });
    const keyToggle = document.createElement("button");
    keyToggle.type = "button";
    keyToggle.className = "icon-button";
    keyToggle.title = "显示或隐藏密钥";
    keyToggle.setAttribute("aria-label", "显示或隐藏密钥");
    keyToggle.append(icon("i-eye"));
    keyToggle.addEventListener("click", () => {
      keyInput.type = keyInput.type === "password" ? "text" : "password";
    });
    keyRow.append(keyInput, keyToggle);
    keyField.append(keyLabel, keyRow);

    const modelField = document.createElement("label");
    modelField.className = "field provider-field";
    const modelLabel = document.createElement("span");
    modelLabel.textContent = "模型";
    const modelRow = document.createElement("div");
    modelRow.className = "model-row";
    const modelInput = document.createElement("input");
    modelInput.className = "provider-model";
    modelInput.type = "text";
    modelInput.autocomplete = "off";
    modelInput.spellcheck = false;
    modelInput.value = provider.model || "";
    const modelOptions = document.createElement("datalist");
    modelOptions.className = "provider-model-options";
    for (const model of runtime.models || []) {
      const option = document.createElement("option");
      option.value = model;
      modelOptions.append(option);
    }
    modelInput.setAttribute("list", `${provider.id}-models`);
    modelOptions.id = `${provider.id}-models`;
    modelInput.addEventListener("change", () => updateProvider(provider.id).catch(() => {}));
    const fetchButton = document.createElement("button");
    fetchButton.type = "button";
    fetchButton.className = "secondary-button";
    fetchButton.title = "拉取模型";
    fetchButton.setAttribute("aria-label", "拉取模型");
    fetchButton.append(icon("i-refresh"));
    fetchButton.addEventListener("click", () => fetchProviderModels(provider.id));
    modelRow.append(modelInput, modelOptions, fetchButton);
    modelField.append(modelLabel, modelRow);

    const actions = document.createElement("div");
    actions.className = "provider-actions";
    const testButton = document.createElement("button");
    testButton.type = "button";
    testButton.className = "secondary-button";
    testButton.textContent = "测试模型";
    testButton.addEventListener("click", () => testProviderModel(provider.id));
    const providerStatus = document.createElement("span");
    providerStatus.className =
      `provider-status${runtime.kind ? ` ${runtime.kind}` : ""}`;
    providerStatus.dataset.providerStatus = provider.id;
    providerStatus.textContent = runtime.text || "未测试";
    const currentButton = document.createElement("button");
    currentButton.type = "button";
    currentButton.className = "provider-current-button";
    currentButton.textContent = isCurrent ? "当前使用" : "使用";
    currentButton.disabled = isCurrent;
    currentButton.addEventListener("click", () => setCurrentProvider(provider.id));
    const removeButton = document.createElement("button");
    removeButton.type = "button";
    removeButton.className = "provider-remove-button";
    removeButton.title = "移除供应商";
    removeButton.setAttribute("aria-label", "移除供应商");
    removeButton.append(icon("i-x"));
    removeButton.addEventListener("click", () => removeProvider(provider.id));
    actions.append(testButton, providerStatus, currentButton, removeButton);

    body.append(baseField, keyField, modelField, actions);
    section.append(header, body);
    dom.providerList.append(section);
  }
}

function setProviderRuntime(id, patch) {
  const runtime = { ...(providerRuntime.get(id) || {}), ...patch };
  providerRuntime.set(id, runtime);
  const status = document.querySelector(`[data-provider-status="${id}"]`);
  if (status) {
    status.textContent = runtime.text || "未测试";
    status.className = `provider-status${runtime.kind ? ` ${runtime.kind}` : ""}`;
  }
}

function beginProviderRename(section, name, rename, provider) {
  if (section.querySelector(".provider-name-input")) {
    return;
  }
  const input = document.createElement("input");
  input.className = "provider-name-input";
  input.value = provider.name;
  input.maxLength = 32;
  name.replaceWith(input);
  rename.hidden = true;
  input.focus();
  input.select();
  let finished = false;
  const finish = async (save) => {
    if (finished) {
      return;
    }
    finished = true;
    const value = input.value.trim();
    input.replaceWith(name);
    rename.hidden = false;
    if (!save || !value || value === provider.name) {
      name.textContent = provider.name;
      return;
    }
    try {
      await updateProvider(provider.id, { name: value });
      name.textContent = value;
      showToast("供应商已重命名");
    } catch (error) {
      name.textContent = provider.name;
      showToast(error.message, "error");
    }
  };
  input.addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
      event.preventDefault();
      finish(true);
    } else if (event.key === "Escape") {
      event.preventDefault();
      finish(false);
    }
  });
  input.addEventListener("blur", () => finish(true));
}

async function updateProvider(id, extra = {}, options = {}) {
  const card = document.querySelector(`.provider-card[data-provider="${id}"]`);
  if (!card) {
    return null;
  }
  const keyValue = card.querySelector(".provider-api-key")?.value.trim();
  const baseProvider = providerById(id);
  const payload = {
    id,
    protocol: baseProvider?.protocol || "openai",
    baseUrl: card.querySelector(".provider-base-url").value.trim(),
    model: card.querySelector(".provider-model").value.trim(),
    ...extra,
  };
  if (keyValue) {
    payload.apiKey = keyValue;
  }
  const updated = await callNative("providers.update", payload);
  state.providers = Array.isArray(updated.providers)
    ? updated.providers
    : state.providers;
  const currentCard = document.querySelector(`.provider-card[data-provider="${id}"]`);
  const currentProvider = providerById(id);
  if (currentCard && currentProvider) {
    currentCard.querySelector(".provider-key-state")
      ?.classList.toggle("is-saved", Boolean(currentProvider.keySaved));
    const keyInput = currentCard.querySelector(".provider-api-key");
    if (keyInput && !options.preserveKey) {
      keyInput.value = "";
      keyInput.placeholder = currentProvider.keySaved
        ? "已保存，粘贴新密钥以替换"
        : "粘贴 API Key";
    }
  }
  setProviderRuntime(id, { text: "已保存", kind: "success" });
  return updated;
}

async function fetchProviderModels(id) {
  const card = document.querySelector(`.provider-card[data-provider="${id}"]`);
  const button = card?.querySelector(".model-row .secondary-button");
  if (button) {
    button.disabled = true;
  }
  setProviderRuntime(id, { text: "正在拉取", kind: "pending" });
  try {
    await updateProvider(id);
    const result = await callNative("models.list", { providerId: id });
    const models = result?.models || [];
    providerRuntime.set(id, {
      ...(providerRuntime.get(id) || {}),
      models,
      text: `获取 ${models.length} 个模型`,
      kind: "success",
    });
    renderApiSettings();
  } catch (error) {
    setProviderRuntime(id, { text: error.message, kind: "error" });
  } finally {
    const current = document.querySelector(
      `.provider-card[data-provider="${id}"] .model-row .secondary-button`,
    );
    if (current) {
      current.disabled = false;
    }
  }
}

async function testProviderModel(id) {
  const button = document.querySelector(
    `.provider-card[data-provider="${id}"] .provider-actions .secondary-button`,
  );
  if (button) {
    button.disabled = true;
  }
  setProviderRuntime(id, { text: "正在测试", kind: "pending" });
  try {
    await updateProvider(id);
    const result = await callNative("models.test", { providerId: id });
    setProviderRuntime(id, {
      text: `正常 ${result?.latencyMs || 0} ms`,
      kind: "success",
      latencyMs: Number(result?.latencyMs || 0),
    });
    showToast("模型连接正常");
  } catch (error) {
    setProviderRuntime(id, { text: error.message, kind: "error" });
  } finally {
    const current = document.querySelector(
      `.provider-card[data-provider="${id}"] .provider-actions .secondary-button`,
    );
    if (current) {
      current.disabled = false;
    }
  }
}

function setCurrentProvider(id) {
  selected.currentProviderId = id;
  renderApiSettings();
  showToast(`当前供应商已切换为${providerById(id).name}`);
}

function renderProviderPicker() {
  dom.providerPresetList.replaceChildren();
  const enabled = new Set(state.enabledProviderIds);
  for (const preset of API_PROVIDER_PRESETS.filter((provider) => !enabled.has(provider.id))) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "provider-preset-option";
    button.dataset.provider = preset.id;
    const name = document.createElement("strong");
    name.textContent = preset.name;
    button.append(name);
    button.addEventListener("click", () => addProvider(preset.id));
    dom.providerPresetList.append(button);
  }
  if (!dom.providerPresetList.children.length) {
    const empty = document.createElement("p");
    empty.className = "provider-picker-empty";
    empty.textContent = "所有供应商均已添加。";
    dom.providerPresetList.append(empty);
  }
}

function openProviderDialog() {
  renderProviderPicker();
  dom.providerDialog.showModal();
}

async function addProvider(id) {
  try {
    const updated = await callNative("providers.add", { id });
    applyState(updated);
    expandedProviders.add(id);
    dom.providerDialog.close();
    renderApiSettings();
    document
      .querySelector(`.provider-card[data-provider="${id}"] .provider-api-key`)
      ?.focus();
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function removeProvider(id) {
  try {
    const updated = await callNative("providers.remove", { id });
    expandedProviders.delete(id);
    if (selected.currentProviderId === id) {
      selected.currentProviderId = "";
    }
    applyState(updated);
    renderApiSettings();
    showToast("供应商已移除");
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function addPath(path) {
  const value = String(path || "").trim();
  if (!value) {
    showToast("请输入有效路径", "warning");
    return;
  }
  try {
    const updated = await callNative("settings.addPath", { path: value });
    applyState(updated);
    dom.pathInput.value = "";
    showToast("路径已添加");
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function removePath(path) {
  try {
    const updated = await callNative("settings.removePath", { path });
    applyState(updated);
    showToast("路径已移除");
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function browsePath() {
  try {
    const result = await callNative("settings.choosePath");
    if (result?.path) {
      dom.pathInput.value = result.path;
      await addPath(result.path);
    }
  } catch (error) {
    showToast(error.message, "error");
  }
}

function handleKeydown(event) {
  if ((event.ctrlKey && event.key.toLowerCase() === "k") ||
      (event.key === "/" && !event.target.closest("input"))) {
    event.preventDefault();
    focusSearch();
    return;
  }

  if (event.key === "Escape") {
    if (!dom.contextMenu.hidden) {
      closeContextMenu();
    } else if (selected.settingsOpen) {
      closeSettingsView();
    } else if (selected.query) {
      clearSearch();
    }
    return;
  }

  if (event.target === dom.search && event.key === "ArrowDown") {
    event.preventDefault();
    moveActive(1);
  } else if (event.target === dom.search && event.key === "ArrowUp") {
    event.preventDefault();
    moveActive(-1);
  } else if (event.target === dom.search && event.key === "Enter") {
    event.preventDefault();
    const first = document.querySelector(".skill-row");
    if (first) {
      first.click();
    }
  }
}

function bindEvents() {
  if (nativeBridge) {
    nativeBridge.addEventListener("message", (event) => {
      const message = event.data;
      if (!message) {
        return;
      }
      if (message.type === "catalogChanged") {
        loadState();
        return;
      }
      if (message.type === "translationProgress") {
        const completed = Number(message.data?.completed || 0);
        const total = Number(message.data?.total || 0);
        dom.translateAll.querySelector("span").textContent =
          total ? `${completed}/${total}` : "翻译中";
        return;
      }
      if (message.type === "translationFinished") {
        const waiters = batchWaiters.splice(0);
        waiters.forEach((waiter) => waiter.resolve(message.data || {}));
        return;
      }
      if (!message.id) {
        return;
      }
      const request = pending.get(message.id);
      if (!request) {
        return;
      }
      pending.delete(message.id);
      if (message.ok) {
        request.resolve(message.data);
      } else {
        request.reject(new Error(message.error?.message || "操作失败"));
      }
    });
  }

  document.querySelectorAll("[data-command]").forEach((button) => {
    button.addEventListener("click", async (event) => {
      event.stopPropagation();
      const command = button.dataset.command;
      try {
        const result = await callNative(command);
        if (command === "window.pin") {
          state.ui.pinned = Boolean(result?.pinned);
          button.classList.toggle("is-active", state.ui.pinned);
          showToast(state.ui.pinned ? "窗口已置顶" : "已取消置顶");
        }
      } catch (error) {
        showToast(error.message, "error");
      }
    });
  });

  document.querySelector("[data-window-drag]")?.addEventListener("pointerdown", (event) => {
    if (event.button !== 0 || event.target.closest("button")) {
      return;
    }
    callNative("window.drag").catch(() => {});
  });

  document.querySelectorAll("[data-resize]").forEach((handle) => {
    handle.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) {
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      callNative("window.resizeStart", { direction: handle.dataset.resize }).catch(() => {});
    });
  });

  dom.search.addEventListener("input", handleSearch);
  dom.clearSearch.addEventListener("click", clearSearch);
  dom.collapseAll.addEventListener("click", toggleAllCategories);
  dom.translateAll.addEventListener("click", translateAll);
  dom.themeToggle.addEventListener("click", async () => {
    const previous = state.ui.theme;
    const next = previous === "dark" ? "light" : "dark";
    applyTheme(next);
    try {
      await callNative("ui.setTheme", { theme: next });
      showToast(next === "dark" ? "已切换到深色主题" : "已切换到亮色主题");
    } catch (error) {
      applyTheme(previous);
      showToast(error.message, "error");
    }
  });
  dom.settingsButton.addEventListener("click", () => openSettings("paths"));
  dom.closeSettings.addEventListener("click", closeSettingsView);
  dom.tabPaths.addEventListener("click", () => switchSettingsTab("paths"));
  dom.tabApi.addEventListener("click", () => switchSettingsTab("api"));
  dom.addPathForm.addEventListener("submit", (event) => {
    event.preventDefault();
    addPath(dom.pathInput.value);
  });
  dom.browsePath.addEventListener("click", browsePath);
  dom.addProviderButton.addEventListener("click", openProviderDialog);
  dom.closeProviderDialog.addEventListener("click", () => dom.providerDialog.close());
  dom.providerDialog.addEventListener("click", (event) => {
    if (event.target === dom.providerDialog) {
      dom.providerDialog.close();
    }
  });
  dom.contextMenu.querySelectorAll("[data-category]").forEach((button) => {
    button.addEventListener("click", () => moveContextSkill(button.dataset.category));
  });
  dom.retranslateSkill.addEventListener("click", retranslateContextSkill);
  document.addEventListener("pointerdown", (event) => {
    if (!dom.contextMenu.hidden && !dom.contextMenu.contains(event.target)) {
      closeContextMenu();
    }
  });
  document.addEventListener("keydown", handleKeydown);
}

bindEvents();
loadState();
window.SkillBoxE2EReady = true;
