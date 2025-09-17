#include "ArgumentParser.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace OrcaSlicerCli {

/**
 * @brief Private implementation for ArgumentParser
 */
class ArgumentParser::Impl {
public:
    std::string program_name;
    std::string description;
    std::vector<CommandDef> commands;
    std::vector<ArgumentDef> global_arguments;

    Impl(const std::string& program_name, const std::string& description)
        : program_name(program_name), description(description) {}

    ParseResult parseArguments(const std::vector<std::string>& args) {
        ParseResult result;
        
        if (args.empty()) {
            result.error_message = "No arguments provided";
            return result;
        }

        size_t arg_index = 0;
        
        // Check for global help
        if (args.size() == 1 && (args[0] == "--help" || args[0] == "-h")) {
            result.success = true;
            result.command = "help";
            return result;
        }

        // Parse command if commands are defined
        if (!commands.empty()) {
            if (arg_index >= args.size()) {
                result.error_message = "No command specified";
                return result;
            }

            std::string command_name = args[arg_index++];
            
            // Find command
            auto cmd_it = std::find_if(commands.begin(), commands.end(),
                [&command_name](const CommandDef& cmd) { return cmd.name == command_name; });
            
            if (cmd_it == commands.end()) {
                result.error_message = "Unknown command: " + command_name;
                return result;
            }

            result.command = command_name;
            
            // Parse command-specific arguments
            return parseCommandArguments(*cmd_it, args, arg_index, result);
        } else {
            // Parse global arguments only
            return parseGlobalArguments(args, arg_index, result);
        }
    }

    ParseResult parseCommandArguments(const CommandDef& command, const std::vector<std::string>& args, 
                                    size_t start_index, ParseResult& result) {
        size_t arg_index = start_index;
        std::vector<std::string> positional_values;

        // Combine command arguments with global arguments
        std::vector<ArgumentDef> all_arguments = global_arguments;
        all_arguments.insert(all_arguments.end(), command.arguments.begin(), command.arguments.end());

        while (arg_index < args.size()) {
            const std::string& arg = args[arg_index];

            if (arg.length() >= 2 && arg.substr(0, 2) == "--") {
                // Long option
                std::string option_name = arg.substr(2);
                std::string option_value;

                // Check for --option=value format
                size_t eq_pos = option_name.find('=');
                if (eq_pos != std::string::npos) {
                    option_value = option_name.substr(eq_pos + 1);
                    option_name = option_name.substr(0, eq_pos);
                }

                // Find argument definition
                auto arg_def = findArgumentByName(all_arguments, option_name);
                if (!arg_def) {
                    result.error_message = "Unknown option: --" + option_name;
                    return result;
                }

                if (arg_def->type == ArgumentType::Flag) {
                    result.arguments[option_name] = "true";
                } else if (arg_def->type == ArgumentType::Option) {
                    if (option_value.empty()) {
                        // Value should be in next argument
                        if (++arg_index >= args.size()) {
                            result.error_message = "Option --" + option_name + " requires a value";
                            return result;
                        }
                        option_value = args[arg_index];
                    }
                    result.arguments[option_name] = option_value;
                }
            } else if (arg.length() > 1 && arg[0] == '-') {
                // Short option(s)
                std::string short_options = arg.substr(1);
                
                for (size_t i = 0; i < short_options.length(); ++i) {
                    std::string short_name(1, short_options[i]);
                    
                    auto arg_def = findArgumentByShortName(all_arguments, short_name);
                    if (!arg_def) {
                        result.error_message = "Unknown option: -" + short_name;
                        return result;
                    }

                    if (arg_def->type == ArgumentType::Flag) {
                        result.arguments[arg_def->name] = "true";
                    } else if (arg_def->type == ArgumentType::Option) {
                        // For short options, value must be in next argument
                        if (i < short_options.length() - 1) {
                            result.error_message = "Option -" + short_name + " requires a value and must be last in group";
                            return result;
                        }
                        if (++arg_index >= args.size()) {
                            result.error_message = "Option -" + short_name + " requires a value";
                            return result;
                        }
                        result.arguments[arg_def->name] = args[arg_index];
                    }
                }
            } else {
                // Positional argument
                positional_values.push_back(arg);
            }

            ++arg_index;
        }

        // Assign positional arguments
        size_t pos_index = 0;
        for (const auto& arg_def : all_arguments) {
            if (arg_def.type == ArgumentType::Positional) {
                if (pos_index < positional_values.size()) {
                    result.arguments[arg_def.name] = positional_values[pos_index];
                    result.positional_args.push_back(positional_values[pos_index]);
                    ++pos_index;
                } else if (arg_def.required) {
                    result.error_message = "Missing required positional argument: " + arg_def.name;
                    return result;
                } else if (!arg_def.default_value.empty()) {
                    result.arguments[arg_def.name] = arg_def.default_value;
                }
            }
        }

        // Check required arguments
        for (const auto& arg_def : all_arguments) {
            if (arg_def.required && result.arguments.find(arg_def.name) == result.arguments.end()) {
                result.error_message = "Missing required argument: " + arg_def.name;
                return result;
            }
            
            // Apply default values
            if (result.arguments.find(arg_def.name) == result.arguments.end() && !arg_def.default_value.empty()) {
                result.arguments[arg_def.name] = arg_def.default_value;
            }
        }

        result.success = true;
        return result;
    }

    ParseResult parseGlobalArguments(const std::vector<std::string>& args, size_t start_index, ParseResult& result) {
        // Similar logic to parseCommandArguments but for global arguments only
        // Implementation would be similar but simpler
        result.success = true;
        return result;
    }

    const ArgumentDef* findArgumentByName(const std::vector<ArgumentDef>& arguments, const std::string& name) {
        auto it = std::find_if(arguments.begin(), arguments.end(),
            [&name](const ArgumentDef& arg) { return arg.name == name; });
        return (it != arguments.end()) ? &(*it) : nullptr;
    }

    const ArgumentDef* findArgumentByShortName(const std::vector<ArgumentDef>& arguments, const std::string& short_name) {
        auto it = std::find_if(arguments.begin(), arguments.end(),
            [&short_name](const ArgumentDef& arg) { return arg.short_name == short_name; });
        return (it != arguments.end()) ? &(*it) : nullptr;
    }

    std::string generateHelp(const std::string& command_name) const {
        std::ostringstream help;
        
        help << program_name;
        if (!description.empty()) {
            help << " - " << description;
        }
        help << "\n\n";

        if (command_name.empty()) {
            // General help
            help << "Usage: " << program_name;
            if (!commands.empty()) {
                help << " <command> [options]";
            } else {
                help << " [options]";
            }
            help << "\n\n";

            if (!commands.empty()) {
                help << "Commands:\n";
                for (const auto& cmd : commands) {
                    help << "  " << std::left << std::setw(15) << cmd.name;
                    if (!cmd.description.empty()) {
                        help << " " << cmd.description;
                    }
                    help << "\n";
                }
                help << "\n";
                help << "Use '" << program_name << " <command> --help' for command-specific help.\n";
            }

            if (!global_arguments.empty()) {
                help << "Global Options:\n";
                for (const auto& arg : global_arguments) {
                    help << formatArgumentHelp(arg);
                }
            }
        } else {
            // Command-specific help
            auto cmd_it = std::find_if(commands.begin(), commands.end(),
                [&command_name](const CommandDef& cmd) { return cmd.name == command_name; });
            
            if (cmd_it != commands.end()) {
                help << "Usage: " << program_name << " " << command_name << " [options]\n\n";
                if (!cmd_it->description.empty()) {
                    help << cmd_it->description << "\n\n";
                }

                if (!cmd_it->arguments.empty()) {
                    help << "Options:\n";
                    for (const auto& arg : cmd_it->arguments) {
                        help << formatArgumentHelp(arg);
                    }
                }
            }
        }

        return help.str();
    }

    std::string formatArgumentHelp(const ArgumentDef& arg) const {
        std::ostringstream line;
        line << "  ";

        if (!arg.short_name.empty()) {
            line << "-" << arg.short_name;
            if (!arg.name.empty()) {
                line << ", ";
            }
        }

        if (!arg.name.empty()) {
            line << "--" << arg.name;
        }

        if (arg.type == ArgumentType::Option) {
            line << " <value>";
        }

        line << std::setw(std::max(1, 25 - static_cast<int>(line.str().length()))) << " ";
        
        if (!arg.description.empty()) {
            line << arg.description;
        }

        if (arg.required) {
            line << " (required)";
        } else if (!arg.default_value.empty()) {
            line << " (default: " << arg.default_value << ")";
        }

        line << "\n";
        return line.str();
    }
};

// ArgumentParser implementation

ArgumentParser::ArgumentParser(const std::string& program_name, const std::string& description)
    : m_impl(std::make_unique<Impl>(program_name, description)) {
}

ArgumentParser::~ArgumentParser() = default;

ArgumentParser& ArgumentParser::addCommand(const CommandDef& command) {
    m_impl->commands.push_back(command);
    return *this;
}

ArgumentParser& ArgumentParser::addGlobalArgument(const ArgumentDef& argument) {
    m_impl->global_arguments.push_back(argument);
    return *this;
}

ArgumentParser& ArgumentParser::addFlag(const std::string& name, const std::string& short_name, 
                                       const std::string& description) {
    ArgumentDef arg(name, ArgumentType::Flag, description);
    arg.short_name = short_name;
    return addGlobalArgument(arg);
}

ArgumentParser& ArgumentParser::addOption(const std::string& name, const std::string& short_name,
                                         const std::string& description, bool required,
                                         const std::string& default_value) {
    ArgumentDef arg(name, ArgumentType::Option, description);
    arg.short_name = short_name;
    arg.required = required;
    arg.default_value = default_value;
    return addGlobalArgument(arg);
}

ArgumentParser& ArgumentParser::addPositional(const std::string& name, const std::string& description,
                                             bool required) {
    ArgumentDef arg(name, ArgumentType::Positional, description);
    arg.required = required;
    return addGlobalArgument(arg);
}

ArgumentParser::ParseResult ArgumentParser::parse(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {  // Skip program name
        args.emplace_back(argv[i]);
    }
    return parse(args);
}

ArgumentParser::ParseResult ArgumentParser::parse(const std::vector<std::string>& args) {
    return m_impl->parseArguments(args);
}

std::string ArgumentParser::getHelp(const std::string& command_name) const {
    return m_impl->generateHelp(command_name);
}

void ArgumentParser::printHelp(const std::string& command_name) const {
    std::cout << getHelp(command_name);
}

std::vector<std::string> ArgumentParser::getCommands() const {
    std::vector<std::string> command_names;
    for (const auto& cmd : m_impl->commands) {
        command_names.push_back(cmd.name);
    }
    return command_names;
}

bool ArgumentParser::hasCommand(const std::string& command_name) const {
    auto commands = getCommands();
    return std::find(commands.begin(), commands.end(), command_name) != commands.end();
}

} // namespace OrcaSlicerCli
