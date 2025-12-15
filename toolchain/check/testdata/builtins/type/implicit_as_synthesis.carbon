// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// INCLUDE-FILE: toolchain/testing/testdata/min_prelude/convert.carbon
//
// AUTOUPDATE
// TIP: To test this file alone, run:
// TIP:   bazel test //toolchain/testing:file_test --test_arg=--file_tests=toolchain/check/testdata/builtins/type/implicit_as_synthesis.carbon
// TIP: To dump output, run:
// TIP:   bazel run //toolchain/testing:file_test -- --dump_output --file_tests=toolchain/check/testdata/builtins/type/implicit_as_synthesis.carbon


// This test checks that `ImplicitAs` is synthesized for builtin conversions.
// We do NOT define `impl forall ... ImplicitAs`.

class A {}
class B {}

impl A as Core.ImplicitAs(B) {
  fn Convert[self: Self]() -> B {
    return {};
  }
}


//@dump-sem-ir-begin
fn Test(a: (A,A)) -> (B,B) {
  return a.(Core.ImplicitAs((B,B)).Convert)();
  // return ({},{});
}
//@dump-sem-ir-end

// CHECK:STDOUT: --- implicit_as_synthesis.carbon
// CHECK:STDOUT:
// CHECK:STDOUT: constants {
// CHECK:STDOUT:   %A: type = class_type @A [concrete]
// CHECK:STDOUT:   %B: type = class_type @B [concrete]
// CHECK:STDOUT:   %empty_tuple.type: type = tuple_type () [concrete]
// CHECK:STDOUT:   %ImplicitAs.type.cc7: type = generic_interface_type @ImplicitAs [concrete]
// CHECK:STDOUT:   %ImplicitAs.generic: %ImplicitAs.type.cc7 = struct_value () [concrete]
// CHECK:STDOUT:   %Dest: type = symbolic_binding Dest, 0 [symbolic]
// CHECK:STDOUT:   %ImplicitAs.assoc_type.ff3: type = assoc_entity_type @ImplicitAs, @ImplicitAs(%Dest) [symbolic]
// CHECK:STDOUT:   %ImplicitAs.Convert.type.6d3: type = fn_type @ImplicitAs.Convert, @ImplicitAs(%Dest) [symbolic]
// CHECK:STDOUT:   %ImplicitAs.Convert.50f: %ImplicitAs.Convert.type.6d3 = struct_value () [symbolic]
// CHECK:STDOUT:   %tuple.type.24b: type = tuple_type (type, type) [concrete]
// CHECK:STDOUT:   %tuple.4a6: %tuple.type.24b = tuple_value (%A, %A) [concrete]
// CHECK:STDOUT:   %tuple.type.718: type = tuple_type (%A, %A) [concrete]
// CHECK:STDOUT:   %pattern_type.8bd: type = pattern_type %tuple.type.718 [concrete]
// CHECK:STDOUT:   %tuple.761: %tuple.type.24b = tuple_value (%B, %B) [concrete]
// CHECK:STDOUT:   %tuple.type.a47: type = tuple_type (%B, %B) [concrete]
// CHECK:STDOUT:   %pattern_type.965: type = pattern_type %tuple.type.a47 [concrete]
// CHECK:STDOUT:   %Test.type: type = fn_type @Test [concrete]
// CHECK:STDOUT:   %Test: %Test.type = struct_value () [concrete]
// CHECK:STDOUT:   %ImplicitAs.type.376: type = facet_type <@ImplicitAs, @ImplicitAs(%tuple.type.a47)> [concrete]
// CHECK:STDOUT:   %ImplicitAs.Convert.type.eae: type = fn_type @ImplicitAs.Convert, @ImplicitAs(%tuple.type.a47) [concrete]
// CHECK:STDOUT:   %ImplicitAs.assoc_type.049: type = assoc_entity_type @ImplicitAs, @ImplicitAs(%tuple.type.a47) [concrete]
// CHECK:STDOUT:   %assoc0.238: %ImplicitAs.assoc_type.049 = assoc_entity element0, imports.%Core.import_ref.d3c [concrete]
// CHECK:STDOUT:   %assoc0.df2: %ImplicitAs.assoc_type.ff3 = assoc_entity element0, imports.%Core.import_ref.a08 [symbolic]
// CHECK:STDOUT:   %Convert.thunk.type: type = fn_type @Convert.thunk [concrete]
// CHECK:STDOUT:   %Convert.thunk: %Convert.thunk.type = struct_value () [concrete]
// CHECK:STDOUT:   %cpp_witness: <witness> = cpp_witness (%Convert.thunk) [concrete]
// CHECK:STDOUT:   %ImplicitAs.facet.0d0: %ImplicitAs.type.376 = facet_value %tuple.type.718, (%cpp_witness) [concrete]
// CHECK:STDOUT:   %.682: type = fn_type_with_self_type %ImplicitAs.Convert.type.eae, %ImplicitAs.facet.0d0 [concrete]
// CHECK:STDOUT:   %Destroy.type: type = facet_type <@Destroy> [concrete]
// CHECK:STDOUT:   %type_where: type = facet_type <type where .Self impls <CanDestroy>> [concrete]
// CHECK:STDOUT:   %facet_value: %type_where = facet_value %B, () [concrete]
// CHECK:STDOUT:   %DestroyT.binding.as_type.as.Destroy.impl.Op.type.3bf: type = fn_type @DestroyT.binding.as_type.as.Destroy.impl.Op, @DestroyT.binding.as_type.as.Destroy.impl(%facet_value) [concrete]
// CHECK:STDOUT:   %DestroyT.binding.as_type.as.Destroy.impl.Op.11d: %DestroyT.binding.as_type.as.Destroy.impl.Op.type.3bf = struct_value () [concrete]
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: imports {
// CHECK:STDOUT:   %Core: <namespace> = namespace file.%Core.import, [concrete] {
// CHECK:STDOUT:     .ImplicitAs = %Core.ImplicitAs
// CHECK:STDOUT:     .Destroy = %Core.Destroy
// CHECK:STDOUT:     import Core//prelude
// CHECK:STDOUT:     import Core//prelude/...
// CHECK:STDOUT:   }
// CHECK:STDOUT:   %Core.ImplicitAs: %ImplicitAs.type.cc7 = import_ref Core//prelude/parts/as, ImplicitAs, loaded [concrete = constants.%ImplicitAs.generic]
// CHECK:STDOUT:   %Core.import_ref.c18: @ImplicitAs.%ImplicitAs.assoc_type (%ImplicitAs.assoc_type.ff3) = import_ref Core//prelude/parts/as, loc{{\d+_\d+}}, loaded [symbolic = @ImplicitAs.%assoc0 (constants.%assoc0.df2)]
// CHECK:STDOUT:   %Core.import_ref.d3c: @ImplicitAs.%ImplicitAs.Convert.type (%ImplicitAs.Convert.type.6d3) = import_ref Core//prelude/parts/as, loc{{\d+_\d+}}, loaded [symbolic = @ImplicitAs.%ImplicitAs.Convert (constants.%ImplicitAs.Convert.50f)]
// CHECK:STDOUT:   %Core.import_ref.a08 = import_ref Core//prelude/parts/as, loc{{\d+_\d+}}, unloaded
// CHECK:STDOUT:   %Core.Destroy: type = import_ref Core//prelude/parts/destroy, Destroy, loaded [concrete = constants.%Destroy.type]
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: file {
// CHECK:STDOUT:   %Test.decl: %Test.type = fn_decl @Test [concrete = constants.%Test] {
// CHECK:STDOUT:     %a.patt: %pattern_type.8bd = value_binding_pattern a [concrete]
// CHECK:STDOUT:     %a.param_patt: %pattern_type.8bd = value_param_pattern %a.patt, call_param0 [concrete]
// CHECK:STDOUT:     %return.patt: %pattern_type.965 = return_slot_pattern [concrete]
// CHECK:STDOUT:     %return.param_patt: %pattern_type.965 = out_param_pattern %return.patt, call_param1 [concrete]
// CHECK:STDOUT:   } {
// CHECK:STDOUT:     %B.ref.loc28_23: type = name_ref B, file.%B.decl [concrete = constants.%B]
// CHECK:STDOUT:     %B.ref.loc28_25: type = name_ref B, file.%B.decl [concrete = constants.%B]
// CHECK:STDOUT:     %.loc28_26.1: %tuple.type.24b = tuple_literal (%B.ref.loc28_23, %B.ref.loc28_25) [concrete = constants.%tuple.761]
// CHECK:STDOUT:     %.loc28_26.2: type = converted %.loc28_26.1, constants.%tuple.type.a47 [concrete = constants.%tuple.type.a47]
// CHECK:STDOUT:     %a.param: %tuple.type.718 = value_param call_param0
// CHECK:STDOUT:     %.loc28_16.1: type = splice_block %.loc28_16.3 [concrete = constants.%tuple.type.718] {
// CHECK:STDOUT:       %A.ref.loc28_13: type = name_ref A, file.%A.decl [concrete = constants.%A]
// CHECK:STDOUT:       %A.ref.loc28_15: type = name_ref A, file.%A.decl [concrete = constants.%A]
// CHECK:STDOUT:       %.loc28_16.2: %tuple.type.24b = tuple_literal (%A.ref.loc28_13, %A.ref.loc28_15) [concrete = constants.%tuple.4a6]
// CHECK:STDOUT:       %.loc28_16.3: type = converted %.loc28_16.2, constants.%tuple.type.718 [concrete = constants.%tuple.type.718]
// CHECK:STDOUT:     }
// CHECK:STDOUT:     %a: %tuple.type.718 = value_binding a, %a.param
// CHECK:STDOUT:     %return.param: ref %tuple.type.a47 = out_param call_param1
// CHECK:STDOUT:     %return: ref %tuple.type.a47 = return_slot %return.param
// CHECK:STDOUT:   }
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: fn @Test(%a.param: %tuple.type.718) -> %return.param: %tuple.type.a47 {
// CHECK:STDOUT: !entry:
// CHECK:STDOUT:   %a.ref: %tuple.type.718 = name_ref a, %a
// CHECK:STDOUT:   %Core.ref: <namespace> = name_ref Core, imports.%Core [concrete = imports.%Core]
// CHECK:STDOUT:   %ImplicitAs.ref: %ImplicitAs.type.cc7 = name_ref ImplicitAs, imports.%Core.ImplicitAs [concrete = constants.%ImplicitAs.generic]
// CHECK:STDOUT:   %B.ref.loc29_30: type = name_ref B, file.%B.decl [concrete = constants.%B]
// CHECK:STDOUT:   %B.ref.loc29_32: type = name_ref B, file.%B.decl [concrete = constants.%B]
// CHECK:STDOUT:   %.loc29_33: %tuple.type.24b = tuple_literal (%B.ref.loc29_30, %B.ref.loc29_32) [concrete = constants.%tuple.761]
// CHECK:STDOUT:   %.loc29_34: type = converted %.loc29_33, constants.%tuple.type.a47 [concrete = constants.%tuple.type.a47]
// CHECK:STDOUT:   %ImplicitAs.type: type = facet_type <@ImplicitAs, @ImplicitAs(constants.%tuple.type.a47)> [concrete = constants.%ImplicitAs.type.376]
// CHECK:STDOUT:   %.loc29_35: %ImplicitAs.assoc_type.049 = specific_constant imports.%Core.import_ref.c18, @ImplicitAs(constants.%tuple.type.a47) [concrete = constants.%assoc0.238]
// CHECK:STDOUT:   %Convert.ref: %ImplicitAs.assoc_type.049 = name_ref Convert, %.loc29_35 [concrete = constants.%assoc0.238]
// CHECK:STDOUT:   <elided>
// CHECK:STDOUT:   %impl.elem0: %.682 = impl_witness_access constants.%cpp_witness, element0 [concrete = constants.%Convert.thunk]
// CHECK:STDOUT:   %bound_method.loc29_11.1: <bound method> = bound_method %a.ref, %impl.elem0
// CHECK:STDOUT:   %.loc28_19: ref %tuple.type.a47 = splice_block %return {}
// CHECK:STDOUT:   %Convert.thunk.call: init %tuple.type.a47 = call %bound_method.loc29_11.1(%a.ref) to %.loc28_19
// CHECK:STDOUT:   %DestroyT.binding.as_type.as.Destroy.impl.Op.bound.loc29_11.1: <bound method> = bound_method @Convert.thunk.%.loc29_11.7, constants.%DestroyT.binding.as_type.as.Destroy.impl.Op.11d
// CHECK:STDOUT:   <elided>
// CHECK:STDOUT:   %bound_method.loc29_11.2: <bound method> = bound_method @Convert.thunk.%.loc29_11.7, %DestroyT.binding.as_type.as.Destroy.impl.Op.specific_fn.1
// CHECK:STDOUT:   %DestroyT.binding.as_type.as.Destroy.impl.Op.call.loc29_11.1: init %empty_tuple.type = call %bound_method.loc29_11.2(@Convert.thunk.%.loc29_11.7)
// CHECK:STDOUT:   %DestroyT.binding.as_type.as.Destroy.impl.Op.bound.loc29_11.2: <bound method> = bound_method @Convert.thunk.%.loc29_11.3, constants.%DestroyT.binding.as_type.as.Destroy.impl.Op.11d
// CHECK:STDOUT:   <elided>
// CHECK:STDOUT:   %bound_method.loc29_11.3: <bound method> = bound_method @Convert.thunk.%.loc29_11.3, %DestroyT.binding.as_type.as.Destroy.impl.Op.specific_fn.2
// CHECK:STDOUT:   %DestroyT.binding.as_type.as.Destroy.impl.Op.call.loc29_11.2: init %empty_tuple.type = call %bound_method.loc29_11.3(@Convert.thunk.%.loc29_11.3)
// CHECK:STDOUT:   return %Convert.thunk.call to %return
// CHECK:STDOUT: }
// CHECK:STDOUT:
