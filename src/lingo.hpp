#pragma once
#include <string>
#include <cstdint>
#include <istream>
#include <vector>
#include <memory>

namespace lingo {
    struct pos_info {
        int line; // 1-indexed
        int column; // 1-indexed
    };

    struct parse_error {
        pos_info pos;
        std::string errmsg;
    };

    namespace ast {
        // tokens
        enum token_type : uint8_t {
            TOKEN_KEYWORD,
            TOKEN_SYMBOL,
            TOKEN_FLOAT,
            TOKEN_INTEGER,
            TOKEN_IDENTIFIER,
            TOKEN_STRING,
            TOKEN_LINE_END
        };

        enum token_keyword : uint8_t {
            KEYWORD_ON,
            KEYWORD_RETURN,
            KEYWORD_END,
            KEYWORD_IF,
            KEYWORD_ELSE,
            KEYWORD_THEN,
            KEYWORD_REPEAT,
            KEYWORD_WITH,
            KEYWORD_TO,
            KEYWORD_WHILE,
            KEYWORD_SWITCH,
            KEYWORD_CASE,
            KEYWORD_OTHERWISE,
            KEYWORD_THE,
            KEYWORD_OF,
            KEYWORD_PUT,
            KEYWORD_AFTER,

            KEYWORD_TYPE,
            KEYWORD_NUMBER,
            KEYWORD_INTEGER,
            KEYWORD_STRING,
            KEYWORD_POINT,
            KEYWORD_RECT,
            KEYWORD_IMAGE,

            KEYWORD_GLOBAL,
            KEYWORD_PROPERTY,

            KEYWORD_AND,
            KEYWORD_OR,
            KEYWORD_NOT,
            KEYWORD_MOD,

            // KEYWORD_TRUE,
            // KEYWORD_FALSE,
            // KEYWORD_VOID,
        };

        enum token_symbol : uint8_t {
            SYMBOL_COMMA, // ,
            SYMBOL_PERIOD, // .

            SYMBOL_MINUS, // -
            SYMBOL_PLUS, // +
            SYMBOL_SLASH, // /
            SYMBOL_STAR, // *
            SYMBOL_AMPERSAND, // &
            SYMBOL_POUND, // #
            SYMBOL_RANGE, // ..
            
            SYMBOL_LPAREN, // (
            SYMBOL_RPAREN, // )
            SYMBOL_LBRACKET, // [
            SYMBOL_RBRACKET, // ]
            SYMBOL_COLON, // :

            SYMBOL_EQUAL, // = (both assignment and comparison)
            SYMBOL_NEQUAL, // <>
            SYMBOL_LT, // <
            SYMBOL_GT, // >
            SYMBOL_LE, // <=
            SYMBOL_GE, // >=

            SYMBOL_DOUBLE_AMPERSAND, // && (concatenates with space)
            SYMBOL_COMMENT, // --
            SYMBOL_LINE_CONT, // \

            SYMBOL_INVALID = UINT8_MAX,
        };

        struct token {
            token_type type;
            pos_info pos;

            std::string str;
            union {
                token_keyword keyword;
                token_symbol symbol;
                double number;
                int32_t integer;
            };

            static token make_keyword(token_keyword v, const pos_info &pos);
            static token make_integer(int32_t v, const pos_info &pos);
            static token make_symbol(token_symbol v, const pos_info &pos);
            static token make_float(double v, const pos_info &pos);
            static token make_identifier(const std::string v, const pos_info &pos);
            static token make_string(const std::string v, const pos_info &pos);
            static token make_line_end(const pos_info &pos);

            constexpr bool is_keyword(token_keyword v) const noexcept {
                return type == TOKEN_KEYWORD && keyword == v;
            }

            constexpr bool is_symbol(token_symbol v) const noexcept {
                return type == TOKEN_SYMBOL && symbol == v;
            }

            constexpr bool is_a(token_type t) const noexcept {
                return type == t;
            }
        };

        constexpr const char* token_type_str(token_type type) {
            switch (type) {
                case TOKEN_KEYWORD:
                    return "keyword";
            
                case TOKEN_SYMBOL:
                    return "symbol";
            
                case TOKEN_FLOAT:
                    return "float";

                case TOKEN_INTEGER:
                    return "integer";
            
                case TOKEN_IDENTIFIER:
                    return "identifier";
            
                case TOKEN_STRING:
                    return "string";
            
                case TOKEN_LINE_END:
                    return "newline";

                default: return "???";
            }
        }

        bool parse_tokens(std::istream &stream, std::vector<token> &tokens,
                          parse_error *error);

        const char* keyword_to_str(token_keyword keyword);
        const char* symbol_to_str(token_symbol symbol);
        std::string token_to_str(const token &tok);

        // AST expressions
        enum ast_expr_type : uint8_t {
            EXPR_BINOP, // X ? Y
            EXPR_UNOP, // ? X
            EXPR_THE, // the X
            EXPR_LITERAL,
            EXPR_IDENTIFIER,
            EXPR_DOT, // X.Y
            EXPR_INDEX, // X[Y] or X[A..B]
            EXPR_CALL, // X(...)
        };

        enum ast_binop : uint8_t {
            EXPR_BINOP_ADD, // X + Y
            EXPR_BINOP_SUB, // X - Y
            EXPR_BINOP_MUL, // X * Y
            EXPR_BINOP_DIV, // X / Y
            EXPR_BINOP_MOD, // X mod Y

            EXPR_BINOP_AND, // X and Y
            EXPR_BINOP_OR, // X or Y

            EXPR_BINOP_LT, // X < Y
            EXPR_BINOP_GT, // X > Y
            EXPR_BINOP_LE, // X <= Y
            EXPR_BINOP_GE, // X >= Y
            EXPR_BINOP_EQ, // X = Y
            EXPR_BINOP_NEQ, // X <> Y

            EXPR_BINOP_CONCAT, // X & Y
            EXPR_BINOP_CONCAT_WITH_SPACE, // X && Y
        };

        enum ast_unop : uint8_t {
            EXPR_UNOP_NEG, // -X
            EXPR_UNOP_NOT // not X
        };

        enum ast_the_id : uint8_t {
            EXPR_THE_MOVIE_PATH,
            EXPR_THE_RANDOM_SEED,
            EXPR_THE_DIR_SEPARATOR,
        };

        enum ast_literal_type : uint8_t {
            EXPR_LITERAL_FLOAT,
            EXPR_LITERAL_INTEGER,
            EXPR_LITERAL_STRING
        };

        struct ast_expr {
            ast_expr_type type;
            pos_info pos;
        };

        struct ast_expr_binop : public ast_expr {
            inline ast_expr_binop() { type = EXPR_BINOP; }

            std::unique_ptr<ast_expr> left;
            std::unique_ptr<ast_expr> right;
            ast_binop op;
        };

        struct ast_expr_unop : public ast_expr {
            inline ast_expr_unop() { type = EXPR_UNOP; }

            std::unique_ptr<ast_expr> expr;
            ast_unop op;
        };

        struct ast_expr_the : public ast_expr {
            inline ast_expr_the() { type = EXPR_THE; }

            ast_the_id identifier;
        };

        struct ast_expr_literal : public ast_expr {
            inline ast_expr_literal() { type = EXPR_LITERAL; }

            ast_literal_type literal_type;
            std::string str;
            union {
                int32_t intv;
                double floatv;
            };
        };

        struct ast_expr_identifier : public ast_expr {
            inline ast_expr_identifier() { type = EXPR_IDENTIFIER; }

            std::string identifier;
        };

        struct ast_expr_dot : public ast_expr {
            inline ast_expr_dot() { type = EXPR_DOT; }

            std::unique_ptr<ast_expr> expr;
            std::string index;
        };

        struct ast_expr_index : public ast_expr {
            inline ast_expr_index() { type = EXPR_INDEX; }

            std::unique_ptr<ast_expr> expr;
            std::unique_ptr<ast_expr> index_from;
            std::unique_ptr<ast_expr> index_to; // nullptr if not a range
        };

        struct ast_expr_call : public ast_expr {
            inline ast_expr_call() { type = EXPR_CALL; }

            std::unique_ptr<ast_expr> method;
            std::vector<std::unique_ptr<ast_expr>> arguments;
        };

        // AST statements
        enum ast_statement_type : uint8_t {
            // top-level script statements
            STATEMENT_DECLARE_GLOBAL, // also usable within handler
            STATEMENT_DECLARE_PROPERTY,
            STATEMENT_DEFINE_HANDLER,

            // handler statements
            STATEMENT_RETURN,
            STATEMENT_ASSIGN,
            STATEMENT_EXPR,
            STATEMENT_IF,
            STATEMENT_REPEAT_WHILE,
            STATEMENT_REPEAT_TO,
            STATEMENT_REPEAT_IN,
            STATEMENT_EXIT_REPEAT, // aka break
            STATEMENT_NEXT_REPEAT, // aka continue
            STATEMENT_PUT,
            STATEMENT_SWITCH
        };

        struct ast_statement {
            ast_statement_type type;
            pos_info pos;
        };

        struct ast_global_declaration : public ast_statement {
            inline ast_global_declaration()
                { type = STATEMENT_DECLARE_GLOBAL; }
            std::vector<std::string> identifiers;
        };

        struct ast_property_declaration : public ast_statement {
            inline ast_property_declaration()
                { type = STATEMENT_DECLARE_PROPERTY; }
            std::vector<std::string> identifiers;
        };

        struct ast_handler_definition : public ast_statement {
            inline ast_handler_definition() { type = STATEMENT_DEFINE_HANDLER; }

            std::string name;
            std::vector<std::string> params;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_return : public ast_statement {
            inline ast_statement_return() { type = STATEMENT_RETURN; }

            std::unique_ptr<ast_expr> expr;
        };

        struct ast_statement_assign : public ast_statement {
            inline ast_statement_assign() { type = STATEMENT_ASSIGN; }

            std::unique_ptr<ast_expr> lvalue;
            std::unique_ptr<ast_expr> rvalue;
        };

        struct ast_statement_expr : public ast_statement {
            inline ast_statement_expr() { type = STATEMENT_EXPR; }

            std::unique_ptr<ast_expr> expr;
        };

        struct ast_statement_if : public ast_statement {
            inline ast_statement_if() { type = STATEMENT_IF; }

            std::unique_ptr<ast_expr> condition;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_repeat_while : public ast_statement {
            inline ast_statement_repeat_while()
                { type = STATEMENT_REPEAT_WHILE; }

            std::unique_ptr<ast_expr> condition;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_repeat_to : public ast_statement {
            inline ast_statement_repeat_to() { type = STATEMENT_REPEAT_TO; }

            std::string iterator_identifier;
            std::unique_ptr<ast_expr> init;
            std::unique_ptr<ast_expr> to;
            bool down;
        };

        struct ast_statement_repeat_in : public ast_statement {
            inline ast_statement_repeat_in() { type = STATEMENT_REPEAT_IN; }

            std::string iterator_identifier;
            std::unique_ptr<ast_expr> iterable;
        };

        struct ast_statement_exit_repeat : public ast_statement {
            inline ast_statement_exit_repeat() { type = STATEMENT_EXIT_REPEAT; }
        };

        struct ast_statement_next_repeat : public ast_statement {
            inline ast_statement_next_repeat() { type = STATEMENT_NEXT_REPEAT; }
        };

        struct ast_statement_put : public ast_statement {
            inline ast_statement_put() { type = STATEMENT_PUT; }

            std::unique_ptr<ast_expr> expr;
            std::unique_ptr<ast_expr> target; // after X syntax, nullptr if not
                                              // present.
        };

        typedef std::vector<std::unique_ptr<ast_statement>> ast_root;

        bool parse_ast(const std::vector<token> &tokens, ast_root &root,
                       parse_error *error);
    } // namespace ast

    namespace codegen {
        bool generate_lua51(const ast::ast_root &root, std::ostream &stream,
                            parse_error *error);
    }
} // namespace lingo
