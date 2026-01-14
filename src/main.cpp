#include <iostream>
#include <istream>
#include <fstream>
#include "lingo/lingo.hpp"

int lingo_compiler_test(int argc, const char *argv[]) {
    if (argc < 3) {
        std::cerr << "error: invalid arguments\nexpected format: evillingo [input] [output]\n";
        return 2;
    }

    int file_index = 0;
    const char *files[] = {nullptr, nullptr};
    bool no_line_numbers = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        
        if (!strcmp(arg, "--no-line-numbers")) {
            no_line_numbers = true;
        }
        else {
            if (file_index >= 2) {
                std::cerr << "no more files please";
                return 2;
            }

            files[file_index++] = arg;
        }
    }

    bool use_cin = !strcmp(files[0], "-");
    bool use_cout = !strcmp(files[1], "-");

    std::unique_ptr<std::istream> istream;
    if (use_cin) {
        istream = std::make_unique<std::istream>(std::cin.rdbuf());
    } else {
        auto fstream = new std::ifstream(files[0]);
        if (!fstream->is_open()) {
            std::cerr << "could not open file " << files[0] << "\n";
            delete fstream;
        }

        istream = std::unique_ptr<std::istream>(fstream);
    }

    std::unique_ptr<std::ostream> ostream;
    if (use_cout) {
        ostream = std::make_unique<std::ostream>(std::cout.rdbuf());
    } else {
        auto fstream = new std::ofstream(files[1]);
        if (!fstream->is_open()) {
            std::cerr << "could not open file " << files[1] << "\n";
            delete fstream;
        }

        ostream = std::unique_ptr<std::ostream>(fstream);
    }

    lingo::parse_error error;
    std::vector<std::vector<uint8_t>> chunks;
    if (!lingo::compile_bytecode(*istream, chunks, &error)) {
        std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
        return 1;
    }

    if (chunks.size() == 0) {
        std::cerr << "no chunks generated\n";
        return 1;
    }

    const lingo::bc::chunk_header *chunk = (lingo::bc::chunk_header *)chunks[0].data();
    const lingo::bc::instr *code = lingo::bc::base_offset(chunk, chunk->instrs);
    const lingo::bc::chunk_const *consts = lingo::bc::base_offset(chunk, chunk->consts);
    const lingo::bc::chunk_const_str **lnames = lingo::bc::base_offset(chunk, chunk->local_names);
    const lingo::bc::chunk_const_str *strpool = lingo::bc::base_offset(chunk, chunk->string_pool);
    
    std::cout << "\tCONSTS:\n";
    for (int i = 0; i < chunk->nconsts; ++i) {
        const lingo::bc::chunk_const *c = consts + i;
        printf("%i - ", i);
        switch (c->type) {
            case lingo::bc::TYPE_INT:
                printf("int:    %i\n", c->i32);
                break;

            case lingo::bc::TYPE_FLOAT:
                printf("float:  %f\n", c->f64);
                break;

            case lingo::bc::TYPE_STRING: {
                const lingo::bc::chunk_const_str *str =
                    lingo::bc::base_offset(strpool, c->str);
                printf("string: (%llu) %s\n", str->size, &str->first);
                break;
            }

            case lingo::bc::TYPE_SYMBOL: {
                const lingo::bc::chunk_const_str *str =
                    lingo::bc::base_offset(strpool, c->str);
                printf("symbol: (%llu) %s\n", str->size, &str->first);
                break;
            }

            default:
                printf("???\n");
                break;
        }
    }

    std::cout << "\tLOCALS:\n";
    for (int i = 0; i < chunk->nargs + chunk->nlocals; ++i) {
        const lingo::bc::chunk_const_str *name_ref = lingo::bc::base_offset(strpool, lnames[i]);
        printf("%i - %s", i, &name_ref->first);

        if (i < chunk->nargs)
            printf(" (param)\n");
        else
            printf("\n");
    }

    std::cout << "\tDISASM:\n";
    char buf[64];
    for (uint32_t i = 0; i < chunk->ninstr; ++i) {
        lingo::bc::instr_disasm(chunk, code[i], buf, sizeof(buf));
        std::cout << buf << "\n";
    }

    ostream->write((char*)chunks[0].data(), chunks[0].size() * sizeof(chunks[0].front()));

    // for (auto &tok : tokens) {
    //     switch (tok.type) {
    //         case lingo::ast::TOKEN_FLOAT:
    //             std::cout << "(FLT) ";
    //             std::cout << tok.number;
    //             break;

    //         case lingo::ast::TOKEN_INTEGER:
    //             std::cout << "(INT) ";
    //             std::cout << tok.integer;
    //             break;

    //         case lingo::ast::TOKEN_KEYWORD:
    //             std::cout << "(KYW) ";
    //             std::cout << lingo::ast::keyword_to_str(tok.keyword);
    //             break;

    //         case lingo::ast::TOKEN_IDENTIFIER:
    //             std::cout << "(IDN) ";
    //             std::cout << tok.str;
    //             break;
            
    //         case lingo::ast::TOKEN_SYMBOL:
    //             std::cout << "(SYM) ";
    //             std::cout << lingo::ast::symbol_to_str(tok.symbol);
    //             break;

    //         case lingo::ast::TOKEN_STRING:
    //             std::cout << "(STR) ";
    //             std::cout << tok.str;
    //             break;
            
    //         case lingo::ast::TOKEN_LINE_END:
    //             std::cout << "(NXT) ";
    //             break;
    //     }

    //     std::cout << "\n";
    // }

    // auto thing = static_cast<lingo::ast::ast_handler_definition*>(statements[0].get());
    // std::cout << statements.size() << "\n";
    // std::cout << thing->params.size() << "\n";

    return 0;
}

int main(int argc, const char *argv[]) {
    if (argc > 1) {
        return lingo_compiler_test(argc, argv);
    }
    
    {
        constexpr const char *FILE_NAME = "input.ls";
        std::ifstream f(FILE_NAME);
        if (!f.is_open()) {
            std::cerr << "could not open " << FILE_NAME << "\n";
            return 1;
        }

        lingo::parse_error error;
        std::vector<std::vector<uint8_t>> chunks;
        if (!lingo::compile_bytecode(f, chunks, &error)) {
            std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
            return 1;
        }

        if (chunks.size() == 0) {
            std::cout << "no chunks generated\n";
            return 0;
        }

        // std::string chunk_name = std::string("@") + FILE_NAME;
        // if (lua_load_lingo(L, f, chunk_name.c_str()) != LUA_OK) {
        //     std::cerr << lua_tostring(L, -1);
        //     return 1;
        // }
    }

    return 0;
}