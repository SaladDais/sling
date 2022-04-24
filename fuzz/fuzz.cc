#include <iostream>

#include "fuzz_utils.hh"
#include "tailslide.hh"
#include "passes/tree_simplifier.hh"
#include "passes/lso/script_compiler.hh"


static bool initialized = false;
static bool compile_lso = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (!initialized) {
    Tailslide::tailslide_init_builtins(nullptr);
    initialized = true;
    compile_lso = getenv("COMPILE_LSO") != nullptr;
  }

  const char* file = buf_to_file(data, size);
  if (!file) {
    exit(EXIT_FAILURE);
  }

  Tailslide::ScopedScriptParser parser(nullptr);
  try {
    auto *script = parser.parseLSL(file);
    if (script) {
      script->collectSymbols();
      script->linkSymbolTables();
      script->determineTypes();
      script->recalculateReferenceData();
      script->propagateValues();
      script->checkBestPractices();
      if (compile_lso) {
        script->validateGlobals(true);
        if (!parser.logger.getErrors()) {
          Tailslide::LSOScriptCompiler visitor(&parser.allocator);
          script->visit(&visitor);
        }
      } else {
        if (!parser.logger.getErrors()) {
          Tailslide::OptimizationOptions ctx{
              .fold_constants = true,
              .prune_unused_locals = true,
              .prune_unused_globals = true,
              .prune_unused_functions = true,
          };
          script->optimize(ctx);
          script->validateGlobals(true);
          script->checkSymbols();
        }
      }
    }
  } catch (const std::exception& e) {
    std::cout << e.what() << std::endl;
  }

  if (delete_file(file) != 0) {
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}
