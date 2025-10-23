#include <iostream>
#include <fstream>
#include <vector>
#include "lingo_ast.hpp"

int main(int argc, const char *argv[]) {
    if (argc == 1) {
        std::cerr << "expected filename as first argument\n";
        return 2;
    }

    std::ifstream stream(argv[1]);
    if (!stream.is_open()) {
        std::cerr << "could not open file " << argv[1] << "\n";
    }

    std::vector<lingo_ast::token> tokens;
    lingo_ast::parse_error_s error;
    if (!parse_tokens(stream, tokens, &error)) {
        std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
    } else {
        for (auto &tok : tokens) {
            switch (tok.type) {
                case lingo_ast::TOKEN_FLOAT:
                    std::cout << "(FLT) ";
                    std::cout << tok.number;
                    break;

                case lingo_ast::TOKEN_INTEGER:
                    std::cout << "(INT) ";
                    std::cout << tok.integer;
                    break;

                case lingo_ast::TOKEN_KEYWORD:
                    std::cout << "(KYW) ";
                    std::cout << lingo_ast::keyword_to_str(tok.keyword);
                    break;

                case lingo_ast::TOKEN_IDENTIFIER:
                    std::cout << "(IDN) ";
                    std::cout << tok.str;
                    break;
                
                case lingo_ast::TOKEN_SYMBOL:
                    std::cout << "(SYM) ";
                    std::cout << lingo_ast::symbol_to_str(tok.symbol);
                    break;

                case lingo_ast::TOKEN_STRING:
                    std::cout << "(STR) ";
                    std::cout << tok.str;
                    break;
                
                case lingo_ast::TOKEN_LINE_END:
                    std::cout << "(NXT) ";
                    break;
            }

            std::cout << "\n";
        }
    }

    return 0;
}
