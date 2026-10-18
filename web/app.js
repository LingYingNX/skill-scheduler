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

const CATEGORY_META = {
  favorite: { icon: "i-star", tone: "favorite" },
  uncommon: { icon: "i-archive", tone: "uncommon" },
  frontend: { icon: "i-panels", tone: "frontend" },
  backend: { icon: "i-server", tone: "backend" },
  other: { icon: "i-package", tone: "other" },
};

const state = {
  skills: [],
  roots: [],
  paths: [],
  categoryDefinitions: [...DEFAULT_CATEGORY_DEFINITIONS],
  api: {
    protocol: "openai",
    baseUrl: "https://api.openai.com/v1",
    model: "gpt-4o-mini",
    keySaved: false,
  },
  ui: {
    pinned: false,
    allCollapsed: false,
    collapsedCategories: [],
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
  closeSettings: document.querySelector("#close-settings"),
  settingsDone: document.querySelector("#settings-done"),
  tabPaths: document.querySelector("#tab-paths"),
  tabApi: document.querySelector("#tab-api"),
  pathsPanel: document.querySelector("#paths-panel"),
  apiPanel: document.querySelector("#api-panel"),
  pathsList: document.querySelector("#paths-list"),
  pathsCount: document.querySelector("#paths-count"),
  addPathForm: document.querySelector("#add-path-form"),
  pathInput: document.querySelector("#path-input"),
  browsePath: document.querySelector("#browse-path"),
  apiBaseUrl: document.querySelector("#api-base-url"),
  apiKey: document.querySelector("#api-key"),
  apiModel: document.querySelector("#api-model"),
  modelOptions: document.querySelector("#model-options"),
  fetchModels: document.querySelector("#fetch-models"),
  testModel: document.querySelector("#test-model"),
  apiStatus: document.querySelector("#api-status"),
  toggleKey: document.querySelector("#toggle-key"),
  translateAll: document.querySelector("#translate-all"),
  contextMenu: document.querySelector("#context-menu"),
  toast: document.querySelector("#toast"),
  toastText: document.querySelector("#toast-text"),
  settingsSaveState: document.querySelector("#settings-save-state"),
  retranslateSkill: document.querySelector("#retranslate-skill"),
  contextCategoryActions: document.querySelector("#context-category-actions"),
};

let toastTimer = 0;
const batchWaiters = [];

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

function categoryById(id) {
  return categories().find((item) => item.id === id) || categoryFromDefinition({
    id,
    name: id,
  });
}

function categoryFromDefinition(definition) {
  const meta = CATEGORY_META[definition.id] || {
    icon: "i-package",
    tone: "other",
  };
  return {
    ...definition,
    label: definition.name,
    ...meta,
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
  return selected.query.trim().toLocaleLowerCase("zh-CN");
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
  command.textContent = skill.command;

  const description = document.createElement("span");
  description.className = "skill-description";
  const showingOriginal = selected.originalPaths.has(skill.path);
  description.textContent =
    skill.translation && !showingOriginal ? skill.translation : skill.description || "无描述";
  description.title = skill.description || "";

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

  row.append(command, description, actions);
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
    section.dataset.tone = category.tone;

    const header = document.createElement("button");
    header.type = "button";
    header.className = "category-header";
    header.setAttribute("aria-expanded", String(!collapsed));
    const chevron = icon("i-chevron-down");
    chevron.classList.add("category-chevron");
    const categoryIcon = icon(category.icon);
    categoryIcon.classList.add("category-icon");
    const label = document.createElement("span");
    label.className = "category-label";
    label.textContent = category.name;
    const count = document.createElement("span");
    count.className = "category-count";
    count.textContent = String(items.length);
    header.append(chevron, categoryIcon, label, count);
    let toggleTimer = 0;
    header.addEventListener("click", (event) => {
      clearTimeout(toggleTimer);
      if (event.detail > 1) {
        return;
      }
      toggleTimer = window.setTimeout(() => toggleCategory(category.id), 220);
    });
    header.addEventListener("dblclick", (event) => {
      event.preventDefault();
      event.stopPropagation();
      clearTimeout(toggleTimer);
      beginCategoryRename(section, header, label, category);
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
  if (!state.api.keySaved) {
    showToast("先在设置中保存 API Key", "warning");
    openSettings("api");
    return;
  }
  showToast("正在翻译描述");
  try {
    await callNative("translation.one", { path: skill.path });
    await loadState();
    showToast("翻译已保存到 config.json");
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function translateAll() {
  if (!state.api.keySaved) {
    showToast("先在设置中保存 API Key", "warning");
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
    const accepted = await callNative("translation.batch");
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
  renderCategories();
  try {
    await callNative("ui.setCollapsed", {
      allCollapsed: state.ui.allCollapsed,
      collapsedCategories: state.ui.collapsedCategories,
    });
  } catch (error) {
    showToast(error.message, "error");
  }
}

async function toggleAllCategories() {
  state.ui.allCollapsed = !state.ui.allCollapsed;
  state.ui.collapsedCategories = [];
  renderCategories();
  try {
    await callNative("ui.setCollapsed", {
      allCollapsed: state.ui.allCollapsed,
      collapsedCategories: state.ui.collapsedCategories,
    });
  } catch (error) {
    showToast(error.message, "error");
  }
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
    button.append(icon(category.icon));
    const label = document.createElement("span");
    label.textContent = category.name;
    button.append(label);
    button.addEventListener("click", () => moveContextSkill(category.id));
    dom.contextCategoryActions.append(button);
  }
  dom.contextMenu.hidden = false;
  const width = 148;
  const height = dom.contextMenu.offsetHeight;
  dom.contextMenu.style.left = `${Math.max(6, Math.min(event.clientX, window.innerWidth - width - 6))}px`;
  dom.contextMenu.style.top = `${Math.max(44, Math.min(event.clientY, window.innerHeight - height - 6))}px`;
}

function closeContextMenu() {
  selected.contextPath = "";
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

function beginCategoryRename(section, header, label, category) {
  if (section.querySelector(".category-name-input")) {
    return;
  }
  label.hidden = true;
  const input = document.createElement("input");
  input.className = "category-name-input";
  input.value = category.name;
  input.maxLength = 24;
  header.append(input);
  input.focus();
  input.select();
  let finished = false;
  const finish = async (save) => {
    if (finished) {
      return;
    }
    finished = true;
    const name = input.value.trim();
    input.remove();
    label.hidden = false;
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
  showToast("正在重新翻译");
  try {
    await callNative("translation.one", { path, force: true });
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
  state.api = next.api || state.api;
  state.ui = next.ui || state.ui;
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
    (tab === "paths" ? dom.pathInput : dom.apiBaseUrl).focus();
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
  document.querySelectorAll('input[name="protocol"]').forEach((radio) => {
    radio.checked = radio.value === state.api.protocol;
  });
  dom.apiBaseUrl.value = state.api.baseUrl || "";
  dom.apiModel.value = state.api.model || "";
  dom.apiKey.value = "";
  dom.apiKey.placeholder = state.api.keySaved
    ? "已保存，输入新密钥以替换"
    : "输入 API Key";
}

async function updateApiSettings(extra = {}) {
  const protocol =
    document.querySelector('input[name="protocol"]:checked')?.value || "openai";
  const payload = {
    protocol,
    baseUrl: dom.apiBaseUrl.value.trim(),
    model: dom.apiModel.value.trim(),
    ...extra,
  };
  if (dom.apiKey.value.trim()) {
    payload.apiKey = dom.apiKey.value.trim();
  }
  try {
    const result = await callNative("settings.updateApi", payload);
    if (result?.api) {
      state.api = result.api;
    }
    dom.apiKey.value = "";
    setApiStatus("已保存", "success");
  } catch (error) {
    setApiStatus(error.message, "error");
    throw error;
  }
}

function setApiStatus(text, kind = "") {
  dom.apiStatus.textContent = text;
  dom.apiStatus.className = `api-status${kind ? ` ${kind}` : ""}`;
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

async function fetchModels() {
  dom.fetchModels.disabled = true;
  setApiStatus("正在拉取", "pending");
  try {
    await updateApiSettings();
    const result = await callNative("models.list");
    dom.modelOptions.replaceChildren();
    for (const model of result?.models || []) {
      const option = document.createElement("option");
      option.value = model;
      dom.modelOptions.append(option);
    }
    setApiStatus(`获取 ${result?.models?.length || 0} 个模型`, "success");
  } catch (error) {
    setApiStatus(error.message, "error");
  } finally {
    dom.fetchModels.disabled = false;
  }
}

async function testModel() {
  dom.testModel.disabled = true;
  setApiStatus("正在测试", "pending");
  try {
    await updateApiSettings();
    const result = await callNative("models.test");
    setApiStatus(`正常 ${result?.latencyMs || 0} ms`, "success");
    showToast("模型连接正常");
  } catch (error) {
    setApiStatus(error.message, "error");
  } finally {
    dom.testModel.disabled = false;
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
  dom.settingsButton.addEventListener("click", () => openSettings("paths"));
  dom.closeSettings.addEventListener("click", closeSettingsView);
  dom.settingsDone.addEventListener("click", closeSettingsView);
  dom.tabPaths.addEventListener("click", () => switchSettingsTab("paths"));
  dom.tabApi.addEventListener("click", () => switchSettingsTab("api"));
  dom.addPathForm.addEventListener("submit", (event) => {
    event.preventDefault();
    addPath(dom.pathInput.value);
  });
  dom.browsePath.addEventListener("click", browsePath);
  dom.fetchModels.addEventListener("click", fetchModels);
  dom.testModel.addEventListener("click", testModel);
  dom.toggleKey.addEventListener("click", () => {
    dom.apiKey.type = dom.apiKey.type === "password" ? "text" : "password";
  });
  dom.apiBaseUrl.addEventListener("change", () => updateApiSettings().catch(() => {}));
  dom.apiModel.addEventListener("change", () => updateApiSettings().catch(() => {}));
  document.querySelectorAll('input[name="protocol"]').forEach((radio) => {
    radio.addEventListener("change", () => {
      const protocol = radio.value;
      dom.apiBaseUrl.value =
        protocol === "anthropic"
          ? "https://api.anthropic.com/v1"
          : "https://api.openai.com/v1";
      updateApiSettings({ protocol }).catch(() => {});
    });
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
