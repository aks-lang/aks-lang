// Copyright (c) 2026 Anoop Kumar Sharma. All rights reserved.
// Tree-sitter grammar definition for the AKS Programming Language.

module.exports = grammar({
  name: 'aks',

  extras: $ => [
    /\s/,
    $.comment,
  ],

  conflicts: $ => [
    [$.type_identifier, $.identifier],
    [$.primary_expression, $.type_identifier],
    [$.parameter, $.pattern],
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat($._statement),

    _statement: $ => choice(
      $.variable_declaration,
      $.function_declaration,
      $.struct_declaration,
      $.enum_declaration,
      $.interface_declaration,
      $.impl_block,
      $.type_alias,
      $.import_statement,
      $.expression_statement,
      $.return_statement,
      $.if_statement,
      $.while_statement,
      $.for_statement,
      $.match_statement,
      $.block
    ),

    comment: $ => token(choice(
      seq('//', /.*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')
    )),

    // Import statement
    import_statement: $ => seq(
      optional('lazy'),
      'import',
      optional(choice('c', 'rust', 'py')),
      field('path', choice($.string_literal, $.raw_string_literal)),
      optional(seq('as', field('alias', $.identifier))),
      ';'
    ),

    // Variable & Constant declarations
    variable_declaration: $ => seq(
      choice('let', 'const'),
      field('pattern', choice($.identifier, $.tuple_pattern, $.struct_pattern)),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('value', $._expression))),
      ';'
    ),

    // Type Alias
    type_alias: $ => seq(
      'type',
      field('name', $.type_identifier),
      optional($.type_parameters),
      '=',
      field('type', $._type),
      ';'
    ),

    // Function declaration
    function_declaration: $ => seq(
      repeat($.decorator),
      optional(choice('pub', 'async')),
      'fn',
      field('name', $.identifier),
      optional($.type_parameters),
      field('parameters', $.parameter_list),
      optional(seq('->', field('return_type', $._type))),
      optional(seq('where', commaSep1($.type_constraint))),
      field('body', $.block)
    ),

    decorator: $ => seq(
      '@',
      field('name', $.identifier),
      optional(seq('(', commaSep($._expression), ')'))
    ),

    parameter_list: $ => seq(
      '(',
      commaSep($.parameter),
      ')'
    ),

    parameter: $ => seq(
      optional('...'),
      field('pattern', choice($.identifier, $.tuple_pattern, $.struct_pattern)),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('default', $._expression)))
    ),

    tuple_pattern: $ => seq(
      '(',
      commaSep(choice($.identifier, $.tuple_pattern, $.struct_pattern)),
      ')'
    ),

    struct_pattern: $ => seq(
      '{',
      commaSep(choice($.identifier, seq($.identifier, ':', $.identifier))),
      '}'
    ),

    // Struct declaration
    struct_declaration: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'struct',
      field('name', $.type_identifier),
      optional($.type_parameters),
      optional(seq('implements', commaSep1($.type_identifier))),
      '{',
      commaSep($.struct_field),
      optional(';'),
      '}'
    ),

    struct_field: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type)
    ),

    // Enum declaration
    enum_declaration: $ => seq(
      repeat($.decorator),
      optional('pub'),
      'enum',
      field('name', $.type_identifier),
      optional($.type_parameters),
      '{',
      commaSep($.enum_variant),
      optional(';'),
      '}'
    ),

    enum_variant: $ => seq(
      field('name', $.identifier),
      optional(choice(
        seq('(', commaSep($._type), ')'),
        seq('{', commaSep($.struct_field), '}')
      )),
      optional(seq('=', $._expression))
    ),

    // Interface / Trait declaration
    interface_declaration: $ => seq(
      repeat($.decorator),
      optional('pub'),
      choice('interface', 'trait'),
      field('name', $.type_identifier),
      optional($.type_parameters),
      '{',
      repeat($.interface_method),
      '}'
    ),

    interface_method: $ => seq(
      'fn',
      field('name', $.identifier),
      optional($.type_parameters),
      field('parameters', $.parameter_list),
      optional(seq('->', field('return_type', $._type))),
      choice(';', field('default_body', $.block))
    ),

    // Impl block
    impl_block: $ => seq(
      'impl',
      optional($.type_parameters),
      choice(
        field('target', $.type_identifier),
        seq(field('trait', $.type_identifier), 'for', field('target', $.type_identifier))
      ),
      '{',
      repeat($.function_declaration),
      '}'
    ),

    // Types
    _type: $ => choice(
      $.primitive_type,
      $.type_identifier,
      $.generic_type,
      $.tuple_type,
      $.array_type,
      $.function_type,
      $.dyn_trait_type,
      $.nullable_type
    ),

    primitive_type: $ => choice(
      'int', 'float', 'bool', 'string', 'void', 'byte', 'char',
      'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'f32', 'f64'
    ),

    generic_type: $ => seq(
      field('name', $.type_identifier),
      '<',
      commaSep1($._type),
      '>'
    ),

    tuple_type: $ => seq(
      '(',
      commaSep1($._type),
      ')'
    ),

    array_type: $ => seq(
      '[',
      field('element', $._type),
      optional(seq(';', field('size', $._expression))),
      ']'
    ),

    function_type: $ => seq(
      'fn',
      '(',
      commaSep($._type),
      ')',
      optional(seq('->', $._type))
    ),

    dyn_trait_type: $ => seq(
      'dyn',
      field('trait', $.type_identifier)
    ),

    nullable_type: $ => seq(
      $._type,
      '?'
    ),

    type_parameters: $ => seq(
      '<',
      commaSep1(field('param', $.type_identifier)),
      '>'
    ),

    type_constraint: $ => seq(
      field('param', $.type_identifier),
      ':',
      field('bound', choice($.type_identifier, seq($.type_identifier, '+', $.type_identifier)))
    ),

    // Statements
    block: $ => seq(
      '{',
      repeat($._statement),
      '}'
    ),

    expression_statement: $ => seq(
      $._expression,
      ';'
    ),

    return_statement: $ => seq(
      'return',
      optional($._expression),
      ';'
    ),

    if_statement: $ => seq(
      'if',
      choice(seq('(', field('condition', $._expression), ')'), field('condition', $._expression)),
      field('consequence', $.block),
      optional(seq('else', field('alternative', choice($.if_statement, $.block))))
    ),

    while_statement: $ => seq(
      'while',
      choice(seq('(', field('condition', $._expression), ')'), field('condition', $._expression)),
      field('body', $.block)
    ),

    for_statement: $ => seq(
      'for',
      optional('('),
      field('item', choice($.identifier, $.tuple_pattern)),
      'in',
      field('iterator', $._expression),
      optional(')'),
      field('body', $.block)
    ),

    match_statement: $ => seq(
      'match',
      choice(seq('(', field('value', $._expression), ')'), field('value', $._expression)),
      '{',
      repeat($.match_arm),
      '}'
    ),

    match_arm: $ => seq(
      field('pattern', $.pattern),
      optional(seq('if', field('guard', $._expression))),
      '=>',
      choice($._statement, $._expression),
      optional(',')
    ),

    pattern: $ => choice(
      '_',
      $.identifier,
      $.number_literal,
      $.string_literal,
      $.boolean_literal,
      $.tuple_pattern,
      $.struct_pattern,
      seq($.type_identifier, '.', $.identifier, optional(seq('(', commaSep($.pattern), ')')))
    ),

    // Expressions
    _expression: $ => choice(
      $.assignment_expression,
      $.binary_expression,
      $.unary_expression,
      $.call_expression,
      $.member_expression,
      $.subscript_expression,
      $.pipe_expression,
      $.range_expression,
      $.primary_expression
    ),

    assignment_expression: $ => prec.right(1, seq(
      field('left', choice($.identifier, $.member_expression, $.subscript_expression)),
      choice('=', '+=', '-=', '*=', '/=', '%='),
      field('right', $._expression)
    )),

    binary_expression: $ => {
      const table = [
        ['||', 2],
        ['&&', 3],
        ['|', 4],
        ['^', 5],
        ['&', 6],
        ['==', 7], ['!=', 7],
        ['<', 8], ['<=', 8], ['>', 8], ['>=', 8],
        ['<<', 9], ['>>', 9],
        ['+', 10], ['-', 10],
        ['*', 11], ['/', 11], ['%', 11],
      ];
      return choice(...table.map(([op, p]) =>
        prec.left(p, seq(
          field('left', $._expression),
          field('operator', op),
          field('right', $._expression)
        ))
      ));
    },

    unary_expression: $ => prec(12, seq(
      field('operator', choice('!', '-', '+', '~', '&', '*')),
      field('argument', $._expression)
    )),

    pipe_expression: $ => prec.left(1, seq(
      field('left', $._expression),
      '|>',
      field('right', $._expression)
    )),

    range_expression: $ => prec.left(8, seq(
      field('start', $._expression),
      choice('..', '..='),
      field('end', $._expression)
    )),

    call_expression: $ => prec(14, seq(
      field('function', $._expression),
      optional($.type_arguments),
      '(',
      commaSep($._expression),
      ')'
    )),

    type_arguments: $ => seq(
      '<',
      commaSep1($._type),
      '>'
    ),

    member_expression: $ => prec(15, seq(
      field('object', $._expression),
      choice('.', '?.'),
      field('property', $.identifier)
    )),

    subscript_expression: $ => prec(15, seq(
      field('object', $._expression),
      '[',
      field('index', $._expression),
      ']'
    )),

    primary_expression: $ => choice(
      $.identifier,
      $.number_literal,
      $.string_literal,
      $.raw_string_literal,
      $.boolean_literal,
      $.null_literal,
      $.array_literal,
      $.tuple_literal,
      $.struct_literal,
      $.closure_expression,
      $.comptime_block,
      seq('(', $._expression, ')')
    ),

    array_literal: $ => seq(
      '[',
      commaSep($._expression),
      ']'
    ),

    tuple_literal: $ => seq(
      '(',
      $._expression,
      ',',
      commaSep($._expression),
      ')'
    ),

    struct_literal: $ => seq(
      field('type', $.type_identifier),
      '{',
      commaSep($.field_initializer),
      '}'
    ),

    field_initializer: $ => seq(
      field('name', $.identifier),
      optional(seq(':', field('value', $._expression)))
    ),

    closure_expression: $ => seq(
      '|',
      commaSep($.parameter),
      '|',
      choice(field('body', $.block), field('body', $._expression))
    ),

    comptime_block: $ => seq(
      'comptime',
      field('body', $.block)
    ),

    // Literals & Identifiers
    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,
    type_identifier: $ => /[A-Z][a-zA-Z0-9_]*/,

    number_literal: $ => token(choice(
      /0[xX][0-9a-fA-F_]+/,
      /0[bB][01_]+/,
      /0[oO][0-7_]+/,
      /[0-9][0-9_]*\.[0-9_]+([eE][+-]?[0-9_]+)?/,
      /[0-9][0-9_]*/
    )),

    string_literal: $ => seq(
      '"',
      repeat(choice(
        token.immediate(prec(1, /[^"\\$\n]+/)),
        $.escape_sequence,
        $.string_interpolation
      )),
      '"'
    ),

    escape_sequence: $ => token.immediate(seq(
      '\\',
      choice(
        /[\\'"0abfnrtv]/,
        /x[0-9a-fA-F]{2}/,
        /u[0-9a-fA-F]{4}/,
        /U[0-9a-fA-F]{8}/
      )
    )),

    string_interpolation: $ => seq(
      '${',
      $._expression,
      '}'
    ),

    raw_string_literal: $ => token(seq(
      'r"',
      repeat(/[^"]/),
      '"'
    )),

    boolean_literal: $ => choice('true', 'false'),
    null_literal: $ => 'null',
  }
});

function commaSep(rule) {
  return optional(commaSep1(rule));
}

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)), optional(','));
}
