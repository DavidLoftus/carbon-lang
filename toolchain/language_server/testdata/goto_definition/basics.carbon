// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// AUTOUPDATE
// TIP: To test this file alone, run:
// TIP:   bazel test //toolchain/testing:file_test --test_arg=--file_tests=toolchain/language_server/testdata/goto_definition/basics.carbon
// TIP: To dump output, run:
// TIP:   bazel run //toolchain/testing:file_test -- --dump_output --file_tests=toolchain/language_server/testdata/goto_definition/basics.carbon

// --- STDIN
[[@LSP-CALL:initialize]]
[[@LSP-NOTIFY:textDocument/didOpen:
  "textDocument": {"uri": "file:/fn.carbon", "languageId": "carbon",
                   "text": "namespace Test;\nfn Test.Println() {}\nfn Main() {\n  Test.Println();\n}"}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/fn.carbon"},
  "position": {"line": 3, "character": 6}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/fn.carbon"},
  "position": {"line": 3, "character": 7}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/fn.carbon"},
  "position": {"line": 3, "character": 10}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/fn.carbon"},
  "position": {"line": 3, "character": 14}
]]
[[@LSP-NOTIFY:textDocument/didOpen:
  "textDocument": {"uri": "file:/binary.carbon", "languageId": "carbon",
                   "text": "let var1: i32 = 2;\nlet x: Test = (var1 + var2) + (var3 + var4);"}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/binary.carbon"},
  "position": {"line": 1, "character": 16}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/binary.carbon"},
  "position": {"line": 1, "character": 23}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/binary.carbon"},
  "position": {"line": 1, "character": 32}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/binary.carbon"},
  "position": {"line": 1, "character": 39}
]]
[[@LSP-CALL:shutdown]]
[[@LSP-NOTIFY:exit]]

// --- AUTOUPDATE-SPLIT

// CHECK:STDOUT: Content-Length: 146{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 1,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "capabilities": {
// CHECK:STDOUT:       "documentSymbolProvider": true,
// CHECK:STDOUT:       "textDocumentSync": 1
// CHECK:STDOUT:     }
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 145{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [],
// CHECK:STDOUT:     "uri": "file:///empty.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 49{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 2,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": []
// CHECK:STDOUT: }Content-Length: 147{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [],
// CHECK:STDOUT:     "uri": "file:///invalid.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 49{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 3,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": []
// CHECK:STDOUT: }Content-Length: 142{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [],
// CHECK:STDOUT:     "uri": "file:///fn.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 459{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 4,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": [
// CHECK:STDOUT:     {
// CHECK:STDOUT:       "kind": 12,
// CHECK:STDOUT:       "name": "F",
// CHECK:STDOUT:       "range": {
// CHECK:STDOUT:         "end": {
// CHECK:STDOUT:           "character": 9,
// CHECK:STDOUT:           "line": 0
// CHECK:STDOUT:         },
// CHECK:STDOUT:         "start": {
// CHECK:STDOUT:           "character": 0,
// CHECK:STDOUT:           "line": 0
// CHECK:STDOUT:         }
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "selectionRange": {
// CHECK:STDOUT:         "end": {
// CHECK:STDOUT:           "character": 4,
// CHECK:STDOUT:           "line": 0
// CHECK:STDOUT:         },
// CHECK:STDOUT:         "start": {
// CHECK:STDOUT:           "character": 3,
// CHECK:STDOUT:           "line": 0
// CHECK:STDOUT:         }
// CHECK:STDOUT:       }
// CHECK:STDOUT:     }
// CHECK:STDOUT:   ]
// CHECK:STDOUT: }Content-Length: 51{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 5,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": null
// CHECK:STDOUT: }
