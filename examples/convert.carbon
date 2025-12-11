// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import Core library "io";

private fn CanBuiltinConvertTo(T: type) -> type = "type.can_builtin_convert_to";

impl forall [T:! type, U:! CanBuiltinConvertTo(T)] T as Core.ImplicitAs(U) {
    fn Convert[self: Self]() -> U = "type.convert";
}

//@dump-sem-ir-begin

class C { var a: i32; }

fn MakeC(x: {.a: i32}) -> C {
    // Expands to:
    return x;
}

fn GenericMakeC[T:! Core.ImplicitAs(C)](x: T) -> C {
    // Expands to x.(Core.ImplicitAs(C).As)()
    return x;
}

fn Run() -> i32 {
    MakeC({.a = 0});
    GenericMakeC({.a = 0});
    return 0;
}

//@dump-sem-ir-end
