#include <gtest/gtest.h>

#include "tool_registry.hpp"

#include <nlohmann/json.hpp>

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

TEST(ToolRegistryTest, ReadOnlyToolExecutesWithoutApproval) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor;
    descriptor.name = "read";
    descriptor.description = "read tool";
    descriptor.category = ToolCategory::ReadOnly;
    descriptor.requires_approval = false;
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}, {"value", 1}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "read";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, ReadOnlyToolApprovalFlagIsNormalizedOffAtRegistration) {
    ToolRegistry registry;

    ToolDescriptor descriptor;
    descriptor.name = "read";
    descriptor.description = "read tool";
    descriptor.category = ToolCategory::ReadOnly;
    descriptor.requires_approval = true;
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    const ToolDescriptor* stored = registry.find("read");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->category, ToolCategory::ReadOnly);
    EXPECT_FALSE(stored->requires_approval);
}

TEST(ToolRegistryTest, MutatingToolBlockedWithoutApproval) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor;
    descriptor.name = "write";
    descriptor.description = "write tool";
    descriptor.category = ToolCategory::Mutating;
    descriptor.requires_approval = true;
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "write";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["category"], "mutating");
}

TEST(ToolRegistryTest, ExecutionToolBlockedWithoutApproval) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor;
    descriptor.name = "exec";
    descriptor.description = "exec tool";
    descriptor.category = ToolCategory::Execution;
    descriptor.requires_approval = true;
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "exec";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(called);
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "blocked");
    EXPECT_EQ(result["category"], "execution");
}

TEST(ToolRegistryTest, MutatingToolExecutesWhenApproved) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor;
    descriptor.name = "write";
    descriptor.description = "write tool";
    descriptor.category = ToolCategory::Mutating;
    descriptor.requires_approval = true;
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "write";
    AgentConfig config;
    config.allow_mutating_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, ExecutionToolExecutesWhenApproved) {
    ToolRegistry registry;
    bool called = false;

    ToolDescriptor descriptor;
    descriptor.name = "exec";
    descriptor.description = "exec tool";
    descriptor.category = ToolCategory::Execution;
    descriptor.requires_approval = true;
    descriptor.json_schema = {{"type", "object"}};
    descriptor.execute = [&](const ToolCall&, const AgentConfig&, size_t) {
        called = true;
        return nlohmann::json{{"ok", true}};
    };

    std::string err;
    ASSERT_TRUE(registry.register_tool(descriptor, &err)) << err;

    ToolCall tc;
    tc.name = "exec";
    AgentConfig config;
    config.allow_execution_tools = true;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_TRUE(called);
    EXPECT_TRUE(result["ok"].get<bool>());
}

TEST(ToolRegistryTest, UnregisteredToolStillReturnsRegistryError) {
    ToolRegistry registry;
    ToolCall tc;
    tc.name = "missing";
    AgentConfig config;

    const auto result = nlohmann::json::parse(registry.execute(tc, config));
    EXPECT_FALSE(result["ok"].get<bool>());
    EXPECT_EQ(result["status"], "failed");
    EXPECT_NE(result["error"].get<std::string>().find("not registered"), std::string::npos);
}
