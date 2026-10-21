#include <winsock2.h>
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    Check(config.providers.empty() && config.enabledProviderIds.empty(),
          "new config starts without a provider");
    const auto presets = skillbox::DefaultApiProviders();
    config.providers["openai-responses"] = presets.at("openai-responses");
    config.enabledProviderIds.push_back("openai-responses");
    config.categoryDefinitions[0].name = "高频";
    config.categories["C:\\skills\\demo\\SKILL.md"] = "favorite";
    config.translations["C:\\skills\\demo\\SKILL.md"] = {"abc123", "演示", "2026-10-18"};
    config.translationsEn["C:\\skills\\demo\\SKILL.md"] = {"abc123", "demo", "2026-10-18"};
    auto* provider = skillbox::FindApiProvider(config, "openai-responses");
    Check(provider != nullptr, "default Responses provider exists");
    if (provider) {
        provider->apiKeyCipher = "cipher";
        provider->name = "主 Responses";
    }
    config.providers["deepseek"] = presets.at("deepseek");
    config.enabledProviderIds.push_back("deepseek");
    config.ui.collapsedCategories.push_back("backend");
    config.ui.theme = "dark";

    const nlohmann::json json = skillbox::ToJson(config);
    std::string error;
    const auto parsed = skillbox::FromJson(json, &error);
    Check(parsed.has_value(), "config JSON round trip parses");
    if (parsed) {
        Check(parsed->categories.at("C:\\skills\\demo\\SKILL.md") == "favorite",
              "config category survives round trip");
        Check(parsed->translations.at("C:\\skills\\demo\\SKILL.md").text == "演示",
              "config translation survives UTF-8 round trip");
        Check(parsed->translationsEn.at("C:\\skills\\demo\\SKILL.md").text == "demo",
              "English translation survives round trip");
        const auto* parsedProvider =
            skillbox::FindApiProvider(*parsed, "openai-responses");
        Check(
            parsedProvider && parsedProvider->apiKeyCipher == "cipher",
            "provider cipher survives round trip");
        Check(
            parsedProvider && parsedProvider->name == "主 Responses",
            "provider name survives round trip");
        Check(
            parsedProvider && parsedProvider->protocol == "openai-responses",
            "provider protocol survives round trip");
        Check(
            parsed->enabledProviderIds ==
                std::vector<std::string>{"openai-responses", "deepseek"},
            "enabled provider list survives round trip");
        Check(parsed->categoryDefinitions[0].name == "高频",
              "category definitions survive round trip");
        Check(parsed->ui.theme == "dark", "theme survives config round trip");
    }

    nlohmann::json legacy = skillbox::ToJson(config);
    legacy.erase("categoryDefinitions");
    const auto migrated = skillbox::FromJson(legacy);
    Check(migrated.has_value() && migrated->categoryDefinitions.size() == 5,
          "legacy config receives default category definitions");

    nlohmann::json legacyApi = skillbox::ToJson(config);
    legacyApi.erase("providers");
    legacyApi.erase("enabledProviderIds");
    legacyApi["api"] = {
        {"protocol", "openai"},
        {"baseUrl", "https://legacy.example/v1"},
        {"model", "legacy-model"},
        {"apiKeyCipher", "legacy-cipher"},
    };
    const auto legacyApiMigrated = skillbox::FromJson(legacyApi);
    const auto* migratedProvider =
        legacyApiMigrated
            ? skillbox::FindApiProvider(*legacyApiMigrated, "openai-chat")
            : nullptr;
    Check(
        migratedProvider && migratedProvider->model == "legacy-model" &&
            migratedProvider->apiKeyCipher == "legacy-cipher" &&
            legacyApiMigrated->enabledProviderIds ==
                std::vector<std::string>{"openai-chat"},
        "legacy single-provider config migrates into the provider list");
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
    config.providers["openai-responses"] =
        skillbox::DefaultApiProviders().at("openai-responses");
    config.enabledProviderIds.push_back("openai-responses");
    auto* provider = skillbox::FindApiProvider(config, "openai-responses");
    Check(provider != nullptr, "atomic save provider exists");
    if (provider) {
        provider->model = "atomic-model";
    }
    skillbox::ConfigService service(file);
    Check(service.Save(config), "atomic config save succeeds");
    Check(std::filesystem::exists(file), "atomic save creates final config");
    Check(!std::filesystem::exists(temporary), "atomic save removes temporary file");

    const skillbox::LoadResult loaded = service.Load();
    Check(!loaded.recreated, "saved config is valid");
    const auto* loadedProvider =
        skillbox::FindApiProvider(loaded.config, "openai-responses");
    Check(
        loadedProvider && loadedProvider->model == "atomic-model",
        "atomic save preserves provider values");
}

void TestConfigSaveFailure() {
    const std::filesystem::path directory = TempDirectory();
    const std::filesystem::path file = directory / "config.json";
    std::filesystem::create_directories(file);

    skillbox::ConfigService service(file);
    Check(!service.Save(skillbox::DefaultConfig()), "config save failure is reported");
    Check(std::filesystem::is_directory(file), "failed save leaves the target untouched");
    Check(!std::filesystem::exists(directory / "config.json.tmp"),
          "failed save removes its temporary file");

    const skillbox::LoadResult result = service.Load();
    Check(!result.recreated, "unbackupable corrupt config is not replaced");
    Check(std::filesystem::is_directory(file),
          "unbackupable corrupt config keeps the original file");
    Check(result.warning.find("could not be backed up") != std::string::npos,
          "unbackupable corrupt config explains the failure");
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

void TestProviderSecretPersistence() {
    const std::filesystem::path directory = TempDirectory();
    const std::filesystem::path file = directory / "config.json";
    skillbox::AppConfig config = skillbox::DefaultConfig();
    config.providers["deepseek"] =
        skillbox::DefaultApiProviders().at("deepseek");
    config.enabledProviderIds.push_back("deepseek");
    auto* provider = skillbox::FindApiProvider(config, "deepseek");
    Check(provider != nullptr, "provider secret test target exists");
    if (provider) {
        provider->apiKeyCipher = skillbox::ProtectSecret("sk-persisted-key");
    }

    skillbox::ConfigService service(file);
    Check(service.Save(config), "provider secret config saves");
    std::ifstream stream(file, std::ios::binary);
    const std::string text(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    Check(
        text.find("sk-persisted-key") == std::string::npos,
        "provider key is not written as plaintext");

    const skillbox::LoadResult loaded = service.Load();
    const auto* loadedProvider =
        skillbox::FindApiProvider(loaded.config, "deepseek");
    const auto secret = loadedProvider
                            ? skillbox::UnprotectSecret(loadedProvider->apiKeyCipher)
                            : std::nullopt;
    Check(
        secret && *secret == "sk-persisted-key",
        "provider key survives a config reload");
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

    std::vector<std::string> order;
    for (const auto& definition : config.categoryDefinitions) {
        order.push_back(definition.id);
    }
    std::swap(order[0], order[1]);
    Check(skillbox::ReorderCategories(config, order), "category reorder succeeds");
    Check(config.categoryDefinitions[0].id == order[0],
          "category reorder is applied");
    Check(!skillbox::ReorderCategories(config, {"favorite"}),
          "incomplete category order is rejected");

    Check(skillbox::DeleteCategory(config, customId), "category delete succeeds");
    Check(!config.categories.contains("C:\\Skills\\demo\\SKILL.md"),
          "deleting a category clears its Skill assignments");
    Check(!skillbox::DeleteCategory(config, "other"),
          "fallback category cannot be deleted");
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
    std::string responsesHeaders;
    std::string chatBody;
    std::string messageBody;
    std::string responsesBody;

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
            if (target == "/large") {
                return {200, std::string(9 * 1024 * 1024, 'x')};
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
            if (target == "/responses" && method == "POST") {
                responsesHeaders = headers;
                responsesBody = body;
                return {
                    200,
                    R"({"output":[{"type":"message","content":[{"type":"output_text","text":"OK"}]}]})"};
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

    const skillbox::HttpResponse plaintext = skillbox::SendHttp({
        .method = "GET",
        .url = "http://example.com/",
    });
    Check(
        !plaintext.ok && plaintext.error.find("HTTPS") != std::string::npos,
        "HTTP client rejects non-loopback plaintext endpoints");

    const skillbox::HttpResponse oversized = skillbox::SendHttp({
        .method = "GET",
        .url = server.baseUrl() + "/large",
    });
    Check(
        !oversized.ok && oversized.error == "response body too large",
        "HTTP client rejects oversized response bodies");

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

    skillbox::ProviderConfig responses{
        "openai-responses", server.baseUrl(), "responses-key", "model-a"};
    const skillbox::ModelsResult responsesModels =
        skillbox::ModelClient{}.ListModels(responses);
    Check(
        responsesModels.ok && responsesModels.models.size() == 2,
        "OpenAI Responses model list uses the compatible endpoint");
    const skillbox::CompletionResult responsesCompletion =
        skillbox::ModelClient{}.Complete(responses, "system", "user", 32);
    Check(
        responsesCompletion.ok && responsesCompletion.text == "OK",
        "OpenAI Responses completion parses");
    Check(
        responsesHeaders.find("Authorization: Bearer responses-key") != std::string::npos,
        "OpenAI Responses request sends bearer token");
    Check(
        responsesBody.find("\"instructions\":\"system\"") != std::string::npos &&
            responsesBody.find("\"max_output_tokens\":32") != std::string::npos,
        "OpenAI Responses completion sends instructions and limit");
}

void TestEnglishTranslationValidation() {
    Check(skillbox::IsUsableEnglishTranslation(
              "Review interface hierarchy and interaction details.",
              "审查界面层级与交互细节。"),
          "English translation is accepted");
    Check(!skillbox::IsUsableEnglishTranslation("中文", "审查界面层级与交互细节。"),
          "Chinese-only text is rejected as English");
    Check(!skillbox::IsUsableEnglishTranslation("OK", "审查界面层级与交互细节。"),
          "too-short English text is rejected");
}

void TestTranslationBatch() {
    Check(skillbox::LooksChinese("中文描述"), "Chinese description detection works");
    Check(!skillbox::LooksChinese("English description"), "English description is detected");

    const std::string source =
        "Review interface hierarchy, spacing, typography, and interaction polish.";
    const std::string noisy =
        "We need answer translate software skill description into concise Simplified Chinese. "
        "Only translation no quotes commentary. Need parse. Source: \"Review interface "
        "hierarchy, spacing, typography, and interaction polish.\" Could say: \"用于审查界面"
        "层级、间距、排版与交互细节。\" Need be accurate. Let's craft natural: \"检查界面"
        "层级、间距、排版与交互细节。\"";
    Check(
        skillbox::NormalizeTranslation(noisy) ==
            "检查界面层级、间距、排版与交互细节。",
        "translation normalizer extracts the final Chinese answer");
    Check(
        skillbox::NormalizeTranslation(
            "```json\n{\"translation\":\"检查界面。\"}\n```") == "检查界面。",
        "translation normalizer extracts JSON output");
    const std::string labeled =
        skillbox::NormalizeTranslation("译文：\"检查界面。\"");
    Check(labeled == "检查界面。", "translation normalizer strips labels and quotes");
    Check(
        skillbox::IsUsableTranslation(skillbox::NormalizeTranslation(noisy)),
        "normalized translation passes the quality check");
    Check(
        !skillbox::IsUsableTranslation(
            "We need translate the source but there is no final Chinese answer."),
        "noise without Chinese is rejected");
    const std::string longSource =
        "Use when creating or updating AGENTS.md files, onboarding AI agents "
        "to a project, standardizing agent documentation, or documenting "
        "codebase conventions for AI agents.";
    const std::string partial =
        "We need translate the source. \"codebase documentation for AI agents\" "
        "= 面向 AI 代理的代码库文档。 Need only translation.";
    Check(
        !skillbox::IsUsableTranslation(partial, longSource),
        "partial Chinese term from noisy output is rejected");

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

    const skillbox::TranslationBatchResult recovered =
        skillbox::RunTranslationBatch(
            {
                {"e", source},
                {"f", "Another English description."},
            },
            {},
            false,
            [&](const std::string& description) {
                if (description.starts_with("Another")) {
                    return skillbox::CompletionResult{
                        true,
                        "We need translate but cannot produce a Chinese answer.",
                        {},
                    };
                }
                return skillbox::CompletionResult{true, noisy, {}};
            });
    Check(
        recovered.translated == 1 && recovered.failed == 1 &&
            recovered.entries.contains("e") &&
            recovered.entries.at("e").text ==
                "检查界面层级、间距、排版与交互细节。",
        "translation batch normalizes usable output and rejects noise");
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
    TestConfigSaveFailure();
    TestDpapiRoundTrip();
    TestProviderSecretPersistence();
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
    TestEnglishTranslationValidation();
    WSACleanup();
    return failures == 0 ? 0 : 1;
}
