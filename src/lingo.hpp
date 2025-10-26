#pragma once
#include <string>
#include <cstdint>
#include <istream>
#include <vector>
#include <utility>
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
            TOKEN_WORD,
            TOKEN_STRING,
            TOKEN_LINE_END
        };

        enum token_keyword : uint8_t {
            KEYWORD_ON,
            KEYWORD_ELSE,
            KEYWORD_THEN,

            KEYWORD_AND,
            KEYWORD_OR,
            KEYWORD_NOT,
            KEYWORD_MOD,
        };

        enum token_word_id : uint8_t {
            WORD_ID_RETURN,
            WORD_ID_END,
            WORD_ID_IF,
            WORD_ID_REPEAT,
            WORD_ID_WITH,
            WORD_ID_TO,
            WORD_ID_DOWN,
            WORD_ID_WHILE,
            WORD_ID_SWITCH,
            WORD_ID_CASE,
            WORD_ID_OTHERWISE,
            WORD_ID_THE,
            WORD_ID_OF,
            WORD_ID_IN,
            WORD_ID_PUT,
            WORD_ID_AFTER,
            WORD_ID_BEFORE,

            WORD_ID_TYPE,
            WORD_ID_NUMBER,
            WORD_ID_INTEGER,
            WORD_ID_STRING,
            WORD_ID_POINT,
            WORD_ID_RECT,
            WORD_ID_IMAGE,

            WORD_ID_GLOBAL,
            WORD_ID_PROPERTY,

            WORD_ID_UNKNOWN = UINT8_MAX
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
            SYMBOL_LBRACE, // {
            SYMBOL_RBRACE, // }
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
                token_word_id word_id;
                double number;
                int32_t integer;
            };

            static token make_keyword(token_keyword v, const pos_info &pos);
            static token make_integer(int32_t v, const pos_info &pos);
            static token make_symbol(token_symbol v, const pos_info &pos);
            static token make_float(double v, const pos_info &pos);
            static token make_word(const std::string v, const pos_info &pos);
            static token make_word(token_word_id word_id, const pos_info &pos);
            static token make_string(const std::string v, const pos_info &pos);
            static token make_line_end(const pos_info &pos);

            constexpr bool is_keyword(token_keyword v) const noexcept {
                return type == TOKEN_KEYWORD && keyword == v;
            }

            constexpr bool is_word(token_word_id v) const noexcept {
                return type == TOKEN_WORD && word_id == v;
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
            
                case TOKEN_WORD:
                    return "word";
            
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
        const char* word_id_to_str(token_word_id word_id);
        std::string token_to_str(const token &tok);

        // AST expressions
        enum ast_expr_type : uint8_t {
            EXPR_BINOP, // X ? Y
            EXPR_UNOP, // ? X
            EXPR_THE, // the X
            EXPR_LITERAL,
            EXPR_LIST,
            EXPR_PROP_LIST,
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
            EXPR_THE_FRAME,
            EXPR_THE_RANDOM_SEED,
            EXPR_THE_DIR_SEPARATOR,
        };

        enum ast_literal_type : uint8_t {
            EXPR_LITERAL_FLOAT,
            EXPR_LITERAL_INTEGER,
            EXPR_LITERAL_STRING,
            EXPR_LITERAL_VOID,
            EXPR_LITERAL_SYMBOL
        };

        enum ast_scope : uint8_t {
            SCOPE_PROPERTY, // highest precedence
            SCOPE_GLOBAL,
            SCOPE_LOCAL,
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

            static inline ast_expr_literal make_int(pos_info pos, int32_t v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_INTEGER;
                ret.intv = v;
                return ret;
            }

            static inline ast_expr_literal make_float(pos_info pos, double v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_FLOAT;
                ret.floatv = v;
                return ret;
            }

            static inline ast_expr_literal make_string(pos_info pos,
                                                       const std::string &v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_STRING;
                ret.str = v;
                return ret;
            }

            static inline ast_expr_literal make_symbol(pos_info pos,
                                                       const std::string &v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_SYMBOL;
                ret.str = v;
                return ret;
            }

            static inline ast_expr_literal make_void(pos_info pos) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_VOID;
                return ret;
            }
        };

        struct ast_expr_identifier : public ast_expr {
            inline ast_expr_identifier() { type = EXPR_IDENTIFIER; }

            std::string identifier;
            ast_scope scope;
        };

        struct ast_expr_list : public ast_expr {
            inline ast_expr_list() { type = EXPR_LIST; }

            std::vector<std::unique_ptr<ast_expr>> items;
        };

        struct ast_expr_prop_list : public ast_expr {
            inline ast_expr_prop_list() { type = EXPR_PROP_LIST; }

            std::vector<std::pair<std::unique_ptr<ast_expr>,
                                  std::unique_ptr<ast_expr>>> pairs;
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
            STATEMENT_PUT_ON,
            STATEMENT_SWITCH
        };

        struct ast_statement {
            ast_statement_type type;
            pos_info pos;
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

        struct ast_if_branch {
            std::unique_ptr<ast_expr> condition;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_if : public ast_statement {
            inline ast_statement_if() { type = STATEMENT_IF; }

            // the first branch is the if branch, then the rest are the
            // else-if branches
            std::vector<std::unique_ptr<ast_if_branch>> branches;
            bool has_else = false;
            std::vector<std::unique_ptr<ast_statement>> else_branch;
        };

        struct ast_statement_repeat_while : public ast_statement {
            inline ast_statement_repeat_while()
                { type = STATEMENT_REPEAT_WHILE; }

            std::unique_ptr<ast_expr> condition;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_repeat_to : public ast_statement {
            inline ast_statement_repeat_to() { type = STATEMENT_REPEAT_TO; }

            std::unique_ptr<ast_expr> iterator;
            std::unique_ptr<ast_expr> init;
            std::unique_ptr<ast_expr> to;
            bool down;

            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_repeat_in : public ast_statement {
            inline ast_statement_repeat_in() { type = STATEMENT_REPEAT_IN; }

            std::unique_ptr<ast_expr> iterator;
            std::unique_ptr<ast_expr> iterable;

            std::vector<std::unique_ptr<ast_statement>> body;
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
        };

        struct ast_statement_put_on : public ast_statement {
            inline ast_statement_put_on() { type = STATEMENT_PUT_ON; }

            std::unique_ptr<ast_expr> expr;
            std::unique_ptr<ast_expr> target; // after/before X syntax
            bool before; // true if before, false if after
        };

        // AST root
        struct ast_handler_decl {
            pos_info pos;

            std::string name;
            std::vector<std::string> params;
            std::vector<std::unique_ptr<ast_statement>> body;
            std::vector<std::string> locals;
        };

        struct ast_root {
            std::vector<std::string> properties;
            std::vector<std::unique_ptr<ast_handler_decl>> handlers;
        };

        bool parse_ast(const std::vector<token> &tokens, ast_root &root,
                       parse_error *error);
    } // namespace ast

    namespace codegen {
        bool generate_luajit_text(const ast::ast_root &root,
                                  std::ostream &stream, parse_error *error);
    }

    bool compile_luajit_text(std::istream &istream, std::ostream &ostream,
                             parse_error *error);
} // namespace lingo
