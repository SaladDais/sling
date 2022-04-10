#include <iostream>
#include <cstdio>

#include "cxxopt.hh"

#include "tailslide.hh"
#include "passes/pretty_print.hh"
#include "passes/tree_print.hh"
#include "passes/tree_simplifier.hh"

using namespace Tailslide;

void version() {
  fprintf(stderr, "Tailslide LSL CLI");
  fprintf(stderr, " built on " __DATE__ "\n");
  fprintf(stderr, " based on https://github.com/pclewis/lslint\n");
}


int main(int argc, char **argv) {
  FILE *yyin = nullptr;
  bool show_tree = false;
  bool pretty_print = true;
  OptimizationContext optim_ctx {};
  PrettyPrintOpts pretty_opts {};

  cxxopts::Options options("tailslide", "");

  options.add_options("General")
          ("help", "Show this message")
          ("version", "Display the banner and current version of Tailslide");

  options.add_options("Obfuscation")
          ("obfuscate", "Standard obfuscation method - uses all methods with no negative performance impact")
          ("minw", "Minimize whitespace within the script")
          ("mangle-globals", "Mangle and shorten global variable names")
          ("mangle-locals", "Mangle and shorten local variable names")
          ("mangle-funcs", "Mangle and shorten function names")
          ("show-unmangled", "Put a comment next to instances of mangled identifiers with the original name")
          ("obfuscate-numbers", "Obfuscate numeric literals");

  options.add_options("Optimization / Debug")
          ("O1", "Simple optimizations with no risk or effect on readability")
          ("O2", "Slightly risky optimizations, logic is partially rewritten")
          ("O3", "Risky optimizations that might render script unreadable by humans")
          ("fold-constants", "Simplify the source by performing constant folding")
          ("prune-globals", "Prune unused globals")
          ("prune-locals", "Prune unused locals")
          ("prune-funcs", "Prune unused functions")
          ("lint", "Only lint the file for errors, don't optimize or pretty print.")
          ("show-tree", "Show the AST after optimizations")
          ("check-asserts", "check assert comments and suppress errors based on matches");

  options.add_options()
          ("script", "Input script's filename", cxxopts::value<std::string>());
  options.parse_positional({"script"});
  options.positional_help("<script>");

  cxxopts::ParseResult vm;
  try {
    auto parse_result = options.parse(argc, argv);
    vm = std::move(parse_result);
  } catch (cxxopts::OptionException &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << options.help() << std::endl;
    return 1;
  }

  if (vm.count("help")) {
    std::cerr << options.help() << std::endl;
    return 0;
  }

  if (vm.count("version")) {
    version();
    return 0;
  }

  if (vm.count("script")) {
    std::string filename = vm["script"].as<std::string>();
    yyin = fopen(filename.c_str(), "r");
    if (yyin == nullptr) {
      fprintf(stderr, "couldn't open %s\n", filename.c_str());
      return 1;
    }
  }

  bool check_assertions = vm.count("check-asserts");
  if (vm.count("show-tree"))
    show_tree = true;
  if (vm.count("lint")) {
    pretty_print = false;
  } else {

    pretty_opts.minify_whitespace = vm.count("minw") != 0;
    pretty_opts.mangle_global_names = vm.count("mangle-globals") != 0;
    pretty_opts.mangle_local_names = vm.count("mangle-locals") != 0;
    pretty_opts.mangle_func_names = vm.count("mangle-funcs") != 0;
    pretty_opts.show_unmangled = vm.count("show-unmangled") != 0;
    pretty_opts.obfuscate_numbers = vm.count("obfuscate-numbers") != 0;
    optim_ctx.fold_constants = vm.count("fold-constants") != 0;
    optim_ctx.prune_unused_globals = vm.count("prune-globals") != 0;
    optim_ctx.prune_unused_functions = vm.count("prune-funcs") != 0;
    optim_ctx.prune_unused_locals = vm.count("prune-locals") != 0;

    if (vm.count("O2")) {
      optim_ctx.prune_unused_globals = true;
      optim_ctx.prune_unused_locals = true;
      optim_ctx.prune_unused_functions = true;
      optim_ctx.fold_constants = true;
    }
    if (vm.count("O3")) {
      optim_ctx.prune_unused_globals = true;
      optim_ctx.prune_unused_locals = true;
      optim_ctx.prune_unused_functions = true;
      optim_ctx.fold_constants = true;
      // the length of global vars / functions and their params has an impact on bytecode size
      pretty_opts.mangle_global_names = true;
      pretty_opts.mangle_func_names = true;
      pretty_opts.show_unmangled = true;
      // stops `-1` from being treated as `unary_minus(1)`
      pretty_opts.obfuscate_numbers = true;
    }
    if (vm.count("obfuscate")) {
      optim_ctx.prune_unused_globals = true;
      optim_ctx.prune_unused_locals = true;
      optim_ctx.prune_unused_functions = true;
      optim_ctx.fold_constants = true;
      pretty_opts.mangle_global_names = true;
      pretty_opts.mangle_func_names = true;
      pretty_opts.mangle_local_names = true;
      pretty_opts.show_unmangled = false;
      pretty_opts.obfuscate_numbers = true;
      pretty_opts.minify_whitespace = true;
    }
  }
  tailslide_init_builtins(nullptr);
  // set up the allocator and logger
  ScopedScriptParser parser;
  Logger *logger = &parser.logger;

  if (check_assertions)
    logger->set_check_assertions(true);

  auto script = parser.parse_lsl(yyin);
  if (yyin != nullptr)
    fclose(yyin);

  if (script) {
    script->collect_symbols();
    script->link_symbol_tables();
    script->determine_types();
    script->recalculate_reference_data();
    script->propagate_values();
    script->check_best_practices();

    if (check_assertions) {
      logger->filter_assert_errors();
      logger->set_check_assertions(false);
    }
    // Don't try to optimize if we have a possibly broken tree
    if (!logger->get_errors()) {
      script->optimize(optim_ctx);

      // do these last since symbol usage and expressions may change
      // when rewriting the tree
      script->validate_globals(true);
      script->check_symbols();
      if (pretty_print) {
        script->get_symbol_table()->set_mangled_names();

        PrettyPrintVisitor print_visitor(pretty_opts);
        script->visit(&print_visitor);
        std::cout << print_visitor.stream.str() << "\n";
      }
    } else {
      script->validate_globals(true);
      script->check_symbols();
    }
    logger->report();
    if (show_tree) {
      std::cout << "Tree:" << std::endl;
      TreePrintingVisitor visitor;
      script->visit(&visitor);
      std::cout << visitor.stream.str();
    }
  } else {
    logger->report();
  }

  return logger->get_errors();
}
