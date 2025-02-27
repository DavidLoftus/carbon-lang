// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <cstdint>
#include <iostream>
#include <iterator>

#include "common/check.h"
#include "common/ostream.h"
#include "toolchain/language_server/handle.h"
#include "toolchain/language_server/symbol.h"
#include "toolchain/lex/token_index.h"
#include "toolchain/lex/token_kind.h"
#include "toolchain/lex/tokenized_buffer.h"
#include "toolchain/parse/node_ids.h"
#include "toolchain/parse/node_kind.h"
#include "toolchain/parse/tree.h"
#include "toolchain/parse/tree_and_subtrees.h"
#include "toolchain/parse/typed_nodes.h"
#include "toolchain/sem_ir/ids.h"
#include "toolchain/sem_ir/typed_insts.h"

namespace Carbon::LanguageServer {

// Finds range of tokens which touch a given code location.
// Can be up to 2 tokens.
static auto GetTokensTouchingCursor(const Lex::TokenizedBuffer& tokens,
                                    const clang::clangd::Position& position)
    -> std::pair<Lex::TokenIndex, Lex::TokenIndex> {
  // Find first token which goes past position.
  int32_t position_offset =
      tokens.GetByteOffset(Lex::LineIndex(position.line)) + position.character;
  auto end_token_it = llvm::partition_point(tokens.tokens(), [&](auto token) {
    return tokens.GetByteOffset(token) <= position_offset;
  });

  // TODO: reverse iterators are super unintuitive, consider just doing a loop.
  while (end_token_it != tokens.tokens().begin() &&
         tokens.GetByteOffset(*std::prev(end_token_it)) == position_offset) {
    --end_token_it;
  }

  // Seek backwards looking for token which does not touch position.
  auto reverse_range =
      llvm::iterator_range(std::make_reverse_iterator(end_token_it),
                           std::make_reverse_iterator(tokens.tokens().begin()));
  auto reverse_it = llvm::find_if(reverse_range, [&](Lex::TokenIndex token) {
    int32_t offset = tokens.GetByteOffset(token);
    int32_t length = tokens.GetTokenText(token).size();
    return offset + length < position_offset;
  });

  // Build final range, reverse_it.base() will give reverse_it->index + 1.
  return {*reverse_it.base(), *end_token_it};
}

static auto PickTokenTouchingCursor(const Lex::TokenizedBuffer& tokens,
                                    const clang::clangd::Position& position)
    -> std::optional<Lex::TokenIndex> {
  auto [start_token, end_token] = GetTokensTouchingCursor(tokens, position);
  if (start_token == end_token) {
    return std::nullopt;
  }

  int count = end_token.index - start_token.index;
  switch (count) {
    case 0:
    case 1:
      return start_token;
    case 2:
      // If one is a symbol, pick the other.
      if (tokens.GetKind(start_token).is_symbol()) {
        return Lex::TokenIndex(start_token.index + 1);
      } else {
        return start_token;
      }
    default:
      CARBON_FATAL("Unexpected number of neighbouring tokens: {}", count);
  }
  return std::nullopt;
}

struct NodeWithPath {
  Parse::NodeId node;
  llvm::SmallVector<Parse::NodeId> path;
};

static auto GetLeftMostNode(const Parse::TreeAndSubtrees& tree_and_subtrees,
                            Parse::NodeId node) -> Parse::NodeId {
  auto range = tree_and_subtrees.postorder(node);
  CARBON_CHECK(!range.empty());
  return *range.begin();
}

static auto GetSelectedAstNodes(const Parse::TreeAndSubtrees& tree_and_subtrees,
                                const clang::clangd::Position& position)
    -> std::optional<NodeWithPath> {
  const auto& tree = tree_and_subtrees.tree();
  // const auto& tokens = tree.tokens();
  auto token =
      PickTokenTouchingCursor(tree_and_subtrees.tree().tokens(), position);
  if (!token) {
    return std::nullopt;
  }

  llvm::SmallVector<Parse::NodeId> path;

  // Unlike tokens, postorder() can give nodes in order different to source
  // code, so we can't binary search. Siblings should always be in order though,
  // so we can use this to descend the tree until we find the node.
  auto siblings = tree_and_subtrees.roots();
  auto it = siblings.begin();
  while (it != siblings.end()) {
    auto node = *it;

    if (tree_and_subtrees.tree().node_token(node) == *token) {
      return NodeWithPath{node, path};
    }

    auto left_most = GetLeftMostNode(tree_and_subtrees, node);
    auto left_most_token = tree.node_token(left_most);
    if (left_most_token.index <= token->index) {
      // First child where left most node comes on or before target node. This
      // means token is a child of this node.
      path.push_back(node);
      siblings = tree_and_subtrees.children(node);
      it = siblings.begin();
    } else {
      // Otherwise it must be in another sibling.
      it++;
    }
  }
  return std::nullopt;
}

// class InstIndex {
//  public:
//   // int32_t matches the input value size.
//   // NOLINTNEXTLINE(performance-enum-size)
//   enum class ScopeId : int32_t {
//     None = -1,
//     File = 0,
//     ImportRefs = 1,
//     Constants = 2,
//     FirstFunction = 3,
//   };
//   static_assert(sizeof(ScopeId) == sizeof(SemIR::FunctionId));

//   struct NumberOfScopesTag {};

//   // Returns the scope ID corresponding to an ID of a function, class, or
//   // interface.
//   template <typename IdT>
//   auto GetScopeFor(IdT id) const -> ScopeId {
//     auto index = static_cast<int32_t>(ScopeId::FirstFunction);

//     if constexpr (!std::same_as<SemIR::FunctionId, IdT>) {
//       index += sem_ir_->functions().size();
//       if constexpr (!std::same_as<SemIR::ClassId, IdT>) {
//         index += sem_ir_->classes().size();
//         if constexpr (!std::same_as<SemIR::InterfaceId, IdT>) {
//           index += sem_ir_->interfaces().size();
//           if constexpr (!std::same_as<SemIR::AssociatedConstantId, IdT>) {
//             index += sem_ir_->associated_constants().size();
//             if constexpr (!std::same_as<SemIR::ImplId, IdT>) {
//               index += sem_ir_->impls().size();
//               static_assert(std::same_as<NumberOfScopesTag, IdT>,
//                             "Unknown ID kind for scope");
//             }
//           }
//         }
//       }
//     }
//     if constexpr (!std::same_as<NumberOfScopesTag, IdT>) {
//       index += id.index;
//     }
//     return static_cast<ScopeId>(index);
//   }

//   template <typename IdT>
//   auto ScopeIdAs(ScopeId id) -> std::optional<IdT> {
//     auto start = static_cast<int32_t>(ScopeId::FirstFunction);
//     auto end = start + static_cast<int32_t>(sem_ir_->functions().size());

//     if constexpr (!std::same_as<SemIR::FunctionId, IdT>) {
//       start = end;
//       end += sem_ir_->functions().size();
//       if constexpr (!std::same_as<SemIR::ClassId, IdT>) {
//         start = end;
//         end += sem_ir_->classes().size();
//         if constexpr (!std::same_as<SemIR::InterfaceId, IdT>) {
//           start = end;
//           end += sem_ir_->interfaces().size();
//           if constexpr (!std::same_as<SemIR::AssociatedConstantId, IdT>) {
//             start = end;
//             end += sem_ir_->associated_constants().size();
//             static_assert(std::same_as<SemIR::ImplId, IdT>,
//                           "Unknown ID kind for scope");
//           }
//         }
//       }
//     }

//     if (static_cast<int32_t>(id) < start || static_cast<int32_t>(id) >= end)
//     {
//       return std::nullopt;
//     }
//     return IdT(id);
//   }

//   explicit InstIndex(const SemIR::File* sem_ir) : sem_ir_(sem_ir) {
//     CARBON_CHECK(sem_ir_ != nullptr);

//     BuildAstLookup();
//   }

//   auto BuildAstLookup() -> void {
//     // Build the constants scope.
//     CollectAstNodesInBlock(ScopeId::Constants,
//                            sem_ir_->constants().array_ref());

//     // Build the ImportRef scope.
//     CollectAstNodesInBlock(ScopeId::ImportRefs,
//     SemIR::InstBlockId::ImportRefs);

//     // Build the file scope.
//     CollectAstNodesInBlock(ScopeId::File, sem_ir_->top_inst_block_id());

//     for (const auto& [i, fn] :
//          llvm::enumerate(sem_ir_->functions().array_ref())) {
//       SemIR::FunctionId fn_id(i);
//       auto fn_scope = GetScopeFor(fn_id);
//       CollectAstNodesInBlock(fn_scope, fn.implicit_param_patterns_id);
//       CollectAstNodesInBlock(fn_scope, fn.param_patterns_id);
//       for (auto block_id : fn.body_block_ids) {
//         CollectAstNodesInBlock(fn_scope, block_id);
//       }
//     }
//   }

//   auto CollectAstNodesInBlock(ScopeId scope_id, SemIR::InstBlockId block_id)
//       -> void {
//     CollectAstNodesInBlock(scope_id, sem_ir_->inst_blocks().Get(block_id));
//   }

//   auto CollectAstNodesInBlock(ScopeId scope_id,
//                               llvm::ArrayRef<SemIR::InstId> block) -> void {
//     const auto& tree = sem_ir_->parse_tree();
//     for (auto inst_id : block) {
//       const auto& [loc_id, inst] = sem_ir_->insts().GetWithLocId(inst_id);
//       if (!loc_id.is_node_id()) {
//         continue;
//       }
//       auto node_id = loc_id.node_id();
//       auto node_kind = tree.node_kind(node_id);
//       switch (node_kind) {
//         case Parse::NodeKind::MemberAccessExpr:
//         case Parse::NodeKind::PointerMemberAccessExpr:
//           node_id = tree_and_subtrees_.Extract();
//           break;

//         default:
//           continue;
//       }

//       ast_lookup_.emplace(node_id, scope_id);
//     }
//   }

//  private:
//   const SemIR::File* sem_ir_;

//   struct InstIdWithScope {
//     SemIR::InstId inst_id;
//     ScopeId scope_id;
//   };

//   std::unordered_map<Parse::NodeId, InstIdWithBlock> ast_lookup_;
// };

template <typename... Ts, typename F>
static auto Visit(const Parse::Tree& tree, Parse::NodeIdOneOf<Ts...> node_id,
                  F&& f) -> void {
  const auto optional_apply = [&](auto optional_node) -> bool {
    if (optional_node) {
      std::forward<F&&>(f)(*optional_node);
    }
  };

  const auto kind = tree.node_kind(node_id);

  (optional_apply(tree.TryAs<Ts>()) || ...);
}

// Tries to get the rhs of a MemberAccessExpr / PointerMemberAccessExpr.
static auto TryGetMemberAccess(const Parse::TreeAndSubtrees& tree_and_subtrees,
                               Parse::NodeId node) -> Parse::NodeId {
  if (auto expr = tree_and_subtrees.ExtractAs<Parse::MemberAccessExpr>(node)) {
    return expr->rhs;
  }
  if (auto expr =
          tree_and_subtrees.ExtractAs<Parse::PointerMemberAccessExpr>(node)) {
    return expr->rhs;
  }
  return Parse::NodeId::None;
}

// Not all AST nodes are pointed to by an Inst, ascend the tree and select
// likely candidate.
static auto PickNodeWithLikelyInst(
    const Parse::TreeAndSubtrees& tree_and_subtrees,
    const NodeWithPath& node_with_path) -> Parse::NodeId {
  const auto& [node, path] = node_with_path;
  if (!path.empty()) {
    auto parent_node = node_with_path.path.back();
    if (TryGetMemberAccess(tree_and_subtrees, parent_node) == node) {
      // node is the rhs of a MemberAccessExpr, parent_node should have an inst
      // that points to members definition.
      return parent_node;
    }
  }
  return node;
}

static auto FindInstWithLoc(const SemIR::File& sem_ir, Parse::NodeId node)
    -> SemIR::InstId {
  // TODO: use path to give hint of where to look.
  for (auto [i, inst] : llvm::enumerate(sem_ir.insts().array_ref())) {
    SemIR::InstId inst_id(i);
    if (auto loc = sem_ir.insts().GetLocId(inst_id);
        loc.is_node_id() && loc.node_id() == node) {
      return inst_id;
    }
  }
  return SemIR::InstId::None;
}

// Returns the token of first child of kind IdentifierNameBeforeParams or
// IdentifierNameNotBeforeParams.
static auto GetSymbolIdentifier(const Parse::TreeAndSubtrees& tree_and_subtrees,
                                Parse::NodeId node)
    -> std::optional<Lex::TokenIndex> {
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
  const auto& tree = tree_and_subtrees.tree();
  const auto& tokens = tree.tokens();
  const auto& sem_ir = file->sem_ir();
  // InstIndex index(sem_ir);

  if (auto node_with_path =
          GetSelectedAstNodes(tree_and_subtrees, params.position)) {
    const auto& [node, path] = *node_with_path;

    std::vector<llvm::json::Value> path_kinds;
    for (auto node : path) {
      path_kinds.emplace_back(tree.node_kind(node).name());
    }

    auto token = tree.node_token(node);

    llvm::json::Object debug_info({{"tokenKind", tokens.GetKind(token).name()},
                                   {"text", tokens.GetTokenText(token)},
                                   {"nodeKind", tree.node_kind(node).name()},
                                   {"path", path_kinds}});

    auto search_node = node;
    if (!path.empty() &&
        tree.node_kind(path.back()) == Parse::NodeKind::MemberAccessExpr) {
      auto parent_id = path.back();
    }
    auto inst_id = FindInst(sem_ir, search_node);
    if (inst_id.has_value()) {
      auto inst = sem_ir.insts().Get(inst_id);
      std::string inst_text;
      llvm::raw_string_ostream ss(inst_text);
      inst.Print(ss);
      debug_info.try_emplace("inst", PrintToString(inst));

      if (auto name_ref = sem_ir.insts().TryGetAs<SemIR::NameRef>(inst_id)) {
        auto value_id = name_ref->value_id;
        auto loc = sem_ir.insts().GetLocId(value_id);
        if (loc.is_node_id()) {
          auto identifier =
              GetSymbolIdentifier(tree_and_subtrees, loc.node_id())
                  .value_or(tree.node_token(loc.node_id()));
          std::cerr << tree.node_kind(node) << " substituted with "
                    << tree.tokens().GetTokenText(token) << "\n";

          clang::clangd::Location location{
              .uri = params.textDocument.uri,
              .range = GetTokenRange(tokens, identifier, identifier)};
          llvm::raw_os_ostream(std::cerr) << location << "\n";
          on_done(std::move(location));
          return;
        }
      }
    }

    if (context.vlog_stream()) {
      llvm::json::Value value = std::move(debug_info);
      *context.vlog_stream() << value;
    }
  }

  on_done(nullptr);
}

}  // namespace Carbon::LanguageServer
