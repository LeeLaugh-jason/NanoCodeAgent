#include "config.hpp"
#include "cli.hpp"
#include "logger.hpp"
#include "workspace.hpp"
#include "llm.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. Config Init
    AgentConfig config = config_init(argc, argv);

    // 2. CLI Parse
    CliResult cli_res = cli_parse(argc, argv, config);
    if (cli_res == CliResult::ExitSuccess) return 0;
    if (cli_res == CliResult::ExitFailure) return 1;

    // 3. Workspace Init
    std::string ws_err;
    if (!workspace_init(&config, &ws_err)) {
        // Provide actionable prompt
        std::cerr << "Agent Error: Failed to initialize workspace: " << ws_err << "\n";
        std::cerr << "Action Required: Ensure the parent directory exists and you have write permissions.\n";
        return 1;
    }

    // 4. Logger Init
    logger_init(config.debug_mode);

    // 5. Validation Check
    if (!config.api_key.has_value() || config.api_key.value().empty()) {
        LOG_ERROR("API Key is missing. Action Required: Provide via --api-key <key> or NCA_API_KEY environment variable.");
        return 1;
    }

    LOG_DEBUG("Configuration loaded:");
    LOG_DEBUG("  Workspace: {}", config.workspace_abs);
    LOG_DEBUG("  Model: {}", config.model);
    LOG_DEBUG("  Base URL: {}", config.base_url);
    LOG_DEBUG("  Prompt: {}", config.prompt);

    // 6. Execution: HTTP to LLM
    LOG_INFO("Sending prompt to LLM: {}", config.prompt);
    
    std::string llm_err;
    LOG_INFO("=== LLM Response ===");
    if (!llm_chat_completion_stream(config, config.prompt, [](const std::string& content) {
        std::cout << content;
        std::cout.flush();
        return true;
    }, &llm_err)) {
        LOG_ERROR("LLM Request Failed: {}", llm_err);
        return 1;
    }

    std::cout << "\n";
    LOG_INFO("====================");

    return 0;
}
