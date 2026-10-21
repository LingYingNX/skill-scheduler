import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { extname, join, normalize, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(fileURLToPath(new URL("../..", import.meta.url)));
const port = Number(process.env.PORT || 4173);
const types = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
};

createServer(async (request, response) => {
  try {
    const url = new URL(request.url || "/", `http://${request.headers.host}`);
    let requestPath = normalize(decodeURIComponent(url.pathname))
      .replace(/^([/\\])+/, "")
      .replaceAll("\\", "/");
    if (!requestPath.includes("/") && [".css", ".js", ".svg"].some((suffix) => requestPath.endsWith(suffix))) {
      requestPath = join("web", requestPath);
    }
    let file = resolve(join(root, requestPath || "web/index.html"));
    const fromRoot = relative(root, file);
    if (fromRoot.startsWith("..") || fromRoot.includes(":")) {
      response.writeHead(403).end("forbidden");
      return;
    }
    if (url.pathname === "/" || url.pathname === "/index.html") {
      file = resolve(join(root, "web/index.html"));
      let html = await readFile(file, "utf8");
      html = html.replace(
        '<script type="module" src="./app.js"></script>',
        '<script src="/tools/preview/mock.js"></script><script type="module" src="/web/app.js"></script>',
      );
      response.writeHead(200, { "content-type": types[".html"], "cache-control": "no-store" });
      response.end(html);
      return;
    }
    const data = await readFile(file);
    response.writeHead(200, {
      "content-type": types[extname(file)] || "application/octet-stream",
      "cache-control": "no-store",
    });
    response.end(data);
  } catch {
    response.writeHead(404).end("not found");
  }
}).listen(port, "127.0.0.1", () => {
  console.log(`SkillBox preview: http://127.0.0.1:${port}`);
});
