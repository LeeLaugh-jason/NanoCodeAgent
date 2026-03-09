#include <gtest/gtest.h>
#include "agent_tools.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>
#include <unistd.h>

using json = nlohmann::json;

TEST(SchemaAndArgsToleranceTest, GetSchemaMatchesCurrentTools) {
    json schema = get_agent_tools_schema();
    EXPECT_EQ(schema.size(), 7u);

    bool has_read        = false;
    bool has_write       = false;
    bool has_bash        = false;
    bool has_list        = false;
    bool has_rg          = false;
    bool has_git         = false;
    bool has_apply_patch = false;
    for (const auto& tool : schema) {
        std::string name = tool["function"]["name"];
        if (name == "read_file_safe")   has_read        = true;
        if (name == "write_file_safe")  has_write       = true;
        if (name == "bash_execute_safe") has_bash       = true;
        if (name == "list_files_bounded") has_list      = true;
        if (name == "rg_search")        has_rg          = true;
        if (name == "git_status")       has_git         = true;
        if (name == "apply_patch")      has_apply_patch = true;
    }
    EXPECT_TRUE(has_read);
    EXPECT_TRUE(has_write);
    EXPECT_TRUE(has_bash);
    EXPECT_TRUE(has_list);
    EXPECT_TRUE(has_rg);
    EXPECT_TRUE(has_git);
    EXPECT_TRUE(has_apply_patch);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsOutOfRangeInteger) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", 3000000000ULL}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsOutOfRangeString) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "3000000000"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutToleratesStrings) {
    AgentConfig config;
    config.workspace_abs = "."; // Allow mock safe dir
    
    ToolCall tc;
    tc.name = "bash_execute_safe";
    // We explicitly pass timeout_ms as string instead of int
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "100"} 
    };
    
    std::string res = execute_tool(tc, config);
    // It should not throw and should safely execute
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(res.find("hello"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsUnsignedOverflow) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", 3000000000ULL}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'timeout_ms' must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, BashTimeoutRejectsStringOverflow) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "bash_execute_safe";
    tc.arguments = {
        {"command", "echo 'hello'"},
        {"timeout_ms", "3000000000"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("Argument 'timeout_ms' must be between 1 and"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, MissingArgumentsReturnsErrorGracefully) {
    AgentConfig config;
    config.workspace_abs = "."; 
    
    ToolCall tc;
    tc.name = "read_file_safe";
    // Empty args!
    tc.arguments = json::object();
    
    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("failed"), std::string::npos);
    EXPECT_NE(res.find("Missing 'path' argument"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, TypeErrorsCaughtSafelyAndDontCrash) {
    AgentConfig config;
    config.workspace_abs = "."; 

    ToolCall tc;
    tc.name = "read_file_safe";
    // Path should be a string, we pass an object
    tc.arguments = {
        {"path", {{"unexpected", "object"}}} 
    };
    
    std::string res = execute_tool(tc, config);
    // Exception handled inside
    EXPECT_NE(res.find("failed"), std::string::npos);
}

TEST(SchemaAndArgsToleranceTest, ExistingToolsStillDispatchThroughRegistry) {
    const auto test_workspace =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_dispatch_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(test_workspace);
    std::filesystem::create_directories(test_workspace);

    AgentConfig config;
    config.workspace_abs = test_workspace;

    ToolCall write_call;
    write_call.name = "write_file_safe";
    write_call.arguments = {
        {"path", "hello.txt"},
        {"content", "world"}
    };

    const std::string write_result = execute_tool(write_call, config);
    EXPECT_NE(write_result.find("\"ok\":true"), std::string::npos);

    ToolCall read_call;
    read_call.name = "read_file_safe";
    read_call.arguments = {
        {"path", "hello.txt"}
    };

    const std::string read_result = execute_tool(read_call, config);
    EXPECT_NE(read_result.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(read_result.find("world"), std::string::npos);

    ToolCall bash_call;
    bash_call.name = "bash_execute_safe";
    bash_call.arguments = {
        {"command", "cat hello.txt"}
    };

    const std::string bash_result = execute_tool(bash_call, config);
    EXPECT_NE(bash_result.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(bash_result.find("world"), std::string::npos);

    std::filesystem::remove_all(test_workspace);
}

// ---------------------------------------------------------------------------
// apply_patch schema: oneOf enforcement
// ---------------------------------------------------------------------------

/// Helper: extract the apply_patch parameters schema from the OpenAI schema
/// array returned by get_agent_tools_schema().
static json get_apply_patch_params() {
    json schema = get_agent_tools_schema();
    for (const auto& tool : schema) {
        if (tool["function"]["name"] == "apply_patch") {
            return tool["function"]["parameters"];
        }
    }
    ADD_FAILURE() << "apply_patch not found in tool schema";
    return {};
}

TEST(ApplyPatchSchemaTest, HasOneOfWithTwoModes) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("oneOf"));
    ASSERT_TRUE(params["oneOf"].is_array());
    EXPECT_EQ(params["oneOf"].size(), 2u);
}

TEST(ApplyPatchSchemaTest, ModeASingleRequiresPathOldTextNewText) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("oneOf"));
    ASSERT_TRUE(params["oneOf"].is_array());
    ASSERT_GE(params["oneOf"].size(), 2u);
    const json& mode_a = params["oneOf"][0];
    ASSERT_TRUE(mode_a.contains("required"));
    const json expected = json::array({"path", "old_text", "new_text"});
    EXPECT_EQ(mode_a["required"], expected);
}

TEST(ApplyPatchSchemaTest, ModeBBatchRequiresPathPatches) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("oneOf"));
    ASSERT_TRUE(params["oneOf"].is_array());
    ASSERT_GE(params["oneOf"].size(), 2u);
    const json& mode_b = params["oneOf"][1];
    ASSERT_TRUE(mode_b.contains("required"));
    const json expected = json::array({"path", "patches"});
    EXPECT_EQ(mode_b["required"], expected);
}

TEST(ApplyPatchSchemaTest, PatchesItemsRequireOldTextAndNewText) {
    const json params = get_apply_patch_params();
    ASSERT_TRUE(params.contains("properties"));
    ASSERT_TRUE(params["properties"].contains("patches"));
    const json& patches = params["properties"]["patches"];
    ASSERT_TRUE(patches.contains("items"));
    const json& items = patches["items"];
    ASSERT_TRUE(items.contains("required"));
    const json expected = json::array({"old_text", "new_text"});
    EXPECT_EQ(items["required"], expected);
}

TEST(ApplyPatchSchemaTest, TopLevelDoesNotHaveBareRequired) {
    // Before the fix the schema had a top-level "required": ["path"].
    // After the fix, the required constraints live inside oneOf only.
    const json params = get_apply_patch_params();
    EXPECT_FALSE(params.contains("required"))
        << "Top-level 'required' should not exist; constraints belong in oneOf";
}

// ---------------------------------------------------------------------------
// apply_patch schema: mixed-mode rejection via additionalProperties
// ---------------------------------------------------------------------------

TEST(ApplyPatchSchemaTest, SchemaModeSingleHasAdditionalPropertiesFalse) {
    const json params = get_apply_patch_params();
    const json& branch = params["oneOf"][0];
    ASSERT_TRUE(branch.contains("additionalProperties"))
        << "Single-mode branch must declare additionalProperties";
    EXPECT_EQ(branch["additionalProperties"], false);
}

TEST(ApplyPatchSchemaTest, SchemaModeBatchHasAdditionalPropertiesFalse) {
    const json params = get_apply_patch_params();
    const json& branch = params["oneOf"][1];
    ASSERT_TRUE(branch.contains("additionalProperties"))
        << "Batch-mode branch must declare additionalProperties";
    EXPECT_EQ(branch["additionalProperties"], false);
}

TEST(ApplyPatchSchemaTest, SchemaModeSingleAllowsOnlyPathOldTextNewText) {
    const json params = get_apply_patch_params();
    const json& props = params["oneOf"][0]["properties"];
    ASSERT_TRUE(props.is_object());
    std::set<std::string> keys;
    for (auto it = props.begin(); it != props.end(); ++it) keys.insert(it.key());
    const std::set<std::string> expected{"path", "old_text", "new_text"};
    EXPECT_EQ(keys, expected)
        << "Single-mode branch properties must be exactly {path, old_text, new_text}";
}

TEST(ApplyPatchSchemaTest, SchemaModeBatchAllowsOnlyPathPatches) {
    const json params = get_apply_patch_params();
    const json& props = params["oneOf"][1]["properties"];
    ASSERT_TRUE(props.is_object());
    std::set<std::string> keys;
    for (auto it = props.begin(); it != props.end(); ++it) keys.insert(it.key());
    const std::set<std::string> expected{"path", "patches"};
    EXPECT_EQ(keys, expected)
        << "Batch-mode branch properties must be exactly {path, patches}";
}

// ---------------------------------------------------------------------------
// apply_patch runtime: argument combination validation
// ---------------------------------------------------------------------------

TEST(ApplyPatchSchemaTest, RuntimeRejectsPathOnly) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {{"path", "any.txt"}};

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Passing only 'path' should fail at runtime";
}

TEST(ApplyPatchSchemaTest, RuntimeAcceptsSingleMode) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_single_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    // Seed a file via write_file_safe
    AgentConfig config;
    config.workspace_abs = ws;

    ToolCall write_tc;
    write_tc.name = "write_file_safe";
    write_tc.arguments = {{"path", "f.txt"}, {"content", "hello world"}};
    execute_tool(write_tc, config);

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "world"},
        {"new_text", "earth"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);

    std::filesystem::remove_all(ws);
}

TEST(ApplyPatchSchemaTest, RuntimeAcceptsBatchMode) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_batch_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    AgentConfig config;
    config.workspace_abs = ws;

    ToolCall write_tc;
    write_tc.name = "write_file_safe";
    write_tc.arguments = {{"path", "f.txt"}, {"content", "aaa bbb"}};
    execute_tool(write_tc, config);

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "aaa"}, {"new_text", "xxx"}},
            {{"old_text", "bbb"}, {"new_text", "yyy"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":true"), std::string::npos);

    std::filesystem::remove_all(ws);
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsMixedPatchesAndOldText) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({json{{"old_text", "a"}, {"new_text", "b"}}})},
        {"old_text", "x"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Mixed patches + old_text must be rejected";
    EXPECT_NE(res.find("Ambiguous"), std::string::npos)
        << "Error should mention ambiguous input";
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsMixedPatchesAndNewText) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({json{{"old_text", "a"}, {"new_text", "b"}}})},
        {"new_text", "y"}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Mixed patches + new_text must be rejected";
    EXPECT_NE(res.find("Ambiguous"), std::string::npos);
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsAllThreeFieldsCombined) {
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "x"},
        {"new_text", "y"},
        {"patches", json::array({json{{"old_text", "a"}, {"new_text", "b"}}})}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "All three fields combined must be rejected";
    EXPECT_NE(res.find("Ambiguous"), std::string::npos);
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsBatchEntryMissingNewText) {
    const auto ws =
        (std::filesystem::temp_directory_path() /
         ("nano_schema_miss_" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(ws);
    std::filesystem::create_directories(ws);

    AgentConfig config;
    config.workspace_abs = ws;

    ToolCall write_tc;
    write_tc.name = "write_file_safe";
    write_tc.arguments = {{"path", "f.txt"}, {"content", "hello"}};
    execute_tool(write_tc, config);

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "hello"}}
        })}
    };

    std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Batch entry missing 'new_text' should fail";

    std::filesystem::remove_all(ws);
}
// ---------------------------------------------------------------------------
// apply_patch runtime: unknown top-level field rejection
// (schema declares additionalProperties: false; runtime must match)
// ---------------------------------------------------------------------------

TEST(ApplyPatchSchemaTest, RuntimeRejectsUnknownTopLevelFieldSingleMode) {
    // No real file needed: the unknown-field check fires before any I/O.
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"old_text", "hello"},
        {"new_text", "world"},
        {"typo_field", "x"}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Unknown field in single-mode call must be rejected";
    EXPECT_NE(res.find("typo_field"), std::string::npos)
        << "Error must name the offending field";
}

TEST(ApplyPatchSchemaTest, RuntimeRejectsUnknownTopLevelFieldBatchMode) {
    // No real file needed: the unknown-field check fires before any I/O.
    AgentConfig config;
    config.workspace_abs = ".";

    ToolCall tc;
    tc.name = "apply_patch";
    tc.arguments = {
        {"path", "f.txt"},
        {"patches", json::array({
            {{"old_text", "hello"}, {"new_text", "world"}}
        })},
        {"extra_key", 42}
    };

    const std::string res = execute_tool(tc, config);
    EXPECT_NE(res.find("\"ok\":false"), std::string::npos)
        << "Unknown field in batch-mode call must be rejected";
    EXPECT_NE(res.find("extra_key"), std::string::npos)
        << "Error must name the offending field";
}