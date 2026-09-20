if(NOT DEFINED TFE OR NOT DEFINED DATA)
  message(FATAL_ERROR "TFE and DATA must be provided")
endif()

file(REMOVE_RECURSE "${DATA}")

execute_process(
  COMMAND "${TFE}" --help
  RESULT_VARIABLE help_result
  OUTPUT_VARIABLE help_output
  ERROR_VARIABLE help_error
)
if(NOT help_result EQUAL 0 OR NOT help_output MATCHES "SUBCOMMANDS")
  message(FATAL_ERROR "tfe --help failed: ${help_result} ${help_error}")
endif()

execute_process(
  COMMAND "${TFE}" completion bash
  RESULT_VARIABLE completion_result
  OUTPUT_VARIABLE completion_output
  ERROR_VARIABLE completion_error
)
if(NOT completion_result EQUAL 0 OR
   NOT completion_output MATCHES "complete -F")
  message(FATAL_ERROR
          "tfe completion bash failed: ${completion_result} ${completion_error}")
endif()

execute_process(
  COMMAND "${TFE}" --json block inspect
  RESULT_VARIABLE json_usage_result
  OUTPUT_VARIABLE json_usage_output
  ERROR_VARIABLE json_usage_error
)
if(NOT json_usage_result EQUAL 2 OR
   NOT json_usage_output MATCHES "\"error\"")
  message(FATAL_ERROR
          "JSON usage error failed: ${json_usage_result} "
          "${json_usage_output} ${json_usage_error}")
endif()

execute_process(
  COMMAND "${TFE}" block inspect
  RESULT_VARIABLE text_usage_result
  OUTPUT_VARIABLE text_usage_output
  ERROR_VARIABLE text_usage_error
)
if(NOT text_usage_result EQUAL 2 OR
   NOT text_usage_error MATCHES "required")
  message(FATAL_ERROR
          "Text usage error failed: ${text_usage_result} "
          "${text_usage_output} ${text_usage_error}")
endif()

execute_process(
  COMMAND "${TFE}" --data "${DATA}" --json block list
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE list_output
  ERROR_VARIABLE list_error
)
if(NOT list_result EQUAL 0 OR NOT list_output MATCHES "\"ids\"")
  message(FATAL_ERROR
          "JSON list failed: ${list_result} ${list_output} ${list_error}")
endif()

execute_process(
  COMMAND "${TFE}" --data "${DATA}" block create hello --template "Hello"
  RESULT_VARIABLE create_result
  OUTPUT_VARIABLE create_output
  ERROR_VARIABLE create_error
)
if(NOT create_result EQUAL 0)
  message(FATAL_ERROR
          "Block setup for completion failed: ${create_result} ${create_error}")
endif()

execute_process(
  COMMAND "${TFE}" --data "${DATA}" __complete block hel
  RESULT_VARIABLE dynamic_result
  OUTPUT_VARIABLE dynamic_output
  ERROR_VARIABLE dynamic_error
)
if(NOT dynamic_result EQUAL 0 OR NOT dynamic_output MATCHES "hello" OR
   NOT dynamic_error STREQUAL "")
  message(FATAL_ERROR
          "Dynamic completion failed: ${dynamic_result} ${dynamic_output} "
          "${dynamic_error}")
endif()

file(REMOVE_RECURSE "${DATA}")
