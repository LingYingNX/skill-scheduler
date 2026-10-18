#include "bridge/Bridge.h"

namespace skillbox {

CommandResult CommandResult::Success(nlohmann::json value) {
    CommandResult result;
    result.ok = true;
    result.data = std::move(value);
    return result;
}

CommandResult CommandResult::Error(std::string code, std::string message) {
    CommandResult result;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

Bridge::Bridge(CommandHandler handler) : handler_(std::move(handler)) {}

std::string Bridge::Success(const std::string& id, nlohmann::json data) {
    return nlohmann::json{
        {"id", id},
        {"ok", true},
        {"data", std::move(data)},
    }.dump();
}

std::string Bridge::Error(
    const std::string& id,
    const std::string& code,
    const std::string& message) {
    return nlohmann::json{
        {"id", id},
        {"ok", false},
        {"error", {{"code", code}, {"message", message}}},
    }.dump();
}

std::string Bridge::Handle(const std::string& rawMessage) const {
    std::string id;
    try {
        const nlohmann::json request = nlohmann::json::parse(rawMessage);
        if (!request.is_object()) {
            return Error({}, "invalid_request", "message must be an object");
        }
        if (request.contains("id") && request["id"].is_string()) {
            id = request["id"].get<std::string>();
        }
        const std::string command =
            request.value("command", std::string{});
        if (command.empty()) {
            return Error(id, "invalid_request", "command is required");
        }
        const nlohmann::json payload =
            request.contains("payload") ? request["payload"] : nlohmann::json::object();
        if (!handler_) {
            return Error(id, "internal_error", "bridge handler is unavailable");
        }

        const CommandResult result = handler_(command, payload);
        return result.ok
                   ? Success(id, result.data)
                   : Error(id, result.code, result.message);
    } catch (const nlohmann::json::exception& exception) {
        return Error(id, "invalid_json", exception.what());
    } catch (const std::exception& exception) {
        return Error(id, "internal_error", exception.what());
    }
}

}  // namespace skillbox
