; Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
; Tree-sitter syntax highlighting queries for AKS.

; Keywords
[
  "let"
  "const"
  "type"
  "struct"
  "enum"
  "interface"
  "trait"
  "impl"
  "implements"
  "for"
  "in"
  "while"
  "if"
  "else"
  "match"
  "comptime"
  "dyn"
  "where"
  "pub"
  "lazy"
] @keyword

; Function and Coroutine Keywords
"fn" @keyword.function
"return" @keyword.return
"async" @keyword.coroutine
"await" @keyword.coroutine
"spawn" @keyword.coroutine
"import" @keyword.import
"as" @keyword.import

; Builtin Types
(primitive_type) @type.builtin

; User Defined Types
(type_identifier) @type
(type_parameters (type_identifier) @type.parameter)
(generic_type (type_identifier) @type)
(dyn_trait_type trait: (type_identifier) @type)

; Function definitions and calls
(function_declaration name: (identifier) @function)
(call_expression function: (identifier) @function.call)
(call_expression function: (member_expression property: (identifier) @function.method))
(interface_method name: (identifier) @function.method)

; Decorators / Attributes
(decorator "@" @attribute (identifier) @attribute)

; Variables and Parameters
(parameter pattern: (identifier) @variable.parameter)
(variable_declaration pattern: (identifier) @variable)
(assignment_expression left: (identifier) @variable)
(member_expression property: (identifier) @property)
(struct_field name: (identifier) @property)
(field_initializer name: (identifier) @property)
(enum_variant name: (identifier) @constant)

; Literals
(number_literal) @number
(string_literal) @string
(raw_string_literal) @string
(escape_sequence) @string.escape
(string_interpolation ["${" "}"] @punctuation.special)
(boolean_literal) @boolean
(null_literal) @constant.builtin

; Operators
[
  "="
  "+="
  "-="
  "*="
  "/="
  "%="
  "=="
  "!="
  "<"
  "<="
  ">"
  ">="
  "&&"
  "||"
  "!"
  "&"
  "|"
  "^"
  "~"
  "<<"
  ">>"
  "+"
  "-"
  "*"
  "/"
  "%"
  "|>"
  ".."
  "..="
  "->"
  "=>"
  "?"
] @operator

; Punctuation
[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ";"
  ":"
  "."
  "?."
] @punctuation.delimiter

; Comments
(comment) @comment
