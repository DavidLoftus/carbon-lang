// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "toolchain/language_server/context.h"

#include <memory>
#include <optional>
#include <utility>

#include "common/check.h"
#include "common/raw_string_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "toolchain/base/clang_invocation.h"
#include "toolchain/base/shared_value_stores.h"
#include "toolchain/check/check.h"
#include "toolchain/diagnostics/consumer.h"
#include "toolchain/diagnostics/diagnostic.h"
#include "toolchain/diagnostics/emitter.h"
#include "toolchain/lex/lex.h"
#include "toolchain/lex/tokenized_buffer.h"
#include "toolchain/parse/parse.h"
#include "toolchain/parse/tree_and_subtrees.h"

namespace Carbon::LanguageServer {

namespace {
// A consumer for turning diagnostics into a `textDocument/publishDiagnostics`
// notification.
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_publishDiagnostics
class DiagnosticConsumer : public Diagnostics::Consumer {
 public:
  // Initializes params with the target file information.
  explicit DiagnosticConsumer(Context* context,
                              const clang::clangd::URIForFile& uri,
                              std::optional<int64_t> version)
      : context_(context), params_{.uri = uri, .version = version} {}

  // Turns a diagnostic into an LSP diagnostic.
  auto HandleDiagnostic(Diagnostics::Diagnostic diagnostic) -> void override {
    const auto& message = diagnostic.messages[0];
    if (message.loc.filename != params_.uri.file()) {
      // `pushDiagnostic` requires diagnostics to be associated with a location
      // in the current file. Suppress diagnostics rooted in other files.
      // TODO: Consider if there's a better way to handle this.
      RawStringOstream stream;
      Diagnostics::StreamConsumer consumer(&stream);
      consumer.HandleDiagnostic(diagnostic);

      CARBON_DIAGNOSTIC(LanguageServerDiagnosticInWrongFile, Warning,
                        "dropping diagnostic in {0}:\n{1}", std::string,
                        std::string);
      context_->file_emitter().Emit(
          params_.uri.file(), LanguageServerDiagnosticInWrongFile,
          message.loc.filename.str(), stream.TakeStr());
      return;
    }

    // Add the main message.
    params_.diagnostics.push_back(clang::clangd::Diagnostic{
        .range = GetRange(message.loc),
        .severity = GetSeverity(diagnostic.level),
        .source = "carbon",
        .message = message.Format(),
    });
    // TODO: Figure out constructing URIs for note locations.
  }

  // Returns the constructed request.
  auto params() -> const clang::clangd::PublishDiagnosticsParams& {
    return params_;
  }

 private:
  // Returns the LSP range for a diagnostic. Note that Carbon uses 1-based
  // numbers while LSP uses 0-based.
  auto GetRange(const Diagnostics::Loc& loc) -> clang::clangd::Range {
    return {.start = {.line = loc.line_number - 1,
                      .character = loc.column_number - 1},
            .end = {.line = loc.line_number,
                    .character = loc.column_number + loc.length}};
  }

  // Converts a diagnostic level to an LSP severity.
  auto GetSeverity(Diagnostics::Level level) -> int {
    // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnosticSeverity
    enum class DiagnosticSeverity {
      Error = 1,
      Warning = 2,
      Information = 3,
      Hint = 4,
    };

    switch (level) {
      case Diagnostics::Level::Error:
        return static_cast<int>(DiagnosticSeverity::Error);
      case Diagnostics::Level::Warning:
        return static_cast<int>(DiagnosticSeverity::Warning);
      default:
        CARBON_FATAL("Unexpected diagnostic level: {0}", level);
    }
  }

  Context* context_;
  clang::clangd::PublishDiagnosticsParams params_;
};
}  // namespace

auto Context::File::SetText(Context& context, std::optional<int64_t> version,
                            llvm::StringRef text) -> void {
  // Clear state dependent on the source text.
  tree_and_subtrees_.reset();
  tree_.reset();
  tokens_.reset();
  value_stores_.reset();
  source_.reset();

  // A consumer to gather diagnostics for the file.
  DiagnosticConsumer consumer(&context, uri_, version);

  // TODO: Make the processing asynchronous, to better handle rapid text
  // updates.
  CARBON_CHECK(!source_ && !value_stores_ && !tokens_ && !tree_,
               "We currently cache everything together");
  // TODO: Diagnostics should be passed to the LSP instead of dropped.
  std::optional source =
      SourceBuffer::MakeFromStringCopy(uri_.file(), text, consumer);
  if (!source) {
    // Failing here should be rare, but provide stub data for recovery so that
    // we can have a simple API.
    source = SourceBuffer::MakeFromStringCopy(uri_.file(), "", consumer);
    CARBON_CHECK(source, "Making an empty buffer should always succeed");
  }
  source_ = std::make_unique<SourceBuffer>(std::move(*source));
  value_stores_ = std::make_unique<SharedValueStores>();

  Lex::LexOptions lex_options;
  lex_options.consumer = &consumer;
  tokens_ = std::make_unique<Lex::TokenizedBuffer>(
      Lex::Lex(*value_stores_, *source_, lex_options));

  Parse::ParseOptions parse_options;
  parse_options.consumer = &consumer;
  parse_options.vlog_stream = context.vlog_stream();
  tree_ = std::make_unique<Parse::Tree>(Parse::Parse(*tokens_, parse_options));
  tree_and_subtrees_ =
      std::make_unique<Parse::TreeAndSubtrees>(*tokens_, *tree_);

  Analyze(context, consumer);

  // Note we need to publish diagnostics even when empty.
  // TODO: Consider caching previously published diagnostics and only publishing
  // when they change.
  context.PublishDiagnostics(consumer.params());
}

auto Context::File::Analyze(Context& context, Diagnostics::Consumer& consumer) -> void {
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> fs =
      llvm::vfs::getRealFileSystem();

  // Load prelude files if requested.
  llvm::SmallVector<std::string> prelude_filenames;
  if (context.options().prelude_import) {
    if (auto find = context.installation().ReadPreludeManifest(); find.ok()) {
      prelude_filenames = std::move(*find);
    } else {
      CARBON_DIAGNOSTIC(LanguageServerPreludeManifestError, Error, "{0}",
                        std::string);
      context.no_loc_emitter().Emit(LanguageServerPreludeManifestError,
                                    find.error().message());
    }
  }

  int total_ir_count = prelude_filenames.size() + 1;

  struct CheckedUnit {
    std::unique_ptr<SourceBuffer> source;
    std::unique_ptr<SharedValueStores> value_stores;
    std::unique_ptr<Lex::TokenizedBuffer> tokens;
    std::unique_ptr<Parse::Tree> tree;
    std::unique_ptr<Parse::TreeAndSubtrees> tree_and_subtrees;
    std::unique_ptr<SemIR::File> sem_ir;
  };

  llvm::SmallVector<CheckedUnit> prelude_units;
  prelude_units.reserve(prelude_filenames.size());

  llvm::SmallVector<Check::Unit> check_units;
  check_units.reserve(total_ir_count);

  auto getters = Parse::GetTreeAndSubtreesStore::MakeWithExplicitSize(
      total_ir_count, nullptr);

  // Keeper to ensure lambdas passed to getters remain alive and at stable
  // addresses.
  llvm::SmallVector<
      std::unique_ptr<std::function<const Parse::TreeAndSubtrees&()>>>
      lambda_keeper;
  lambda_keeper.reserve(total_ir_count);

  int unit_index = 0;

  class IgnoreConsumer : public Diagnostics::Consumer {
   public:
    auto HandleDiagnostic(Diagnostics::Diagnostic /*diagnostic*/)
        -> void override {}
  };
  IgnoreConsumer prelude_consumer;

  for (const auto& filename : prelude_filenames) {
    auto source_buf =
        SourceBuffer::MakeFromFileOrStdin(*fs, filename, prelude_consumer);
    if (!source_buf) {
      continue;
    }
    auto heap_source = std::make_unique<SourceBuffer>(std::move(*source_buf));
    auto val_stores = std::make_unique<SharedValueStores>();
    Lex::LexOptions lex_opts;
    lex_opts.consumer = &prelude_consumer;
    auto tokens_buf = std::make_unique<Lex::TokenizedBuffer>(
        Lex::Lex(*val_stores, *heap_source, lex_opts));

    Parse::ParseOptions parse_opts;
    parse_opts.consumer = &prelude_consumer;
    auto parse_tree =
        std::make_unique<Parse::Tree>(Parse::Parse(*tokens_buf, parse_opts));

    auto tree_sub =
        std::make_unique<Parse::TreeAndSubtrees>(*tokens_buf, *parse_tree);

    auto sem_ir_file = std::make_unique<SemIR::File>(
        parse_tree.get(), SemIR::CheckIRId(unit_index),
        parse_tree->packaging_decl(), *val_stores, filename);

    auto lambda =
        std::make_unique<std::function<const Parse::TreeAndSubtrees&()>>(
            [tree_sub_ptr = tree_sub.get()]() -> const Parse::TreeAndSubtrees& {
              return *tree_sub_ptr;
            });
    getters.Set(SemIR::CheckIRId(unit_index), *lambda);
    lambda_keeper.push_back(std::move(lambda));

    check_units.push_back({.consumer = &prelude_consumer,
                           .value_stores = val_stores.get(),
                           .timings = nullptr,
                           .sem_ir = sem_ir_file.get(),
                           .llvm_context = nullptr,
                           .total_ir_count = total_ir_count});

    prelude_units.push_back({.source = std::move(heap_source),
                             .value_stores = std::move(val_stores),
                             .tokens = std::move(tokens_buf),
                             .tree = std::move(parse_tree),
                             .tree_and_subtrees = std::move(tree_sub),
                             .sem_ir = std::move(sem_ir_file)});

    ++unit_index;
  }

  SemIR::File sem_ir(tree_.get(), SemIR::CheckIRId(unit_index),
                     tree_->packaging_decl(), *value_stores_,
                     uri_.file().str());

  auto lambda =
      std::make_unique<std::function<const Parse::TreeAndSubtrees&()>>(
          [tree_sub_ptr =
               tree_and_subtrees_.get()]() -> const Parse::TreeAndSubtrees& {
            return *tree_sub_ptr;
          });
  getters.Set(SemIR::CheckIRId(unit_index), *lambda);
  lambda_keeper.push_back(std::move(lambda));

  check_units.push_back({.consumer = &consumer,
                         .value_stores = value_stores_.get(),
                         .timings = nullptr,
                         .sem_ir = &sem_ir,
                         .llvm_context = nullptr,
                         .total_ir_count = total_ir_count});

  Check::CheckParseTreesOptions check_options;
  check_options.prelude_import = context.options().prelude_import;
  check_options.vlog_stream = context.vlog_stream();

  auto clang_invocation =
      BuildClangInvocation(consumer, fs, context.installation(),
                           llvm::sys::getDefaultTargetTriple());

  Check::CheckParseTrees(check_units, getters, fs, check_options,
                         std::move(clang_invocation));
}

auto Context::LookupFile(llvm::StringRef filename) -> File* {
  if (!filename.ends_with(".carbon")) {
    CARBON_DIAGNOSTIC(LanguageServerFileUnsupported, Warning,
                      "non-Carbon file requested");
    file_emitter_.Emit(filename, LanguageServerFileUnsupported);
    return nullptr;
  }

  if (auto lookup_result = files().Lookup(filename)) {
    return &lookup_result.value();
  } else {
    CARBON_DIAGNOSTIC(LanguageServerFileUnknown, Warning,
                      "unknown file requested");
    file_emitter_.Emit(filename, LanguageServerFileUnknown);
    return nullptr;
  }
}

}  // namespace Carbon::LanguageServer
