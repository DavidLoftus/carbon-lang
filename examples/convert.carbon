// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import Core library "io";

private fn CanBuiltinConvertTo(T: type) -> type = "type.can_builtin_convert_to";

impl forall [T:! type, U:! CanBuiltinConvertTo(T)] T as Core.ImplicitAs(U) {
    fn Convert[self: Self]() -> U = "type.convert";
}

//@dump-sem-ir-begin

fn ForceConvert(T:! type, value: T) {}

fn Convert(T:! Core.Destroy, U:! Core.ImplicitAs(T), value: U) {
    ForceConvert(T, value);
}

interface I {
  require Self impls Core.Destroy;
  let X:! type;
}

fn PrintArray(a: array(i32, 2)) {
    if (a[0] == 0) {
        Core.Print('0');
    } else {
        Core.Print('1');
    }

    if (a[1] == 1) {
        Core.Print('1');
    } else {
        Core.Print('0');
    }
}

fn F[T:! Core.ImplicitAs(array(i32, 2))](i: T) {
    PrintArray(i);
}

fn Run() -> i32 {
    F((0, 1));
    return 0;
}

//@dump-sem-ir-end
