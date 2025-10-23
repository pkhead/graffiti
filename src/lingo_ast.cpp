#include "lingo_parser.hpp"

using namespace lingo_ast;

bool lingo_ast::parse_ast(const std::vector<token> &tokens,
                          std::vector<ast_statement> &out_statements,
                          parse_error_s *error) {
    (void)tokens;
    (void)out_statements;
    (void)error;
    return false;
}