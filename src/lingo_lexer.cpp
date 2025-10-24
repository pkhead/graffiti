#include "lingo.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <cassert>

using namespace lingo::ast;

#define ARRLEN(arr) (sizeof(arr)/sizeof(*arr))

struct { token_symbol e; const char *str; } static const symbol_pairs[] = {
    { SYMBOL_LE, "<=" },
    { SYMBOL_GE, ">=" },
    { SYMBOL_NEQUAL, "<>" },
    { SYMBOL_COMMENT, "--" },
    { SYMBOL_DOUBLE_AMPERSAND, "&&" },
    { SYMBOL_RANGE, ".." },

    { SYMBOL_COMMA, "," },
    { SYMBOL_PERIOD, "." },
    { SYMBOL_MINUS, "-" },
    { SYMBOL_PLUS, "+" },
    { SYMBOL_SLASH, "/" },
    { SYMBOL_STAR, "*" },
    { SYMBOL_AMPERSAND, "&" },
    { SYMBOL_POUND, "#" },
    { SYMBOL_LPAREN, "(" },
    { SYMBOL_RPAREN, ")" },
    { SYMBOL_LBRACKET, "[" },
    { SYMBOL_RBRACKET, "]" },
    { SYMBOL_COLON, ":" },
    { SYMBOL_EQUAL, "=" },
    { SYMBOL_LT, "<" }, 
    { SYMBOL_GT, ">" },
    { SYMBOL_LINE_CONT, "\\" },
};

struct { token_keyword e; const char *str; } static const keyword_pairs[] = {
    { KEYWORD_ON, "on" },
    { KEYWORD_RETURN, "return" },
    { KEYWORD_END, "end" },
    { KEYWORD_IF, "if" },
    { KEYWORD_ELSE, "else" },
    { KEYWORD_THEN, "then" },
    { KEYWORD_REPEAT, "repeat" },
    { KEYWORD_WITH, "with" },
    { KEYWORD_TO, "to" },
    { KEYWORD_WHILE, "while" },
    { KEYWORD_SWITCH, "switch" },
    { KEYWORD_CASE, "case" },
    { KEYWORD_OTHERWISE, "otherwise" },
    { KEYWORD_THE, "the" },
    { KEYWORD_OF, "of" },
    { KEYWORD_PUT, "put" },
    { KEYWORD_AFTER, "after" },
    { KEYWORD_BEFORE, "before" },
    { KEYWORD_TYPE, "type" },
    { KEYWORD_NUMBER, "number" },
    { KEYWORD_INTEGER, "integer" },
    { KEYWORD_STRING, "string" },
    { KEYWORD_POINT, "point" },
    { KEYWORD_RECT, "rect" },
    { KEYWORD_IMAGE, "image" },
    { KEYWORD_GLOBAL, "global" },
    { KEYWORD_PROPERTY, "property" },
    { KEYWORD_AND, "and" },
    { KEYWORD_OR, "or" },
    { KEYWORD_NOT, "not" },
    { KEYWORD_MOD, "mod" },
    // { KEYWORD_TRUE, "true" },
    // { KEYWORD_FALSE, "false" },
    // { KEYWORD_VOID, "void" }
};

token token::make_keyword(token_keyword v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_KEYWORD;
    tok.keyword = v;
    return tok;
}

token token::make_integer(int32_t v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_INTEGER;
    tok.integer = v;
    return tok;
}

token token::make_symbol(token_symbol v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_SYMBOL;
    tok.symbol = v;
    return tok;
}

token token::make_float(double v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_FLOAT;
    tok.number = v;
    return tok;
}

token token::make_identifier(const std::string v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_IDENTIFIER;
    tok.str = std::move(v);
    return tok;
}

token token::make_string(const std::string v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_STRING;
    tok.str = std::move(v);
    return tok;
}

token token::make_line_end(const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_LINE_END;
    return tok;
}

const char* lingo::ast::keyword_to_str(token_keyword keyword) {
    for (size_t i = 0; i < ARRLEN(keyword_pairs); ++i) {
        auto pair = keyword_pairs[i];
        if (pair.e == keyword)
            return pair.str;
    }

    assert(false && "invalid keyword");
    return nullptr;
}

const char* lingo::ast::symbol_to_str(token_symbol symbol) {
    for (size_t i = 0; i < ARRLEN(symbol_pairs); ++i) {
        auto pair = symbol_pairs[i];
        if (pair.e == symbol)
            return pair.str;
    }

    assert(false && "invalid symbol");
    return nullptr;
}

std::string lingo::ast::token_to_str(const token &tok) {
    std::stringstream out;
    out << token_type_str(tok.type);

    switch (tok.type) {
        case TOKEN_IDENTIFIER:
            out << ' ';
            out << tok.str;
            break;

        case TOKEN_KEYWORD:
            out << " '";
            out << keyword_to_str(tok.keyword);
            out << "' ";
            break;

        case TOKEN_SYMBOL:
            out << " '";
            out << symbol_to_str(tok.symbol);
            out << "'";
            break;

        default:
            break;
    }

    return out.str();
}

static bool identify_keyword(const char *str, token_keyword &kw) {
    for (size_t i = 0; i < ARRLEN(keyword_pairs); ++i) {
        auto pair = keyword_pairs[i];
        if (!strcmp(pair.str, str)) {
            kw = pair.e;
            return true;
        }
    }

    return false;
}

static token_symbol identify_symbol(const char *str) {
    for (size_t i = 0; i < ARRLEN(symbol_pairs); ++i) {
        auto pair = symbol_pairs[i];
        if (!strcmp(pair.str, str)) {
            return pair.e;
        }
    }

    return SYMBOL_INVALID;
}

bool lingo::ast::parse_tokens(std::istream &stream, std::vector<token> &tokens,
                              parse_error *error) {
    char wordbuf[64];
    int wordbuf_i = 0;
    wordbuf[0] = '\0';

    enum {
        MODE_NONE,
        MODE_NUMBER,
        MODE_WORD,
        MODE_SYMBOL,
        MODE_STRING,
    } parse_mode;
    parse_mode = MODE_NONE;

    char ch = (char)stream.get();
    bool num_is_float = false;
    std::string strbuf;
    token_symbol tmp_symbol = SYMBOL_INVALID;

    pos_info pos { 1, 1 };
    pos_info word_pos { 1, 1 };

    auto next_char = [&]() {
        if (stream.eof()) {
            ch = '\n';
            return;
        }

        ch = (char)stream.get();
        if (ch == '\n') {
            ++pos.line;
            pos.column = 0;
        } else {
            ++pos.column;
        }
    };

    auto check_line_cont = [&]() {
        if (tokens.size() == 0) return false;

        token &tok = tokens.back();
        return tok.type == TOKEN_SYMBOL && tok.symbol == SYMBOL_LINE_CONT;
    };

    while (true) {
        if (stream.eof() && parse_mode == MODE_NONE) break;

        switch (parse_mode) {
            case MODE_NONE:
                if (isspace(ch)) {
                    if (ch == '\n') {
                        if (check_line_cont()) {
                            tokens.pop_back(); // remove line cont token

                        // don't add a newline token on these conditions:
                        // 1. it will be the first token in the list
                        // 2. the last token previously added is also a newline
                        } else if (tokens.size() > 0 && !tokens.back().is_a(TOKEN_LINE_END)) {
                            tokens.push_back(token::make_line_end(pos));
                        }
                    }
                    
                    next_char();
                } else if (ch == '"') {
                    parse_mode = MODE_STRING;
                    word_pos = pos;
                    next_char();
                    strbuf.clear();
                } else {
                    wordbuf_i = 0;
                    word_pos = pos;

                    if (isalpha(ch) || ch == '_') {
                        parse_mode = MODE_WORD;
                    } else if (isdigit(ch)) {
                        parse_mode = MODE_NUMBER;
                        num_is_float = false;
                    } else {
                        parse_mode = MODE_SYMBOL;
                        tmp_symbol = SYMBOL_INVALID;
                    }
                }

                break;

            case MODE_NUMBER:
                if (!isalnum(ch) && ch != '.') {
                    wordbuf[wordbuf_i++] = '\0';

                    if (num_is_float) {
                        char *str_end;
                        double v = strtod(wordbuf, &str_end);

                        // conversion failed
                        if (str_end != wordbuf + wordbuf_i - 1) {
                            if (error)
                                *error = parse_error {
                                    word_pos,
                                    std::string("could not parse number literal ") + wordbuf
                                };
                            
                            return false;

                        // conversion successful
                        } else {
                            tokens.push_back(token::make_float(v, word_pos));
                            parse_mode = MODE_NONE;
                        }
                    } else {
                        char *str_end;
                        int32_t v = (int32_t)strtol(wordbuf, &str_end, 10);

                        // conversion failed
                        if (str_end != wordbuf + wordbuf_i - 1) {
                            if (error)
                                *error = parse_error {
                                    word_pos,
                                    std::string("could not parse number literal ") + wordbuf
                                };
                            
                            return false;

                        // conversion successful
                        } else {
                            tokens.push_back(token::make_integer(v, word_pos));
                            parse_mode = MODE_NONE;
                        }
                    }
                } else {
                    if (ch == '.') num_is_float = true;
                     
                    wordbuf[wordbuf_i++] = ch;
                    next_char();
                }

                break;

            case MODE_WORD:
                ch = (char)tolower((int)ch);
                if (!(isalnum(ch) || ch == '_')) {
                    wordbuf[wordbuf_i++] = '\0';

                    token_keyword kw;
                    if (identify_keyword(wordbuf, kw)) {
                        tokens.push_back(token::make_keyword(kw, word_pos));
                    } else {
                        tokens.push_back(token::make_identifier(wordbuf, word_pos));
                    }

                    parse_mode = MODE_NONE;
                } else {
                    wordbuf[wordbuf_i++] = ch;
                    next_char();
                }

                break;

            case MODE_SYMBOL: {
                wordbuf[wordbuf_i] = ch;
                wordbuf[++wordbuf_i] = '\0';

                token_symbol symbol = identify_symbol(wordbuf);
                if (symbol == SYMBOL_INVALID) {
                    if (tmp_symbol == SYMBOL_INVALID) {
                        if (error)
                                *error = parse_error {
                                    word_pos,
                                    std::string("invalid symbol ") + wordbuf
                                };
                            
                        return false;
                    }

                    if (tmp_symbol == SYMBOL_COMMENT) {
                        // discard rest of line as it is a comment line
                        while (ch != '\n') next_char();
                    } else {
                        tokens.push_back(token::make_symbol(tmp_symbol, word_pos));
                    }

                    parse_mode = MODE_NONE;
                } else {
                    tmp_symbol = symbol;
                    next_char();
                }

                break;
            }

            case MODE_STRING:
                if (ch == '"') {
                    tokens.push_back(token::make_string(strbuf, word_pos));
                    parse_mode = MODE_NONE;
                } else {
                    strbuf.push_back(ch);
                }
                next_char();

                break;
        }
    }

    tokens.push_back(token::make_line_end(pos));
    return true;
}
