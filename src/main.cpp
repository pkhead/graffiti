#include <iostream>
#include <fstream>
#include <cstring>
#include "lingo.hpp"

int main(int argc, const char *argv[]) {
    if (argc < 3) {
        std::cerr << "error: invalid arguments\nexpected format: evillingo [input] [output]\n";
        return 2;
    }

    bool use_cin = !strcmp(argv[1], "-");
    bool use_cout = !strcmp(argv[2], "-");

    std::unique_ptr<std::istream> istream;
    if (use_cin) {
        istream = std::make_unique<std::istream>(std::cin.rdbuf());
    } else {
        auto fstream = new std::ifstream(argv[1]);
        if (!fstream->is_open()) {
            std::cerr << "could not open file " << argv[1] << "\n";
            delete fstream;
        }

        istream = std::unique_ptr<std::istream>(fstream);
    }

    std::unique_ptr<std::ostream> ostream;
    if (use_cout) {
        ostream = std::make_unique<std::ostream>(std::cout.rdbuf());
    } else {
        auto fstream = new std::ofstream(argv[2]);
        if (!fstream->is_open()) {
            std::cerr << "could not open file " << argv[2] << "\n";
            delete fstream;
        }

        ostream = std::unique_ptr<std::ostream>(fstream);
    }

    lingo::parse_error error;
    if (!lingo::compile_luajit_text(*istream, *ostream, &error)) {
        std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
        return 1;
    }

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
