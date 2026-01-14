project('graffiti', 'cpp',
        version : '0.1',
        default_options : ['warning_level=3', 'cpp_std=c++17'])

sources = files(
  'src/main.cpp',
  'src/lingo/lingo_lexer.cpp',
  'src/lingo/lingo_ast.cpp',
  'src/lingo/lingo_bcgen.cpp',
)

executable('graffiti',
           sources : sources)
