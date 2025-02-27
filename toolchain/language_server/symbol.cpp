// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "toolchain/language_server/symbol.h"

#include "toolchain/parse/node_ids.h"
#include "toolchain/parse/tree_and_subtrees.h"

namespace Carbon::LanguageServer {

// Returns the token of first child of kind IdentifierNameBeforeParams or
// IdentifierNameNotBeforeParams.
auto GetSymbolIdentifier(const Parse::TreeAndSubtrees& tree_and_subtrees,
                         Parse::NodeId node) -> std::optional<Lex::TokenIndex> {
  const auto& tokens = tree_and_subtrees.tree().tokens();
  for (auto child : tree_and_subtrees.children(node)) {
    switch (tree_and_subtrees.tree().node_kind(child)) {
      case Parse::NodeKind::IdentifierNameBeforeParams:
      case Parse::NodeKind::IdentifierNameNotBeforeParams: {
        auto token = tree_and_subtrees.tree().node_token(child);
        if (tokens.GetKind(token) == Lex::TokenKind::Identifier) {
          return token;
        }
        break;
      }
      default:
        break;
    }
  }
  return std::nullopt;
}

// Constructs a Range from a closed interval of tokens [start, end].
static auto GetTokenRange(const Lex::TokenizedBuffer& tokens,
                          Lex::TokenIndex start, Lex::TokenIndex end)
    -> clang::clangd::Range {
  auto start_line = tokens.GetLine(start);
  auto start_col = tokens.GetColumnNumber(start);
  auto [end_line, end_col] = tokens.GetEndLoc(end);

  return clang::clangd::Range{
      .start = {.line = start_line.index, .character = start_col - 1},
      .end = {.line = end_line.index, .character = end_col - 1},
  };
}

auto GetSelectionRange(const Lex::TokenizedBuffer& tokens,
                       Lex::TokenIndex identifier) -> clang::clangd::Range {
  return GetTokenRange(tokens, identifier, identifier);
}

// Finds a spanning range for the provided definition / declaration ast node.
// In the case of a definition start, will include the body as well.
auto GetSymbolRange(const Parse::TreeAndSubtrees& tree_and_subtrees,
                    const Parse::NodeId& ast_node) -> clang::clangd::Range {
  const auto& tokens = tree_and_subtrees.tree().tokens();

  // The left-most node will always be the first node in postorder traversal.
  auto start_node = *tree_and_subtrees.postorder(ast_node).begin();

  auto start_token = tree_and_subtrees.tree().node_token(start_node);
  auto end_token = tree_and_subtrees.tree().node_token(ast_node);
  if (tokens.GetKind(end_token).is_opening_symbol()) {
    // DefinitionStart nodes use an opening token, so find its closing token to
    // span the entire class/function body.
    return GetTokenRange(tokens, start_token,
                         tokens.GetMatchedClosingToken(end_token));
  } else {
    return GetTokenRange(tokens, start_token, end_token);
  }
}

}  // namespace Carbon::LanguageServer
