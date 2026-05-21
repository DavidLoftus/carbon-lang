// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CARBON_TOOLCHAIN_LANGUAGE_SERVER_CONTEXT_H_
#define CARBON_TOOLCHAIN_LANGUAGE_SERVER_CONTEXT_H_

#include <memory>
#include <string>

#include "clang-tools-extra/clangd/LSPBinder.h"
#include "common/map.h"
#include "toolchain/base/install_paths.h"
#include "toolchain/base/shared_value_stores.h"
#include "toolchain/diagnostics/consumer.h"
#include "toolchain/diagnostics/emitter.h"
#include "toolchain/diagnostics/file_diagnostics.h"
#include "toolchain/lex/tokenized_buffer.h"
#include "toolchain/parse/tree_and_subtrees.h"
#include "toolchain/sem_ir/file.h"
#include "toolchain/source/source_buffer.h"

namespace Carbon::LanguageServer {

struct Options {
  bool prelude_import = true;
};

// Context for LSP call handling.
class Context {
 public:
  // Cached information for an open file.
  class File {
   public:
    explicit File(clang::clangd::URIForFile uri) : uri_(std::move(uri)) {}

    // Changes the file's text, updating dependent state.
    auto SetText(Context& context, std::optional<int64_t> version,
                 llvm::StringRef text) -> void;

    auto text() const -> llvm::StringRef { return source_->text(); }

    auto tree_and_subtrees() const -> const Parse::TreeAndSubtrees& {
      return *tree_and_subtrees_;
    }

   private:
    // Runs the type-checker and compiles the file and dependencies.
    auto Analyze(Context& context, Diagnostics::Consumer& consumer) -> void;

   private:
    // The filename, stable across instances.
    clang::clangd::URIForFile uri_;

    // Current file content, and derived values.
    std::unique_ptr<SourceBuffer> source_;
    std::unique_ptr<SharedValueStores> value_stores_;
    std::unique_ptr<Lex::TokenizedBuffer> tokens_;
    std::unique_ptr<Parse::Tree> tree_;
    std::unique_ptr<Parse::TreeAndSubtrees> tree_and_subtrees_;
  };

  // `vlog_stream` is optional; other parameters are required.
  explicit Context(const InstallPaths* installation,
                   llvm::raw_ostream* vlog_stream,
                   Diagnostics::Consumer* consumer,
                   clang::clangd::LSPBinder::RawOutgoing* outgoing,
                   Options options)
      : installation_(installation),
        vlog_stream_(vlog_stream),
        file_emitter_(consumer),
        no_loc_emitter_(consumer),
        outgoing_(outgoing),
        options_(options) {}

  // Returns a reference to the file if it's known, or diagnoses and returns
  // null.
  auto LookupFile(llvm::StringRef filename) -> File*;

  // Wrapper for LSP notification.
  auto PublishDiagnostics(clang::clangd::PublishDiagnosticsParams params)
      -> void {
    outgoing_->notify("textDocument/publishDiagnostics", params);
  }

  auto installation() -> const InstallPaths& { return *installation_; }

  auto vlog_stream() -> llvm::raw_ostream* { return vlog_stream_; }
  auto file_emitter() -> Diagnostics::FileEmitter& { return file_emitter_; }
  auto no_loc_emitter() -> Diagnostics::NoLocEmitter& {
    return no_loc_emitter_;
  }

  auto files() -> Map<std::string, File>& { return files_; }

  auto options() const -> const Options& { return options_; }

 private:
  const InstallPaths* installation_;

  // Diagnostic and output streams.
  llvm::raw_ostream* vlog_stream_;
  Diagnostics::FileEmitter file_emitter_;
  Diagnostics::NoLocEmitter no_loc_emitter_;
  clang::clangd::LSPBinder::RawOutgoing* outgoing_;

  // Content of files managed by the language client.
  Map<std::string, File> files_;

  Options options_;
};

}  // namespace Carbon::LanguageServer

#endif  // CARBON_TOOLCHAIN_LANGUAGE_SERVER_CONTEXT_H_
