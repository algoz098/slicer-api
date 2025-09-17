#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

namespace OrcaSlicerCli {

/**
 * @brief Command line argument parser
 * 
 * A simple but robust argument parser that handles commands, options, and flags
 * without external dependencies. Designed to be extensible and easy to use.
 */
class ArgumentParser {
public:
    /**
     * @brief Argument types
     */
    enum class ArgumentType {
        Flag,       // Boolean flag (--verbose, -v)
        Option,     // Option with value (--input file.stl, -i file.stl)
        Positional  // Positional argument
    };

    /**
     * @brief Argument definition
     */
    struct ArgumentDef {
        std::string name;
        std::string short_name;
        std::string description;
        ArgumentType type;
        bool required = false;
        std::string default_value;
        std::vector<std::string> choices;
        
        ArgumentDef(const std::string& name, ArgumentType type, const std::string& description = "")
            : name(name), type(type), description(description) {}
    };

    /**
     * @brief Command definition
     */
    struct CommandDef {
        std::string name;
        std::string description;
        std::vector<ArgumentDef> arguments;
        std::function<int(const std::map<std::string, std::string>&)> handler;
        
        CommandDef(const std::string& name, const std::string& description = "")
            : name(name), description(description) {}
    };

    /**
     * @brief Parsed arguments result
     */
    struct ParseResult {
        bool success = false;
        std::string error_message;
        std::string command;
        std::map<std::string, std::string> arguments;
        std::vector<std::string> positional_args;
        
        bool hasArgument(const std::string& name) const {
            return arguments.find(name) != arguments.end();
        }
        
        std::string getArgument(const std::string& name, const std::string& default_value = "") const {
            auto it = arguments.find(name);
            return (it != arguments.end()) ? it->second : default_value;
        }
        
        bool getFlag(const std::string& name) const {
            auto it = arguments.find(name);
            return (it != arguments.end()) && (it->second == "true" || it->second == "1");
        }
    };

public:
    /**
     * @brief Constructor
     * @param program_name Name of the program
     * @param description Program description
     */
    ArgumentParser(const std::string& program_name, const std::string& description = "");

    /**
     * @brief Destructor
     */
    ~ArgumentParser();

    /**
     * @brief Add a command
     * @param command Command definition
     * @return Reference to this parser for chaining
     */
    ArgumentParser& addCommand(const CommandDef& command);

    /**
     * @brief Add a global argument (applies to all commands)
     * @param argument Argument definition
     * @return Reference to this parser for chaining
     */
    ArgumentParser& addGlobalArgument(const ArgumentDef& argument);

    /**
     * @brief Add a flag argument
     * @param name Long name (--name)
     * @param short_name Short name (-n), optional
     * @param description Description for help
     * @return Reference to this parser for chaining
     */
    ArgumentParser& addFlag(const std::string& name, const std::string& short_name = "", 
                           const std::string& description = "");

    /**
     * @brief Add an option argument
     * @param name Long name (--name)
     * @param short_name Short name (-n), optional
     * @param description Description for help
     * @param required Whether the option is required
     * @param default_value Default value if not provided
     * @return Reference to this parser for chaining
     */
    ArgumentParser& addOption(const std::string& name, const std::string& short_name = "",
                             const std::string& description = "", bool required = false,
                             const std::string& default_value = "");

    /**
     * @brief Add a positional argument
     * @param name Name of the positional argument
     * @param description Description for help
     * @param required Whether the argument is required
     * @return Reference to this parser for chaining
     */
    ArgumentParser& addPositional(const std::string& name, const std::string& description = "",
                                 bool required = false);

    /**
     * @brief Parse command line arguments
     * @param argc Argument count
     * @param argv Argument values
     * @return Parse result
     */
    ParseResult parse(int argc, char* argv[]);

    /**
     * @brief Parse command line arguments from vector
     * @param args Vector of argument strings
     * @return Parse result
     */
    ParseResult parse(const std::vector<std::string>& args);

    /**
     * @brief Generate help text
     * @param command_name Specific command to show help for (empty for general help)
     * @return Help text
     */
    std::string getHelp(const std::string& command_name = "") const;

    /**
     * @brief Print help to stdout
     * @param command_name Specific command to show help for (empty for general help)
     */
    void printHelp(const std::string& command_name = "") const;

    /**
     * @brief Get list of available commands
     * @return Vector of command names
     */
    std::vector<std::string> getCommands() const;

    /**
     * @brief Check if a command exists
     * @param command_name Command name to check
     * @return True if command exists
     */
    bool hasCommand(const std::string& command_name) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace OrcaSlicerCli
