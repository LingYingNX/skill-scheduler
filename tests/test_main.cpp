#include <winsock2.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "core/ChangeMonitor.h"
#include "core/Hash.h"
#include "core/PathUtils.h"
#include "core/SkillCatalog.h"
#include "core/SkillParser.h"
#include "bridge/Bridge.h"
#include "net/HttpClient.h"
#include "net/ModelClient.h"
#include "net/TranslationService.h"
#include "platform/Clipboard.h"
#include "platform/Dpapi.h"
#include "MockHttpServer.h"

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::filesystem::path TempDirectory() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("SkillBoxTests-" + std::to_string(GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
    return path;
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << text;
}

void TestConfigRoundTrip() {
    skillbox::AppConfig config = skillbox::DefaultConfig();
    config.categoryDefinitions[0].name = "高频";
    config.categories["C:\\skills\\demo\\SKILL.md"] = "favorite";
    config.translations["C:\\skills\\demo\\SKILL.md"] = {"abc123", "演示", "2026-10-18"};
    config.api.apiKeyCipher = "cipher";
    config.ui.collapsedCategories.push_back("backend");

    const nlohmann::json json = skillbox::ToJson(config);
    std::string error;
    const auto parsed = skillbox::FromJson(json, &error);
    Check(parsed.has_value(), "config JSON round trip parses");
    if (parsed) {
        Check(parsed->categories.at("C:\\skills\\demo\\SKILL.md") == "favorite",
              "config category survives round trip");
        Check(parsed->translations.at("C:\\skills\\demo\\SKILL.md").text == "演示",
              "config translation survives UTF-8 round trip");
        Check(parsed->api.apiKeyCipher == "cipher", "config cipher survives round trip");
        Check(parsed->categoryDefinitions[0].name == "高频",
              "category definitions survive round trip");
    }

    nlohmann::json legacy = skillbox::ToJson(config);
    legacy.erase("categoryDefinitions");
    const auto migrated = skillbox::FromJson(legacy);
    Check(migrated.has_value() && migrated->categoryDefinitions.size() == 5,
          "legacy config receives default category definitions");
}

void TestConfigRecovery() {
    const std::filesystem::path directory = TempDirectory();
    const std::filesystem::path file = directory / "config.json";
    {
        std::ofstream stream(file, std::ios::binary);
        stream << "{ invalid";
    }

    skillbox::ConfigService service(file);
    const skillbox::LoadResult result = service.Load();
    Check(result.recreated, "invalid config is detected");
    Check(!result.backupPath.empty(), "corrupt config is backed up");
    Check(std::filesystem::exists(file), "default config is recreated");
    Check(std::filesystem::exists(result.backupPath), "backup file exists");
}

void TestAtomicConfigSave() {
    const std::filesystem::path directory = TempDirectory();
    const std::filesystem::path file = directory / "config.json";
    const std::filesystem::path temporary = directory / "config.json.tmp";
    {
        std::ofstream stale(temporary, std::ios::binary);
        stale << "stale partial write";
    }

    skillbox::AppConfig config = skillbox::DefaultConfig();
    config.api.model = "atomic-model";
    skillbox::ConfigService service(file);
    Check(service.Save(config), "atomic config save succeeds");
    Check(std::filesystem::exists(file), "atomic save creates final config");
    Check(!std::filesystem::exists(temporary), "atomic save removes temporary file");

    const skillbox::LoadResult loaded = service.Load();
    Check(!loaded.recreated, "saved config is valid");
    Check(loaded.config.api.model == "atomic-model", "atomic save preserves current values");
}

void TestDpapiRoundTrip() {
    const std::string plaintext = "sk-test-secret";
    const std::string ciphertext = skillbox::ProtectSecret(plaintext);
    Check(!ciphertext.empty(), "DPAPI encryption succeeds");
    Check(ciphertext.find(plaintext) == std::string::npos,
          "DPAPI ciphertext does not contain plaintext");

    const auto restored = skillbox::UnprotectSecret(ciphertext);
    Check(restored.has_value() && *restored == plaintext, "DPAPI round trip succeeds");
    Check(!skillbox::UnprotectSecret("not-base64").has_value(),
          "invalid ciphertext is rejected");
}

void TestPathNormalization() {
    const std::filesystem::path directory = TempDirectory();
    const std::filesystem::path child = directory / "Nested" / "SKILL.md";
    std::filesystem::create_directories(child.parent_path());
    {
        std::ofstream stream(child);
        stream << "---\nname: nested\n---\n";
    }

    const std::string normalized = skillbox::NormalizePath(child);
    const std::string alternate =
        skillbox::WideToUtf8(directory.wstring()) + "\\nested\\skill.md";
    Check(skillbox::SamePath(normalized, alternate), "case and separators normalize");
    Check(skillbox::IsPathWithin(
              skillbox::NormalizePath(directory), normalized),
          "child path is within parent");
    Check(!skillbox::IsPathWithin(normalized, skillbox::NormalizePath(directory)),
          "parent path is not within child");

    const auto deduped = skillbox::DeduplicatePaths({normalized, alternate});
    Check(deduped.size() == 1, "duplicate paths collapse");
}

void TestSkillParser() {
    const std::filesystem::path directory = TempDirectory();
    const auto plain = directory / "plain.md";
    WriteText(
        plain,
        "---\nname: plain-skill\ndescription: Single-line description\n---\n");
    const skillbox::ParsedSkill plainResult = skillbox::ParseSkillFile(plain);
    Check(plainResult.ok && plainResult.description == "Single-line description",
          "plain single-line description parses");

    const auto folded = directory / "folded.md";
    WriteText(
        folded,
        "---\nname: folded-skill\ndescription: >\n  folded line one\n  folded line two\n---\n");
    const skillbox::ParsedSkill foldedResult = skillbox::ParseSkillFile(folded);
    Check(foldedResult.ok, "folded frontmatter parses");
    Check(foldedResult.name == "folded-skill", "quoted or plain name parses");
    Check(foldedResult.description.find("folded line one folded line two") != std::string::npos,
          "folded description joins lines");

    const auto literal = directory / "literal.md";
    WriteText(
        literal,
        "---\nname: literal-skill\ndescription: |\n  line one\n  line two\n---\n");
    const skillbox::ParsedSkill literalResult = skillbox::ParseSkillFile(literal);
    Check(literalResult.description.find("line one\nline two") != std::string::npos,
          "literal description preserves lines");

    const auto fallback = directory / "fallback" / "SKILL.md";
    WriteText(fallback, "---\ndescription: no name\n---\n");
    const skillbox::ParsedSkill fallbackResult = skillbox::ParseSkillFile(fallback);
    Check(fallbackResult.ok && fallbackResult.name == "fallback",
          "missing name uses directory name");
    Check(!fallbackResult.warning.empty(), "missing name records warning");

    const auto malformed = directory / "bad.md";
    WriteText(malformed, "---\nname: [broken\n---\n");
    Check(!skillbox::ParseSkillFile(malformed).ok, "malformed YAML is skipped");

    const auto unterminated = directory / "unterminated.md";
    WriteText(unterminated, "name: no-delimiters\n");
    Check(!skillbox::ParseSkillFile(unterminated).ok,
          "missing frontmatter delimiters is skipped");
}

void TestCatalogScanning() {
    const std::filesystem::path root = TempDirectory() / "root";
    WriteText(root / "deep" / "a" / "SKILL.md", "---\nname: zebra\ndescription: Z\n---\n");
    WriteText(root / "front" / "SKILL.md", "---\nname: alpha\ndescription: A\n---\n");
    WriteText(root / "bad" / "SKILL.md", "---\nname: [broken\n---\n");

    const std::filesystem::path missing = root.parent_path() / "missing";
    const std::string normalizedRoot = skillbox::NormalizePath(root);
    skillbox::AppConfig config = skillbox::DefaultConfig();
    skillbox::CatalogSnapshot snapshot = skillbox::ScanSkills(
        {normalizedRoot, normalizedRoot, skillbox::NormalizePath(missing)},
        {},
        config.categoryDefinitions);

    Check(snapshot.roots.size() == 2, "catalog reports each root once");
    Check(snapshot.roots[1].available == false, "unavailable root is reported");
    Check(snapshot.skills.size() == 2, "catalog loads valid Skills only");
    Check(snapshot.skills[0].name == "alpha" && snapshot.skills[1].name == "zebra",
          "catalog uses stable name ordering");
    Check(snapshot.skills[0].category == "other", "new Skill defaults to other");

    skillbox::SetSkillCategory(config, snapshot.skills[1].path, "favorite");
    snapshot = skillbox::ScanSkills(
        {normalizedRoot}, config.categories, config.categoryDefinitions);
    Check(snapshot.skills[0].name == "zebra" && snapshot.skills[0].category == "favorite",
          "configured category is applied and sorted first");

    config.translations[snapshot.skills[0].path] = {"hash", "译文", "now"};
    skillbox::ReconcileCatalogConfig(config, snapshot);
    Check(config.translations.contains(snapshot.skills[0].path),
          "translation survives while Skill exists");
    std::filesystem::remove_all(root / "deep");
    snapshot = skillbox::ScanSkills(
        {normalizedRoot}, config.categories, config.categoryDefinitions);
    skillbox::ReconcileCatalogConfig(config, snapshot);
    Check(!config.translations.contains(
              skillbox::NormalizePath(root / "deep" / "a" / "SKILL.md")),
          "translation is removed with deleted Skill");

    config.categories[skillbox::NormalizePath(missing / "cached" / "SKILL.md")] = "backend";
    const skillbox::CatalogSnapshot offline = skillbox::ScanSkills(
        {skillbox::NormalizePath(missing)},
        config.categories,
        config.categoryDefinitions);
    skillbox::ReconcileCatalogConfig(config, offline);
    Check(!config.categories.empty(), "offline root preserves cached category entries");
}

void TestCategoryEditing() {
    skillbox::AppConfig config = skillbox::DefaultConfig();
    Check(skillbox::RenameCategory(config, "favorite", "高频"), "category rename succeeds");
    Check(!skillbox::RenameCategory(config, "favorite", ""), "empty category rename is rejected");
    Check(!skillbox::RenameCategory(config, "favorite", "前端"),
          "duplicate category name is rejected");

    std::string customId;
    Check(skillbox::AddCategory(config, "设计", &customId), "category add succeeds");
    Check(!customId.empty() && skillbox::FindCategory(config, customId),
          "new category receives stable id");
    Check(!skillbox::AddCategory(config, "设计"), "duplicate category add is rejected");
    Check(skillbox::SetSkillCategory(config, "C:\\Skills\\demo\\SKILL.md", customId),
          "custom category accepts Skill assignment");
}

void TestSha256() {
    Check(
        skillbox::Sha256Hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 matches known vector");
}

bool WaitFor(std::atomic_int& value, int minimum, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (value.load() >= minimum) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return value.load() >= minimum;
}

void TestChangeMonitor() {
    const std::filesystem::path root = TempDirectory() / "watch";
    std::filesystem::create_directories(root);

    std::atomic_int changes = 0;
    skillbox::ChangeMonitor monitor;
    monitor.Start(
        {skillbox::NormalizePath(root)},
        [&] { ++changes; },
        std::chrono::seconds(10));

    WriteText(root / "SKILL.md", "---\nname: watched\n---\n");
    Check(WaitFor(changes, 1, std::chrono::seconds(2)),
          "native watcher reports added Skill");

    const int afterAdd = changes.load();
    WriteText(root / "SKILL.md", "---\nname: watched\ndescription: changed\n---\n");
    Check(WaitFor(changes, afterAdd + 1, std::chrono::seconds(2)),
          "native watcher reports modified Skill");

    const int afterModify = changes.load();
    std::filesystem::remove(root / "SKILL.md");
    Check(WaitFor(changes, afterModify + 1, std::chrono::seconds(2)),
          "native watcher reports deleted Skill");
    monitor.Stop();

    const std::filesystem::path missing = root.parent_path() / "missing-watch";
    std::atomic_int fallbackChanges = 0;
    monitor.Start(
        {skillbox::NormalizePath(missing)},
        [&] { ++fallbackChanges; },
        std::chrono::milliseconds(150));
    Check(WaitFor(fallbackChanges, 1, std::chrono::seconds(1)),
          "periodic fallback works when native watcher is unavailable");
    monitor.Stop();
}

void TestBridge() {
    skillbox::Bridge bridge(
        [](const std::string& command, const nlohmann::json& payload) {
            if (command == "echo") {
                return skillbox::CommandResult::Success(
                    {{"value", payload.value("value", std::string{})}});
            }
            return skillbox::CommandResult::Error(
                "unknown_command", "unknown command");
        });

    nlohmann::json success = nlohmann::json::parse(bridge.Handle(
        R"({"id":"1","command":"echo","payload":{"value":"ok"}})"));
    Check(success["id"] == "1" && success["ok"] == true,
          "bridge returns correlated success");
    Check(success["data"]["value"] == "ok", "bridge forwards payload");

    nlohmann::json error = nlohmann::json::parse(bridge.Handle(
        R"({"id":"2","command":"missing"})"));
    Check(error["id"] == "2" && error["ok"] == false,
          "bridge returns correlated error");
    Check(error["error"]["code"] == "unknown_command",
          "bridge exposes stable error code");

    nlohmann::json invalid = nlohmann::json::parse(bridge.Handle("{bad"));
    Check(invalid["ok"] == false && invalid["error"]["code"] == "invalid_json",
          "bridge rejects invalid JSON");
}

void TestClipboard() {
    const std::string expected = "/clipboard-test";
    Check(skillbox::SetClipboardText(expected), "clipboard write succeeds");
    const auto actual = skillbox::GetClipboardText();
    Check(actual.has_value() && *actual == expected, "clipboard round trip matches");
}

void TestHttpAndProviders() {
    std::string openAiHeaders;
    std::string anthropicHeaders;
    std::string chatBody;
    std::string messageBody;

    skillbox::test::MockHttpServer server(
        [&](const std::string& method,
            const std::string& target,
            const std::string& headers,
            const std::string& body) -> std::pair<int, std::string> {
            if (target == "/ping") {
                return {200, R"({"ok":true})"};
            }
            if (target == "/slow") {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                return {200, R"({"late":true})"};
            }
            if (target == "/models") {
                if (headers.find("x-api-key: anthropic-key") != std::string::npos) {
                    anthropicHeaders = headers;
                } else {
                    openAiHeaders = headers;
                }
                return {200, R"({"data":[{"id":"model-b"},{"id":"model-a"},{"id":"model-a"}]})"};
            }
            if (target == "/chat/completions" && method == "POST") {
                chatBody = body;
                return {200, R"({"choices":[{"message":{"content":"OK"}}]})"};
            }
            if (target == "/messages" && method == "POST") {
                messageBody = body;
                return {200, R"({"content":[{"type":"text","text":"OK"}]})"};
            }
            return {404, R"({"error":{"message":"not found"}})"};
        });
    Check(server.started(), "mock HTTP server starts");

    const skillbox::HttpResponse ping = skillbox::SendHttp({
        .method = "GET",
        .url = server.baseUrl() + "/ping",
    });
    Check(ping.ok && ping.statusCode == 200, "HTTP client receives successful response");
    Check(ping.body == R"({"ok":true})", "HTTP client preserves body");

    std::stop_source cancelled;
    cancelled.request_stop();
    const skillbox::HttpResponse cancelResponse = skillbox::SendHttp(
        {.method = "GET", .url = server.baseUrl() + "/ping"},
        cancelled.get_token());
    Check(cancelResponse.error == "request cancelled", "HTTP client honors cancellation");

    const skillbox::HttpResponse timeout = skillbox::SendHttp({
        .method = "GET",
        .url = server.baseUrl() + "/slow",
        .timeout = std::chrono::milliseconds(100),
    });
    Check(!timeout.ok && !timeout.error.empty(), "HTTP client enforces timeout");

    skillbox::ProviderConfig openai{
        "openai", server.baseUrl(), "openai-key", "model-a"};
    const skillbox::ModelsResult models = skillbox::ModelClient{}.ListModels(openai);
    Check(models.ok && models.models.size() == 2, "OpenAI model list parses and deduplicates");
    Check(
        openAiHeaders.find("Authorization: Bearer openai-key") != std::string::npos,
        "OpenAI request sends bearer token");

    const skillbox::CompletionResult completion =
        skillbox::ModelClient{}.Complete(openai, "system", "user", 32);
    Check(completion.ok && completion.text == "OK", "OpenAI completion parses");
    Check(chatBody.find("\"model\":\"model-a\"") != std::string::npos,
          "OpenAI completion sends selected model");

    skillbox::ProviderConfig anthropic{
        "anthropic", server.baseUrl(), "anthropic-key", "model-b"};
    Check(skillbox::ModelClient{}.ListModels(anthropic).ok,
          "Anthropic model list uses compatible endpoint");
    Check(
        anthropicHeaders.find("x-api-key: anthropic-key") != std::string::npos &&
            anthropicHeaders.find("anthropic-version: 2023-06-01") != std::string::npos,
        "Anthropic request sends required headers");
    Check(
        skillbox::ModelClient{}.Complete(anthropic, "system", "user", 32).ok,
        "Anthropic completion parses");
    Check(messageBody.find("\"system\":\"system\"") != std::string::npos,
          "Anthropic completion includes system prompt");
}

void TestTranslationBatch() {
    Check(skillbox::LooksChinese("中文描述"), "Chinese description detection works");
    Check(!skillbox::LooksChinese("English description"), "English description is detected");

    const skillbox::TranslationEntry valid =
        skillbox::MakeTranslationEntry("English description", "中文描述");
    Check(skillbox::TranslationIsValid(valid, "English description"),
          "translation hash remains valid");
    Check(!skillbox::TranslationIsValid(valid, "Changed description"),
          "translation invalidates after source change");

    int successCallbacks = 0;
    const skillbox::TranslationBatchResult batch = skillbox::RunTranslationBatch(
        {
            {"a", "First English description"},
            {"b", "Second English description"},
            {"c", "中文已经存在"},
            {"d", "English description"},
        },
        {{"d", valid}},
        false,
        [](const std::string& description) {
            if (description.starts_with("Second")) {
                return skillbox::CompletionResult{false, {}, "provider failed"};
            }
            return skillbox::CompletionResult{true, "译文", {}};
        },
        [&](const std::string&, const skillbox::TranslationEntry&) {
            ++successCallbacks;
        });
    Check(batch.translated == 1 && batch.failed == 1 && batch.skipped == 2,
          "translation batch isolates success, failure, and cache skips");
    Check(successCallbacks == 1 && batch.entries.contains("a"),
          "translation batch persists successful entries only");
}

}  // namespace

int main() {
    WSADATA socketData{};
    if (WSAStartup(MAKEWORD(2, 2), &socketData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
    TestConfigRoundTrip();
    TestConfigRecovery();
    TestAtomicConfigSave();
    TestDpapiRoundTrip();
    TestPathNormalization();
    TestSkillParser();
    TestCatalogScanning();
    TestCategoryEditing();
    TestSha256();
    TestChangeMonitor();
    TestBridge();
    TestClipboard();
    TestHttpAndProviders();
    TestTranslationBatch();
    WSACleanup();
    return failures == 0 ? 0 : 1;
}
