#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace skillbox {

struct CommandResult {
    bool ok = false;
    nlohmann::json data;
    std::string code;
    std::string message;

    static CommandResult Success(nlohmann::json value = {});
    static CommandResult Error(std::string code, std::string message);
};

using CommandHandler =
    std::function<CommandResult(const std::string& command, const nlohmann::json& payload)>;

class Bridge {
public:
    explicit Bridge(CommandHandler handler);

    std::string Handle(const std::string& rawMessage) const;
    static std::string Success(const std::string& id, nlohmann::json data = {});
    static std::string Error(
        const std::string& id,
        const std::string& code,
        const std::string& message);

private:
    CommandHandler handler_;
};

}  // namespace skillbox

