#include <gtest/gtest.h>
#include "config.hpp"
#include "cli.hpp"
#include <fstream>
#include <cstdlib>

class ConfigPrecedenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset environment
        unsetenv("NCA_MODEL");
        unsetenv("NCA_API_KEY");
        unsetenv("NCA_BASE_URL");
        unsetenv("NCA_WORKSPACE");
        unsetenv("NCA_DEBUG");
        unsetenv("NCA_CONFIG");
        unsetenv("NCA_ALLOW_MUTATING_TOOLS");
        unsetenv("NCA_ALLOW_EXECUTION_TOOLS");

        // Create a fake config file
        std::ofstream out("test_conf.ini");
        out << "model = conf-gpt\n";
        out << "api_key= conf-123\n";
        out << "base_url = conf-url\n";
        out << "workspace = /conf/workspace\n";
        out << "debug = true\n";
        out << "allow_mutating_tools = true\n";
        out << "allow_execution_tools = false\n";
        out.close();
    }

    void TearDown() override {
        std::remove("test_conf.ini");
    }
};

TEST_F(ConfigPrecedenceTest, DefaultsOnly) {
    const char* argv[] = {"agent"};
    int argc = 1;
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(cfg.model, "gpt-4o");
    EXPECT_FALSE(cfg.api_key.has_value());
    EXPECT_EQ(cfg.workspace, ".");
    EXPECT_FALSE(cfg.debug_mode);
    EXPECT_FALSE(cfg.allow_mutating_tools);
    EXPECT_FALSE(cfg.allow_execution_tools);
}

TEST_F(ConfigPrecedenceTest, ConfigFileOverridesDefaults) {
    setenv("NCA_CONFIG", "test_conf.ini", 1);
    const char* argv[] = {"agent"};
    int argc = 1;
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(cfg.model, "conf-gpt");
    EXPECT_EQ(cfg.api_key.value_or(""), "conf-123");
    EXPECT_EQ(cfg.base_url, "conf-url");
    EXPECT_EQ(cfg.workspace, "/conf/workspace");
    EXPECT_TRUE(cfg.debug_mode);
    EXPECT_TRUE(cfg.allow_mutating_tools);
    EXPECT_FALSE(cfg.allow_execution_tools);
}

TEST_F(ConfigPrecedenceTest, EnvOverridesConfigFile) {
    setenv("NCA_CONFIG", "test_conf.ini", 1);
    setenv("NCA_MODEL", "env-gpt", 1);
    setenv("NCA_API_KEY", "env-api", 1);
    setenv("NCA_ALLOW_MUTATING_TOOLS", "0", 1);
    setenv("NCA_ALLOW_EXECUTION_TOOLS", "1", 1);
    const char* argv[] = {"agent"};
    int argc = 1;
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    // Env > Config
    EXPECT_EQ(cfg.model, "env-gpt");
    EXPECT_EQ(cfg.api_key.value_or(""), "env-api");
    // Rest remain from Config
    EXPECT_EQ(cfg.base_url, "conf-url");
    EXPECT_FALSE(cfg.allow_mutating_tools);
    EXPECT_TRUE(cfg.allow_execution_tools);
}

TEST_F(ConfigPrecedenceTest, CliOverridesEnv) {
    setenv("NCA_CONFIG", "test_conf.ini", 1);
    setenv("NCA_MODEL", "env-gpt", 1);
    setenv("NCA_ALLOW_MUTATING_TOOLS", "0", 1);

    const char* argv[] = {"agent", "--model", "cli-gpt", "--allow-mutating-tools", "-e", "test"};
    int argc = 6;
    
    // Config loader handles defaults, config file, and env
    AgentConfig cfg = config_init(argc, const_cast<char**>(argv));
    
    EXPECT_EQ(cfg.model, "env-gpt"); // Before CLI parse
    EXPECT_FALSE(cfg.allow_mutating_tools);

    // Parse CLI options
    cli_parse(argc, const_cast<char**>(argv), cfg);
    
    EXPECT_EQ(cfg.model, "cli-gpt"); // After CLI parse
    EXPECT_EQ(cfg.api_key.value_or(""), "conf-123");
    EXPECT_TRUE(cfg.allow_mutating_tools);
}
