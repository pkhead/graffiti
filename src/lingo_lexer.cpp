#include "lingo.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <unordered_map>
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
    { SYMBOL_LBRACE, "{" },
    { SYMBOL_RBRACE, "}" },
    { SYMBOL_COLON, ":" },
    { SYMBOL_EQUAL, "=" },
    { SYMBOL_LT, "<" }, 
    { SYMBOL_GT, ">" },
    { SYMBOL_LINE_CONT, "\\" },
};

struct { token_keyword e; const char *str; } static const keyword_pairs[] = {
    { KEYWORD_ON, "on" },
    { KEYWORD_ELSE, "else" },
    { KEYWORD_THEN, "then" },
    { KEYWORD_AND, "and" },
    { KEYWORD_OR, "or" },
    { KEYWORD_NOT, "not" },
    { KEYWORD_MOD, "mod" },
    // { KEYWORD_TRUE, "true" },
    // { KEYWORD_FALSE, "false" },
    // { KEYWORD_VOID, "void" }
};

static const std::unordered_map<std::string, token_word_id> str_to_word_id = {
    { "return", WORD_ID_RETURN },
    { "end", WORD_ID_END },
    { "next", WORD_ID_NEXT },
    { "exit", WORD_ID_EXIT },
    { "if", WORD_ID_IF },
    { "repeat", WORD_ID_REPEAT },
    { "with", WORD_ID_WITH },
    { "to", WORD_ID_TO },
    { "down", WORD_ID_DOWN },
    { "while", WORD_ID_WHILE },
    { "case", WORD_ID_CASE },
    { "otherwise", WORD_ID_OTHERWISE },
    { "the", WORD_ID_THE },
    { "of", WORD_ID_OF },
    { "in", WORD_ID_IN },
    { "put", WORD_ID_PUT },
    { "after", WORD_ID_AFTER },
    { "before", WORD_ID_BEFORE },
    { "type", WORD_ID_TYPE },
    { "number", WORD_ID_NUMBER },
    { "integer", WORD_ID_INTEGER },
    { "string", WORD_ID_STRING },
    { "point", WORD_ID_POINT },
    { "rect", WORD_ID_RECT },
    { "image", WORD_ID_IMAGE },
    { "global", WORD_ID_GLOBAL },
    { "property", WORD_ID_PROPERTY },
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

token token::make_word(const std::string &v, const pos_info &pos) {
    token_word_id word_id = WORD_ID_UNKNOWN;
    const auto &word_id_it = str_to_word_id.find(v);
    if (word_id_it != str_to_word_id.end()) {
        word_id = word_id_it->second;
    }

    token tok;
    tok.pos = pos;
    tok.type = TOKEN_WORD;
    tok.str = v;
    tok.word_id = word_id;
    return tok;
}

token token::make_word(token_word_id word_id, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_WORD;
    tok.str = word_id_to_str(word_id);
    tok.word_id = word_id;
    return tok;
}

token token::make_string(const std::string &v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_STRING;
    tok.str = v;
    return tok;
}

token token::make_symbol_literal(const std::string &v, const pos_info &pos) {
    token tok;
    tok.pos = pos;
    tok.type = TOKEN_SYMBOL_LITERAL;
    tok.str = v;
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

const char* lingo::ast::word_id_to_str(token_word_id word_id) {
    for (auto it = str_to_word_id.begin(); it != str_to_word_id.end(); ++it) {
        if (it->second == word_id) {
            return it->first.c_str();
        }
    }

    assert(false && "invalid word_id");
    return nullptr;
}

std::string lingo::ast::token_to_str(const token &tok) {
    std::stringstream out;

    out << token_type_str(tok.type);

    switch (tok.type) {
        case TOKEN_WORD:
            out << " '";
            out << tok.str;
            out << "'";
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
    bool make_symlit = false;
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
                make_symlit = false;

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
                    if (make_symlit) {
                        tokens.push_back(token::make_symbol_literal(wordbuf, word_pos));
                    } else if (identify_keyword(wordbuf, kw)) {
                        tokens.push_back(token::make_keyword(kw, word_pos));
                    } else {
                        tokens.push_back(token::make_word(wordbuf, word_pos));
                    }

                    parse_mode = MODE_NONE;
                    make_symlit = false;
                } else {
                    wordbuf[wordbuf_i++] = ch;
                    next_char();
                }
                
                break;

            case MODE_SYMBOL: {
                make_symlit = false;

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
                    } else if (tmp_symbol == SYMBOL_POUND) {
                        make_symlit = true;
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
                make_symlit = false;

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

    if (tokens.size() > 0 && !tokens.back().is_a(TOKEN_LINE_END)) {
        tokens.push_back(token::make_line_end(pos));
    }
    
    return true;
}
