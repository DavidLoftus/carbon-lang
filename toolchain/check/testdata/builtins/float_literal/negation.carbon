// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// INCLUDE-FILE: toolchain/testing/testdata/min_prelude/full.carbon
// EXTRA-ARGS: --expect-full-prelude --exclude-dump-file-prefix=min_prelude/
//
// AUTOUPDATE
// TIP: To test this file alone, run:
// TIP:   bazel test //toolchain/testing:file_test --test_arg=--file_tests=toolchain/check/testdata/builtins/float_literal/negation.carbon
// TIP: To dump output, run:
// TIP:   bazel run //toolchain/testing:file_test -- --dump_output --file_tests=toolchain/check/testdata/builtins/float_literal/negation.carbon

// --- negation.carbon
library "[[@TEST_NAME]]";

//@dump-sem-ir-begin
let ne_val: f64 = -1.0;
let sw_val: f64 = 0.0 - 1.0;
//@dump-sem-ir-end

// CHECK:STDOUT: --- negation.carbon
// CHECK:STDOUT:
// CHECK:STDOUT: constants {
// CHECK:STDOUT:   %int_64: Core.IntLiteral = int_value 64 [concrete]
// CHECK:STDOUT:   %N: Core.IntLiteral = symbolic_binding N, 0 [symbolic]
// CHECK:STDOUT:   %f64.dc1: type = class_type @Float, @Float(%int_64) [concrete]
// CHECK:STDOUT:   %pattern_type.fb7: type = pattern_type %f64.dc1 [concrete]
// CHECK:STDOUT:   %ne_val.patt: %pattern_type.fb7 = value_binding_pattern ne_val [concrete]
// CHECK:STDOUT:   %float.28b5b9.1: Core.FloatLiteral = float_literal_value 10e-1 [concrete]
// CHECK:STDOUT:   %Negate.type: type = facet_type <@Negate> [concrete]
// CHECK:STDOUT:   %Negate.impl_witness: <witness> = impl_witness imports.%Negate.impl_witness_table [concrete]
// CHECK:STDOUT:   %Negate.facet: %Negate.type = facet_value Core.FloatLiteral, (%Negate.impl_witness) [concrete]
// CHECK:STDOUT:   %Negate.WithSelf.Op.type.56a: type = fn_type @Negate.WithSelf.Op, @Negate.WithSelf(%Negate.facet) [concrete]
// CHECK:STDOUT:   %.abc: type = fn_type_with_self_type %Negate.WithSelf.Op.type.56a, %Negate.facet [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.Negate.impl.Op.type: type = fn_type @Core.FloatLiteral.as.Negate.impl.Op [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.Negate.impl.Op: %Core.FloatLiteral.as.Negate.impl.Op.type = struct_value () [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.Negate.impl.Op.bound: <bound method> = bound_method %float.28b5b9.1, %Core.FloatLiteral.as.Negate.impl.Op [concrete]
// CHECK:STDOUT:   %float.184128.1: Core.FloatLiteral = float_literal_value -10e-1 [concrete]
// CHECK:STDOUT:   %ImplicitAs.type.91e: type = facet_type <@ImplicitAs, @ImplicitAs(%f64.dc1)> [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.type.063: type = fn_type @Core.FloatLiteral.as.ImplicitAs.impl.Convert, @Core.FloatLiteral.as.ImplicitAs.impl(%N) [symbolic]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.d4f: %Core.FloatLiteral.as.ImplicitAs.impl.Convert.type.063 = struct_value () [symbolic]
// CHECK:STDOUT:   %ImplicitAs.impl_witness.43c: <witness> = impl_witness imports.%ImplicitAs.impl_witness_table.393, @Core.FloatLiteral.as.ImplicitAs.impl(%int_64) [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.type.ef0: type = fn_type @Core.FloatLiteral.as.ImplicitAs.impl.Convert, @Core.FloatLiteral.as.ImplicitAs.impl(%int_64) [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.cf0: %Core.FloatLiteral.as.ImplicitAs.impl.Convert.type.ef0 = struct_value () [concrete]
// CHECK:STDOUT:   %ImplicitAs.facet: %ImplicitAs.type.91e = facet_value Core.FloatLiteral, (%ImplicitAs.impl_witness.43c) [concrete]
// CHECK:STDOUT:   %ImplicitAs.WithSelf.Convert.type.6c5: type = fn_type @ImplicitAs.WithSelf.Convert, @ImplicitAs.WithSelf(%f64.dc1, %ImplicitAs.facet) [concrete]
// CHECK:STDOUT:   %.2d7: type = fn_type_with_self_type %ImplicitAs.WithSelf.Convert.type.6c5, %ImplicitAs.facet [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.bound.c4961f.1: <bound method> = bound_method %float.184128.1, %Core.FloatLiteral.as.ImplicitAs.impl.Convert.cf0 [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.specific_fn: <specific function> = specific_function %Core.FloatLiteral.as.ImplicitAs.impl.Convert.cf0, @Core.FloatLiteral.as.ImplicitAs.impl.Convert(%int_64) [concrete]
// CHECK:STDOUT:   %bound_method.820037.1: <bound method> = bound_method %float.184128.1, %Core.FloatLiteral.as.ImplicitAs.impl.Convert.specific_fn [concrete]
// CHECK:STDOUT:   %float.b2f: %f64.dc1 = float_value -1 [concrete]
// CHECK:STDOUT:   %sw_val.patt: %pattern_type.fb7 = value_binding_pattern sw_val [concrete]
// CHECK:STDOUT:   %float.e3b: Core.FloatLiteral = float_literal_value 0e-1 [concrete]
// CHECK:STDOUT:   %float.28b5b9.2: Core.FloatLiteral = float_literal_value 10e-1 [concrete]
// CHECK:STDOUT:   %SubWith.type.98a: type = facet_type <@SubWith, @SubWith(Core.FloatLiteral)> [concrete]
// CHECK:STDOUT:   %SubWith.impl_witness: <witness> = impl_witness imports.%SubWith.impl_witness_table [concrete]
// CHECK:STDOUT:   %SubWith.facet: %SubWith.type.98a = facet_value Core.FloatLiteral, (%SubWith.impl_witness) [concrete]
// CHECK:STDOUT:   %SubWith.WithSelf.Op.type.0aa: type = fn_type @SubWith.WithSelf.Op, @SubWith.WithSelf(Core.FloatLiteral, %SubWith.facet) [concrete]
// CHECK:STDOUT:   %.e7f: type = fn_type_with_self_type %SubWith.WithSelf.Op.type.0aa, %SubWith.facet [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.SubWith.impl.Op.type: type = fn_type @Core.FloatLiteral.as.SubWith.impl.Op [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.SubWith.impl.Op: %Core.FloatLiteral.as.SubWith.impl.Op.type = struct_value () [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.SubWith.impl.Op.bound: <bound method> = bound_method %float.e3b, %Core.FloatLiteral.as.SubWith.impl.Op [concrete]
// CHECK:STDOUT:   %float.184128.2: Core.FloatLiteral = float_literal_value -10e-1 [concrete]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.bound.c4961f.2: <bound method> = bound_method %float.184128.2, %Core.FloatLiteral.as.ImplicitAs.impl.Convert.cf0 [concrete]
// CHECK:STDOUT:   %bound_method.820037.2: <bound method> = bound_method %float.184128.2, %Core.FloatLiteral.as.ImplicitAs.impl.Convert.specific_fn [concrete]
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: imports {
// CHECK:STDOUT:   %Core.import_ref.b1e03b.1 = import_ref Core//prelude/operators/arithmetic, loc{{\d+_\d+}}, unloaded
// CHECK:STDOUT:   %Core.import_ref.9c3475.1: %Core.FloatLiteral.as.Negate.impl.Op.type = import_ref Core//prelude/operators/arithmetic, loc{{\d+_\d+}}, loaded [concrete = constants.%Core.FloatLiteral.as.Negate.impl.Op]
// CHECK:STDOUT:   %Negate.impl_witness_table = impl_witness_table (%Core.import_ref.b1e03b.1, %Core.import_ref.9c3475.1), @Core.FloatLiteral.as.Negate.impl [concrete]
// CHECK:STDOUT:   %Core.import_ref.c4c: @Core.FloatLiteral.as.ImplicitAs.impl.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.type (%Core.FloatLiteral.as.ImplicitAs.impl.Convert.type.063) = import_ref Core//prelude/types/float, loc{{\d+_\d+}}, loaded [symbolic = @Core.FloatLiteral.as.ImplicitAs.impl.%Core.FloatLiteral.as.ImplicitAs.impl.Convert (constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.d4f)]
// CHECK:STDOUT:   %ImplicitAs.impl_witness_table.393 = impl_witness_table (%Core.import_ref.c4c), @Core.FloatLiteral.as.ImplicitAs.impl [concrete]
// CHECK:STDOUT:   %Core.import_ref.b1e03b.2 = import_ref Core//prelude/operators/arithmetic, loc{{\d+_\d+}}, unloaded
// CHECK:STDOUT:   %Core.import_ref.9c3475.2: %Core.FloatLiteral.as.SubWith.impl.Op.type = import_ref Core//prelude/operators/arithmetic, loc{{\d+_\d+}}, loaded [concrete = constants.%Core.FloatLiteral.as.SubWith.impl.Op]
// CHECK:STDOUT:   %SubWith.impl_witness_table = impl_witness_table (%Core.import_ref.b1e03b.2, %Core.import_ref.9c3475.2), @Core.FloatLiteral.as.SubWith.impl [concrete]
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: file {
// CHECK:STDOUT:   name_binding_decl {
// CHECK:STDOUT:     %ne_val.patt: %pattern_type.fb7 = value_binding_pattern ne_val [concrete = constants.%ne_val.patt]
// CHECK:STDOUT:   }
// CHECK:STDOUT:   %impl.elem0.loc4: %.2d7 = impl_witness_access constants.%ImplicitAs.impl_witness.43c, element0 [concrete = constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.cf0]
// CHECK:STDOUT:   %bound_method.loc4_19.1: <bound method> = bound_method @__global_init.%Core.FloatLiteral.as.Negate.impl.Op.call, %impl.elem0.loc4 [concrete = constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.bound.c4961f.1]
// CHECK:STDOUT:   %specific_fn.loc4: <specific function> = specific_function %impl.elem0.loc4, @Core.FloatLiteral.as.ImplicitAs.impl.Convert(constants.%int_64) [concrete = constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.specific_fn]
// CHECK:STDOUT:   %bound_method.loc4_19.2: <bound method> = bound_method @__global_init.%Core.FloatLiteral.as.Negate.impl.Op.call, %specific_fn.loc4 [concrete = constants.%bound_method.820037.1]
// CHECK:STDOUT:   %.loc4_19.1: Core.FloatLiteral = value_of_initializer @__global_init.%Core.FloatLiteral.as.Negate.impl.Op.call [concrete = constants.%float.184128.1]
// CHECK:STDOUT:   %.loc4_19.2: Core.FloatLiteral = converted @__global_init.%Core.FloatLiteral.as.Negate.impl.Op.call, %.loc4_19.1 [concrete = constants.%float.184128.1]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.call.loc4: init %f64.dc1 = call %bound_method.loc4_19.2(%.loc4_19.2) [concrete = constants.%float.b2f]
// CHECK:STDOUT:   %.loc4_19.3: %f64.dc1 = value_of_initializer %Core.FloatLiteral.as.ImplicitAs.impl.Convert.call.loc4 [concrete = constants.%float.b2f]
// CHECK:STDOUT:   %.loc4_19.4: %f64.dc1 = converted @__global_init.%Core.FloatLiteral.as.Negate.impl.Op.call, %.loc4_19.3 [concrete = constants.%float.b2f]
// CHECK:STDOUT:   %f64.loc4: type = type_literal constants.%f64.dc1 [concrete = constants.%f64.dc1]
// CHECK:STDOUT:   %ne_val: %f64.dc1 = value_binding ne_val, %.loc4_19.4
// CHECK:STDOUT:   name_binding_decl {
// CHECK:STDOUT:     %sw_val.patt: %pattern_type.fb7 = value_binding_pattern sw_val [concrete = constants.%sw_val.patt]
// CHECK:STDOUT:   }
// CHECK:STDOUT:   %impl.elem0.loc5: %.2d7 = impl_witness_access constants.%ImplicitAs.impl_witness.43c, element0 [concrete = constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.cf0]
// CHECK:STDOUT:   %bound_method.loc5_23.1: <bound method> = bound_method @__global_init.%Core.FloatLiteral.as.SubWith.impl.Op.call, %impl.elem0.loc5 [concrete = constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.bound.c4961f.2]
// CHECK:STDOUT:   %specific_fn.loc5: <specific function> = specific_function %impl.elem0.loc5, @Core.FloatLiteral.as.ImplicitAs.impl.Convert(constants.%int_64) [concrete = constants.%Core.FloatLiteral.as.ImplicitAs.impl.Convert.specific_fn]
// CHECK:STDOUT:   %bound_method.loc5_23.2: <bound method> = bound_method @__global_init.%Core.FloatLiteral.as.SubWith.impl.Op.call, %specific_fn.loc5 [concrete = constants.%bound_method.820037.2]
// CHECK:STDOUT:   %.loc5_23.1: Core.FloatLiteral = value_of_initializer @__global_init.%Core.FloatLiteral.as.SubWith.impl.Op.call [concrete = constants.%float.184128.2]
// CHECK:STDOUT:   %.loc5_23.2: Core.FloatLiteral = converted @__global_init.%Core.FloatLiteral.as.SubWith.impl.Op.call, %.loc5_23.1 [concrete = constants.%float.184128.2]
// CHECK:STDOUT:   %Core.FloatLiteral.as.ImplicitAs.impl.Convert.call.loc5: init %f64.dc1 = call %bound_method.loc5_23.2(%.loc5_23.2) [concrete = constants.%float.b2f]
// CHECK:STDOUT:   %.loc5_23.3: %f64.dc1 = value_of_initializer %Core.FloatLiteral.as.ImplicitAs.impl.Convert.call.loc5 [concrete = constants.%float.b2f]
// CHECK:STDOUT:   %.loc5_23.4: %f64.dc1 = converted @__global_init.%Core.FloatLiteral.as.SubWith.impl.Op.call, %.loc5_23.3 [concrete = constants.%float.b2f]
// CHECK:STDOUT:   %f64.loc5: type = type_literal constants.%f64.dc1 [concrete = constants.%f64.dc1]
// CHECK:STDOUT:   %sw_val: %f64.dc1 = value_binding sw_val, %.loc5_23.4
// CHECK:STDOUT: }
// CHECK:STDOUT:
// CHECK:STDOUT: fn @__global_init() {
// CHECK:STDOUT: !entry:
// CHECK:STDOUT:   %float.loc4: Core.FloatLiteral = float_literal_value 10e-1 [concrete = constants.%float.28b5b9.1]
// CHECK:STDOUT:   %impl.elem1.loc4: %.abc = impl_witness_access constants.%Negate.impl_witness, element1 [concrete = constants.%Core.FloatLiteral.as.Negate.impl.Op]
// CHECK:STDOUT:   %bound_method.loc4: <bound method> = bound_method %float.loc4, %impl.elem1.loc4 [concrete = constants.%Core.FloatLiteral.as.Negate.impl.Op.bound]
// CHECK:STDOUT:   %Core.FloatLiteral.as.Negate.impl.Op.call: init Core.FloatLiteral = call %bound_method.loc4(%float.loc4) [concrete = constants.%float.184128.1]
// CHECK:STDOUT:   %float.loc5_19: Core.FloatLiteral = float_literal_value 0e-1 [concrete = constants.%float.e3b]
// CHECK:STDOUT:   %float.loc5_25: Core.FloatLiteral = float_literal_value 10e-1 [concrete = constants.%float.28b5b9.2]
// CHECK:STDOUT:   %impl.elem1.loc5: %.e7f = impl_witness_access constants.%SubWith.impl_witness, element1 [concrete = constants.%Core.FloatLiteral.as.SubWith.impl.Op]
// CHECK:STDOUT:   %bound_method.loc5: <bound method> = bound_method %float.loc5_19, %impl.elem1.loc5 [concrete = constants.%Core.FloatLiteral.as.SubWith.impl.Op.bound]
// CHECK:STDOUT:   %Core.FloatLiteral.as.SubWith.impl.Op.call: init Core.FloatLiteral = call %bound_method.loc5(%float.loc5_19, %float.loc5_25) [concrete = constants.%float.184128.2]
// CHECK:STDOUT:   <elided>
// CHECK:STDOUT: }
// CHECK:STDOUT:
