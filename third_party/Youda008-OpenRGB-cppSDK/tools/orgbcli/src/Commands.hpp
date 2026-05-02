#ifndef CLI_COMMANDS_INCLUDED
#define CLI_COMMANDS_INCLUDED

#include "CommandRegistration.hpp"


// This must be here and not in command registration, because the order of initialization of the static variables is
// undefined and each instance of CommandRegistrator depends on g_registeredCommands being already initialized.

extern RegisteredCommands g_standardCommands;  ///< commands equally usable in all modes

extern RegisteredCommands g_specialCommands;  ///< special type of commands that are used differently in different modes

// allow direct access for custom usage in main
extern const RegisteredCommand helpCmd;
extern const RegisteredCommand exitCmd;
extern const RegisteredCommand connectCmd;
extern const RegisteredCommand disconnectCmd;


#endif // CLI_COMMANDS_INCLUDED
