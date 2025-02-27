// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef CARBON_TOOLCHAIN_LANGUAGE_SERVER_SYMBOL_H_
#define CARBON_TOOLCHAIN_LANGUAGE_SERVER_SYMBOL_H_

#include "clang-tools-extra/clangd/Protocol.h"
#include "toolchain/parse/tree_and_subtrees.h"

namespace Carbon::LanguageServer {

auto GetSymbolIdentifier(const Parse::TreeAndSubtrees& tree_and_subtrees,
                         Parse::NodeId node) -> std::optional<Lex::TokenIndex>;

auto GetSelectionRange(const Lex::TokenizedBuffer& tokens,
                       Lex::TokenIndex identifier) -> clang::clangd::Range;

// Finds a spanning range for the provided definition / declaration ast node.
// In the case of a definition start, will include the body as well.
auto GetSymbolRange(const Parse::TreeAndSubtrees& tree_and_subtrees,
                    const Parse::NodeId& ast_node) -> clang::clangd::Range;

}  // namespace Carbon::LanguageServer

#endif  // CARBON_TOOLCHAIN_LANGUAGE_SERVER_SYMBOL_H_
