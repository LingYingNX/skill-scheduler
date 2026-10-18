#pragma once

#include <string>
#include <vector>

namespace skillbox {

struct ProviderConfig {
    std::string protocol;
    std::string baseUrl;
    std::string apiKey;
    std::string model;
};

struct ModelsResult {
    bool ok = false;
    std::vector<std::string> models;
    std::string error;
};

struct CompletionResult {
    bool ok = false;
    std::string text;
    std::string error;
};

class ModelClient {
public:
    ModelsResult ListModels(const ProviderConfig& config) const;
    CompletionResult Complete(
        const ProviderConfig& config,
        const std::string& systemPrompt,
        const std::string& userPrompt,
        int maxTokens = 512) const;
};

}  // namespace skillbox

