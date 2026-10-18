#include "net/ModelClient.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

#include "net/HttpClient.h"

namespace skillbox {
namespace {

std::string JoinUrl(std::string base, const std::string& path) {
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + path;
}

std::string ProviderError(const HttpResponse& response) {
    if (!response.body.empty()) {
        try {
            const nlohmann::json json = nlohmann::json::parse(response.body);
            if (json.contains("error")) {
                if (json["error"].is_string()) {
                    return json["error"].get<std::string>();
                }
                if (json["error"].is_object() && json["error"].contains("message")) {
                    return json["error"]["message"].get<std::string>();
                }
            }
        } catch (...) {
        }
    }
    return response.error.empty() ? "model request failed" : response.error;
}

std::vector<std::pair<std::string, std::string>> Headers(
    const ProviderConfig& config) {
    std::vector<std::pair<std::string, std::string>> headers{
        {"Content-Type", "application/json"},
        {"Accept", "application/json"},
    };
    if (config.protocol == "anthropic") {
        headers.emplace_back("x-api-key", config.apiKey);
        headers.emplace_back("anthropic-version", "2023-06-01");
    } else {
        headers.emplace_back("Authorization", "Bearer " + config.apiKey);
    }
    return headers;
}

}  // namespace

ModelsResult ModelClient::ListModels(const ProviderConfig& config) const {
    ModelsResult result;
    if (config.apiKey.empty()) {
        result.error = "API Key 未配置";
        return result;
    }
    const HttpResponse response = SendHttp(HttpRequest{
        .method = "GET",
        .url = JoinUrl(config.baseUrl, "/models"),
        .headers = Headers(config),
    });
    if (!response.ok) {
        result.error = ProviderError(response);
        return result;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(response.body);
        if (json.contains("data") && json["data"].is_array()) {
            for (const auto& item : json["data"]) {
                if (item.is_object() && item.contains("id") && item["id"].is_string()) {
                    result.models.push_back(item["id"].get<std::string>());
                }
            }
        }
        std::sort(result.models.begin(), result.models.end());
        result.models.erase(
            std::unique(result.models.begin(), result.models.end()),
            result.models.end());
        result.ok = !result.models.empty();
        if (!result.ok) {
            result.error = "服务没有返回可用模型";
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
    }
    return result;
}

CompletionResult ModelClient::Complete(
    const ProviderConfig& config,
    const std::string& systemPrompt,
    const std::string& userPrompt,
    int maxTokens) const {
    CompletionResult result;
    if (config.apiKey.empty()) {
        result.error = "API Key 未配置";
        return result;
    }
    if (config.model.empty()) {
        result.error = "模型名不能为空";
        return result;
    }

    nlohmann::json body;
    std::string path;
    if (config.protocol == "anthropic") {
        path = "/messages";
        body = {
            {"model", config.model},
            {"max_tokens", maxTokens},
            {"system", systemPrompt},
            {"messages", nlohmann::json::array({
                {{"role", "user"}, {"content", userPrompt}},
            })},
        };
    } else {
        path = "/chat/completions";
        body = {
            {"model", config.model},
            {"temperature", 0},
            {"max_tokens", maxTokens},
            {"messages", nlohmann::json::array({
                {{"role", "system"}, {"content", systemPrompt}},
                {{"role", "user"}, {"content", userPrompt}},
            })},
        };
    }

    const HttpResponse response = SendHttp(HttpRequest{
        .method = "POST",
        .url = JoinUrl(config.baseUrl, path),
        .headers = Headers(config),
        .body = body.dump(),
    });
    if (!response.ok) {
        result.error = ProviderError(response);
        return result;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(response.body);
        if (config.protocol == "anthropic") {
            if (json.contains("content") && json["content"].is_array() &&
                !json["content"].empty() && json["content"][0].contains("text")) {
                result.text = json["content"][0]["text"].get<std::string>();
            }
        } else if (
            json.contains("choices") && json["choices"].is_array() &&
            !json["choices"].empty() &&
            json["choices"][0].contains("message") &&
            json["choices"][0]["message"].contains("content")) {
            result.text = json["choices"][0]["message"]["content"].get<std::string>();
        }
        result.ok = !result.text.empty();
        if (!result.ok) {
            result.error = "模型响应没有文本内容";
        }
    } catch (const std::exception& exception) {
        result.error = exception.what();
    }
    return result;
}

}  // namespace skillbox

