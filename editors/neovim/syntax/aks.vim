" Vim syntax file for AKS (Aks Programming Language)
" Language: AKS
" Maintainer: Anoop Kumar Sharma <anoop@aks-lang.org>
" Latest Revision: 2026

if exists("b:current_syntax")
  finish
endif

" Comments
syn keyword aksTodo contained TODO FIXME XXX BUG NOTE HACK
syn match aksComment "//.*$" contains=aksTodo,@Spell
syn region aksComment start="/\*" end="\*/" contains=aksTodo,@Spell

" Strings
syn match aksEscape "\\\([nrtbf\\"'0]\|x[0-9a-fA-F]\{2}\|u[0-9a-fA-F]\{4}\|U[0-9a-fA-F]\{8}\)" contained
syn region aksString start='"' skip='\\\\\|\\"' end='"' contains=aksEscape,@Spell
syn region aksString start="'" skip="\\\\\|\\'" end="'" contains=aksEscape,@Spell
syn region aksFormatString start='\(\$\|f\)"' skip='\\\\\|\\"' end='"' contains=aksEscape,aksInterpolation,@Spell
syn region aksInterpolation matchgroup=aksInterpolationDelimiter start="{" end="}" contained contains=TOP

" Numbers
syn match aksNumber "\v<0x[0-9a-fA-F_]+>"
syn match aksNumber "\v<0b[01_]+>"
syn match aksNumber "\v<0o[0-7_]+>"
syn match aksFloat  "\v<[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9_]+)?>"
syn match aksNumber "\v<[0-9][0-9_]*>"

" Keywords
syn keyword aksKeyword let const mut var fn return if else match while for in
syn keyword aksKeyword break continue struct enum interface trait impl type
syn keyword aksKeyword comptime defer errdefer async await spawn yield import
syn keyword aksKeyword as pub private where require ensure opaque effect perform handle resume

" Concurrency & Systems Keywords
syn keyword aksKeyword go select channel arena scoped vector ffi extern inline asm

" Types
syn keyword aksType int float string bool void byte any char usize isize
syn keyword aksType i8 i16 i32 i64 u8 u16 u32 u64 f32 f64
syn keyword aksType Option Result Task Channel Vector Arena Ffi

" Constants & Booleans
syn keyword aksBoolean true false
syn keyword aksConstant null None Some Ok Err

" Operators
syn match aksOperator "\v[-+*/%&|^!=<>?~:]"
syn match aksOperator "\v\=\>"
syn match aksOperator "\v\|\>"
syn match aksOperator "\v\:\:"
syn match aksOperator "\v\.\."

" Function definitions
syn match aksFunction "\v<fn\s+([a-zA-Z_][a-zA-Z0-9_]*)"hs=s+3

" Highlighting Links
hi def link aksTodo Todo
hi def link aksComment Comment
hi def link aksString String
hi def link aksFormatString String
hi def link aksEscape SpecialChar
hi def link aksInterpolationDelimiter Delimiter
hi def link aksNumber Number
hi def link aksFloat Float
hi def link aksKeyword Keyword
hi def link aksType Type
hi def link aksBoolean Boolean
hi def link aksConstant Constant
hi def link aksOperator Operator
hi def link aksFunction Function

let b:current_syntax = "aks"
