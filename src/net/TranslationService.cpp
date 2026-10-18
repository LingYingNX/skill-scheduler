#include "net/TranslationService.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "core/Hash.h"

namespace skillbox {

bool LooksChinese(const std::string& text) {
    for (size_t index = 0; index + 2 < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if ((first & 0x80) == 0) {
            ++index;
            continue;
        }
        char32_t codepoint = 0;
        int length = 0;
        if ((first & 0xE0) == 0xC0) {
            codepoint = first & 0x1F;
            length = 2;
        } else if ((first & 0xF0) == 0xE0) {
            codepoint = first & 0x0F;
            length = 3;
        } else if ((first & 0xF8) == 0xF0) {
            codepoint = first & 0x07;
            length = 4;
        } else {
            ++index;
            continue;
        }
        if (index + static_cast<size_t>(length) > text.size()) {
            break;
        }
        for (int offset = 1; offset < length; ++offset) {
            codepoint = (codepoint << 6) |
                        (static_cast<unsigned char>(text[index + offset]) & 0x3F);
        }
        if (codepoint >= 0x3400 && codepoint <= 0x9FFF) {
            return true;
        }
        index += static_cast<size_t>(length);
    }
    return false;
}

bool TranslationIsValid(
    const TranslationEntry& entry,
    const std::string& sourceDescription) {
    return !entry.text.empty() &&
           entry.sourceHash == Sha256Hex(sourceDescription);
}

TranslationEntry MakeTranslationEntry(
    const std::string& sourceDescription,
    const std::string& translatedText) {
    return {
        Sha256Hex(sourceDescription),
        translatedText,
        UtcTimestamp(),
    };
}

CompletionResult TranslateDescription(
    const ModelClient& client,
    const ProviderConfig& config,
    const std::string& description) {
    return client.Complete(
        config,
        "Translate the provided software skill description into concise Simplified Chinese. "
        "Return only the translation, without quotes or commentary.",
        description,
        320);
}

TranslationBatchResult RunTranslationBatch(
    const std::vector<TranslationCandidate>& candidates,
    const std::unordered_map<std::string, TranslationEntry>& existing,
    bool force,
    const TranslationFunction& translate,
    const TranslationSuccessCallback& onSuccess) {
    TranslationBatchResult result;
    for (const TranslationCandidate& candidate : candidates) {
        if (candidate.description.empty() || LooksChinese(candidate.description)) {
            ++result.skipped;
            continue;
        }
        if (!force) {
            const auto cached = existing.find(candidate.path);
            if (cached != existing.end() &&
                TranslationIsValid(cached->second, candidate.description)) {
                ++result.skipped;
                continue;
            }
        }

        const CompletionResult translated = translate(candidate.description);
        if (!translated.ok) {
            ++result.failed;
            continue;
        }
        TranslationEntry entry =
            MakeTranslationEntry(candidate.description, translated.text);
        result.entries[candidate.path] = entry;
        ++result.translated;
        if (onSuccess) {
            onSuccess(candidate.path, entry);
        }
    }
    return result;
}

std::string UtcTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    std::ostringstream value;
    value << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return value.str();
}

}  // namespace skillbox
