// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// AUTOUPDATE
// TIP: To test this file alone, run:
// TIP:   bazel test //toolchain/testing:file_test --test_arg=--file_tests=toolchain/language_server/testdata/goto_definition/basics.carbon
// TIP: To dump output, run:
// TIP:   bazel run //toolchain/testing:file_test -- --dump_output --file_tests=toolchain/language_server/testdata/goto_definition/basics.carbon

// --- fn.carbon
namespace Test;
fn Test.Println() {}
fn Main() {
  Test.Println();
}

// --- STDIN
[[@LSP-CALL:initialize]]
[[@LSP-NOTIFY:textDocument/didOpen:
  "textDocument": {
    "uri": "file:/fn.carbon",
    "languageId": "carbon",
    "text": "FROM_FILE_SPLIT"
  }
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
// CHECK:STDOUT: }Content-Length: 142{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [],
// CHECK:STDOUT:     "uri": "file:///fn.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 239{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 2,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 15,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 0,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///fn.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 239{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 3,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 20,
// CHECK:STDOUT:         "line": 1
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 0,
// CHECK:STDOUT:         "line": 1
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///fn.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 239{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 4,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 20,
// CHECK:STDOUT:         "line": 1
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 0,
// CHECK:STDOUT:         "line": 1
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///fn.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 239{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 5,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 20,
// CHECK:STDOUT:         "line": 1
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 0,
// CHECK:STDOUT:         "line": 1
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///fn.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 143{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [],
// CHECK:STDOUT:     "uri": "file:///ptr.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 51{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 6,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": null
// CHECK:STDOUT: }Content-Length: 51{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 7,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": null
// CHECK:STDOUT: }
