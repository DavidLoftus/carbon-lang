// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

//@include-in-dumps

import Core library "io";

class A {
  var i: i32;
}
class B {
  var i: i32;
}


//@dump-sem-ir-begin
impl A as Core.ImplicitAs(B) {
  fn Convert[self: Self]() -> B {
    return {.i = self.i + 1};
  }
}
fn MyConvert(a: (A,A)) -> (B,B) {
  return a;
}
//@dump-sem-ir-end


fn Run() -> i32 {
  let b: (B,B) = MyConvert(({.i = 0},{.i = 2}));
  Core.Print(b.0.i);
  Core.Print(b.1.i);
  return 0;
}
