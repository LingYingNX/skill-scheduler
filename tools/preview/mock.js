(() => {
  const listeners = new Set();
  const state = {
    categoryDefinitions: [
      { id: "favorite", name: "常用" },
      { id: "uncommon", name: "不常用" },
      { id: "frontend", name: "前端" },
      { id: "backend", name: "后端" },
      { id: "other", name: "其他" },
    ],
    skills: [
      {
        path: "C:\\Skills\\frontend\\ui-review\\SKILL.md",
        root: "C:\\Skills",
        name: "ui-review",
        command: "/ui-review",
        description: "Review interface hierarchy, spacing, typography, and interaction polish.",
        translation: "检查界面层级、间距、排版与交互细节。",
        category: "favorite",
        warning: "",
      },
      {
        path: "C:\\Skills\\frontend\\motion-web\\SKILL.md",
        root: "C:\\Skills",
        name: "motion-web",
        command: "/motion-web",
        description: "Design and build motion-first websites with scroll-linked scenes.",
        translation: "设计并构建带滚动场景的动效网站。",
        category: "frontend",
        warning: "",
      },
      {
        path: "C:\\Skills\\frontend\\design-polish\\SKILL.md",
        root: "C:\\Skills",
        name: "design-polish",
        command: "/design-polish",
        description: "Refine hierarchy and remove visual noise before release.",
        translation: "发布前收敛层级并移除视觉噪音。",
        category: "frontend",
        warning: "",
      },
      {
        path: "C:\\Skills\\backend\\api-security\\SKILL.md",
        root: "C:\\Skills",
        name: "api-security",
        command: "/api-security",
        description: "Audit authentication, authorization, rate limits, and sensitive data paths.",
        translation: "审查鉴权、授权、限流与敏感数据路径。",
        category: "backend",
        warning: "",
      },
      {
        path: "C:\\Skills\\backend\\diagnosing-bugs\\SKILL.md",
        root: "C:\\Skills",
        name: "diagnosing-bugs",
        command: "/diagnosing-bugs",
        description: "Trace a failing behavior from reproduction to root cause.",
        translation: "",
        category: "backend",
        warning: "",
      },
      {
        path: "C:\\Skills\\workflow\\openspec-apply-change\\SKILL.md",
        root: "C:\\Skills",
        name: "openspec-apply-change",
        command: "/openspec-apply-change",
        description: "Implement tracked tasks from an OpenSpec change.",
        translation: "",
        category: "uncommon",
        warning: "description uses an unsupported detail",
      },
      {
        path: "C:\\Skills\\workflow\\session-resume\\SKILL.md",
        root: "C:\\Skills",
        name: "session-resume",
        command: "/session-resume",
        description: "Resume a previous coding agent session with evidence.",
        translation: "基于证据继续先前的编程代理会话。",
        category: "favorite",
        warning: "",
      },
      {
        path: "C:\\Skills\\other\\caveman\\SKILL.md",
        root: "C:\\Skills",
        name: "caveman",
        command: "/caveman",
        description: "Compress communication while preserving technical accuracy.",
        translation: "",
        category: "other",
        warning: "",
      },
    ],
    roots: [
      { path: "C:\\Skills", available: true, error: "" },
      { path: "D:\\Archived Skills", available: false, error: "路径不可用" },
    ],
    paths: ["C:\\Skills", "D:\\Archived Skills"],
    api: {
      protocol: "openai",
      baseUrl: "https://api.openai.com/v1",
      model: "gpt-4o-mini",
      keySaved: true,
    },
    ui: {
      pinned: false,
      allCollapsed: false,
      collapsedCategories: [],
    },
    warning: "",
    monitorDegraded: false,
  };

  function stateCopy() {
    return JSON.parse(JSON.stringify(state));
  }

  function emit(message) {
    listeners.forEach((listener) => listener({ data: message }));
  }

  function handle(command, payload = {}) {
    if (command === "catalog.getState") {
      return stateCopy();
    }
    if (command === "category.set") {
      const skill = state.skills.find((item) => item.path === payload.path);
      if (skill && state.categoryDefinitions.some((item) => item.id === payload.category)) {
        skill.category = payload.category;
      }
      return stateCopy();
    }
    if (command === "category.rename") {
      const category = state.categoryDefinitions.find((item) => item.id === payload.id);
      const name = String(payload.name || "").trim();
      if (!category || !name ||
          state.categoryDefinitions.some((item) => item.id !== payload.id && item.name === name)) {
        throw new Error("分类名称无效或已存在");
      }
      category.name = name;
      return stateCopy();
    }
    if (command === "category.add") {
      const name = String(payload.name || "").trim();
      if (!name || state.categoryDefinitions.some((item) => item.name === name)) {
        throw new Error("分类名称无效或已存在");
      }
      state.categoryDefinitions.push({
        id: `custom-${Date.now()}`,
        name,
      });
      return stateCopy();
    }
    if (command === "ui.setCollapsed") {
      if (typeof payload.allCollapsed === "boolean") {
        state.ui.allCollapsed = payload.allCollapsed;
      }
      if (payload.category) {
        state.ui.collapsedCategories = state.ui.collapsedCategories.filter(
          (item) => item !== payload.category,
        );
        if (payload.collapsed) {
          state.ui.collapsedCategories.push(payload.category);
        }
      }
      return stateCopy();
    }
    if (command === "settings.addPath") {
      if (state.paths.includes(payload.path)) {
        throw new Error("该路径已存在");
      }
      state.paths.push(payload.path);
      state.roots.push({ path: payload.path, available: true, error: "" });
      return stateCopy();
    }
    if (command === "settings.removePath") {
      state.paths = state.paths.filter((path) => path !== payload.path);
      state.roots = state.roots.filter((root) => root.path !== payload.path);
      return stateCopy();
    }
    if (command === "settings.choosePath") {
      return { path: "C:\\Skills" };
    }
    if (command === "settings.updateApi") {
      state.api = {
        ...state.api,
        protocol: payload.protocol,
        baseUrl: payload.baseUrl,
        model: payload.model,
        keySaved: state.api.keySaved || Boolean(payload.apiKey),
      };
      return { api: state.api };
    }
    if (command === "models.list") {
      return { models: ["gpt-4o-mini", "gpt-4.1", "deepseek-chat", "claude-sonnet-4"] };
    }
    if (command === "models.test") {
      return { latencyMs: 182 };
    }
    if (command === "translation.one") {
      const skill = state.skills.find((item) => item.path === payload.path);
      if (skill) {
        skill.translation = "中文译文已保存到配置文件。";
      }
      return { translated: Boolean(skill), failed: 0 };
    }
    if (command === "translation.batch") {
      return { translated: 3, failed: 1, skipped: 4 };
    }
    if (command.startsWith("window.")) {
      if (command === "window.pin") {
        return { pinned: !state.ui.pinned };
      }
      return {};
    }
    if (command === "clipboard.copy") {
      return { text: payload.text };
    }
    throw new Error(`preview command unavailable: ${command}`);
  }

  if (!window.chrome) {
    window.chrome = {};
  }
  window.chrome.webview = {
    addEventListener(type, listener) {
      if (type === "message") {
        listeners.add(listener);
      }
    },
    removeEventListener(type, listener) {
      if (type === "message") {
        listeners.delete(listener);
      }
    },
    postMessage(request) {
      Promise.resolve().then(() => {
        try {
          emit({
            id: request.id,
            ok: true,
            data: handle(request.command, request.payload),
          });
        } catch (error) {
          emit({
            id: request.id,
            ok: false,
            error: { code: "preview_error", message: error.message },
          });
        }
      });
    },
  };

  window.SkillBoxPreviewControl = {
    setMonitorDegraded(value) {
      state.monitorDegraded = Boolean(value);
    },
    catalogChanged() {
      emit({ type: "catalogChanged" });
    },
  };
})();
