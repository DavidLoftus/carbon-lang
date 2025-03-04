// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// AUTOUPDATE
// TIP: To test this file alone, run:
// TIP:   bazel test //toolchain/testing:file_test --test_arg=--file_tests=toolchain/language_server/testdata/goto_definition/prefix.carbon
// TIP: To dump output, run:
// TIP:   bazel run //toolchain/testing:file_test -- --dump_output --file_tests=toolchain/language_server/testdata/goto_definition/prefix.carbon

// --- prefix_amp.carbon
var x: ();
let y: ()* = &x;

// --- if_expr.carbon
var (x: (), y: (), z: ());
fn Foo() {
  let a: () = if x then y else z;
}

// --- STDIN
[[@LSP-CALL:initialize]]
[[@LSP-NOTIFY:textDocument/didOpen:
  "textDocument": {
    "uri": "file:/prefix_amp.carbon",
    "languageId": "carbon",
    "text": "FROM_FILE_SPLIT"
  }
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/prefix_amp.carbon"},
  "position": {"line": 1, "character": 14}
]]
[[@LSP-NOTIFY:textDocument/didOpen:
  "textDocument": {
    "uri": "file:/if_expr.carbon",
    "languageId": "carbon",
    "text": "FROM_FILE_SPLIT"
  }
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/if_expr.carbon"},
  "position": {"line": 2, "character": 17}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/if_expr.carbon"},
  "position": {"line": 2, "character": 24}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/if_expr.carbon"},
  "position": {"line": 2, "character": 31}
]]
[[@LSP-CALL:shutdown]]
[[@LSP-NOTIFY:exit]]

// --- AUTOUPDATE-SPLIT

// CHECK:STDOUT: Content-Length: 180{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 1,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "capabilities": {
// CHECK:STDOUT:       "definitionProvider": true,
// CHECK:STDOUT:       "documentSymbolProvider": true,
// CHECK:STDOUT:       "textDocumentSync": 2
// CHECK:STDOUT:     }
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 150{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [],
// CHECK:STDOUT:     "uri": "file:///prefix_amp.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 246{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 2,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 5,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 4,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///prefix_amp.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 512{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [
// CHECK:STDOUT:       {
// CHECK:STDOUT:         "message": "`Core.ImplicitAs` implicitly referenced here, but package `Core` not found",
// CHECK:STDOUT:         "range": {
// CHECK:STDOUT:           "end": {
// CHECK:STDOUT:             "character": 19,
// CHECK:STDOUT:             "line": 3
// CHECK:STDOUT:           },
// CHECK:STDOUT:           "start": {
// CHECK:STDOUT:             "character": 14,
// CHECK:STDOUT:             "line": 2
// CHECK:STDOUT:           }
// CHECK:STDOUT:         },
// CHECK:STDOUT:         "severity": 1,
// CHECK:STDOUT:         "source": "carbon"
// CHECK:STDOUT:       }
// CHECK:STDOUT:     ],
// CHECK:STDOUT:     "uri": "file:///if_expr.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 243{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 3,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 6,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 5,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///if_expr.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 245{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 4,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 13,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 12,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///if_expr.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 245{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 5,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 20,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 19,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///if_expr.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 51{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 6,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": null
// CHECK:STDOUT: }
