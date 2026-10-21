#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Config.h"
#include "net/ModelClient.h"

namespace skillbox {

struct TranslationCandidate {
    std::string path;
    std::string description;
};

struct TranslationBatchResult {
    std::unordered_map<std::string, TranslationEntry> entries;
    int translated = 0;
    int failed = 0;
    int skipped = 0;
};

using TranslationFunction =
    std::function<CompletionResult(const std::string& description)>;
using TranslationSuccessCallback =
    std::function<void(const std::string& path, const TranslationEntry& entry)>;

bool LooksChinese(const std::string& text);
std::string NormalizeTranslation(const std::string& text);
bool IsUsableTranslation(const std::string& text);
bool IsUsableTranslation(
    const std::string& text,
    const std::string& sourceDescription);
bool TranslationIsValid(
    const TranslationEntry& entry,
    const std::string& sourceDescription);
bool IsUsableEnglishTranslation(
    const std::string& text,
    const std::string& sourceDescription = {});
bool EnglishTranslationIsValid(
    const TranslationEntry& entry,
    const std::string& sourceDescription);
TranslationEntry MakeTranslationEntry(
    const std::string& sourceDescription,
    const std::string& translatedText);
CompletionResult TranslateDescription(
    const ModelClient& client,
    const ProviderConfig& config,
    const std::string& description);
CompletionResult TranslateDescriptionToEnglish(
    const ModelClient& client,
    const ProviderConfig& config,
    const std::string& description);
TranslationBatchResult RunTranslationBatch(
    const std::vector<TranslationCandidate>& candidates,
    const std::unordered_map<std::string, TranslationEntry>& existing,
    bool force,
    const TranslationFunction& translate,
    const TranslationSuccessCallback& onSuccess = {});
std::string UtcTimestamp();

}  // namespace skillbox

