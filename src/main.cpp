#include <iostream>
#include <fstream>
#include <vector>
#include "lingo.hpp"

int main(int argc, const char *argv[]) {
    if (argc == 1) {
        std::cerr << "expected filename as first argument\n";
        return 2;
    }

    std::ifstream stream(argv[1]);
    if (!stream.is_open()) {
        std::cerr << "could not open file " << argv[1] << "\n";
    }

    std::vector<lingo::ast::token> tokens;
    lingo::parse_error error;
    if (!lingo::ast::parse_tokens(stream, tokens, &error)) {
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

    std::vector<std::unique_ptr<lingo::ast::ast_statement>> script_tree;
    if (!lingo::ast::parse_ast(tokens, script_tree, &error)) {
        std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
        return 1;
    }

    lingo::codegen::generate_lua51(script_tree, std::cout, &error);
    if (!lingo::ast::parse_ast(tokens, script_tree, &error)) {
        std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
        return 1;
    }

    // auto thing = static_cast<lingo::ast::ast_handler_definition*>(statements[0].get());
    // std::cout << statements.size() << "\n";
    // std::cout << thing->params.size() << "\n";

    return 0;
}
