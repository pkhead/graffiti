project('graffiti', 'cpp',
        version : '0.1',
        default_options : ['warning_level=3', 'cpp_std=c++17'])

sources = files(
  'src/main.cpp',
  'src/lingo/lang/lexer.cpp',
  'src/lingo/lang/ast.cpp',
  'src/lingo/lang/bcgen.cpp',
  'src/lingo/vm/vm.cpp',
)

executable('graffiti',
           sources : sources)
