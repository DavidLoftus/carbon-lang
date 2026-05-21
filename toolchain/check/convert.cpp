// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "toolchain/check/convert.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "common/check.h"
#include "common/error.h"
#include "common/map.h"
#include "llvm/ADT/STLExtras.h"
#include "toolchain/base/kind_switch.h"
#include "toolchain/check/action.h"
#include "toolchain/check/context.h"
#include "toolchain/check/control_flow.h"
#include "toolchain/check/core_identifier.h"
#include "toolchain/check/diagnostic_helpers.h"
#include "toolchain/check/eval.h"
#include "toolchain/check/impl_lookup.h"
#include "toolchain/check/import_ref.h"
#include "toolchain/check/inst.h"
#include "toolchain/check/member_access.h"
#include "toolchain/check/operator.h"
#include "toolchain/check/pattern_match.h"
#include "toolchain/check/pending_block.h"
#include "toolchain/check/type.h"
#include "toolchain/check/type_completion.h"
#include "toolchain/diagnostics/emitter.h"
#include "toolchain/diagnostics/format_providers.h"
#include "toolchain/sem_ir/copy_on_write_block.h"
#include "toolchain/sem_ir/expr_info.h"
#include "toolchain/sem_ir/file.h"
#include "toolchain/sem_ir/generic.h"
#include "toolchain/sem_ir/ids.h"
#include "toolchain/sem_ir/inst.h"
#include "toolchain/sem_ir/inst_kind.h"
#include "toolchain/sem_ir/type.h"
#include "toolchain/sem_ir/type_info.h"
#include "toolchain/sem_ir/typed_insts.h"

// TODO: This contains a lot of recursion. Consider removing it in order to
// prevent accidents.
// NOLINTBEGIN(misc-no-recursion)

namespace Carbon::Check {

// If the initializing expression `init_id` has a storage argument that refers
// to a temporary, overwrites it with the inst at `target.storage_id`, and
// returns the ID that should now be used to refer to `init_id`'s storage. Has
// no effect and returns `target.storage_id` unchanged if `target.storage_id` is
// None, if `init_id` doesn't have a storage arg, or if the storage argument
// doesn't point to a temporary. In the latter case, we assume it was set
// correctly when the instruction was created.
static auto OverwriteTemporaryStorageArg(SemIR::File& sem_ir,
                                         SemIR::InstId init_id,
                                         const ConversionTarget& target)
    -> SemIR::InstId {
  CARBON_CHECK(target.is_initializer());
  if (!target.storage_id.has_value()) {
    return SemIR::InstId::None;
  }
  auto storage_arg_id = FindStorageArgForInitializer(sem_ir, init_id);
  if (!storage_arg_id.has_value() || storage_arg_id == target.storage_id ||
      !sem_ir.insts().Is<SemIR::TemporaryStorage>(storage_arg_id)) {
    return target.storage_id;
  }
  // Replace the temporary in the storage argument with a reference to our
  // target.
  return target.storage_access_block->MergeReplacing(storage_arg_id,
                                                     target.storage_id);
}

// Materializes and returns a temporary initialized from the initializer
// `init_id`. If `init_id` has a storage arg, it must be a `TemporaryStorage`;
// if not, this function allocates one for it.
static auto MaterializeTemporary(Context& context, SemIR::InstId init_id)
    -> SemIR::InstId {
  auto& sem_ir = context.sem_ir();
  auto category = SemIR::GetExprCategory(sem_ir, init_id);
  CARBON_CHECK(SemIR::IsInitializerCategory(category));
  auto init = sem_ir.insts().Get(init_id);
  auto storage_id = FindStorageArgForInitializer(sem_ir, init_id);
  if (!storage_id.has_value()) {
    CARBON_CHECK(category == SemIR::ExprCategory::ReprInitializing);
    // The initializer has no storage arg, but we want to produce an ephemeral
    // reference, so we need to allocate temporary storage.
    storage_id = AddInst<SemIR::TemporaryStorage>(
        context, SemIR::LocId(init_id), {.type_id = init.type_id()});
  }

  CARBON_CHECK(
      sem_ir.insts().Get(storage_id).kind() == SemIR::TemporaryStorage::Kind,
      "Storage arg for initializer does not contain a temporary; "
      "initialized multiple times? Have {0}",
      sem_ir.insts().Get(storage_id));
  return AddInstWithCleanup<SemIR::Temporary>(context, SemIR::LocId(init_id),
                                              {.type_id = init.type_id(),
                                               .storage_id = storage_id,
                                               .init_id = init_id});
}

// Discards the initializer `init_id`. If `init_id` intrinsically writes to
// memory, this materializes a temporary for it and starts its lifetime.
//
// TODO: We should probably start its lifetime unconditionally, because
// types with by-copy representations can still have nontrivial destructors.
static auto DiscardInitializer(Context& context, SemIR::InstId init_id)
    -> void {
  auto& sem_ir = context.sem_ir();
  auto storage_id = FindStorageArgForInitializer(sem_ir, init_id);
  if (!storage_id.has_value()) {
    CARBON_CHECK(SemIR::GetExprCategory(sem_ir, init_id) ==
                 SemIR::ExprCategory::ReprInitializing);
    return;
  }

  // init_id writes to temporary storage, so we need to materialize a temporary
  // for it.
  MaterializeTemporary(context, init_id);
}

// If `expr_id` is an initializer, materializes it and returns the resulting
// ephemeral reference expression. Otherwise, returns `expr_id`.
static auto MaterializeIfInitializer(Context& context, SemIR::InstId expr_id)
    -> SemIR::InstId {
  if (SemIR::IsInitializerCategory(
          SemIR::GetExprCategory(context.sem_ir(), expr_id))) {
    return MaterializeTemporary(context, expr_id);
  } else {
    return expr_id;
  }
}

// Helper to allow `MakeElementAccessInst` to call `AddInst` with either a
// `PendingBlock` or `Context` (defined in `inst.h`).
template <typename AccessInstT>
static auto AddInst(PendingBlock& block, SemIR::LocId loc_id, AccessInstT inst)
    -> SemIR::InstId {
  return block.AddInst<AccessInstT>(loc_id, inst);
}

// Creates and adds an instruction to perform element access into an aggregate.
template <typename AccessInstT, typename InstBlockT>
static auto MakeElementAccessInst(Context& context, SemIR::LocId loc_id,
                                  SemIR::InstId aggregate_id,
                                  SemIR::TypeId elem_type_id, InstBlockT& block,
                                  size_t i) -> SemIR::InstId {
  if (!aggregate_id.has_value()) {
    return SemIR::InstId::None;
  }
  if constexpr (std::is_same_v<AccessInstT, SemIR::ArrayIndex>) {
    // TODO: Add a new instruction kind for indexing an array at a constant
    // index so that we don't need an integer literal instruction here, and
    // remove this special case.
    auto index_id = AddInst<SemIR::IntValue>(
        block, loc_id,
        {.type_id =
             GetSingletonType(context, SemIR::IntLiteralType::TypeInstId),
         .int_id = context.ints().Add(static_cast<int64_t>(i))});
    return AddInst<AccessInstT>(block, loc_id,
                                {elem_type_id, aggregate_id, index_id});
  } else {
    return AddInst<AccessInstT>(
        block, loc_id, {elem_type_id, aggregate_id, SemIR::ElementIndex(i)});
  }
}

// Get the conversion target kind to use when initializing an element of an
// aggregate.
static auto GetAggregateElementConversionTargetKind(SemIR::File& sem_ir,
                                                    ConversionTarget target)
    -> ConversionTarget::Kind {
  // If we're forming an initializer, then we want an initializer for each
  // element.
  if (target.is_initializer()) {
    // Perform a final destination store if we're performing an in-place
    // initialization.
    auto init_repr = SemIR::InitRepr::ForType(sem_ir, target.type_id);
    CARBON_CHECK(init_repr.kind != SemIR::InitRepr::Dependent,
                 "Aggregate should not have dependent init kind");
    if (init_repr.kind == SemIR::InitRepr::InPlace) {
      return ConversionTarget::InPlaceInitializing;
    }
    return ConversionTarget::Initializing;
  }

  // Otherwise, we want a value representation for each element.
  return ConversionTarget::Value;
}

enum class AccessKind { Tuple, Struct, Class, Array };

enum class BuildTargetKind {
  TupleToTuple,
  StructToStruct,
  StructToClass,
  TupleToArray,
  FoundationAdapter,
  MixedLiteral,
};

struct ConvertWorkItem {
  SemIR::LocId loc_id;
  SemIR::InstId value_id;
  ConversionTarget target;
  std::optional<int> output_block_index;
  bool is_base_field = false;
  SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None;
  bool builtin_only = false;
};

struct BuildTargetWorkItem {
  SemIR::LocId top_loc_id;
  SemIR::LocId value_loc_id;
  BuildTargetKind build_kind;
  ConversionTarget target;
  ConversionTarget orig_target;
  std::unique_ptr<PendingBlock> owned_storage_access_block;
  std::optional<int> output_block_index;
  bool is_partial = false;
  std::optional<SemIR::ClassType> vtable_class_type = std::nullopt;
  SemIR::InstId orig_value_id = SemIR::InstId::None;
  std::optional<SemIR::TypeId> orig_value_type_id = std::nullopt;
  bool builtin_only = false;
  std::shared_ptr<void> annotation_scope = nullptr;
  bool is_base_field = false;
  SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None;
};

struct StartElementWorkItem {
  SemIR::LocId loc_id;
  SemIR::InstId src_id;
  SemIR::TypeInstId src_elem_type_inst;
  SemIR::InstBlockId src_literal_elems_id;
  ConversionTarget::Kind kind;
  SemIR::InstId target_id;
  SemIR::TypeInstId target_elem_type_inst;
  PendingBlock* target_block;
  size_t src_field_index;
  size_t target_field_index;
  AccessKind src_access_kind;
  AccessKind target_access_kind;
  int output_block_index;
  bool is_base_field;
  SemIR::TypeInstId orig_dest_elem_type_inst = SemIR::TypeInstId::None;
};

struct FinishElementWorkItem {
  PendingBlock* target_block;
  size_t size;
  size_t cleanups_size;
};

struct InitVptrWorkItem {
  SemIR::LocId value_loc_id;
  SemIR::TypeInstId vptr_type_inst_id;
  ConversionTarget target;
  std::optional<SemIR::ClassType> vtable_class_type = std::nullopt;
  int output_block_index;
};

using WorkItem =
    std::variant<ConvertWorkItem, BuildTargetWorkItem, StartElementWorkItem,
                 FinishElementWorkItem, InitVptrWorkItem>;

class ConversionWorklist {
 public:
  explicit ConversionWorklist(Context& context) : context_(context) {}

  auto Run(SemIR::LocId loc_id, SemIR::InstId expr_id, ConversionTarget target)
      -> SemIR::InstId {
    worklist_.push_back(ConvertWorkItem{
        .loc_id = loc_id,
        .value_id = expr_id,
        .target = target,
        .output_block_index = std::nullopt,
    });
    Loop();
    CARBON_CHECK(results_.size() == 1, "Expected 1 result, got {0}",
                 results_.size());
    return results_[0];
  }

 private:
  auto StoreResult(std::optional<int> output_block_index,
                   SemIR::InstId result_id) -> void {
    if (output_block_index.has_value()) {
      new_block_stack_.back().Set(*output_block_index, result_id);
    } else {
      results_.push_back(result_id);
    }
  }

  template <typename InstBlockT>
  auto EmitElementAccessInst(AccessKind access_kind, SemIR::LocId loc_id,
                             SemIR::InstId aggregate_id,
                             SemIR::TypeId elem_type_id, InstBlockT& block,
                             size_t i) -> SemIR::InstId {
    switch (access_kind) {
      case AccessKind::Tuple:
        return MakeElementAccessInst<SemIR::TupleAccess>(
            context_, loc_id, aggregate_id, elem_type_id, block, i);
      case AccessKind::Struct:
        return MakeElementAccessInst<SemIR::StructAccess>(
            context_, loc_id, aggregate_id, elem_type_id, block, i);
      case AccessKind::Class:
        return MakeElementAccessInst<SemIR::ClassElementAccess>(
            context_, loc_id, aggregate_id, elem_type_id, block, i);
      case AccessKind::Array:
        return MakeElementAccessInst<SemIR::ArrayIndex>(
            context_, loc_id, aggregate_id, elem_type_id, block, i);
    }
  }

  auto FinishConvert(
      SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId expr_id,
      ConversionTarget target, std::optional<int> output_block_index,
      bool is_base_field = false,
      SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None,
      bool builtin_only = false) -> void;
  auto Convert(
      SemIR::LocId loc_id, SemIR::InstId expr_id, ConversionTarget target,
      std::optional<int> output_block_index = std::nullopt,
      bool is_base_field = false,
      SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None,
      bool builtin_only = false) -> void;
  auto PerformBuiltinConversion(
      SemIR::LocId loc_id, SemIR::InstId value_id, ConversionTarget target,
      std::optional<int> output_block_index = std::nullopt,
      bool is_base_field = false,
      SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None,
      bool builtin_only = false,
      SemIR::InstId orig_expr_id = SemIR::InstId::None) -> void;
  auto ConvertTupleToTuple(SemIR::LocId loc_id, SemIR::InstId orig_expr_id,
                           SemIR::InstId value_id, SemIR::TupleType tuple_type,
                           SemIR::TupleType target_tuple_type,
                           ConversionTarget target,
                           std::optional<int> output_block_index,
                           bool builtin_only) -> void;
  auto ConvertTupleToArray(SemIR::LocId loc_id, SemIR::InstId orig_expr_id,
                           SemIR::InstId value_id, SemIR::TupleType tuple_type,
                           SemIR::ArrayType array_type, ConversionTarget target,
                           std::optional<int> output_block_index,
                           bool builtin_only) -> void;
  auto ConvertStructToStruct(
      SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
      SemIR::StructType src_struct_type, SemIR::StructType dest_struct_type,
      ConversionTarget target, std::optional<int> output_block_index,
      bool builtin_only = false, bool is_base_field = false,
      SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None)
      -> void;
  auto ConvertStructToClass(
      SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
      SemIR::StructType src_struct_type, SemIR::ClassType dest_type,
      ConversionTarget target, bool is_partial,
      std::optional<int> output_block_index, bool builtin_only = false,
      bool is_base_field = false,
      SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None)
      -> void;
  template <typename TargetAccessInstT>
  auto ConvertStructToStructOrClass(
      SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
      SemIR::StructType src_type, SemIR::StructType dest_type,
      ConversionTarget target, bool is_partial,
      std::optional<int> output_block_index,
      std::unique_ptr<PendingBlock> owned_block = nullptr,
      std::optional<SemIR::ClassType> vtable_class_type = std::nullopt,
      std::optional<ConversionTarget> orig_target = std::nullopt,
      bool builtin_only = false, bool is_base_field = false,
      SemIR::TypeInstId target_elem_type_inst = SemIR::TypeInstId::None)
      -> void;

  auto ProcessWorkItem(ConvertWorkItem item) -> void;
  auto ProcessWorkItem(StartElementWorkItem item) -> void;
  auto ProcessWorkItem(FinishElementWorkItem item) -> void;
  auto ProcessWorkItem(InitVptrWorkItem item) -> void;
  auto ProcessWorkItem(BuildTargetWorkItem item) -> void;
  auto Loop() -> void;

  Context& context_;
  llvm::SmallVector<WorkItem> worklist_;
  llvm::SmallVector<SemIR::InstId> results_;
  llvm::SmallVector<SemIR::CopyOnWriteInstBlock> new_block_stack_;
};

// Performs a conversion from a tuple to an array type. This function only
// converts the type, and does not perform a final conversion to the requested
// expression category.
auto ConversionWorklist::ConvertTupleToArray(
    SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
    SemIR::TupleType tuple_type, SemIR::ArrayType array_type,
    ConversionTarget target, std::optional<int> output_block_index,
    bool builtin_only) -> void {
  auto& sem_ir = context_.sem_ir();
  auto src_elem_types = sem_ir.inst_blocks().Get(tuple_type.type_elements_id);
  auto value = sem_ir.insts().Get(value_id);

  // If we're initializing from a tuple literal, we will use its elements
  // directly. Otherwise, materialize a temporary if needed and index into the
  // result.
  SemIR::InstBlockId literal_elems_id = SemIR::InstBlockId::None;
  if (auto tuple_literal = value.TryAs<SemIR::TupleLiteral>()) {
    literal_elems_id = tuple_literal->elements_id;
  } else {
    value_id = MaterializeIfInitializer(context_, value_id);
  }

  // Check that the tuple is the right size.
  std::optional<uint64_t> array_bound =
      sem_ir.GetZExtIntValue(array_type.bound_id);
  if (!array_bound) {
    // TODO: Should this fall back to using `ImplicitAs`?
    if (target.diagnose) {
      CARBON_DIAGNOSTIC(ArrayInitDependentBound, Error,
                        "cannot initialize array with dependent bound from a "
                        "list of initializers");
      context_.emitter().Emit(SemIR::LocId(value_id), ArrayInitDependentBound);
    }
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }

  if (src_elem_types.size() != array_bound) {
    if (target.diagnose) {
      CARBON_DIAGNOSTIC(ArrayInitFromLiteralArgCountMismatch, Error,
                        "cannot initialize array of {0} element{0:s} from {1} "
                        "initializer{1:s}",
                        Diagnostics::IntAsSelect, Diagnostics::IntAsSelect);
      CARBON_DIAGNOSTIC(ArrayInitFromExprArgCountMismatch, Error,
                        "cannot initialize array of {0} element{0:s} from "
                        "tuple with {1} element{1:s}",
                        Diagnostics::IntAsSelect, Diagnostics::IntAsSelect);
      context_.emitter().Emit(SemIR::LocId(value_id),
                              literal_elems_id.has_value()
                                  ? ArrayInitFromLiteralArgCountMismatch
                                  : ArrayInitFromExprArgCountMismatch,
                              *array_bound, src_elem_types.size());
    }
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }

  std::unique_ptr<PendingBlock> owned_block;
  PendingBlock* target_block = target.storage_access_block;
  if (!target_block) {
    owned_block = std::make_unique<PendingBlock>(&context_);
    target_block = owned_block.get();
  }

  // Arrays are always initialized in-place. Allocate a temporary as the
  // destination for the array initialization if we weren't given one.
  SemIR::InstId return_slot_arg_id = target.storage_id;
  if (!target.storage_id.has_value()) {
    return_slot_arg_id = target_block->AddInst<SemIR::TemporaryStorage>(
        SemIR::LocId(value_id), {.type_id = target.type_id});
  }

  auto new_block = SemIR::CopyOnWriteInstBlock(
      &sem_ir,
      SemIR::CopyOnWriteInstBlock::UninitializedBlock{src_elem_types.size()});
  new_block_stack_.push_back(std::move(new_block));

  ConversionTarget aggregate_target = target;
  aggregate_target.storage_id = return_slot_arg_id;
  aggregate_target.storage_access_block = target_block;

  auto value_loc_id = SemIR::LocId(value_id);
  worklist_.push_back(BuildTargetWorkItem{
      .top_loc_id = loc_id,
      .value_loc_id = value_loc_id,
      .build_kind = BuildTargetKind::TupleToArray,
      .target = aggregate_target,
      .orig_target = target,
      .owned_storage_access_block = std::move(owned_block),
      .output_block_index = output_block_index,
      .orig_value_id = orig_expr_id,
      .builtin_only = builtin_only,
  });

  // Initialize each element of the array from the corresponding element of the
  // tuple.
  // TODO: Annotate diagnostics coming from here with the array element index,
  // if initializing from a tuple literal.
  ConversionTarget::Kind inner_kind = ConversionTarget::InPlaceInitializing;
  for (int i = src_elem_types.size() - 1; i >= 0; --i) {
    auto src_elem_type = src_elem_types[i];
    worklist_.push_back(StartElementWorkItem{
        .loc_id = value_loc_id,
        .src_id = value_id,
        .src_elem_type_inst = context_.types().GetAsTypeInstId(src_elem_type),
        .src_literal_elems_id = literal_elems_id,
        .kind = inner_kind,
        .target_id = return_slot_arg_id,
        .target_elem_type_inst = array_type.element_type_inst_id,
        .target_block = target_block,
        .src_field_index = static_cast<size_t>(i),
        .target_field_index = static_cast<size_t>(i),
        .src_access_kind = AccessKind::Tuple,
        .target_access_kind = AccessKind::Array,
        .output_block_index = i,
        .is_base_field = false,
        .orig_dest_elem_type_inst = array_type.element_type_inst_id,
    });
  }
}

// Performs a conversion from a tuple to a tuple type. This function only
// converts the type, and does not perform a final conversion to the requested
// expression category.
auto ConversionWorklist::ConvertTupleToTuple(
    SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
    SemIR::TupleType tuple_type, SemIR::TupleType target_tuple_type,
    ConversionTarget target, std::optional<int> output_block_index,
    bool builtin_only) -> void {
  auto& sem_ir = context_.sem_ir();
  auto src_elem_types = sem_ir.inst_blocks().Get(tuple_type.type_elements_id);
  auto dest_elem_types =
      sem_ir.inst_blocks().Get(target_tuple_type.type_elements_id);

  auto value = sem_ir.insts().Get(value_id);
  SemIR::LocId value_loc_id(value_id);

  // If we're initializing from a tuple literal, we will use its elements
  // directly. Otherwise, materialize a temporary if needed and index into the
  // result.
  auto literal_elems_id = SemIR::InstBlockId::None;
  if (auto tuple_literal = value.TryAs<SemIR::TupleLiteral>()) {
    literal_elems_id = tuple_literal->elements_id;
  } else {
    value_id = MaterializeIfInitializer(context_, value_id);
  }

  // Check that the tuples are the same size.
  if (src_elem_types.size() != dest_elem_types.size()) {
    if (target.diagnose) {
      CARBON_DIAGNOSTIC(
          TupleInitElementCountMismatch, Error,
          "cannot initialize tuple of {0} element{0:s} from tuple "
          "with {1} element{1:s}",
          Diagnostics::IntAsSelect, Diagnostics::IntAsSelect);
      context_.emitter().Emit(SemIR::LocId(value_id),
                              TupleInitElementCountMismatch,
                              dest_elem_types.size(), src_elem_types.size());
    }
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }

  auto new_block =
      literal_elems_id.has_value()
          ? SemIR::CopyOnWriteInstBlock(&sem_ir, literal_elems_id)
          : SemIR::CopyOnWriteInstBlock(
                &sem_ir, SemIR::CopyOnWriteInstBlock::UninitializedBlock{
                             src_elem_types.size()});
  new_block_stack_.push_back(std::move(new_block));

  worklist_.push_back(BuildTargetWorkItem{
      .top_loc_id = loc_id,
      .value_loc_id = value_loc_id,
      .build_kind = BuildTargetKind::TupleToTuple,
      .target = target,
      .orig_target = target,
      .output_block_index = output_block_index,
      .orig_value_id = orig_expr_id,
      .builtin_only = builtin_only,
  });

  // Initialize each element of the destination from the corresponding element
  // of the source.
  // TODO: Annotate diagnostics coming from here with the element index.
  ConversionTarget::Kind inner_kind =
      GetAggregateElementConversionTargetKind(sem_ir, target);
  for (int i = src_elem_types.size() - 1; i >= 0; --i) {
    auto src_elem_type = src_elem_types[i];
    auto dest_elem_type = dest_elem_types[i];
    worklist_.push_back(StartElementWorkItem{
        .loc_id = value_loc_id,
        .src_id = value_id,
        .src_elem_type_inst = context_.types().GetAsTypeInstId(src_elem_type),
        .src_literal_elems_id = literal_elems_id,
        .kind = inner_kind,
        .target_id = target.storage_id,
        .target_elem_type_inst =
            context_.types().GetAsTypeInstId(dest_elem_type),
        .target_block = target.storage_access_block,
        .src_field_index = static_cast<size_t>(i),
        .target_field_index = static_cast<size_t>(i),
        .src_access_kind = AccessKind::Tuple,
        .target_access_kind = AccessKind::Tuple,
        .output_block_index = static_cast<int>(i),
        .is_base_field = false,
        .orig_dest_elem_type_inst =
            context_.types().GetAsTypeInstId(dest_elem_type),
    });
  }
}

// Converts a tuple of elements that are convertible to `type` into a `type`
// that is a tuple of types.
static auto ConvertTupleToType(Context& context, SemIR::LocId loc_id,
                               SemIR::InstId value_id,
                               SemIR::TypeId value_type_id,
                               ConversionTarget target) -> SemIR::TypeInstId {
  auto value_const_id = context.constant_values().Get(value_id);
  if (!value_const_id.is_constant()) {
    // Types are constants. The input value must have a constant value to
    // convert.
    return SemIR::TypeInstId::None;
  }

  llvm::SmallVector<SemIR::InstId> type_inst_ids;

  if (auto tuple_value =
          context.constant_values().TryGetInstAs<SemIR::TupleValue>(
              value_const_id)) {
    for (auto tuple_inst_id :
         context.inst_blocks().Get(tuple_value->elements_id)) {
      // TODO: This call recurses back into conversion. Switch to an
      // iterative approach.
      type_inst_ids.push_back(
          ExprAsType(context, loc_id, tuple_inst_id, target.diagnose).inst_id);
    }
  } else {
    // A value of type TupleType that isn't a TupleValue must be a symbolic
    // binding.
    CARBON_CHECK(context.constant_values().InstIs<SemIR::SymbolicBinding>(
        value_const_id));
    // Form a TupleAccess for each element in the symbolic value, which is then
    // converted to a `type` or diagnosed as an error.
    auto tuple_type = context.types().GetAs<SemIR::TupleType>(value_type_id);
    auto type_elements = context.types().GetBlockAsTypeIds(
        context.inst_blocks().Get(tuple_type.type_elements_id));
    for (auto [i, type_id] : llvm::enumerate(type_elements)) {
      auto access_inst_id =
          GetOrAddInst<SemIR::TupleAccess>(context, loc_id,
                                           {.type_id = type_id,
                                            .tuple_id = value_id,
                                            .index = SemIR::ElementIndex(i)});
      // TODO: This call recurses back into conversion. Switch to an
      // iterative approach.
      type_inst_ids.push_back(
          ExprAsType(context, loc_id, access_inst_id, target.diagnose).inst_id);
    }
  }

  // TODO: Should we add this as an instruction? It will contain
  // references to local InstIds.
  auto tuple_type_id = GetTupleType(context, type_inst_ids);
  return context.types().GetTypeInstId(tuple_type_id);
}

// Create a reference to the vtable pointer for a class. Returns None if the
// class has no vptr.
static auto CreateVtablePtrRef(Context& context, SemIR::LocId loc_id,
                               SemIR::ClassType vtable_class_type)
    -> SemIR::InstId {
  auto vtable_decl_id =
      context.classes().Get(vtable_class_type.class_id).vtable_decl_id;
  if (!vtable_decl_id.has_value()) {
    return SemIR::InstId::None;
  }

  LoadImportRef(context, vtable_decl_id);
  auto canonical_vtable_decl_id =
      context.constant_values().GetConstantInstId(vtable_decl_id);
  return AddInst<SemIR::VtablePtr>(
      context, loc_id,
      {.type_id = GetPointerType(context, SemIR::VtableType::TypeInstId),
       .vtable_id = context.insts()
                        .GetAs<SemIR::VtableDecl>(canonical_vtable_decl_id)
                        .vtable_id,
       .specific_id = vtable_class_type.specific_id});
}

// Returns whether the given expression performs in-place initialization (or is
// invalid). The category can be passed if known, otherwise it will be computed.
static auto IsInPlaceInitializing(Context& context, SemIR::InstId result_id,
                                  SemIR::ExprCategory category) {
  return category == SemIR::ExprCategory::InPlaceInitializing ||
         (category == SemIR::ExprCategory::ReprInitializing &&
          SemIR::InitRepr::ForType(context.sem_ir(),
                                   context.insts().Get(result_id).type_id())
                  .kind == SemIR::InitRepr::InPlace) ||
         category == SemIR::ExprCategory::Error;
}
static auto IsInPlaceInitializing(Context& context, SemIR::InstId result_id) {
  auto category = SemIR::GetExprCategory(context.sem_ir(), result_id);
  return IsInPlaceInitializing(context, result_id, category);
}

// Returns the index of the vptr field in the given struct type fields, or
// None if there is no vptr field.
static auto GetVptrFieldIndex(llvm::ArrayRef<SemIR::StructTypeField> fields)
    -> SemIR::ElementIndex {
  // If the type introduces a vptr, it will always be the first field.
  bool has_vptr =
      !fields.empty() && fields.front().name_id == SemIR::NameId::Vptr;
  return has_vptr ? SemIR::ElementIndex(0) : SemIR::ElementIndex::None;
}

// Builds a member access expression naming the vptr field of the given class
// object. This is analogous to what `PerformMemberAccess` for `NameId::Vptr`
// would return if the vptr could be found by name lookup.
static auto PerformVptrAccess(Context& context, SemIR::LocId loc_id,
                              SemIR::InstId class_ref_id) -> SemIR::InstId {
  auto class_type_id = context.insts().Get(class_ref_id).type_id();
  while (class_ref_id.has_value()) {
    // The type of `ref_id` must be a class type.
    if (class_type_id == SemIR::ErrorInst::TypeId) {
      return SemIR::ErrorInst::InstId;
    }
    auto class_type = context.types().GetAs<SemIR::ClassType>(class_type_id);
    auto& class_info = context.classes().Get(class_type.class_id);

    // Get the object representation.
    auto object_repr_id =
        class_info.GetObjectRepr(context.sem_ir(), class_type.specific_id);
    if (object_repr_id == SemIR::ErrorInst::TypeId) {
      return SemIR::ErrorInst::InstId;
    }
    if (context.types().Is<SemIR::CustomLayoutType>(object_repr_id)) {
      context.TODO(loc_id, "accessing vptr of custom layout class");
      return SemIR::ErrorInst::InstId;
    }

    // Check to see if this class introduces the vptr.
    auto repr_struct_type =
        context.types().GetAs<SemIR::StructType>(object_repr_id);
    auto repr_fields =
        context.struct_type_fields().Get(repr_struct_type.fields_id);
    if (auto vptr_field_index = GetVptrFieldIndex(repr_fields);
        vptr_field_index.has_value()) {
      return AddInst<SemIR::ClassElementAccess>(
          context, loc_id,
          {.type_id = context.types().GetTypeIdForTypeInstId(
               repr_fields[vptr_field_index.index].type_inst_id),
           .base_id = class_ref_id,
           .index = vptr_field_index});
    }

    // Otherwise, step through to the base class and try again.
    CARBON_CHECK(class_info.base_id.has_value(),
                 "Could not find vptr for dynamic class");
    auto base_decl = context.insts().GetAs<SemIR::BaseDecl>(class_info.base_id);
    class_type_id = context.types().GetTypeIdForTypeInstId(
        repr_fields[base_decl.index.index].type_inst_id);
    class_ref_id =
        AddInst<SemIR::ClassElementAccess>(context, loc_id,
                                           {.type_id = class_type_id,
                                            .base_id = class_ref_id,
                                            .index = base_decl.index});
  }
  return class_ref_id;
}

// Converts an initializer for a type `partial T` to an initializer for `T` by
// initializing the vptr if necessary.
static auto ConvertPartialInitializerToNonPartial(
    Context& context, ConversionTarget target,
    SemIR::ClassType vtable_class_type, SemIR::InstId result_id)
    -> SemIR::InstId {
  auto loc_id = SemIR::LocId(result_id);
  auto vptr_id = CreateVtablePtrRef(context, loc_id, vtable_class_type);
  if (!vptr_id.has_value()) {
    // No vtable pointer in this class, nothing to do.
    return result_id;
  }

  CARBON_CHECK(
      IsInPlaceInitializing(context, result_id),
      "Type with vptr should have in-place initializing representation");

  target.storage_access_block->InsertHere();
  auto dest_id = PerformVptrAccess(context, loc_id, target.storage_id);
  auto vptr_init_id = AddInst<SemIR::InPlaceInit>(
      context, loc_id,
      {.type_id = context.insts().Get(dest_id).type_id(),
       .src_id = vptr_id,
       .dest_id = dest_id});
  return AddInst<SemIR::UpdateInit>(context, loc_id,
                                    {.type_id = target.type_id,
                                     .base_init_id = result_id,
                                     .update_init_id = vptr_init_id});
}

// Common implementation for ConvertStructToStruct and ConvertStructToClass.
template <typename TargetAccessInstT>
auto ConversionWorklist::ConvertStructToStructOrClass(
    SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
    SemIR::StructType src_type, SemIR::StructType dest_type,
    ConversionTarget target, bool is_partial,
    std::optional<int> output_block_index,
    std::unique_ptr<PendingBlock> owned_block,
    std::optional<SemIR::ClassType> vtable_class_type,
    std::optional<ConversionTarget> orig_target, bool builtin_only,
    bool is_base_field, SemIR::TypeInstId target_elem_type_inst) -> void {
  static_assert(std::is_same_v<SemIR::ClassElementAccess, TargetAccessInstT> ||
                std::is_same_v<SemIR::StructAccess, TargetAccessInstT>);
  constexpr bool ToClass =
      std::is_same_v<SemIR::ClassElementAccess, TargetAccessInstT>;

  auto& sem_ir = context_.sem_ir();
  auto src_elem_fields = sem_ir.struct_type_fields().Get(src_type.fields_id);
  auto dest_elem_fields = sem_ir.struct_type_fields().Get(dest_type.fields_id);
  auto dest_vptr_index = GetVptrFieldIndex(dest_elem_fields);
  auto dest_elem_fields_size =
      dest_elem_fields.size() - (dest_vptr_index.has_value() ? 1 : 0);

  auto value = sem_ir.insts().Get(value_id);
  SemIR::LocId value_loc_id(value_id);

  // If we're initializing from a struct literal, we will use its elements
  // directly. Otherwise, materialize a temporary if needed and index into the
  // result.
  llvm::ArrayRef<SemIR::InstId> literal_elems;
  auto literal_elems_id = SemIR::InstBlockId::None;
  if (auto struct_literal = value.TryAs<SemIR::StructLiteral>()) {
    literal_elems_id = struct_literal->elements_id;
    literal_elems = sem_ir.inst_blocks().Get(literal_elems_id);
  } else {
    value_id = MaterializeIfInitializer(context_, value_id);
  }

  // Check that the structs are the same size.
  // TODO: If not, include the name of the first source field that doesn't
  // exist in the destination or vice versa in the diagnostic.
  if (src_elem_fields.size() != dest_elem_fields_size) {
    if (target.diagnose) {
      CARBON_DIAGNOSTIC(
          StructInitElementCountMismatch, Error,
          "cannot initialize {0:class|struct} with {1} field{1:s} from struct "
          "with {2} field{2:s}",
          Diagnostics::BoolAsSelect, Diagnostics::IntAsSelect,
          Diagnostics::IntAsSelect);
      context_.emitter().Emit(value_loc_id, StructInitElementCountMismatch,
                              ToClass, dest_elem_fields_size,
                              src_elem_fields.size());
    }
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }

  // Prepare to look up fields in the source by index.
  Map<SemIR::NameId, int32_t> src_field_indexes;
  if (src_type.fields_id != dest_type.fields_id) {
    for (auto [i, field] : llvm::enumerate(src_elem_fields)) {
      auto result = src_field_indexes.Insert(field.name_id, i);
      CARBON_CHECK(result.is_inserted(), "Duplicate field in source structure");
    }
  }
  llvm::SmallVector<int32_t> reordered_src_fields;
  for (auto [i, dest_field] : llvm::enumerate(dest_elem_fields)) {
    if (dest_field.name_id == SemIR::NameId::Vptr) {
      reordered_src_fields.push_back(-1);
      continue;
    }
    if (src_type.fields_id == dest_type.fields_id) {
      reordered_src_fields.push_back(i);
      continue;
    }
    if (auto src_field_index = src_field_indexes.Lookup(dest_field.name_id)) {
      reordered_src_fields.push_back(src_field_index.value());
    } else {
      if (target.diagnose) {
        if (literal_elems_id.has_value()) {
          CARBON_DIAGNOSTIC(
              StructInitMissingFieldInLiteral, Error,
              "missing value for field `{0}` in struct initialization",
              SemIR::NameId);
          context_.emitter().Emit(SemIR::LocId(value_id),
                                  StructInitMissingFieldInLiteral,
                                  dest_field.name_id);
        } else {
          CARBON_DIAGNOSTIC(StructInitMissingFieldInConversion, Error,
                            "cannot convert from struct type {0} to {1}: "
                            "missing field `{2}` in source type",
                            TypeOfInstId, SemIR::TypeId, SemIR::NameId);
          context_.emitter().Emit(SemIR::LocId(value_id),
                                  StructInitMissingFieldInConversion, value_id,
                                  target.type_id, dest_field.name_id);
        }
      }
      StoreResult(output_block_index, SemIR::ErrorInst::InstId);
      return;
    }
  }

  ConversionTarget::Kind inner_kind =
      GetAggregateElementConversionTargetKind(sem_ir, target);

  {
    auto new_block =
        literal_elems_id.has_value() && !dest_vptr_index.has_value()
            ? SemIR::CopyOnWriteInstBlock(&sem_ir, literal_elems_id)
            : SemIR::CopyOnWriteInstBlock(
                  &sem_ir, SemIR::CopyOnWriteInstBlock::UninitializedBlock{
                               dest_elem_fields.size()});
    new_block_stack_.push_back(std::move(new_block));
  }
  SemIR::CopyOnWriteInstBlock* new_block = &new_block_stack_.back();

  auto vtable_class_type_for_build = vtable_class_type;
  if (dest_vptr_index.has_value()) {
    vtable_class_type_for_build = std::nullopt;
  }

  worklist_.push_back(BuildTargetWorkItem{
      .top_loc_id = loc_id,
      .value_loc_id = value_loc_id,
      .build_kind = ToClass ? BuildTargetKind::StructToClass
                            : BuildTargetKind::StructToStruct,
      .target = target,
      .orig_target = orig_target.has_value() ? *orig_target : target,
      .owned_storage_access_block = std::move(owned_block),
      .output_block_index = output_block_index,
      .is_partial = is_partial,
      .vtable_class_type = vtable_class_type_for_build,
      .orig_value_id = orig_expr_id,
      .builtin_only = builtin_only,
      .is_base_field = is_base_field,
      .target_elem_type_inst = target_elem_type_inst,
  });

  // Initialize each element of the destination from the corresponding element
  // of the source.
  // TODO: Annotate diagnostics coming from here with the element index.
  for (auto [i, dest_field, src_field_index] : llvm::reverse(
           llvm::zip(llvm::index_range(size_t{0}, dest_elem_fields.size()),
                     dest_elem_fields, reordered_src_fields))) {
    if (dest_field.name_id == SemIR::NameId::Vptr) {
      if constexpr (!ToClass) {
        CARBON_FATAL("Only classes should have vptrs.");
      }
      target.storage_access_block->InsertHere();
      auto vptr_type_id =
          context_.types().GetTypeIdForTypeInstId(dest_field.type_inst_id);
      auto dest_id =
          AddInst<SemIR::ClassElementAccess>(context_, value_loc_id,
                                             {.type_id = vptr_type_id,
                                              .base_id = target.storage_id,
                                              .index = SemIR::ElementIndex(i)});
      auto vtable_ptr_id = SemIR::InstId::None;
      if (vtable_class_type) {
        vtable_ptr_id =
            CreateVtablePtrRef(context_, value_loc_id, *vtable_class_type);
        // Track that we initialized the vptr so we don't do it again.
        vtable_class_type = std::nullopt;
      } else {
        // For a partial class type, we leave the vtable pointer uninitialized.
        // TODO: Consider storing a specified value such as null for hardening.
        vtable_ptr_id = AddInst<SemIR::UninitializedValue>(
            context_, value_loc_id,
            {.type_id =
                 GetPointerType(context_, SemIR::VtableType::TypeInstId)});
      }
      auto init_id = AddInst<SemIR::InPlaceInit>(context_, value_loc_id,
                                                 {.type_id = vptr_type_id,
                                                  .src_id = vtable_ptr_id,
                                                  .dest_id = dest_id});
      new_block->Set(i, init_id);
      continue;
    }

    auto src_field = src_elem_fields[src_field_index];

    // When initializing the `.base` field of a class, the destination type is
    // `partial Base`, not `Base`.
    // TODO: Skip this if the source field is an initializing expression of the
    // non-partial type in order to produce smaller IR.
    auto dest_field_type_inst_id = dest_field.type_inst_id;
    bool is_base = false;
    if (dest_field.name_id == SemIR::NameId::Base) {
      auto partial_type_id = GetQualifiedType(
          context_,
          context_.types().GetTypeIdForTypeInstId(dest_field.type_inst_id),
          SemIR::TypeQualifiers::Partial);
      dest_field_type_inst_id = context_.types().GetTypeInstId(partial_type_id);
      is_base = true;
    }

    auto dest_field_index = src_field_index;
    if (dest_vptr_index.has_value() &&
        static_cast<int32_t>(src_field_index) >= dest_vptr_index.index) {
      dest_field_index += 1;
    }

    worklist_.push_back(StartElementWorkItem{
        .loc_id = value_loc_id,
        .src_id = value_id,
        .src_elem_type_inst = src_field.type_inst_id,
        .src_literal_elems_id = literal_elems_id,
        .kind = inner_kind,
        .target_id = target.storage_id,
        .target_elem_type_inst = dest_field_type_inst_id,
        .target_block = target.storage_access_block,
        .src_field_index = static_cast<size_t>(src_field_index),
        .target_field_index = static_cast<size_t>(dest_field_index),
        .src_access_kind = AccessKind::Struct,
        .target_access_kind = ToClass ? AccessKind::Class : AccessKind::Struct,
        .output_block_index = static_cast<int>(i),
        .is_base_field = is_base,
        .orig_dest_elem_type_inst = dest_field.type_inst_id,
    });
  }
}

// Performs a conversion from a struct to a struct type. This function only
// converts the type, and does not perform a final conversion to the requested
// expression category.
auto ConversionWorklist::ConvertStructToStruct(
    SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
    SemIR::StructType src_type, SemIR::StructType dest_type,
    ConversionTarget target, std::optional<int> output_block_index,
    bool builtin_only, bool is_base_field,
    SemIR::TypeInstId target_elem_type_inst) -> void {
  ConvertStructToStructOrClass<SemIR::StructAccess>(
      loc_id, orig_expr_id, value_id, src_type, dest_type, target, false,
      output_block_index, nullptr, std::nullopt, target, builtin_only,
      is_base_field, target_elem_type_inst);
}

// Performs a conversion from a struct to a class type. This function only
// converts the type, and does not perform a final conversion to the requested
// expression category.
auto ConversionWorklist::ConvertStructToClass(
    SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId value_id,
    SemIR::StructType src_type, SemIR::ClassType dest_type,
    ConversionTarget target, bool is_partial,
    std::optional<int> output_block_index, bool builtin_only,
    bool is_base_field, SemIR::TypeInstId target_elem_type_inst) -> void {
  auto& dest_class_info = context_.classes().Get(dest_type.class_id);
  auto object_repr_id =
      dest_class_info.GetObjectRepr(context_.sem_ir(), dest_type.specific_id);
  if (object_repr_id == SemIR::ErrorInst::TypeId) {
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }
  if (context_.types().Is<SemIR::CustomLayoutType>(object_repr_id)) {
    // Builtin conversion does not apply.
    if (builtin_only) {
      StoreResult(output_block_index, value_id);
    } else {
      FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                    is_base_field, target_elem_type_inst);
    }
    return;
  }
  auto dest_struct_type =
      context_.types().GetAs<SemIR::StructType>(object_repr_id);

  std::unique_ptr<PendingBlock> owned_block;
  ConversionTarget aggregate_target = target;

  // If we're trying to create a class value, form temporary storage to hold the
  // initializer.
  if (!target.is_initializer()) {
    aggregate_target.kind = ConversionTarget::Initializing;
    owned_block = std::make_unique<PendingBlock>(&context_);
    aggregate_target.storage_access_block = owned_block.get();
    aggregate_target.storage_id = owned_block->AddInst<SemIR::TemporaryStorage>(
        SemIR::LocId(value_id), {.type_id = target.type_id});
  }

  ConvertStructToStructOrClass<SemIR::ClassElementAccess>(
      loc_id, orig_expr_id, value_id, src_type, dest_struct_type,
      aggregate_target, is_partial, output_block_index, std::move(owned_block),
      is_partial ? std::nullopt : std::optional<SemIR::ClassType>(dest_type),
      target, builtin_only, is_base_field, target_elem_type_inst);
}

// An inheritance path is a sequence of `BaseDecl`s and corresponding base types
// in order from derived to base.
using InheritancePath =
    llvm::SmallVector<std::pair<SemIR::InstId, SemIR::TypeId>>;

// Computes the inheritance path from class `derived_id` to class `base_id`.
// Returns nullopt if `derived_id` is not a class derived from `base_id`.
static auto ComputeInheritancePath(Context& context, SemIR::LocId loc_id,
                                   SemIR::TypeId derived_id,
                                   SemIR::TypeId base_id)
    -> std::optional<InheritancePath> {
  // We intend for NRVO to be applied to `result`. All `return` statements in
  // this function should `return result;`.
  std::optional<InheritancePath> result(std::in_place);
  if (!TryToCompleteType(context, derived_id, loc_id)) {
    // TODO: Should we give an error here? If we don't, and there is an
    // inheritance path when the class is defined, we may have a coherence
    // problem.
    result = std::nullopt;
    return result;
  }
  while (derived_id != base_id) {
    auto derived_class_type =
        context.types().TryGetAs<SemIR::ClassType>(derived_id);
    if (!derived_class_type) {
      result = std::nullopt;
      break;
    }
    auto& derived_class = context.classes().Get(derived_class_type->class_id);
    auto base_type_id = derived_class.GetBaseType(
        context.sem_ir(), derived_class_type->specific_id);
    if (!base_type_id.has_value()) {
      result = std::nullopt;
      break;
    }
    result->push_back({derived_class.base_id, base_type_id});
    derived_id = base_type_id;
  }
  return result;
}

// Performs a conversion from a derived class value or reference to a base class
// value or reference.
static auto ConvertDerivedToBase(Context& context, SemIR::LocId loc_id,
                                 SemIR::InstId value_id,
                                 const InheritancePath& path) -> SemIR::InstId {
  // Materialize a temporary if necessary.
  value_id = ConvertToValueOrRefExpr(context, value_id);

  // Preserve type qualifiers.
  auto quals = context.types()
                   .GetUnqualifiedTypeAndQualifiers(
                       context.insts().Get(value_id).type_id())
                   .second;

  // Add a series of `.base` accesses.
  for (auto [base_id, base_type_id] : path) {
    auto base_decl = context.insts().GetAs<SemIR::BaseDecl>(base_id);
    value_id = AddInst<SemIR::ClassElementAccess>(
        context, loc_id,
        {.type_id = GetQualifiedType(context, base_type_id, quals),
         .base_id = value_id,
         .index = base_decl.index});
  }
  return value_id;
}

// Performs a conversion from a derived class pointer to a base class pointer.
static auto ConvertDerivedPointerToBasePointer(
    Context& context, SemIR::LocId loc_id, SemIR::PointerType src_ptr_type,
    SemIR::TypeId dest_ptr_type_id, SemIR::InstId ptr_id,
    const InheritancePath& path) -> SemIR::InstId {
  auto pointee_type_id =
      context.types().GetTypeIdForTypeInstId(src_ptr_type.pointee_id);

  // Form `*p`.
  ptr_id = ConvertToValueExpr(context, ptr_id);
  auto ref_id = AddInst<SemIR::Deref>(
      context, loc_id, {.type_id = pointee_type_id, .pointer_id = ptr_id});

  // Convert as a reference expression.
  ref_id = ConvertDerivedToBase(context, loc_id, ref_id, path);

  // Take the address.
  return AddInst<SemIR::AddrOf>(
      context, loc_id, {.type_id = dest_ptr_type_id, .lvalue_id = ref_id});
}

// Returns whether `category` is a valid expression category to produce as a
// result of a conversion with kind `target_kind`.
static auto IsValidExprCategoryForConversionTarget(
    SemIR::ExprCategory category, ConversionTarget::Kind target_kind) -> bool {
  switch (target_kind) {
    case ConversionTarget::Value:
      return category == SemIR::ExprCategory::Value;
    case ConversionTarget::ValueOrRef:
      return category == SemIR::ExprCategory::Value ||
             category == SemIR::ExprCategory::DurableRef ||
             category == SemIR::ExprCategory::EphemeralRef;
    case ConversionTarget::Discarded:
      return category == SemIR::ExprCategory::Value ||
             category == SemIR::ExprCategory::DurableRef ||
             category == SemIR::ExprCategory::EphemeralRef ||
             category == SemIR::ExprCategory::ReprInitializing ||
             category == SemIR::ExprCategory::InPlaceInitializing;
    case ConversionTarget::RefParam:
    case ConversionTarget::UnmarkedRefParam:
      return category == SemIR::ExprCategory::DurableRef ||
             category == SemIR::ExprCategory::EphemeralRef;
    case ConversionTarget::DurableRef:
      return category == SemIR::ExprCategory::DurableRef;
    case ConversionTarget::CppThunkRef:
      return category == SemIR::ExprCategory::EphemeralRef;
    case ConversionTarget::NoOp:
    case ConversionTarget::ExplicitAs:
    case ConversionTarget::ExplicitUnsafeAs:
      return true;
    case ConversionTarget::InPlaceInitializing:
      return category == SemIR::ExprCategory::InPlaceInitializing;
    case ConversionTarget::Initializing:
      return category == SemIR::ExprCategory::ReprInitializing;
  }
}

// Determines whether the initialization representation of the type is a copy of
// the value representation.
static auto InitReprIsCopyOfValueRepr(const SemIR::File& sem_ir,
                                      SemIR::TypeId type_id) -> bool {
  // The initializing representation is a copy of the value representation if
  // they're both copies of the object representation.
  return SemIR::InitRepr::ForType(sem_ir, type_id).IsCopyOfObjectRepr() &&
         SemIR::ValueRepr::ForType(sem_ir, type_id)
             .IsCopyOfObjectRepr(sem_ir, type_id);
}

// Determines whether we can pull a value directly out of an initializing
// expression of type `type_id` to initialize a target of type `type_id` and
// kind `target_kind`.
static auto CanUseValueOfInitializer(const SemIR::File& sem_ir,
                                     SemIR::TypeId type_id,
                                     ConversionTarget::Kind target_kind)
    -> bool {
  if (!IsValidExprCategoryForConversionTarget(SemIR::ExprCategory::Value,
                                              target_kind)) {
    // We don't want a value expression.
    return false;
  }

  // We can pull a value out of an initializing expression if it holds one.
  return InitReprIsCopyOfValueRepr(sem_ir, type_id);
}

// Determine whether the given set of qualifiers can be added by a conversion
// of an expression of the given category.
static auto CanAddQualifiers(SemIR::TypeQualifiers quals,
                             SemIR::ExprCategory cat) -> bool {
  if (quals.HasAnyOf(SemIR::TypeQualifiers::MaybeUnformed) &&
      !SemIR::IsRefCategory(cat)) {
    // `MaybeUnformed(T)` may have a different value representation or
    // initializing representation from `T`, so only allow it to be added for a
    // reference expression.
    // TODO: We should allow converting an initializing expression of type `T`
    // to `MaybeUnformed(T)`. `PerformBuiltinConversion` will need to generate
    // an `InPlaceInit` instruction when needed.
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    return false;
  }

  // `const` and `partial` can always be added.
  return true;
}

// Determine whether the given set of qualifiers can be removed by a conversion
// of an expression of the given category.
static auto CanRemoveQualifiers(SemIR::TypeQualifiers quals,
                                SemIR::ExprCategory cat,
                                ConversionTarget::Kind kind) -> bool {
  bool allow_unsafe = kind == ConversionTarget::ExplicitUnsafeAs;

  if (quals.HasAnyOf(SemIR::TypeQualifiers::Const) && !allow_unsafe &&
      SemIR::IsRefCategory(cat) &&
      IsValidExprCategoryForConversionTarget(cat, kind)) {
    // Removing `const` is an unsafe conversion for a reference expression. But
    // it's OK if we will be converting to a different category as part of this
    // overall conversion anyway.
    return false;
  }

  if (quals.HasAnyOf(SemIR::TypeQualifiers::Partial) && !allow_unsafe &&
      !SemIR::IsInitializerCategory(cat)) {
    // Removing `partial` is an unsafe conversion for a non-initializing
    // expression. But it's OK for an initializing expression because we will
    // initialize the vptr as part of the conversion.
    return false;
  }

  if (quals.HasAnyOf(SemIR::TypeQualifiers::MaybeUnformed) &&
      (!allow_unsafe || SemIR::IsInitializerCategory(cat))) {
    // As an unsafe conversion, `MaybeUnformed` can be removed from a value or
    // reference expression.
    return false;
  }

  return true;
}

static auto DiagnoseConversionFailureToConstraintValue(
    Context& context, SemIR::LocId loc_id, SemIR::InstId expr_id,
    SemIR::TypeId target_type_id) -> void {
  CARBON_CHECK(context.types().IsFacetType(target_type_id));

  // If the source type is/has a facet value (converted with `as type` or
  // otherwise), then we can include its `FacetType` in the diagnostic to help
  // explain what interfaces the source type implements.
  auto const_expr_id = GetCanonicalFacetOrTypeValue(context, expr_id);
  auto const_expr_type_id = context.insts().Get(const_expr_id).type_id();

  if (context.types().Is<SemIR::FacetType>(const_expr_type_id)) {
    CARBON_DIAGNOSTIC(ConversionFailureFacetToFacet, Error,
                      "cannot convert type {0} that implements {1} into type "
                      "implementing {2}",
                      InstIdAsType, SemIR::TypeId, SemIR::TypeId);
    context.emitter().Emit(loc_id, ConversionFailureFacetToFacet, expr_id,
                           const_expr_type_id, target_type_id);
  } else {
    CARBON_DIAGNOSTIC(ConversionFailureTypeToFacet, Error,
                      "cannot convert type {0} into type implementing {1}",
                      InstIdAsType, SemIR::TypeId);
    context.emitter().Emit(loc_id, ConversionFailureTypeToFacet, expr_id,
                           target_type_id);
  }
}

auto ConversionWorklist::PerformBuiltinConversion(
    SemIR::LocId loc_id, SemIR::InstId value_id, ConversionTarget target,
    std::optional<int> output_block_index, bool is_base_field,
    SemIR::TypeInstId target_elem_type_inst, bool builtin_only,
    SemIR::InstId orig_expr_id) -> void {
  auto& sem_ir = context_.sem_ir();
  auto value = sem_ir.insts().Get(value_id);
  auto value_type_id = value.type_id();
  auto target_type_inst = sem_ir.types().GetAsInst(target.type_id);

  // Various forms of implicit conversion are supported as builtin conversions,
  // either in addition to or instead of `impl`s of `ImplicitAs` in the Carbon
  // prelude. There are a few reasons we need to perform some of these
  // conversions as builtins:
  //
  // 1) Conversions from struct and tuple *literals* have special rules that
  //    cannot be implemented by invoking `ImplicitAs`. Specifically, we must
  //    recurse into the elements of the literal before performing
  //    initialization in order to avoid unnecessary conversions between
  //    expression categories that would be performed by `ImplicitAs.Convert`.
  // 2) (Not implemented yet) Conversion of a facet to a facet type depends on
  //    the value of the facet, not only its type, and therefore cannot be
  //    modeled by `ImplicitAs`.
  // 3) Some of these conversions are used while checking the library
  //    definition of `ImplicitAs` itself or implementations of it.
  //
  // We also expect to see better performance by avoiding an `impl` lookup for
  // common conversions.
  //
  // TODO: We should provide a debugging flag to turn off as many of these
  // builtin conversions as we can so that we can test that they do the same
  // thing as the library implementations.
  //
  // The builtin conversions that correspond to `impl`s in the library all
  // correspond to `final impl`s, so we don't need to worry about `ImplicitAs`
  // being specialized in any of these cases.

  // If the value is already of the right kind and expression category, there's
  // nothing to do. Performing a conversion would decompose and rebuild tuples
  // and structs, so it's important that we bail out early in this case.
  if (value_type_id == target.type_id) {
    auto value_cat = SemIR::GetExprCategory(sem_ir, value_id);
    if (IsValidExprCategoryForConversionTarget(value_cat, target.kind)) {
      FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                    is_base_field, target_elem_type_inst, builtin_only);
      return;
    }

    // If the source is an initializing expression, we may be able to pull a
    // value right out of it.
    if (value_cat == SemIR::ExprCategory::ReprInitializing &&
        CanUseValueOfInitializer(sem_ir, value_type_id, target.kind)) {
      auto res = AddInst<SemIR::ValueOfInitializer>(
          context_, loc_id, {.type_id = value_type_id, .init_id = value_id});
      FinishConvert(loc_id, orig_expr_id, res, target, output_block_index,
                    is_base_field, target_elem_type_inst, builtin_only);
      return;
    }

    // Materialization is handled as part of the enclosing conversion.
    if (SemIR::IsInitializerCategory(value_cat) &&
        target.kind == ConversionTarget::ValueOrRef) {
      FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                    is_base_field, target_elem_type_inst, builtin_only);
      return;
    }

    // Final destination store is handled as part of the enclosing conversion.
    if (value_cat == SemIR::ExprCategory::ReprInitializing &&
        target.kind == ConversionTarget::InPlaceInitializing) {
      FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                    is_base_field, target_elem_type_inst, builtin_only);
      return;
    }

    // PerformBuiltinConversion converts each part of a tuple or struct, even
    // when the types are the same. This is not done for classes since they have
    // to define their conversions as part of their api.
    //
    // If a class adapts a tuple or struct, we convert each of its parts when
    // there's no other conversion going on (the source and target types are the
    // same). To do so, we have to insert a conversion of the value up to the
    // foundation and back down, and a conversion of the initializing object if
    // there is one.
    //
    // Implementation note: We do the conversion through a call to
    // PerformBuiltinConversion() call rather than a Convert() call to avoid
    // extraneous `converted` semir instructions on the adapted types, and as a
    // shortcut to doing the explicit calls to walk the parts of the
    // tuple/struct which happens inside PerformBuiltinConversion().
    if (auto foundation_type_id =
            context_.types().GetTransitiveAdaptedType(value_type_id);
        foundation_type_id != value_type_id &&
        context_.types().IsOneOf<SemIR::StructType, SemIR::TupleType>(
            foundation_type_id)) {
      auto foundation_value_id = AddInst<SemIR::AsCompatible>(
          context_, loc_id,
          {.type_id = foundation_type_id, .source_id = value_id});
      auto foundation_init_id = target.storage_id;
      if (foundation_init_id != SemIR::InstId::None) {
        foundation_init_id =
            target.storage_access_block->AddInst<SemIR::AsCompatible>(
                loc_id, {.type_id = foundation_type_id,
                         .source_id = target.storage_id});
      }
      std::shared_ptr<void> note_scope;
      if (target.kind == ConversionTarget::Initializing) {
        // While the types are the same, the conversion can still fail if it
        // performs a copy while converting the value to another category, and
        // the type (or some part of it) is not copyable.
        note_scope = std::shared_ptr<void>(new Diagnostics::AnnotationScope(
            &context_.emitter(), [value_id = value_id](auto& builder) {
              CARBON_DIAGNOSTIC(InCopy, Note, "in copy of {0}", TypeOfInstId);
              builder.Note(SemIR::LocId(value_id), InCopy, value_id);
            }));
      }
      worklist_.push_back(BuildTargetWorkItem{
          .top_loc_id = loc_id,
          .value_loc_id = loc_id,
          .build_kind = BuildTargetKind::FoundationAdapter,
          .target = target,
          .orig_target = target,
          .output_block_index = output_block_index,
          .orig_value_id = orig_expr_id,
          .builtin_only = builtin_only,
          .annotation_scope = std::move(note_scope),
      });
      worklist_.push_back(ConvertWorkItem{
          .loc_id = loc_id,
          .value_id = foundation_value_id,
          .target =
              {
                  .kind = target.kind,
                  .type_id = foundation_type_id,
                  .storage_id = foundation_init_id,
                  .storage_access_block = target.storage_access_block,
                  .diagnose = target.diagnose,
              },
          .output_block_index = std::nullopt,
          .builtin_only = true,
      });
      return;
    }
  }

  // T implicitly converts to U if T and U are the same ignoring qualifiers, and
  // we're allowed to remove / add any qualifiers that differ. Similarly, T
  // explicitly converts to U if T is compatible with U, and we're allowed to
  // remove / add any qualifiers that differ.
  if (target.type_id != value_type_id) {
    auto [target_foundation_id, target_quals] =
        target.is_explicit_as()
            ? context_.types().GetTransitiveUnqualifiedAdaptedType(
                  target.type_id)
            : context_.types().GetUnqualifiedTypeAndQualifiers(target.type_id);
    auto [value_foundation_id, value_quals] =
        target.is_explicit_as()
            ? context_.types().GetTransitiveUnqualifiedAdaptedType(
                  value_type_id)
            : context_.types().GetUnqualifiedTypeAndQualifiers(value_type_id);
    if (target_foundation_id == value_foundation_id) {
      auto category = SemIR::GetExprCategory(context_.sem_ir(), value_id);
      auto added_quals = target_quals & ~value_quals;
      auto removed_quals = value_quals & ~target_quals;
      if (CanAddQualifiers(added_quals, category) &&
          CanRemoveQualifiers(removed_quals, category, target.kind)) {
        // For a struct or tuple literal, perform a category conversion if
        // necessary.
        if (category == SemIR::ExprCategory::Mixed) {
          worklist_.push_back(BuildTargetWorkItem{
              .top_loc_id = loc_id,
              .value_loc_id = loc_id,
              .build_kind = BuildTargetKind::MixedLiteral,
              .target = target,
              .orig_target = target,
              .output_block_index = output_block_index,
              .orig_value_id = orig_expr_id,
              .orig_value_type_id = value_type_id,
              .builtin_only = builtin_only,
          });
          worklist_.push_back(ConvertWorkItem{
              .loc_id = loc_id,
              .value_id = value_id,
              .target = {.kind = ConversionTarget::Value,
                         .type_id = value_type_id,
                         .diagnose = target.diagnose},
              .output_block_index = std::nullopt,
              .builtin_only = true,
          });
          return;
        }

        // `MaybeUnformed(T)` might have a pointer value representation when `T`
        // does not, so convert as needed when removing `MaybeUnformed`.
        bool need_value_binding = false;
        if ((removed_quals & SemIR::TypeQualifiers::MaybeUnformed) !=
                SemIR::TypeQualifiers::None &&
            category == SemIR::ExprCategory::Value) {
          auto value_rep =
              SemIR::ValueRepr::ForType(context_.sem_ir(), value_type_id);
          auto unformed_value_rep =
              SemIR::ValueRepr::ForType(context_.sem_ir(), target.type_id);
          if (value_rep.kind != unformed_value_rep.kind) {
            CARBON_CHECK(unformed_value_rep.kind == SemIR::ValueRepr::Pointer);
            value_id = AddInst<SemIR::ValueAsRef>(
                context_, loc_id,
                {.type_id = value_type_id, .value_id = value_id});
            need_value_binding = true;
          }
        }

        if ((removed_quals & SemIR::TypeQualifiers::Partial) !=
                SemIR::TypeQualifiers::None &&
            SemIR::IsInitializerCategory(category)) {
          auto unqual_target_type_id =
              context_.types().GetUnqualifiedType(target.type_id);
          if (auto target_class_type =
                  context_.types().TryGetAs<SemIR::ClassType>(
                      unqual_target_type_id)) {
            value_id = ConvertPartialInitializerToNonPartial(
                context_, target, *target_class_type, value_id);
          }
        }

        value_id = AddInst<SemIR::AsCompatible>(
            context_, loc_id,
            {.type_id = target.type_id, .source_id = value_id});
        if (need_value_binding) {
          value_id = AddInst<SemIR::AcquireValue>(
              context_, loc_id,
              {.type_id = target.type_id, .value_id = value_id});
        }
        FinishConvert(loc_id, orig_expr_id, value_id, target,
                      output_block_index, is_base_field, target_elem_type_inst,
                      builtin_only);
        return;
      } else {
        // TODO: Produce a custom diagnostic explaining that we can't perform
        // this conversion due to the change in qualifiers and/or the expression
        // category.
      }
    }
  }

  // A tuple (T1, T2, ..., Tn) converts to (U1, U2, ..., Un) if each Ti
  // converts to Ui.
  if (auto target_tuple_type = target_type_inst.TryAs<SemIR::TupleType>()) {
    if (auto src_tuple_type =
            sem_ir.types().TryGetAs<SemIR::TupleType>(value_type_id)) {
      ConvertTupleToTuple(loc_id, orig_expr_id, value_id, *src_tuple_type,
                          *target_tuple_type, target, output_block_index,
                          builtin_only);
      return;
    }
  }

  // A struct {.f_1: T_1, .f_2: T_2, ..., .f_n: T_n} converts to
  // {.f_p(1): U_p(1), .f_p(2): U_p(2), ..., .f_p(n): U_p(n)} if
  // (p(1), ..., p(n)) is a permutation of (1, ..., n) and each Ti converts
  // to Ui.
  if (auto target_struct_type = target_type_inst.TryAs<SemIR::StructType>()) {
    if (auto src_struct_type =
            sem_ir.types().TryGetAs<SemIR::StructType>(value_type_id)) {
      ConvertStructToStruct(loc_id, orig_expr_id, value_id, *src_struct_type,
                            *target_struct_type, target, output_block_index,
                            builtin_only, is_base_field, target_elem_type_inst);
      return;
    }
  }

  // No other conversions apply when the source and destination types are the
  // same.
  if (value_type_id == target.type_id) {
    FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                  is_base_field, target_elem_type_inst, builtin_only);
    return;
  }

  // A tuple (T1, T2, ..., Tn) converts to array(T, n) if each Ti converts to T.
  if (auto target_array_type = target_type_inst.TryAs<SemIR::ArrayType>()) {
    if (auto src_tuple_type =
            sem_ir.types().TryGetAs<SemIR::TupleType>(value_type_id)) {
      ConvertTupleToArray(loc_id, orig_expr_id, value_id, *src_tuple_type,
                          *target_array_type, target, output_block_index,
                          builtin_only);
      return;
    }
  }

  // Split the qualifiers off the target type.
  // TODO: Most conversions should probably be looking at the unqualified target
  // type.
  auto [target_unqual_type_id, target_quals] =
      context_.types().GetUnqualifiedTypeAndQualifiers(target.type_id);
  auto target_unqual_type_inst =
      sem_ir.types().GetAsInst(target_unqual_type_id);

  // A struct {.f_1: T_1, .f_2: T_2, ..., .f_n: T_n} converts to a class type
  // if it converts to the struct type that is the class's representation type
  // (a struct with the same fields as the class, plus a base field where
  // relevant).
  if (auto target_class_type =
          target_unqual_type_inst.TryAs<SemIR::ClassType>()) {
    if (auto src_struct_type =
            sem_ir.types().TryGetAs<SemIR::StructType>(value_type_id)) {
      if (!context_.classes()
               .Get(target_class_type->class_id)
               .adapt_id.has_value()) {
        ConvertStructToClass(
            loc_id, orig_expr_id, value_id, *src_struct_type,
            *target_class_type, target,
            target_quals.HasAnyOf(SemIR::TypeQualifiers::Partial),
            output_block_index, builtin_only, is_base_field,
            target_elem_type_inst);
        return;
      }
    }

    // An expression of type T converts to U if T is a class derived from U.
    //
    // TODO: Combine this with the qualifiers and adapter conversion logic above
    // to allow qualifiers and inheritance conversions to be performed together.
    if (auto path = ComputeInheritancePath(context_, loc_id, value_type_id,
                                           target.type_id);
        path && !path->empty()) {
      auto res = ConvertDerivedToBase(context_, loc_id, value_id, *path);
      FinishConvert(loc_id, orig_expr_id, res, target, output_block_index,
                    is_base_field, target_elem_type_inst, builtin_only);
      return;
    }
  }

  // A pointer T* converts to [qualified] U* if T is the same as U, or is a
  // class derived from U.
  if (auto target_pointer_type = target_type_inst.TryAs<SemIR::PointerType>()) {
    if (auto src_pointer_type =
            sem_ir.types().TryGetAs<SemIR::PointerType>(value_type_id)) {
      auto target_pointee_id = context_.types().GetTypeIdForTypeInstId(
          target_pointer_type->pointee_id);
      auto src_pointee_id =
          context_.types().GetTypeIdForTypeInstId(src_pointer_type->pointee_id);
      // Try to complete the pointee types so that we can walk through adapters
      // to their adapted types.
      TryToCompleteType(context_, target_pointee_id, loc_id);
      TryToCompleteType(context_, src_pointee_id, loc_id);
      auto [unqual_target_pointee_type_id, target_quals] =
          sem_ir.types().GetTransitiveUnqualifiedAdaptedType(target_pointee_id);
      auto [unqual_src_pointee_type_id, src_quals] =
          sem_ir.types().GetTransitiveUnqualifiedAdaptedType(src_pointee_id);

      // If the qualifiers are incompatible, we can't perform a conversion,
      // except with `unsafe as`.
      if ((src_quals & ~target_quals) != SemIR::TypeQualifiers::None &&
          target.kind != ConversionTarget::ExplicitUnsafeAs) {
        // TODO: Consider producing a custom diagnostic here for a cast that
        // discards constness.
        FinishConvert(loc_id, orig_expr_id, value_id, target,
                      output_block_index, is_base_field, target_elem_type_inst,
                      builtin_only);
        return;
      }

      if (unqual_target_pointee_type_id != unqual_src_pointee_type_id) {
        // If there's an inheritance path from target to source, this is a
        // derived to base conversion.
        if (auto path = ComputeInheritancePath(context_, loc_id,
                                               unqual_src_pointee_type_id,
                                               unqual_target_pointee_type_id);
            path && !path->empty()) {
          value_id = ConvertDerivedPointerToBasePointer(
              context_, loc_id, *src_pointer_type, target.type_id, value_id,
              *path);
        } else {
          // No conversion was possible.
          FinishConvert(loc_id, orig_expr_id, value_id, target,
                        output_block_index, is_base_field,
                        target_elem_type_inst, builtin_only);
          return;
        }
      }

      // Perform a compatible conversion to add any new qualifiers.
      if (src_quals != target_quals) {
        value_id = AddInst<SemIR::AsCompatible>(
            context_, loc_id,
            {.type_id = target.type_id, .source_id = value_id});
      }
      FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                    is_base_field, target_elem_type_inst, builtin_only);
      return;
    }
  }

  if (sem_ir.types().IsFacetType(target.type_id)) {
    auto type_value_id = SemIR::TypeInstId::None;

    // A tuple of types converts to type `type`.
    if (sem_ir.types().Is<SemIR::TupleType>(value_type_id)) {
      type_value_id =
          ConvertTupleToType(context_, loc_id, value_id, value_type_id, target);
    }

    // `{}` converts to `{} as type`.
    if (auto struct_type =
            sem_ir.types().TryGetAs<SemIR::StructType>(value_type_id)) {
      if (struct_type->fields_id == SemIR::StructTypeFieldsId::Empty) {
        type_value_id = sem_ir.types().GetTypeInstId(value_type_id);
      }
    }

    if (type_value_id != SemIR::InstId::None) {
      if (sem_ir.types().Is<SemIR::FacetType>(target.type_id)) {
        // Use the converted `TypeType` value for converting to a facet.
        value_id = type_value_id;
        value_type_id = SemIR::TypeType::TypeId;
      } else {
        // We wanted a `TypeType`, and we've done that.
        FinishConvert(loc_id, orig_expr_id, type_value_id, target,
                      output_block_index, is_base_field, target_elem_type_inst,
                      builtin_only);
        return;
      }
    }
  }

  // FacetType converts to Type by wrapping the facet value in
  // FacetAccessType.
  if (target.type_id == SemIR::TypeType::TypeId &&
      sem_ir.types().Is<SemIR::FacetType>(value_type_id)) {
    auto res = AddInst<SemIR::FacetAccessType>(
        context_, loc_id,
        {.type_id = target.type_id, .facet_value_inst_id = value_id});
    FinishConvert(loc_id, orig_expr_id, res, target, output_block_index,
                  is_base_field, target_elem_type_inst, builtin_only);
    return;
  }

  // Type values can convert to facet values, and facet values can convert to
  // other facet values, as long as they satisfy the required interfaces of the
  // target `FacetType`.
  if (sem_ir.types().Is<SemIR::FacetType>(target.type_id) &&
      sem_ir.types().IsOneOf<SemIR::TypeType, SemIR::FacetType>(
          value_type_id)) {
    // TODO: Runtime facet values should be allowed to convert based on their
    // FacetTypes, but we assume constant values for impl lookup at the moment.
    if (!context_.constant_values().Get(value_id).is_constant()) {
      context_.TODO(loc_id, "conversion of runtime facet value");
      StoreResult(output_block_index, SemIR::ErrorInst::InstId);
      return;
    }

    // Get the canonical type for which we want to attach a new set of witnesses
    // to match the requirements of the target FacetType.
    auto type_inst_id = SemIR::TypeInstId::None;
    if (sem_ir.types().Is<SemIR::FacetType>(value_type_id)) {
      type_inst_id = AddTypeInst<SemIR::FacetAccessType>(
          context_, loc_id,
          {.type_id = SemIR::TypeType::TypeId,
           .facet_value_inst_id = value_id});
    } else {
      type_inst_id = context_.types().GetAsTypeInstId(value_id);

      // Shortcut for lossless round trips through a FacetAccessType when
      // converting back to the type of the original symbolic binding facet
      // value.
      //
      // In the case where the FacetAccessType wraps a SymbolicBinding with the
      // exact facet type that we are converting to, the resulting FacetValue
      // would evaluate back to the original SymbolicBinding as its canonical
      // form. We can skip past the whole impl lookup step then and do that
      // here.
      auto facet_value_inst_id =
          GetCanonicalFacetOrTypeValue(context_, type_inst_id);
      if (sem_ir.insts().Get(facet_value_inst_id).type_id() == target.type_id) {
        FinishConvert(loc_id, orig_expr_id, facet_value_inst_id, target,
                      output_block_index, is_base_field, target_elem_type_inst,
                      builtin_only);
        return;
      }
    }

    // Conversion from a facet value (which has type `FacetType`) or a type
    // value (which has type `TypeType`) to a facet value. We can do this if the
    // type satisfies the requirements of the target `FacetType`, as determined
    // by finding impl witnesses for the target FacetType.
    auto lookup_result = LookupImplWitness(
        context_, loc_id, sem_ir.constant_values().Get(type_inst_id),
        sem_ir.types().GetConstantId(target.type_id), target.diagnose);
    if (lookup_result.has_value()) {
      if (lookup_result.has_error_value()) {
        StoreResult(output_block_index, SemIR::ErrorInst::InstId);
        return;
      } else {
        // Note that `FacetValue`'s type is the same `FacetType` that was used
        // to construct the set of witnesses, ie. the query to
        // `LookupImplWitness()`. This ensures that the witnesses are in the
        // same order as the `required_impls()` in the `IdentifiedFacetType` of
        // the `FacetValue`'s type.
        auto res = AddInst<SemIR::FacetValue>(
            context_, loc_id,
            {.type_id = target.type_id,
             .type_inst_id = type_inst_id,
             .witnesses_block_id = lookup_result.inst_block_id()});
        FinishConvert(loc_id, orig_expr_id, res, target, output_block_index,
                      is_base_field, target_elem_type_inst, builtin_only);
        return;
      }
    } else {
      // If impl lookup fails, don't keep looking for another way to convert.
      // See https://github.com/carbon-language/carbon-lang/issues/5122.
      // TODO: Pass this function into `LookupImplWitness` so it can construct
      // the error add notes explaining failure.
      if (target.diagnose) {
        DiagnoseConversionFailureToConstraintValue(context_, loc_id, value_id,
                                                   target.type_id);
      }
      StoreResult(output_block_index, SemIR::ErrorInst::InstId);
      return;
    }
  }

  FinishConvert(loc_id, orig_expr_id, value_id, target, output_block_index,
                is_base_field, target_elem_type_inst, builtin_only);
}

auto ConversionWorklist::ProcessWorkItem(StartElementWorkItem item) -> void {
  if (item.output_block_index > 0 &&
      context_.sem_ir().inst_blocks().Get(
          new_block_stack_.back().id())[item.output_block_index - 1] ==
          SemIR::ErrorInst::InstId) {
    new_block_stack_.back().Set(item.output_block_index,
                                SemIR::ErrorInst::InstId);
    return;
  }

  auto src_elem_type =
      context_.types().GetTypeIdForTypeInstId(item.src_elem_type_inst);
  auto target_elem_type =
      context_.types().GetTypeIdForTypeInstId(item.target_elem_type_inst);

  llvm::ArrayRef<SemIR::InstId> src_literal_elems =
      item.src_literal_elems_id.has_value()
          ? context_.inst_blocks().Get(item.src_literal_elems_id)
          : llvm::ArrayRef<SemIR::InstId>();

  auto src_elem_id = !src_literal_elems.empty()
                         ? src_literal_elems[item.src_field_index]
                         : EmitElementAccessInst(
                               item.src_access_kind, item.loc_id, item.src_id,
                               src_elem_type, context_, item.src_field_index);

  ConversionTarget target = {.kind = item.kind, .type_id = target_elem_type};
  size_t snap_size = item.target_block ? item.target_block->Size() : 0;
  size_t snap_cleanups =
      item.target_block ? item.target_block->CleanupsSize() : 0;

  if (target.is_initializer()) {
    target.storage_access_block = item.target_block;
    target.storage_id = EmitElementAccessInst(
        item.target_access_kind, item.loc_id, item.target_id, target_elem_type,
        *item.target_block, item.target_field_index);
  }

  worklist_.push_back(FinishElementWorkItem{
      .target_block = item.target_block,
      .size = snap_size,
      .cleanups_size = snap_cleanups,
  });
  worklist_.push_back(ConvertWorkItem{
      .loc_id = item.loc_id,
      .value_id = src_elem_id,
      .target = target,
      .output_block_index = item.output_block_index,
      .is_base_field = item.is_base_field,
      .target_elem_type_inst = item.orig_dest_elem_type_inst,
  });
}

auto ConversionWorklist::ProcessWorkItem(FinishElementWorkItem item) -> void {
  if (item.target_block) {
    item.target_block->DiscardUnusedInsts(item.size, item.cleanups_size);
  }
}

auto ConversionWorklist::ProcessWorkItem(InitVptrWorkItem item) -> void {
  item.target.storage_access_block->InsertHere();
  auto vptr_type_id =
      context_.types().GetTypeIdForTypeInstId(item.vptr_type_inst_id);
  auto dest_id = AddInst<SemIR::ClassElementAccess>(
      context_, item.value_loc_id,
      {.type_id = vptr_type_id,
       .base_id = item.target.storage_id,
       .index = SemIR::ElementIndex(item.output_block_index)});
  auto vtable_ptr_id = SemIR::InstId::None;
  if (item.vtable_class_type.has_value()) {
    vtable_ptr_id = CreateVtablePtrRef(context_, item.value_loc_id,
                                       *item.vtable_class_type);
  } else {
    vtable_ptr_id = AddInst<SemIR::UninitializedValue>(
        context_, item.value_loc_id,
        {.type_id = GetPointerType(context_, SemIR::VtableType::TypeInstId)});
  }
  auto init_id = AddInst<SemIR::InPlaceInit>(
      context_, item.value_loc_id,
      {.type_id = vptr_type_id, .src_id = vtable_ptr_id, .dest_id = dest_id});
  new_block_stack_.back().Set(item.output_block_index, init_id);
}

auto ConversionWorklist::ProcessWorkItem(BuildTargetWorkItem item) -> void {
  auto& sem_ir = context_.sem_ir();
  if (item.build_kind == BuildTargetKind::FoundationAdapter) {
    auto res = results_.pop_back_val();
    if (res == SemIR::ErrorInst::InstId) {
      StoreResult(item.output_block_index, res);
      return;
    }
    auto final_res = AddInst<SemIR::AsCompatible>(
        context_, item.top_loc_id,
        {.type_id = item.target.type_id, .source_id = res});
    FinishConvert(item.top_loc_id, item.orig_value_id, final_res,
                  item.orig_target, item.output_block_index);
    return;
  }
  if (item.build_kind == BuildTargetKind::MixedLiteral) {
    auto value_id = results_.pop_back_val();
    if (value_id == SemIR::ErrorInst::InstId) {
      StoreResult(item.output_block_index, value_id);
      return;
    }
    auto [target_foundation_id, target_quals] =
        item.target.is_explicit_as()
            ? context_.types().GetTransitiveUnqualifiedAdaptedType(
                  item.target.type_id)
            : context_.types().GetUnqualifiedTypeAndQualifiers(
                  item.target.type_id);
    auto [value_foundation_id, value_quals] =
        item.target.is_explicit_as()
            ? context_.types().GetTransitiveUnqualifiedAdaptedType(
                  *item.orig_value_type_id)
            : context_.types().GetUnqualifiedTypeAndQualifiers(
                  *item.orig_value_type_id);
    auto removed_quals = value_quals & ~target_quals;
    auto category = SemIR::GetExprCategory(context_.sem_ir(), value_id);

    bool need_value_binding = false;
    if ((removed_quals & SemIR::TypeQualifiers::MaybeUnformed) !=
            SemIR::TypeQualifiers::None &&
        category == SemIR::ExprCategory::Value) {
      auto value_rep = SemIR::ValueRepr::ForType(context_.sem_ir(),
                                                 *item.orig_value_type_id);
      auto unformed_value_rep =
          SemIR::ValueRepr::ForType(context_.sem_ir(), item.target.type_id);
      if (value_rep.kind != unformed_value_rep.kind) {
        CARBON_CHECK(unformed_value_rep.kind == SemIR::ValueRepr::Pointer);
        value_id = AddInst<SemIR::ValueAsRef>(
            context_, item.top_loc_id,
            {.type_id = *item.orig_value_type_id, .value_id = value_id});
        need_value_binding = true;
      }
    }
    if ((removed_quals & SemIR::TypeQualifiers::Partial) !=
            SemIR::TypeQualifiers::None &&
        SemIR::IsInitializerCategory(category)) {
      auto unqual_target_type_id =
          context_.types().GetUnqualifiedType(item.target.type_id);
      if (auto target_class_type = context_.types().TryGetAs<SemIR::ClassType>(
              unqual_target_type_id)) {
        value_id = ConvertPartialInitializerToNonPartial(
            context_, item.target, *target_class_type, value_id);
      }
    }
    value_id = AddInst<SemIR::AsCompatible>(
        context_, item.top_loc_id,
        {.type_id = item.target.type_id, .source_id = value_id});
    if (need_value_binding) {
      value_id = AddInst<SemIR::AcquireValue>(
          context_, item.top_loc_id,
          {.type_id = item.target.type_id, .value_id = value_id});
    }
    FinishConvert(item.top_loc_id, item.orig_value_id, value_id,
                  item.orig_target, item.output_block_index);
    return;
  }

  auto new_block_id = new_block_stack_.pop_back_val().id();
  auto elems = sem_ir.inst_blocks().Get(new_block_id);

  for (auto elem_id : elems) {
    if (elem_id == SemIR::ErrorInst::InstId) {
      StoreResult(item.output_block_index, SemIR::ErrorInst::InstId);
      return;
    }
  }

  SemIR::InstId res_id = SemIR::InstId::None;
  if (item.build_kind == BuildTargetKind::TupleToTuple) {
    if (item.target.is_initializer()) {
      item.target.storage_access_block->InsertHere();
      res_id = AddInst<SemIR::TupleInit>(context_, item.value_loc_id,
                                         {.type_id = item.target.type_id,
                                          .elements_id = new_block_id,
                                          .dest_id = item.target.storage_id});
    } else {
      res_id = AddInst<SemIR::TupleValue>(
          context_, item.value_loc_id,
          {.type_id = item.target.type_id, .elements_id = new_block_id});
    }
  } else if (item.build_kind == BuildTargetKind::TupleToArray) {
    // Flush the temporary here if we didn't insert it earlier, so we can add a
    // reference to the return slot.
    item.target.storage_access_block->InsertHere();
    res_id = AddInst<SemIR::ArrayInit>(context_, item.value_loc_id,
                                       {.type_id = item.target.type_id,
                                        .inits_id = new_block_id,
                                        .dest_id = item.target.storage_id});
  } else if (item.build_kind == BuildTargetKind::StructToClass) {
    item.target.storage_access_block->InsertHere();
    res_id = AddInst<SemIR::ClassInit>(context_, item.value_loc_id,
                                       {.type_id = item.target.type_id,
                                        .elements_id = new_block_id,
                                        .dest_id = item.target.storage_id});
    if (item.vtable_class_type.has_value()) {
      res_id = ConvertPartialInitializerToNonPartial(
          context_, item.target, *item.vtable_class_type, res_id);
    }
  } else if (item.build_kind == BuildTargetKind::StructToStruct) {
    if (item.target.is_initializer()) {
      item.target.storage_access_block->InsertHere();
      res_id = AddInst<SemIR::StructInit>(context_, item.value_loc_id,
                                          {.type_id = item.target.type_id,
                                           .elements_id = new_block_id,
                                           .dest_id = item.target.storage_id});
    } else {
      res_id = AddInst<SemIR::StructValue>(
          context_, item.value_loc_id,
          {.type_id = item.target.type_id, .elements_id = new_block_id});
    }
  }

  FinishConvert(item.top_loc_id, item.orig_value_id, res_id, item.orig_target,
                item.output_block_index, item.is_base_field,
                item.target_elem_type_inst, item.builtin_only);
}
// Given a value expression, form a corresponding initializer that copies from
// that value to the specified target, if it is possible to do so.
static auto PerformCopy(Context& context, SemIR::InstId expr_id,
                        const ConversionTarget& target) -> SemIR::InstId {
  auto copy_id = BuildUnaryOperator(
      context, SemIR::LocId(expr_id), {.interface_name = CoreIdentifier::Copy},
      expr_id, target.diagnose, [&](auto& builder) {
        CARBON_DIAGNOSTIC(CopyOfUncopyableType, Context,
                          "cannot copy value of type {0}", TypeOfInstId);
        builder.Context(expr_id, CopyOfUncopyableType, expr_id);
      });
  return copy_id;
}

// Tries to form a `ValueAsRef` conversion that extracts the pointer value from
// a value expression with a pointer value representation. Returns the converted
// expression, or None if the conversion was not applicable.
static auto TryMakeValueAsRef(Context& context, SemIR::InstId expr_id)
    -> SemIR::InstId {
  auto expr = context.insts().Get(expr_id);

  // If the expression has a pointer value representation, extract that and use
  // it directly.
  if (SemIR::ValueRepr::ForType(context.sem_ir(), expr.type_id()).kind ==
      SemIR::ValueRepr::Pointer) {
    return AddInst<SemIR::ValueAsRef>(
        context, SemIR::LocId(expr_id),
        {.type_id = expr.type_id(), .value_id = expr_id});
  }

  return SemIR::InstId::None;
}

// Returns the Core interface name to use for a given kind of conversion.
static auto GetConversionInterfaceName(ConversionTarget::Kind kind)
    -> CoreIdentifier {
  switch (kind) {
    case ConversionTarget::ExplicitAs:
      return CoreIdentifier::As;
    case ConversionTarget::ExplicitUnsafeAs:
      return CoreIdentifier::UnsafeAs;
    default:
      return CoreIdentifier::ImplicitAs;
  }
}

auto PerformAction(Context& context, SemIR::LocId loc_id,
                   SemIR::ConvertToValueAction action) -> SemIR::InstId {
  return Convert(context, loc_id, action.inst_id,
                 {.kind = ConversionTarget::Value,
                  .type_id = context.types().GetTypeIdForTypeInstId(
                      action.target_type_inst_id)});
}

// State machine for performing category conversions.
class CategoryConverter {
 public:
  // Constructs a converter which converts an expression at the given location
  // to the given conversion target.
  CategoryConverter(Context& context, SemIR::LocId loc_id,
                    ConversionTarget& target)
      : context_(context),
        sem_ir_(context.sem_ir()),
        loc_id_(loc_id),
        target_(target) {}

  // Converts expr_id to the target specified in the constructor, and returns
  // the converted inst.
  auto Convert(SemIR::InstId expr_id) && -> SemIR::InstId {
    auto category = SemIR::GetExprCategory(sem_ir_, expr_id);
    while (true) {
      if (expr_id == SemIR::ErrorInst::InstId) {
        return expr_id;
      }
      CARBON_KIND_SWITCH(DoStep(expr_id, category)) {
        case CARBON_KIND(NextStep next_step): {
          CARBON_CHECK(next_step.expr_id != SemIR::InstId::None);
          expr_id = next_step.expr_id;
          category = next_step.category;
          break;
        }
        case CARBON_KIND(Done done): {
          return done.expr_id;
        }
      }
    }
  }

 private:
  // State that indicates there's more work to be done. As a convenience,
  // if expr_id is SemIR::ErrorInst::InstId, this is equivalent to
  // Done{SemIR::ErrorInst::InstId}.
  struct NextStep {
    // The inst to convert.
    SemIR::InstId expr_id;
    // The category of expr_id.
    SemIR::ExprCategory category;
  };

  // State that indicates we've finished category conversion.
  struct Done {
    // The result of the conversion.
    SemIR::InstId expr_id;
  };

  using State = std::variant<NextStep, Done>;

  // Performs the first step of converting `expr_id` with category `category`
  // to the target specified in the constructor, and returns the state after
  // that step.
  auto DoStep(SemIR::InstId expr_id, SemIR::ExprCategory category) const
      -> State;

  Context& context_;
  SemIR::File& sem_ir_;
  SemIR::LocId loc_id_;
  const ConversionTarget& target_;
};

auto CategoryConverter::DoStep(const SemIR::InstId expr_id,
                               const SemIR::ExprCategory category) const
    -> State {
  CARBON_DCHECK(SemIR::GetExprCategory(sem_ir_, expr_id) == category);
  switch (category) {
    case SemIR::ExprCategory::NotExpr:
    case SemIR::ExprCategory::Mixed:
    case SemIR::ExprCategory::Pattern:
      CARBON_FATAL("Unexpected expression {0} after builtin conversions",
                   sem_ir_.insts().Get(expr_id));

    case SemIR::ExprCategory::Error:
      return Done{SemIR::ErrorInst::InstId};

    case SemIR::ExprCategory::Dependent:
      context_.TODO(expr_id, "Support symbolic expression forms");
      return Done{SemIR::ErrorInst::InstId};

    case SemIR::ExprCategory::InPlaceInitializing:
    case SemIR::ExprCategory::ReprInitializing:
      if (target_.is_initializer()) {
        // Overwrite the initializer's storage argument with the inst currently
        // at target_.storage_id, if both are present and the storage argument
        // hasn't already been set. However, we skip this if the type is a C++
        // enum: in that case, we don't actually have an initializing
        // expression, we're just pretending we do.
        auto new_storage_id =
            OverwriteTemporaryStorageArg(sem_ir_, expr_id, target_);

        // If in-place initialization was requested, and it hasn't already
        // happened, ensure it happens now.
        if (target_.kind == ConversionTarget::InPlaceInitializing &&
            !IsInPlaceInitializing(context_, expr_id, category)) {
          target_.storage_access_block->InsertHere();
          CARBON_CHECK(new_storage_id.has_value());
          return Done{AddInst<SemIR::InPlaceInit>(context_, loc_id_,
                                                  {.type_id = target_.type_id,
                                                   .src_id = expr_id,
                                                   .dest_id = new_storage_id})};
        }
        return Done{expr_id};
      }

      if (target_.kind == ConversionTarget::Discarded) {
        DiscardInitializer(context_, expr_id);
        return Done{SemIR::InstId::None};
      } else if (IsValidExprCategoryForConversionTarget(category,
                                                        target_.kind)) {
        return Done{expr_id};
      } else {
        // Commit to using a temporary for this initializing expression.
        // TODO: Don't create a temporary if the initializing representation is
        // already a value representation.
        // TODO: If the target is DurableRef, materialize a VarStorage instead
        // of a TemporaryStorage to lifetime-extend.
        return NextStep{.expr_id = MaterializeTemporary(context_, expr_id),
                        .category = SemIR::ExprCategory::EphemeralRef};
      }

    case SemIR::ExprCategory::RefTagged: {
      auto tagged_expr_id =
          sem_ir_.insts().GetAs<SemIR::RefTagExpr>(expr_id).expr_id;
      auto tagged_expr_category =
          SemIR::GetExprCategory(sem_ir_, tagged_expr_id);
      if (target_.diagnose &&
          tagged_expr_category != SemIR::ExprCategory::DurableRef) {
        CARBON_DIAGNOSTIC(
            RefTagNotDurableRef, Error,
            "expression tagged with `ref` is not a durable reference");
        context_.emitter().Emit(tagged_expr_id, RefTagNotDurableRef);
      }

      if (target_.kind == ConversionTarget::RefParam) {
        return Done{expr_id};
      }

      // If the target isn't a reference parameter, ignore the `ref` tag.
      // Unnecessary `ref` tags are diagnosed earlier.
      return NextStep{.expr_id = tagged_expr_id,
                      .category = tagged_expr_category};
    }

    case SemIR::ExprCategory::DurableRef:
      if (target_.kind == ConversionTarget::DurableRef ||
          target_.kind == ConversionTarget::UnmarkedRefParam) {
        return Done{expr_id};
      }
      if (target_.kind == ConversionTarget::RefParam) {
        if (target_.diagnose) {
          CARBON_DIAGNOSTIC(
              RefParamNoRefTag, Error,
              "argument to `ref` parameter not marked with `ref`");
          context_.emitter().Emit(expr_id, RefParamNoRefTag);
        }
        return Done{expr_id};
      }
      [[fallthrough]];

    case SemIR::ExprCategory::EphemeralRef:
      // If a reference expression is an acceptable result, we're done.
      if (target_.kind == ConversionTarget::ValueOrRef ||
          target_.kind == ConversionTarget::Discarded ||
          target_.kind == ConversionTarget::CppThunkRef ||
          target_.kind == ConversionTarget::RefParam ||
          target_.kind == ConversionTarget::UnmarkedRefParam) {
        return Done{expr_id};
      }

      // If we have a reference and don't want one, form a value binding.
      // TODO: Support types with custom value representations.
      return NextStep{.expr_id = AddInst<SemIR::AcquireValue>(
                          context_, SemIR::LocId(expr_id),
                          {.type_id = target_.type_id, .value_id = expr_id}),
                      .category = SemIR::ExprCategory::Value};

    case SemIR::ExprCategory::Value:
      if (target_.kind == ConversionTarget::DurableRef) {
        if (target_.diagnose) {
          CARBON_DIAGNOSTIC(ConversionFailureNonRefToRef, Error,
                            "cannot bind durable reference to non-reference "
                            "value of type {0}",
                            SemIR::TypeId);
          context_.emitter().Emit(loc_id_, ConversionFailureNonRefToRef,
                                  target_.type_id);
        }
        return Done{SemIR::ErrorInst::InstId};
      }

      if (target_.kind == ConversionTarget::RefParam ||
          target_.kind == ConversionTarget::UnmarkedRefParam) {
        if (target_.diagnose) {
          CARBON_DIAGNOSTIC(ValueForRefParam, Error,
                            "value expression passed to reference parameter");
          context_.emitter().Emit(loc_id_, ValueForRefParam);
        }
        return Done{SemIR::ErrorInst::InstId};
      }

      // When initializing a C++ thunk parameter, try to pass a value "by
      // reference".
      if (target_.kind == ConversionTarget::CppThunkRef) {
        if (auto result_id = TryMakeValueAsRef(context_, expr_id);
            result_id.has_value()) {
          return Done{result_id};
        }
        // Otherwise, fall through to make a copy.
      }

      // When initializing from a value, perform a copy.
      if (target_.is_initializer() ||
          target_.kind == ConversionTarget::CppThunkRef) {
        auto copy_id = PerformCopy(context_, expr_id, target_);
        if (copy_id == SemIR::ErrorInst::InstId) {
          return Done{SemIR::ErrorInst::InstId};
        }
        return NextStep{.expr_id = copy_id,
                        .category = SemIR::GetExprCategory(sem_ir_, copy_id)};
      }

      return Done{expr_id};
  }
}

// Returns true if converting `expr_id` to `target` requires `target.type_id`
// to be complete.
static auto ConversionNeedsCompleteTarget(Context& context,
                                          SemIR::InstId expr_id,
                                          ConversionTarget target) -> bool {
  auto source_type_id = context.insts().Get(expr_id).type_id();

  // We allow conversion to incomplete facet types, since their representation
  // is fixed. This allows us to support using the `Self` of an interface inside
  // its definition.
  if (context.types().IsFacetType(target.type_id)) {
    return false;
  }

  // If the types are the same, we only have to worry about form conversions.
  if (source_type_id == target.type_id) {
    auto source_category = SemIR::GetExprCategory(context.sem_ir(), expr_id);

    // If there's no form conversion and no type conversion, the conversion is
    // a no-op, so we don't need a complete type.
    if (IsValidExprCategoryForConversionTarget(source_category, target.kind)) {
      return false;
    }
  }

  return true;
}

auto ConversionWorklist::ProcessWorkItem(ConvertWorkItem item) -> void {
  Convert(item.loc_id, item.value_id, item.target, item.output_block_index,
          item.is_base_field, item.target_elem_type_inst, item.builtin_only);
}

auto ConversionWorklist::Convert(SemIR::LocId loc_id, SemIR::InstId expr_id,
                                 ConversionTarget target,
                                 std::optional<int> output_block_index,
                                 bool is_base_field,
                                 SemIR::TypeInstId target_elem_type_inst,
                                 bool builtin_only) -> void {
  auto orig_expr_id = expr_id;
  auto& sem_ir = context_.sem_ir();

  // Start by making sure both sides are non-errors. If any part is an error,
  // the result is an error and we shouldn't diagnose.
  if (sem_ir.insts().Get(expr_id).type_id() == SemIR::ErrorInst::TypeId ||
      target.type_id == SemIR::ErrorInst::TypeId) {
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }

  auto starting_category = SemIR::GetExprCategory(sem_ir, expr_id);
  if (starting_category == SemIR::ExprCategory::NotExpr) {
    // TODO: We currently encounter this for use of namespaces and functions.
    // We should provide a better diagnostic for inappropriate use of
    // namespace names, and allow use of functions as values.
    if (target.diagnose) {
      CARBON_DIAGNOSTIC(UseOfNonExprAsValue, Error,
                        "expression cannot be used as a value");
      context_.emitter().Emit(expr_id, UseOfNonExprAsValue);
    }
    StoreResult(output_block_index, SemIR::ErrorInst::InstId);
    return;
  }

  if (target.kind == ConversionTarget::NoOp) {
    CARBON_CHECK(target.type_id == sem_ir.insts().Get(expr_id).type_id());
    StoreResult(output_block_index, expr_id);
    return;
  }

  // Diagnose unnecessary `ref` tags early, so that they're not obscured by
  // conversions.
  if (starting_category == SemIR::ExprCategory::RefTagged &&
      target.kind != ConversionTarget::RefParam && target.diagnose) {
    CARBON_DIAGNOSTIC(RefTagNoRefParam, Error,
                      "`ref` tag is not an argument to a `ref` parameter");
    context_.emitter().Emit(expr_id, RefTagNoRefParam);
  }

  // TODO: Allow abstract but complete types if the conversion is just a
  // same-type value acqisition.
  // TODO: Push this check down to the points where we perform operations that
  // need the type to be complete.
  if (ConversionNeedsCompleteTarget(context_, expr_id, target)) {
    if (target.diagnose) {
      if (!RequireConcreteType(
              context_, target.type_id, loc_id,
              [&](auto& builder) {
                CARBON_CHECK(!target.is_initializer(),
                             "Initialization of incomplete types is expected "
                             "to be caught elsewhere.");
                CARBON_DIAGNOSTIC(IncompleteTypeInValueConversion, Context,
                                  "forming value of incomplete type {0}",
                                  SemIR::TypeId);
                CARBON_DIAGNOSTIC(IncompleteTypeInConversion, Context,
                                  "invalid use of incomplete type {0}",
                                  SemIR::TypeId);
                builder.Context(loc_id,
                                target.kind == ConversionTarget::Value
                                    ? IncompleteTypeInValueConversion
                                    : IncompleteTypeInConversion,
                                target.type_id);
              },
              [&](auto& builder) {
                CARBON_DIAGNOSTIC(AbstractTypeInInit, Context,
                                  "initialization of abstract type {0}",
                                  SemIR::TypeId);
                builder.Context(loc_id, AbstractTypeInInit, target.type_id);
              })) {
        StoreResult(output_block_index, SemIR::ErrorInst::InstId);
        return;
      }
    } else {
      if (!TryIsConcreteType(context_, target.type_id, loc_id)) {
        StoreResult(output_block_index, SemIR::ErrorInst::InstId);
        return;
      }
    }
  }

  // Clear storage_id in cases where it's clearly meaningless, to avoid misuse
  // and simplify the resulting SemIR.
  if (!target.is_initializer() ||
      (target.kind == ConversionTarget::Initializing &&
       SemIR::InitRepr::ForType(sem_ir, target.type_id).kind ==
           SemIR::InitRepr::None)) {
    target.storage_id = SemIR::InstId::None;
  }

  // The source type doesn't need to be complete, but its completeness can
  // affect the result. For example, we don't know what type it adapts or
  // derives from unless it's complete.
  // TODO: Is there a risk of coherence problems if the source type is
  // incomplete, but a conversion would have been possible or would have behaved
  // differently if it were complete?
  TryToCompleteType(context_, context_.insts().Get(expr_id).type_id(), loc_id);

  // Check whether any builtin conversion applies.
  PerformBuiltinConversion(loc_id, expr_id, target, output_block_index,
                           is_base_field, target_elem_type_inst, builtin_only,
                           orig_expr_id);
}

auto ConversionWorklist::FinishConvert(
    SemIR::LocId loc_id, SemIR::InstId orig_expr_id, SemIR::InstId expr_id,
    ConversionTarget target, std::optional<int> output_block_index,
    bool is_base_field, SemIR::TypeInstId target_elem_type_inst,
    bool builtin_only) -> void {
  if (builtin_only || expr_id == SemIR::ErrorInst::InstId) {
    StoreResult(output_block_index, expr_id);
    return;
  }
  auto& sem_ir = context_.sem_ir();

  // Defer the action if it's dependent. We do this now rather than before
  // attempting any conversion so that we can still perform builtin conversions
  // on dependent arguments. This matters for things like converting a
  // `template T:! SomeInterface` to `type`, where it's important to form a
  // `FacetAccessType` when checking the template. But when running the action
  // later, we need to try builtin conversions again, because one may apply that
  // didn't apply in the template definition.
  // TODO: Support this for targets other than `Value`.
  if (sem_ir.insts().Get(expr_id).type_id() != target.type_id &&
      target.kind == ConversionTarget::Value) {
    auto target_type_inst_id = context_.types().GetTypeInstId(target.type_id);
    SemIR::ConvertToValueAction convert_action = {
        .type_id = SemIR::InstType::TypeId,
        .inst_id = expr_id,
        .target_type_inst_id = target_type_inst_id};
    // We don't use `HandleAction` here because it would call `PerformAction`
    // inline if it's performable, which would lead to infinite recursion.
    if (!ActionIsPerformable(context_, convert_action)) {
      expr_id = AddDependentActionSplice(context_, loc_id, convert_action,
                                         target_type_inst_id);
      StoreResult(output_block_index, expr_id);
      return;
    }
  }

  // If this is not a builtin conversion, try an `ImplicitAs` conversion.
  if (sem_ir.insts().Get(expr_id).type_id() != target.type_id) {
    SemIR::InstId interface_args[] = {
        context_.types().GetTypeInstId(target.type_id)};
    Operator op = {
        .interface_name = GetConversionInterfaceName(target.kind),
        .interface_args_ref = interface_args,
        .op_name = CoreIdentifier::Convert,
    };
    expr_id = BuildUnaryOperator(
        context_, loc_id, op, expr_id, target.diagnose, [&](auto& builder) {
          int target_kind_for_diag =
              target.kind == ConversionTarget::ExplicitAs         ? 1
              : target.kind == ConversionTarget::ExplicitUnsafeAs ? 2
                                                                  : 0;
          if (target.type_id == SemIR::TypeType::TypeId ||
              sem_ir.types().Is<SemIR::FacetType>(target.type_id)) {
            CARBON_DIAGNOSTIC(
                ConversionFailureNonTypeToFacet, Context,
                "cannot{0:=0: implicitly|:} convert non-type value of type {1} "
                "{2:to|into type implementing} {3}"
                "{0:=1: with `as`|=2: with `unsafe as`|:}",
                Diagnostics::IntAsSelect, TypeOfInstId,
                Diagnostics::BoolAsSelect, SemIR::TypeId);
            builder.Context(loc_id, ConversionFailureNonTypeToFacet,
                            target_kind_for_diag, expr_id,
                            target.type_id == SemIR::TypeType::TypeId,
                            target.type_id);
          } else {
            CARBON_DIAGNOSTIC(
                ConversionFailure, Context,
                "cannot{0:=0: implicitly|:} convert expression of type "
                "{1} to {2}{0:=1: with `as`|=2: with `unsafe as`|:}",
                Diagnostics::IntAsSelect, TypeOfInstId, SemIR::TypeId);
            builder.Context(loc_id, ConversionFailure, target_kind_for_diag,
                            expr_id, target.type_id);
          }
        });

    if (expr_id != SemIR::ErrorInst::InstId &&
        CanUseValueOfInitializer(sem_ir, target.type_id, target.kind)) {
      expr_id = AddInst<SemIR::ValueOfInitializer>(
          context_, loc_id, {.type_id = target.type_id, .init_id = expr_id});
    }
  }

  // Track that we performed a type conversion, if we did so.
  if (orig_expr_id != expr_id) {
    expr_id = AddInst<SemIR::Converted>(context_, loc_id,
                                        {.type_id = target.type_id,
                                         .original_id = orig_expr_id,
                                         .result_id = expr_id});
  }

  // For `as`, don't perform any value category conversions. In particular, an
  // identity conversion shouldn't change the expression category.
  if (!target.is_explicit_as()) {
    // Now perform any necessary value category conversions.
    expr_id = CategoryConverter(context_, loc_id, target).Convert(expr_id);
  }

  // When initializing the base, adjust the type of the initializer from
  // `partial Base` to `Base`. This isn't strictly correct, since we haven't
  // finished initializing a `Base` until we store to the vptr, but is better
  // than having an inconsistent type for the struct field initializer.
  if (expr_id != SemIR::ErrorInst::InstId && is_base_field) {
    auto type_id =
        context_.types().GetTypeIdForTypeInstId(target_elem_type_inst);
    if (context_.insts().Get(expr_id).type_id() != type_id) {
      expr_id = AddInst<SemIR::AsCompatible>(
          context_, loc_id, {.type_id = type_id, .source_id = expr_id});
    }
  }

  StoreResult(output_block_index, expr_id);
}

auto ConversionWorklist::Loop() -> void {
  while (!worklist_.empty()) {
    CARBON_KIND_SWITCH(worklist_.pop_back_val()) {
      case CARBON_KIND(ConvertWorkItem convert_item): {
        ProcessWorkItem(convert_item);
        break;
      }
      case CARBON_KIND(BuildTargetWorkItem build_target): {
        ProcessWorkItem(std::move(build_target));
        break;
      }
      case CARBON_KIND(StartElementWorkItem start_elem): {
        ProcessWorkItem(start_elem);
        break;
      }
      case CARBON_KIND(FinishElementWorkItem finish_elem): {
        ProcessWorkItem(finish_elem);
        break;
      }
      case CARBON_KIND(InitVptrWorkItem init_vptr): {
        ProcessWorkItem(init_vptr);
        break;
      }
    }
  }
}

auto Convert(Context& context, SemIR::LocId loc_id, SemIR::InstId expr_id,
             ConversionTarget target) -> SemIR::InstId {
  ConversionWorklist worklist(context);
  return worklist.Run(loc_id, expr_id, target);
}

auto InitializeExisting(Context& context, SemIR::LocId loc_id,
                        SemIR::InstId storage_id, SemIR::InstId value_id,
                        bool for_return) -> SemIR::InstId {
  auto type_id = context.insts().Get(storage_id).type_id();
  if (for_return &&
      !SemIR::InitRepr::ForType(context.sem_ir(), type_id).MightBeInPlace()) {
    // TODO: Is it safe to use storage_id when the init repr is dependent?
    storage_id = SemIR::InstId::None;
  }

  // TODO: This is only an approximation of a dominance check. Add a general
  // end-of-phase dominance check and remove the check here and the one in
  // `MergeReplacing`.
  CARBON_CHECK(!storage_id.has_value() ||
                   value_id == SemIR::ErrorInst::InstId ||
                   context.insts().GetRawIndex(storage_id) <=
                       context.insts().GetRawIndex(value_id),
               "Storage might not dominate initializer");
  PendingBlock target_block(&context);
  return Convert(context, loc_id, value_id,
                 {.kind = ConversionTarget::Initializing,
                  .type_id = type_id,
                  .storage_id = storage_id,
                  .storage_access_block = &target_block});
}

auto Initialize(Context& context, SemIR::LocId loc_id,
                SemIR::InstId&& storage_id, PendingBlock&& storage_access_block,
                SemIR::InstId value_id) -> InitializeResult {
  CARBON_CHECK(storage_id.has_value());
  auto type_id = context.insts().Get(storage_id).type_id();
  auto result_id = Convert(context, loc_id, value_id,
                           {.kind = ConversionTarget::Initializing,
                            .type_id = type_id,
                            .storage_id = storage_id,
                            .storage_access_block = &storage_access_block});

  // Insert the storage block now, in case it wasn't used by the initializer.
  storage_access_block.InsertHere();
  if (result_id == SemIR::ErrorInst::InstId) {
    return {.storage_id = SemIR::ErrorInst::InstId,
            .init_id = SemIR::ErrorInst::InstId};
  }

  // Find the storage argument. If the storage block was spliced or written over
  // an existing storage argument by `Convert`, the resulting expression will
  // have a storage argument that points to the possibly-rewritten storage
  // instruction, and we can use that. Otherwise, the storage access block will
  // have been inserted above, and we can use `storage_id` unchanged.
  auto storage_arg_id =
      SemIR::FindStorageArgForInitializer(context.sem_ir(), result_id);
  return {
      .storage_id = storage_arg_id.has_value() ? storage_arg_id : storage_id,
      .init_id = result_id};
}

auto ConvertToValueExpr(Context& context, SemIR::InstId expr_id)
    -> SemIR::InstId {
  return Convert(context, SemIR::LocId(expr_id), expr_id,
                 {.kind = ConversionTarget::Value,
                  .type_id = context.insts().Get(expr_id).type_id()});
}

auto ConvertToValueOrRefExpr(Context& context, SemIR::InstId expr_id)
    -> SemIR::InstId {
  return Convert(context, SemIR::LocId(expr_id), expr_id,
                 {.kind = ConversionTarget::ValueOrRef,
                  .type_id = context.insts().Get(expr_id).type_id()});
}

auto ConvertToValueOfType(Context& context, SemIR::LocId loc_id,
                          SemIR::InstId expr_id, SemIR::TypeId type_id,
                          bool diagnose) -> SemIR::InstId {
  return Convert(context, loc_id, expr_id,
                 {.kind = ConversionTarget::Value,
                  .type_id = type_id,
                  .diagnose = diagnose});
}

auto ConvertToValueOrRefOfType(Context& context, SemIR::LocId loc_id,
                               SemIR::InstId expr_id, SemIR::TypeId type_id)
    -> SemIR::InstId {
  return Convert(context, loc_id, expr_id,
                 {.kind = ConversionTarget::ValueOrRef, .type_id = type_id});
}

// Like ConvertToValueOfType but failure to convert does not result in
// diagnostics. An ErrorInst instruction is still returned on failure.
auto TryConvertToValueOfType(Context& context, SemIR::LocId loc_id,
                             SemIR::InstId expr_id, SemIR::TypeId type_id)
    -> SemIR::InstId {
  return Convert(
      context, loc_id, expr_id,
      {.kind = ConversionTarget::Value, .type_id = type_id, .diagnose = false});
}

auto ConvertToBoolValue(Context& context, SemIR::LocId loc_id,
                        SemIR::InstId value_id) -> SemIR::InstId {
  return ConvertToValueOfType(
      context, loc_id, value_id,
      GetSingletonType(context, SemIR::BoolType::TypeInstId));
}

auto ConvertForExplicitAs(Context& context, Parse::NodeId as_node,
                          SemIR::InstId value_id, SemIR::TypeId type_id,
                          bool unsafe) -> SemIR::InstId {
  return Convert(context, as_node, value_id,
                 {.kind = unsafe ? ConversionTarget::ExplicitUnsafeAs
                                 : ConversionTarget::ExplicitAs,
                  .type_id = type_id});
}

// TODO: Consider moving this to pattern_match.h.
auto ConvertCallArgs(Context& context, SemIR::LocId call_loc_id,
                     SemIR::InstId self_id,
                     llvm::ArrayRef<SemIR::InstId> arg_refs,
                     SemIR::InstId return_arg_id, const SemIR::Function& callee,
                     SemIR::SpecificId callee_specific_id, bool is_desugared)
    -> SemIR::InstBlockId {
  auto param_patterns =
      context.inst_blocks().GetOrEmpty(callee.param_patterns_id);
  auto return_pattern_id = callee.return_pattern_id;

  // The caller should have ensured this callee has the right arity.
  CARBON_CHECK(arg_refs.size() == param_patterns.size());

  if (callee.self_param_id.has_value() && !self_id.has_value()) {
    CARBON_DIAGNOSTIC(MissingObjectInMethodCall, Error,
                      "missing object argument in method call");
    CARBON_DIAGNOSTIC(InCallToFunction, Note, "calling function declared here");
    context.emitter()
        .Build(call_loc_id, MissingObjectInMethodCall)
        .Note(callee.latest_decl_id(), InCallToFunction)
        .Emit();
    self_id = SemIR::ErrorInst::InstId;
  }

  return CallerPatternMatch(context, callee_specific_id, callee.self_param_id,
                            callee.param_patterns_id, return_pattern_id,
                            self_id, arg_refs, return_arg_id, is_desugared);
}

auto TypeExpr::ForUnsugared(Context& context, SemIR::TypeId type_id)
    -> TypeExpr {
  return {.inst_id = context.types().GetTypeInstId(type_id),
          .type_id = type_id};
}

static auto DiagnoseTypeExprEvaluationFailure(Context& context,
                                              SemIR::LocId loc_id) -> void {
  CARBON_DIAGNOSTIC(TypeExprEvaluationFailure, Error,
                    "cannot evaluate type expression");
  context.emitter().Emit(loc_id, TypeExprEvaluationFailure);
}

auto ExprAsType(Context& context, SemIR::LocId loc_id, SemIR::InstId value_id,
                bool diagnose) -> TypeExpr {
  auto type_as_inst_id = ConvertToValueOfType(
      context, loc_id, value_id, SemIR::TypeType::TypeId, diagnose);
  if (type_as_inst_id == SemIR::ErrorInst::InstId) {
    return {.inst_id = SemIR::ErrorInst::TypeInstId,
            .type_id = SemIR::ErrorInst::TypeId};
  }

  auto type_as_const_id = context.constant_values().Get(type_as_inst_id);
  if (!type_as_const_id.is_constant()) {
    if (diagnose) {
      DiagnoseTypeExprEvaluationFailure(context, loc_id);
    }
    return {.inst_id = SemIR::ErrorInst::TypeInstId,
            .type_id = SemIR::ErrorInst::TypeId};
  }

  return {
      .inst_id = context.types().GetAsTypeInstId(type_as_inst_id),
      .type_id = context.types().GetTypeIdForTypeConstantId(type_as_const_id)};
}

auto FormExprAsForm(Context& context, SemIR::LocId loc_id,
                    SemIR::InstId value_id) -> Context::FormExpr {
  auto form_inst_id =
      ConvertToValueOfType(context, loc_id, value_id, SemIR::FormType::TypeId);
  if (form_inst_id == SemIR::ErrorInst::InstId) {
    return Context::FormExpr::Error;
  }

  form_inst_id = HandleAction<SemIR::RefineFormAction>(
      context, loc_id, SemIR::FormType::TypeInstId,
      {.type_id = SemIR::InstType::TypeId, .form_id = form_inst_id});

  auto form_const_id = context.constant_values().Get(form_inst_id);
  if (!form_const_id.is_constant()) {
    CARBON_DIAGNOSTIC(FormExprEvaluationFailure, Error,
                      "cannot evaluate form expression");
    context.emitter().Emit(loc_id, FormExprEvaluationFailure);
    return Context::FormExpr::Error;
  }

  auto type_id = GetTypeComponent(context, form_inst_id);
  auto type_inst_id = context.types().GetTypeInstId(type_id);
  return {.form_inst_id = form_inst_id,
          .type_component_inst_id = type_inst_id,
          .type_component_id = type_id};
}

auto ReturnExprAsForm(Context& context, SemIR::LocId loc_id,
                      SemIR::InstId value_id) -> Context::FormExpr {
  auto form_inst_id = SemIR::InstId::None;
  auto type_inst_id = SemIR::InstId::None;
  if (auto ref_tag = context.insts().TryGetAs<SemIR::RefTagExpr>(value_id)) {
    type_inst_id = ConvertToValueOfType(context, loc_id, ref_tag->expr_id,
                                        SemIR::TypeType::TypeId);
    if (type_inst_id == SemIR::ErrorInst::InstId) {
      return Context::FormExpr::Error;
    }
    if (!context.constant_values().Get(type_inst_id).is_constant()) {
      DiagnoseTypeExprEvaluationFailure(context,
                                        SemIR::LocId(ref_tag->expr_id));
      return Context::FormExpr::Error;
    }
    form_inst_id = AddInst(
        context,
        SemIR::LocIdAndInst::RuntimeVerified(
            context.sem_ir(), loc_id,
            SemIR::RefForm{.type_id = SemIR::FormType::TypeId,
                           .type_component_inst_id =
                               context.types().GetAsTypeInstId(type_inst_id)}));
  } else {
    type_inst_id = ConvertToValueOfType(context, loc_id, value_id,
                                        SemIR::TypeType::TypeId);
    if (type_inst_id == SemIR::ErrorInst::InstId) {
      return Context::FormExpr::Error;
    }
    if (!context.constant_values().Get(type_inst_id).is_constant()) {
      DiagnoseTypeExprEvaluationFailure(context, loc_id);
      return Context::FormExpr::Error;
    }
    form_inst_id = AddInst(
        context, SemIR::LocIdAndInst::RuntimeVerified(
                     context.sem_ir(), loc_id,
                     SemIR::InitForm{
                         .type_id = SemIR::FormType::TypeId,
                         .type_component_inst_id =
                             context.types().GetAsTypeInstId(type_inst_id)}));
  }

  auto type_const_id = context.constant_values().Get(type_inst_id);
  CARBON_CHECK(type_const_id.is_constant());

  return {
      .form_inst_id = form_inst_id,
      .type_component_inst_id = context.types().GetAsTypeInstId(type_inst_id),
      .type_component_id =
          context.types().GetTypeIdForTypeConstantId(type_const_id)};
}

auto DiscardExpr(Context& context, SemIR::InstId expr_id) -> void {
  // If we discard an initializing expression, convert it to a value or
  // reference so that it has something to initialize.
  auto expr = context.insts().Get(expr_id);
  Convert(context, SemIR::LocId(expr_id), expr_id,
          {.kind = ConversionTarget::Discarded, .type_id = expr.type_id()});

  // TODO: This will eventually need to do some "do not discard" analysis.
}

}  // namespace Carbon::Check

// NOLINTEND(misc-no-recursion)
