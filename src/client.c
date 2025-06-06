#define _XOPEN_SOURCE 500 // string.h: strdup

#define NX_JSON_CALLOC(SIZE) ((nx_json*) Mem_Calloc(1, SIZE))
#define NX_JSON_FREE(JSON)   (Mem_Free((void*) (JSON)))

#include <float.h>    // FLT_MAX
#include <limits.h>   // INT_MAX
#include <locale.h>   // setlocale, LC_NUMERIC
#include <stdio.h>    // printf
#include <string.h>   // strcmp
#include <sys/stat.h> // mkdir

#include "help/client.help.h"
#include "log.h"
#include "nbfc.h"
#include "parse_number.h"
#include "parse_double.h"
#include "client/client_global.h"

#include "log.c"
#include "error.c"
#include "file_utils.c"
#include "model_config.c"
#include "memory.c"
#include "program_name.c"
#include "protocol.c"
#include "nxjson.c"
#include "reverse_nxjson.c"
#include "service_config.c"
#include "stack_memory.c"
#include "trace.c"
#include "optparse/optparse.c"
#include "client/mkdir_p.c"
#include "client/dmi.c"
#include "client/config_files.c"
#include "client/str_functions.c"
#include "client/service_control.c"

const cli99_option main_options[] = {
  {"-h|--help",     Option_Help,       0},
  {"-v|--version",  Option_Version,    0},
  {"command",       Option_Command,    1 | cli99_required_option},
  cli99_options_end()
};

// ============================================================================
// Commands
// ============================================================================

#include "client/cmd_start_stop.c"
#include "client/cmd_status.c"
#include "client/cmd_config.c"
#include "client/cmd_set.c"
#include "client/cmd_update.c"
#include "client/cmd_show_variable.c"
#include "client/cmd_misc.c"
#include "client/cmd_warranty.c"
#include "client/cmd_donate.c"

#define NBFC_CLIENT_COMMANDS \
  o("start",          Start,          START,           start)         \
  o("stop",           Stop,           STOP,            main)          \
  o("restart",        Restart,        RESTART,         start)         \
  o("status",         Status,         STATUS,          status)        \
  o("config",         Config,         CONFIG,          config)        \
  o("set",            Set,            SET,             set)           \
  o("update",         Update,         UPDATE,          update)        \
  o("wait-for-hwmon", Wait_For_Hwmon, WAIT_FOR_HWMON,  main)          \
  o("get-model-name", Get_Model_Name, GET_MODEL,       main)          \
  o("complete-fans",  Complete_Fans,  COMPLETE_FANS,   main)          \
  o("show-variable",  Show_Variable,  SHOW_VARIABLE,   show_variable) \
  o("warranty",       Warranty,       WARRANTY,        main)          \
  o("donate",         Donate,         DONATE,          main)          \
  o("help",           Help,           HELP,            main)

enum Command {
#define o(COMMAND, ENUM, HELP, OPTIONS)  Command_ ## ENUM,
  NBFC_CLIENT_COMMANDS
  Command_End
#undef o
};

static const char *HelpTexts[] = {
#define o(COMMAND, ENUM, HELP, OPTIONS)  CLIENT_ ## HELP ## _HELP_TEXT,
  NBFC_CLIENT_COMMANDS
#undef o
};

static enum Command Command_From_String(const char* s) {
  const char* commands[] = {
#define o(COMMAND, ENUM, HELP, OPTIONS)  COMMAND,
    NBFC_CLIENT_COMMANDS
#undef o
  };

  for (int i = 0; i < ARRAY_SSIZE(commands); ++i)
    if (!strcmp(commands[i], s))
      return (enum Command) i;

  return Command_End;
}

static const cli99_option *Options[] = {
#define o(COMMAND, ENUM, HELP, OPTIONS)  OPTIONS ## _options,
  NBFC_CLIENT_COMMANDS
#undef o
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *const argv[]) {
  if (argc == 1) {
    printf(CLIENT_HELP_HELP_TEXT);
    return NBFC_EXIT_CMDLINE;
  }

  Program_Name_Set(argv[0]);
  setlocale(LC_NUMERIC, "C"); // for json floats
  mkdir_p(NBFC_CONFIG_DIR, 0755);
  mkdir_p(NBFC_MODEL_CONFIGS_DIR_MUTABLE, 0755);

  int o;
  const char* err;
  enum Command cmd = Command_Help;
  cli99 p;
  cli99_Init(&p, argc, argv, main_options, cli99_options_python);
  while ((o = cli99_GetOpt(&p))) {
    switch (o) {

    // ========================================================================
    // General options
    // ========================================================================

    case Option_Help:
      printf("%s", HelpTexts[cmd]);
      return NBFC_EXIT_SUCCESS;

    case Option_Version:
      printf("nbfc " NBFC_VERSION "\n");
      return NBFC_EXIT_SUCCESS;

    // ========================================================================
    // Command
    // ========================================================================

    case Option_Command:
      cmd = Command_From_String(p.optarg);
      if (cmd == Command_End) {
        Log_Error("Invalid command: %s\n", p.optarg);
        return NBFC_EXIT_CMDLINE;
      }

      if (cmd == Command_Help) {
        printf("%s", HelpTexts[Command_Help]);
        return NBFC_EXIT_SUCCESS;
      }
      cli99_SetOptions(&p, Options[cmd], false);
      break;

    // ========================================================================
    // Status options
    // ========================================================================

    case Option_Status_All:
      Status_Options.all = 1;
      break;

    case Option_Status_Service:
      Status_Options.service = 1;
      break;

    case Option_Status_Fan:
      {
        const int fan = parse_number(p.optarg, 0, INT_MAX, &err);
        if (err) {
          Log_Error("-f|--fan: %s\n", err);
          return NBFC_EXIT_FAILURE;
        }

        array_of(int)* fans = &Status_Options.fans;
        fans->size++;
        fans->data = Mem_Realloc(fans->data, fans->size * sizeof(int));
        fans->data[fans->size - 1] = fan;
      }
      break;

    case Option_Status_Watch:
      Status_Options.watch = parse_double(p.optarg, 0.1, FLT_MAX, &err);
      if (err) {
        Log_Error("-w|--watch: %s\n", err);
        return NBFC_EXIT_FAILURE;
      }
      break;

    // ========================================================================
    // Config options
    // ========================================================================

    case Option_Config_Apply:
      Set_Config_Action(Config_Action_Apply);
      Config_Options.config = p.optarg;
      break;

    case Option_Config_Set:
      Set_Config_Action(Config_Action_Set);
      Config_Options.config = p.optarg;
      break;

    case Option_Config_List:
      Set_Config_Action(Config_Action_List);
      break;

    case Option_Config_Recommend:
      Set_Config_Action(Config_Action_Recommend);
      break;

    // ========================================================================
    // Set options
    // ========================================================================

    case Option_Set_Auto:
      if (Set_Options.speed != -2.0) {
        Log_Error("Options -a|--auto or -s|--speed may only be specified once\n");
        return NBFC_EXIT_FAILURE;
      }

      Set_Options.speed = -1.0;
      break;

    case Option_Set_Speed:
      if (Set_Options.speed != -2.0) {
        Log_Error("Options -a|--auto or -s|--speed may only be specified once\n");
        return NBFC_EXIT_FAILURE;
      }

      Set_Options.speed = parse_double(p.optarg, 0, 100, &err);
      if (err) {
        Log_Error("-s|--speed: %s\n", err);
        return NBFC_EXIT_FAILURE;
      }
      break;

    case Option_Set_Fan:
      if (Set_Options.fan != -1) {
        Log_Error("Option -f|--fan may only be specified once\n");
        return NBFC_EXIT_FAILURE;
      }

      Set_Options.fan = parse_number(p.optarg, 0, INT_MAX, &err);
      if (err) {
        Log_Error("-f|--fan: %s\n", err);
        return NBFC_EXIT_FAILURE;
      }
      break;

    // ========================================================================
    // Update options
    // ========================================================================

    case Option_Update_Parallel:
      Update_Options.parallel = parse_number(p.optarg, 0, INT_MAX, &err);
      if (err) {
        Log_Error("-p|--parallel: %s\n", err);
        return NBFC_EXIT_FAILURE;
      }
      break;

    case Option_Update_Quiet:
      Update_Options.quiet = 1;
      break;

    // ========================================================================
    // Start/Restart options
    // ========================================================================

    case Option_Start_ReadOnly:
      Start_Options.read_only = 1;
      break;

    // ========================================================================
    // Show-Variable options
    // ========================================================================

    case Option_ShowVariable_Variable:
      Show_Variable_Options.variable = p.optarg;
      break;

    // ========================================================================
    // Error
    // ========================================================================

    default:
      cli99_ExplainError(&p);
      return NBFC_EXIT_CMDLINE;
    }
  }

  switch (cmd) {
  case Command_Start:          return Start();
  case Command_Stop:           return Stop();
  case Command_Restart:        return Restart();
  case Command_Config:         return Config();
  case Command_Set:            return Set();
  case Command_Status:         return Status();
  case Command_Update:         return Update();
  case Command_Wait_For_Hwmon: return Wait_For_Hwmon();
  case Command_Get_Model_Name: return Get_Model_Name();
  case Command_Show_Variable:  return Show_Variable();
  case Command_Complete_Fans:  return Complete_Fans();
  case Command_Warranty:       return Warranty();
  case Command_Donate:         return Donate();
  default:                     return NBFC_EXIT_FAILURE;
  }
}
