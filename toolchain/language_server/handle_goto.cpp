// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdint>
#include <iostream>
#include <iterator>
#include <optional>

#include "common/check.h"
#include "common/ostream.h"
#include "toolchain/language_server/handle.h"
#include "toolchain/language_server/symbol.h"
#include "toolchain/lex/token_index.h"
#include "toolchain/lex/token_kind.h"
#include "toolchain/lex/tokenized_buffer.h"
#include "toolchain/parse/node_category.h"
#include "toolchain/parse/node_ids.h"
#include "toolchain/parse/node_kind.h"
#include "toolchain/parse/tree.h"
#include "toolchain/parse/tree_and_subtrees.h"
#include "toolchain/parse/typed_nodes.h"
#include "toolchain/sem_ir/ids.h"
#include "toolchain/sem_ir/inst.h"
#include "toolchain/sem_ir/typed_insts.h"

namespace Carbon::LanguageServer {

static auto PositionToByteOffset(const Lex::TokenizedBuffer& tokens,
                                 const clang::clangd::Position& position)
    -> llvm::Expected<int32_t> {
  if (position.line < 0 || position.line >= tokens.GetLineCount()) {
    return llvm::make_error<clang::clangd::LSPError>(
        llvm::formatv("no line with index `{0}`", position.line),
        clang::clangd::ErrorCode::InvalidParams);
  }

  auto line_index = Lex::LineIndex(position.line);
  int32_t line_offset = tokens.GetByteOffset(line_index);
  int32_t line_length = tokens.GetLineLength(line_index);
  if (position.character < 0 || position.character >= line_length) {
    return llvm::make_error<clang::clangd::LSPError>(
        llvm::formatv("no character at column `{0}`", position.character),
        clang::clangd::ErrorCode::InvalidParams);
  }
  return line_offset + position.character;
}

// Finds token for given offset into source, returns a pair (token_index,
// touches_next) where token_index is the left most token token that touches the
// cursor, and touches_next indicates that cursor also touches token at
// token_index+1.
static auto FindTokenIndex(const Lex::TokenizedBuffer& tokens,
                           int32_t byte_offset)
    -> std::pair<Lex::TokenIndex, bool> {
  if (tokens.size() == 0) {
    return {Lex::TokenIndex::None, false};
  }

  auto partition_it = llvm::partition_point(tokens.tokens(), [&](auto token) {
    return tokens.GetByteOffset(token) < byte_offset;
  });

  bool touching_right_token =
      partition_it != tokens.tokens().end() &&
      tokens.GetByteOffset(*partition_it) == byte_offset;
  if (partition_it != tokens.tokens().begin()) {
    auto token_it = std::prev(partition_it);
    int32_t end_offset =
        tokens.GetByteOffset(*token_it) + tokens.GetTokenText(*token_it).size();
    if (end_offset >= byte_offset) {
      return {*token_it, touching_right_token};
    }
  }
  if (touching_right_token) {
    return {*partition_it, false};
  }
  return {Lex::TokenIndex::None, false};
}

static auto FindSymbolToken(const Lex::TokenizedBuffer& tokens,
                            int32_t byte_offset) -> Lex::TokenIndex {
  auto [token_index, touches_next] = FindTokenIndex(tokens, byte_offset);
  if (!token_index.has_value()) {
    return Lex::TokenIndex::None;
  }
  // If the token is punctuation, and is split between two tokens, then prefer
  // the right token.
  if (tokens.GetKind(token_index).is_symbol() && touches_next) {
    return Lex::TokenIndex(token_index.index + 1);
  }
  return token_index;
}

struct NodeWithPath {
  Parse::NodeId node;
  llvm::SmallVector<Parse::NodeId> path;
};

// Given a node returns the TokenIndex of the left most token within the nodes
// subtree.
static auto GetLeftMostToken(const Parse::TreeAndSubtrees& tree_and_subtrees,
                             Parse::NodeId node) -> Lex::TokenIndex {
  // For both infix operators and postfix, left most token is always the left
  // most node in the parse tree. Left most node will be the first node in
  // postorder traversal.
  auto subtree_range = tree_and_subtrees.postorder(node);
  CARBON_CHECK(!subtree_range.empty());
  auto left_most_node = *subtree_range.begin();

  auto left_most_token = tree_and_subtrees.tree().node_token(left_most_node);
  auto node_token = tree_and_subtrees.tree().node_token(node);

  // In postfix + infix operators, left most token is always the left most node.
  // In prefix operators, left most token is the token of the node itself,
  // rather than checking the node kind, simply compare the indexes.
  return left_most_token.index < node_token.index ? left_most_token
                                                  : node_token;
}

// Given a range of sibling ast nodes and a token, find the node whose subtree
// contains that token.
static auto FindChildWithToken(
    const Parse::TreeAndSubtrees& tree_and_subtrees,
    llvm::iterator_range<Parse::TreeAndSubtrees::SiblingIterator> children,
    Lex::TokenIndex token) -> Parse::NodeId {
  if (children.empty()) {
    return Parse::NodeId::None;
  }

  // SiblingIterator iterates children in reverse, so we are searching for the
  // first child where the left most node
  for (auto child : children) {
    auto min_token = GetLeftMostToken(tree_and_subtrees, child);
    if (min_token.index <= token.index) {
      return child;
    }
  }
  return Parse::NodeId::None;
}

// Given a token, finds the ast node that contains the token, returns both the
// node and the path from root to the node.
static auto FindSymbolNodeWithPath(
    const Parse::TreeAndSubtrees& tree_and_subtrees, Lex::TokenIndex token)
    -> std::optional<NodeWithPath> {
  const auto& tree = tree_and_subtrees.tree();

  llvm::SmallVector<Parse::NodeId> path;
  auto siblings = tree_and_subtrees.roots();
  while (true) {
    auto node = FindChildWithToken(tree_and_subtrees, siblings, token);
    if (!node.has_value()) {
      return std::nullopt;
    }
    if (tree.node_token(node).index == token.index) {
      return NodeWithPath{node, std::move(path)};
    }

    path.push_back(node);
    siblings = tree_and_subtrees.children(node);
  }
}

// Checks if node is a member name for parent MemberAccessExpr /
// PointerMemberAccessExpr.
static auto IsNamedMemberAccessExpr(
    const Parse::TreeAndSubtrees& tree_and_subtrees, Parse::NodeId parent,
    Parse::NodeId node) -> bool {
  if (!tree_and_subtrees.tree().node_kind(node).category().HasAnyOf(
          Parse::NodeCategory::MemberName)) {
    return false;
  }

  if (auto expr =
          tree_and_subtrees.ExtractAs<Parse::MemberAccessExpr>(parent)) {
    return expr->rhs == node;
  }
  if (auto expr =
          tree_and_subtrees.ExtractAs<Parse::PointerMemberAccessExpr>(parent)) {
    return expr->rhs == node;
  }
  return false;
}

// Given a token for some symbol, find the AST node that best represents the
// symbol. Will ascend the tree if the directly related AST node is insufficient
// for scanning SemIR.
static auto FindSymbolNode(const Parse::TreeAndSubtrees& tree_and_subtrees,
                           Lex::TokenIndex token) -> Parse::NodeId {
  auto node_with_path = FindSymbolNodeWithPath(tree_and_subtrees, token);
  if (!node_with_path) {
    return Parse::NodeId::None;
  }

  const auto& [node, path] = *node_with_path;
  if (!path.empty()) {
    auto parent_node = path.back();
    if (IsNamedMemberAccessExpr(tree_and_subtrees, parent_node, node)) {
      // node is the rhs of a MemberAccessExpr, parent_node should have an inst
      // that points to members definition.
      return parent_node;
    }
  }
  return node;
}

// Given an ast node, finds the Inst that points to the node.
static auto FindSymbolInst(const SemIR::File& sem_ir, Parse::NodeId node)
    -> SemIR::InstId {
  // TODO: use path to give hint of where to look.
  for (auto [i, inst] : llvm::enumerate(sem_ir.insts().array_ref())) {
    SemIR::InstId inst_id(i);
    if (sem_ir.insts().Is<SemIR::Deref>(inst_id)) {
      continue;
    }
    if (auto loc = sem_ir.insts().GetLocId(inst_id);
        loc.is_node_id() && loc.node_id() == node) {
      return inst_id;
    }
  }
  return SemIR::InstId::None;
}

// Given a byte_offset cursor position, finds the symbol that cursor is on, in
// the form of its InstId.
static auto LookupSymbol(const SemIR::File& sem_ir,
                         const Parse::TreeAndSubtrees& tree_and_subtrees,
                         int32_t byte_offset) -> SemIR::InstId {
  auto token = FindSymbolToken(tree_and_subtrees.tree().tokens(), byte_offset);
  if (!token.has_value()) {
    return SemIR::InstId::None;
  }

  auto node = FindSymbolNode(tree_and_subtrees, token);
  if (!node.has_value()) {
    return SemIR::InstId::None;
  }
  return FindSymbolInst(sem_ir, node);
}

// Given an InstId value, tries to resolve an the Inst for the symbols
// definition.
static auto FindSymbolDefinition(const SemIR::File& sem_ir,
                                 SemIR::InstId symbol) -> SemIR::InstId {
  if (auto name_ref = sem_ir.insts().TryGetAs<SemIR::NameRef>(symbol)) {
    return name_ref->value_id;
  }
  return SemIR::InstId::None;
}

static auto GetSymbolLocation(const SemIR::File& sem_ir,
                              const Parse::TreeAndSubtrees& tree_and_subtrees,
                              const clang::clangd::URIForFile& uri,
                              SemIR::InstId inst)
    -> std::optional<clang::clangd::Location> {
  auto loc = sem_ir.insts().GetLocId(inst);
  if (!loc.is_node_id()) {
    // TODO: handle imported symbols.
    return std::nullopt;
  }

  return clang::clangd::Location{
      .uri = uri, .range = GetSymbolRange(tree_and_subtrees, loc.node_id())};
}

auto HandleGotoDefinition(
    Context& context, const clang::clangd::TextDocumentPositionParams& params,
    llvm::function_ref<auto(llvm::Expected<llvm::json::Value>)->void> on_done)
    -> void {
  auto* file = context.LookupFile(params.textDocument.uri.file());
  if (!file) {
    on_done(llvm::make_error<clang::clangd::LSPError>(
        llvm::formatv("unknown file `{0}`", params.textDocument.uri.file()),
        clang::clangd::ErrorCode::InvalidRequest));
    return;
  }

  const auto& tree_and_subtrees = file->tree_and_subtrees();
  const auto& sem_ir = file->sem_ir();

  auto byte_offset =
      PositionToByteOffset(tree_and_subtrees.tree().tokens(), params.position);
  if (auto err = byte_offset.takeError()) {
    on_done(std::move(err));
    return;
  }

  auto inst = LookupSymbol(sem_ir, tree_and_subtrees, *byte_offset);
  if (!inst.has_value()) {
    on_done(nullptr);
    return;
  }

  auto definition_inst = FindSymbolDefinition(sem_ir, inst);
  if (!definition_inst.has_value()) {
    on_done(nullptr);
    return;
  }

  auto location = GetSymbolLocation(sem_ir, tree_and_subtrees,
                                    params.textDocument.uri, definition_inst);
  if (!location) {
    on_done(nullptr);
    return;
  }

  on_done(std::move(*location));
}

}  // namespace Carbon::LanguageServer
