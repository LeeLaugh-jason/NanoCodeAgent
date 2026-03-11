#include "tool_registry.hpp"

#include <algorithm>

namespace {

nlohmann::json make_registry_error(const std::string& message) {
    return nlohmann::json{
        {"ok", false},
        {"status", "failed"},
        {"error", message}
    };
}

nlohmann::json make_approval_blocked_result(const ToolDescriptor& descriptor) {
    return nlohmann::json{
        {"ok", false},
        {"status", "blocked"},
        {"tool", descriptor.name},
        {"category", tool_category_to_string(descriptor.category)},
        {"requires_approval", descriptor.requires_approval},
        {"error", "Tool execution requires approval under the current policy."}
    };
}

size_t resolve_effective_output_limit(size_t descriptor_limit, size_t config_limit) {
    if (descriptor_limit == 0) {
        return config_limit;
    }
    if (config_limit == 0) {
        return descriptor_limit;
    }
    return std::min(descriptor_limit, config_limit);
}

bool tool_execution_allowed(const ToolDescriptor& descriptor, const AgentConfig& config) {
    if (!descriptor.requires_approval) {
        return true;
    }

    switch (descriptor.category) {
        case ToolCategory::ReadOnly:
            return true;
        case ToolCategory::Mutating:
            return config.allow_mutating_tools;
        case ToolCategory::Execution:
            return config.allow_execution_tools;
    }
    return false;
}

} // namespace

std::string tool_category_to_string(ToolCategory category) {
    switch (category) {
        case ToolCategory::ReadOnly:
            return "read_only";
        case ToolCategory::Mutating:
            return "mutating";
        case ToolCategory::Execution:
            return "execution";
    }
    return "unknown";
}

bool ToolRegistry::register_tool(ToolDescriptor descriptor, std::string* err) {
    if (descriptor.name.empty()) {
        if (err) *err = "Tool name must not be empty";
        return false;
    }
    if (!descriptor.execute) {
        if (err) *err = "Tool '" + descriptor.name + "' is missing an execute callback";
        return false;
    }
    if (index_by_name_.find(descriptor.name) != index_by_name_.end()) {
        if (err) *err = "Tool '" + descriptor.name + "' is already registered";
        return false;
    }

    if (descriptor.category == ToolCategory::ReadOnly) {
        descriptor.requires_approval = false;
    }

    index_by_name_[descriptor.name] = descriptors_.size();
    descriptors_.push_back(std::move(descriptor));
    return true;
}

const ToolDescriptor* ToolRegistry::find(const std::string& name) const {
    auto it = index_by_name_.find(name);
    if (it == index_by_name_.end()) {
        return nullptr;
    }
    return &descriptors_[it->second];
}

std::string ToolRegistry::execute(const ToolCall& call, const AgentConfig& config) const {
    const ToolDescriptor* descriptor = find(call.name);
    if (!descriptor) {
        return make_registry_error("Tool '" + call.name + "' is not registered.").dump();
    }

    const size_t output_limit = resolve_effective_output_limit(descriptor->max_output_bytes, config.max_tool_output_bytes);

    if (!tool_execution_allowed(*descriptor, config)) {
        return make_approval_blocked_result(*descriptor).dump();
    }

    try {
        nlohmann::json result = descriptor->execute(call, config, output_limit);
        if (!result.is_object()) {
            return make_registry_error("Tool '" + call.name + "' returned a non-object result.").dump();
        }
        return result.dump();
    } catch (const std::exception& e) {
        return make_registry_error(std::string("Exception during tool execution: ") + e.what()).dump();
    }
}

nlohmann::json ToolRegistry::to_openai_schema() const {
    nlohmann::json schema = nlohmann::json::array();

    for (const auto& descriptor : descriptors_) {
        schema.push_back({
            {"type", "function"},
            {"function", {
                {"name", descriptor.name},
                {"description", descriptor.description},
                {"parameters", descriptor.json_schema}
            }}
        });
    }

    return schema;
}
