#include <gtest/gtest.h>

#include "tool_registry.hpp"

TEST(ToolRegistryTest, RegistersAndFindsToolsByName) {
    ToolRegistry registry;

    ToolDescriptor descriptor;
    descriptor.name = "alpha";
    descriptor.description = "alpha tool";
    descriptor.category = ToolCategory::ReadOnly;
    descriptor.requires_approval = false;
    descriptor.json_schema = {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()}
    };
    descriptor.max_output_bytes = 128;
    descriptor.execute = [](const ToolCall&, const AgentConfig&, size_t) {
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    EXPECT_TRUE(registry.register_tool(descriptor, &err)) << err;

    const ToolDescriptor* found = registry.find("alpha");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "alpha");
    EXPECT_EQ(found->description, "alpha tool");
    EXPECT_EQ(found->category, ToolCategory::ReadOnly);
    EXPECT_FALSE(found->requires_approval);
}

TEST(ToolRegistryTest, RejectsDuplicateRegistration) {
    ToolRegistry registry;

    ToolDescriptor descriptor;
    descriptor.name = "duplicate";
    descriptor.description = "tool";
    descriptor.json_schema = {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()}
    };
    descriptor.execute = [](const ToolCall&, const AgentConfig&, size_t) {
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    EXPECT_TRUE(registry.register_tool(descriptor, &err)) << err;
    EXPECT_FALSE(registry.register_tool(descriptor, &err));
    EXPECT_NE(err.find("already registered"), std::string::npos);
}

TEST(ToolRegistryTest, SchemaPreservesRegistrationOrder) {
    ToolRegistry registry;

    ToolDescriptor first;
    first.name = "first";
    first.description = "first tool";
    first.json_schema = {
        {"type", "object"},
        {"properties", nlohmann::json::object()},
        {"required", nlohmann::json::array()}
    };
    first.execute = [](const ToolCall&, const AgentConfig&, size_t) {
        return nlohmann::json{{"ok", true}};
    };

    ToolDescriptor second = first;
    second.name = "second";
    second.description = "second tool";

    std::string err;
    EXPECT_TRUE(registry.register_tool(first, &err)) << err;
    EXPECT_TRUE(registry.register_tool(second, &err)) << err;

    const auto schema = registry.to_openai_schema();
    ASSERT_TRUE(schema.is_array());
    ASSERT_EQ(schema.size(), 2);
    EXPECT_EQ(schema[0]["function"]["name"], "first");
    EXPECT_EQ(schema[1]["function"]["name"], "second");
}
