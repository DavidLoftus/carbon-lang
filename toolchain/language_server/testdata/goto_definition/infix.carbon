// Part of the Carbon Language project, under the Apache License v2.0 with LLVM
// Exceptions. See /LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// AUTOUPDATE
// TIP: To test this file alone, run:
// TIP:   bazel test //toolchain/testing:file_test --test_arg=--file_tests=toolchain/language_server/testdata/goto_definition/infix.carbon
// TIP: To dump output, run:
// TIP:   bazel run //toolchain/testing:file_test -- --dump_output --file_tests=toolchain/language_server/testdata/goto_definition/infix.carbon

// --- STDIN
[[@LSP-CALL:initialize]]
[[@LSP-NOTIFY:textDocument/didOpen:
  "textDocument": {"uri": "file:/infix.carbon", "languageId": "carbon",
                   "text": "var (var1: (), var2: (), var3: (), var4: ());\nlet xyz: () = (var1 + var2) + (var3 + var4);"}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/infix.carbon"},
  "position": {"line": 1, "character": 16}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/infix.carbon"},
  "position": {"line": 1, "character": 23}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/infix.carbon"},
  "position": {"line": 1, "character": 32}
]]
[[@LSP-CALL:textDocument/definition:
  "textDocument": {"uri": "file:/infix.carbon"},
  "position": {"line": 1, "character": 39}
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
// CHECK:STDOUT: }Content-Length: 1211{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "method": "textDocument/publishDiagnostics",
// CHECK:STDOUT:   "params": {
// CHECK:STDOUT:     "diagnostics": [
// CHECK:STDOUT:       {
// CHECK:STDOUT:         "message": "`Core.Add` implicitly referenced here, but package `Core` not found",
// CHECK:STDOUT:         "range": {
// CHECK:STDOUT:           "end": {
// CHECK:STDOUT:             "character": 27,
// CHECK:STDOUT:             "line": 2
// CHECK:STDOUT:           },
// CHECK:STDOUT:           "start": {
// CHECK:STDOUT:             "character": 15,
// CHECK:STDOUT:             "line": 1
// CHECK:STDOUT:           }
// CHECK:STDOUT:         },
// CHECK:STDOUT:         "severity": 1,
// CHECK:STDOUT:         "source": "carbon"
// CHECK:STDOUT:       },
// CHECK:STDOUT:       {
// CHECK:STDOUT:         "message": "`Core.Add` implicitly referenced here, but package `Core` not found",
// CHECK:STDOUT:         "range": {
// CHECK:STDOUT:           "end": {
// CHECK:STDOUT:             "character": 43,
// CHECK:STDOUT:             "line": 2
// CHECK:STDOUT:           },
// CHECK:STDOUT:           "start": {
// CHECK:STDOUT:             "character": 31,
// CHECK:STDOUT:             "line": 1
// CHECK:STDOUT:           }
// CHECK:STDOUT:         },
// CHECK:STDOUT:         "severity": 1,
// CHECK:STDOUT:         "source": "carbon"
// CHECK:STDOUT:       },
// CHECK:STDOUT:       {
// CHECK:STDOUT:         "message": "`Core.Add` implicitly referenced here, but package `Core` not found",
// CHECK:STDOUT:         "range": {
// CHECK:STDOUT:           "end": {
// CHECK:STDOUT:             "character": 44,
// CHECK:STDOUT:             "line": 2
// CHECK:STDOUT:           },
// CHECK:STDOUT:           "start": {
// CHECK:STDOUT:             "character": 14,
// CHECK:STDOUT:             "line": 1
// CHECK:STDOUT:           }
// CHECK:STDOUT:         },
// CHECK:STDOUT:         "severity": 1,
// CHECK:STDOUT:         "source": "carbon"
// CHECK:STDOUT:       }
// CHECK:STDOUT:     ],
// CHECK:STDOUT:     "uri": "file:///infix.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 241{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 2,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 9,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 5,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///infix.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 243{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 3,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 19,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 15,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///infix.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 243{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 4,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 29,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 25,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///infix.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 243{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 5,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": {
// CHECK:STDOUT:     "range": {
// CHECK:STDOUT:       "end": {
// CHECK:STDOUT:         "character": 39,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       },
// CHECK:STDOUT:       "start": {
// CHECK:STDOUT:         "character": 35,
// CHECK:STDOUT:         "line": 0
// CHECK:STDOUT:       }
// CHECK:STDOUT:     },
// CHECK:STDOUT:     "uri": "file:///infix.carbon"
// CHECK:STDOUT:   }
// CHECK:STDOUT: }Content-Length: 51{{\r}}
// CHECK:STDOUT: {{\r}}
// CHECK:STDOUT: {
// CHECK:STDOUT:   "id": 6,
// CHECK:STDOUT:   "jsonrpc": "2.0",
// CHECK:STDOUT:   "result": null
// CHECK:STDOUT: }
