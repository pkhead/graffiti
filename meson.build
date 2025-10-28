project('evillingo', 'cpp',
        version : '0.1',
        default_options : ['warning_level=3', 'cpp_std=c++17'])

sources = files(
  'src/main.cpp',
  'src/lingo/lingo_lexer.cpp',
  'src/lingo/lingo_ast.cpp',
  'src/lingo/lingo_codegen.cpp',
)

cxx = meson.get_compiler('cpp')
lj_dir = meson.current_source_dir() + '/luajit/build'

deps = []
deps += declare_dependency(
    dependencies: [
      cxx.find_library('lua51', dirs: lj_dir / 'lib', static : true),
      cxx.find_library('luajit', dirs: lj_dir / 'lib', static : true)],
    include_directories: include_directories('luajit/build/include')
)

executable('evillingo',
           sources : sources,
           dependencies : deps)
