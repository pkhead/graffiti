#pragma once
#include <string>
#include <cstdint>
#include <istream>
#include <vector>

namespace lingo_ast {
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

        KEYWORD_AND,
        KEYWORD_OR,
        KEYWORD_NOT,

        KEYWORD_TRUE,
        KEYWORD_FALSE,
        KEYWORD_VOID,
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

    struct pos_info {
        int line; // 1-indexed
        int column; // 1-indexed
    };

    struct parse_error_s {
        pos_info pos;
        std::string errmsg;
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
    };

    bool parse_tokens(std::istream &stream, std::vector<token> &tokens,
                      parse_error_s *error);

    const char* keyword_to_str(token_keyword keyword);
    const char* symbol_to_str(token_symbol symbol);
} // namespace lingo_ast
