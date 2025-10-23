#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <cassert>

#define ARRLEN(arr) (sizeof(arr)/sizeof(*arr))

enum token_type_e : uint8_t {
  TOKEN_KEYWORD,
  TOKEN_SYMBOL,
  TOKEN_FLOAT,
  TOKEN_INTEGER,
  TOKEN_IDENTIFIER,
  TOKEN_STRING,
};

enum token_keyword_e : uint8_t {
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

enum token_symbol_e : uint8_t {
  SYMBOL_COMMA, // ,
  SYMBOL_PERIOD, // .

  SYMBOL_MINUS, // -
  SYMBOL_PLUS, // +
  SYMBOL_SLASH, // /
  SYMBOL_STAR, // *
  SYMBOL_AMPERSAND, // &
  
  SYMBOL_LPAREN, // (
  SYMBOL_RPAREN, // )
  SYMBOL_LBRACKET, // [
  SYMBOL_RBRACKET, // ]
  SYMBOL_COLON,

  SYMBOL_EQUAL, // = (both assignment and comparison)
  SYMBOL_NEQUAL, // <>
  SYMBOL_LT, // <
  SYMBOL_GT, // >
  SYMBOL_LE, // <=
  SYMBOL_GE, // >=

  SYMBOL_INVALID = UINT8_MAX,
};

struct { token_symbol_e e; const char *str; } static const symbol_pairs[] = {
  { SYMBOL_LE, "<=" },
  { SYMBOL_GE, ">=" },
  { SYMBOL_NEQUAL, "<>" },

  { SYMBOL_COMMA, "," },
  { SYMBOL_PERIOD, "." },
  { SYMBOL_MINUS, "-" },
  { SYMBOL_PLUS, "+" },
  { SYMBOL_SLASH, "/" },
  { SYMBOL_STAR, "*" },
  { SYMBOL_AMPERSAND, "&" },
  { SYMBOL_LPAREN, "(" },
  { SYMBOL_RPAREN, ")" },
  { SYMBOL_LBRACKET, "[" },
  { SYMBOL_RBRACKET, "]" },
  { SYMBOL_COLON, ":" },
  { SYMBOL_EQUAL, "=" },
  { SYMBOL_LT, "<" }, 
  { SYMBOL_GT, ">" },
};

struct { token_keyword_e e; const char *str; } static const keyword_pairs[] = {
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
  { KEYWORD_TYPE, "type" },
  { KEYWORD_NUMBER, "number" },
  { KEYWORD_INTEGER, "integer" },
  { KEYWORD_STRING, "string" },
  { KEYWORD_POINT, "point" },
  { KEYWORD_RECT, "rect" },
  { KEYWORD_IMAGE, "image" },
  { KEYWORD_GLOBAL, "global" },
  { KEYWORD_AND, "and" },
  { KEYWORD_OR, "or" },
  { KEYWORD_NOT, "not" },
  { KEYWORD_TRUE, "true" },
  { KEYWORD_FALSE, "false" },
  { KEYWORD_VOID, "void" }
};

struct pos_info_s {
  int line; // 1-indexed
  int column; // 1-indexed
};

struct token_s {
  token_type_e type;
  pos_info_s pos;

  std::string str;
  union {
    token_keyword_e keyword;
    token_symbol_e symbol;
    double number;
    int32_t integer;
  };

  static token_s make_keyword(token_keyword_e v, const pos_info_s &pos) {
    token_s tok;
    tok.pos = pos;
    tok.type = TOKEN_KEYWORD;
    tok.keyword = v;
    return tok;
  }

  static token_s make_integer(int32_t v, const pos_info_s &pos) {
    token_s tok;
    tok.pos = pos;
    tok.type = TOKEN_INTEGER;
    tok.integer = v;
    return tok;
  }

  static token_s make_symbol(token_symbol_e v, const pos_info_s &pos) {
    token_s tok;
    tok.pos = pos;
    tok.type = TOKEN_SYMBOL;
    tok.symbol = v;
    return tok;
  }

  static token_s make_float(double v, const pos_info_s &pos) {
    token_s tok;
    tok.pos = pos;
    tok.type = TOKEN_FLOAT;
    tok.number = v;
    return tok;
  }

  static token_s make_identifier(const std::string v, const pos_info_s &pos) {
    token_s tok;
    tok.pos = pos;
    tok.type = TOKEN_IDENTIFIER;
    tok.str = std::move(v);
    return tok;
  }

  static token_s make_string(const std::string v, const pos_info_s &pos) {
    token_s tok;
    tok.pos = pos;
    tok.type = TOKEN_STRING;
    tok.str = std::move(v);
    return tok;
  }
};

bool identify_keyword(const char *str, token_keyword_e &kw) {
  for (int i = 0; i < ARRLEN(keyword_pairs); ++i) {
    auto pair = keyword_pairs[i];
    if (!strcmp(pair.str, str)) {
      kw = pair.e;
      return true;
    }
  }

  return false;
}

const char* keyword_to_str(token_keyword_e keyword) {
  for (int i = 0; i < ARRLEN(keyword_pairs); ++i) {
    auto pair = keyword_pairs[i];
    if (pair.e == keyword)
      return pair.str;
  }

  assert(false && "invalid keyword");
  return nullptr;
}

token_symbol_e identify_symbol(const char *str) {
  for (int i = 0; i < ARRLEN(symbol_pairs); ++i) {
    auto pair = symbol_pairs[i];
    if (!strcmp(pair.str, str)) {
      return pair.e;
    }
  }

  return SYMBOL_INVALID;
}

const char* symbol_to_str(token_symbol_e symbol) {
  for (int i = 0; i < ARRLEN(symbol_pairs); ++i) {
    auto pair = symbol_pairs[i];
    if (pair.e == symbol)
      return pair.str;
  }

  assert(false && "invalid symbol");
  return nullptr;
}

struct parse_error_s {
  pos_info_s pos;
  std::string errmsg;
};

bool parse_tokens(std::istream &stream, std::vector<token_s> &tokens, parse_error_s *error) {
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

  char ch = stream.get();
  bool num_is_float = false;
  std::string strbuf;
  token_symbol_e tmp_symbol = SYMBOL_INVALID;

  pos_info_s pos { 1, 1 };
  pos_info_s word_pos { 1, 1 };

  auto next_char = [&]() {
    if (stream.eof()) {
      ch = '\n';
      return;
    }

    ch = stream.get();
    if (ch == '\n') {
      ++pos.line;
      pos.column = 0;
    } else {
      ++pos.column;
    }
  };

  while (true) {
    if (stream.eof() && parse_mode == MODE_NONE) break;

    switch (parse_mode) {
      case MODE_NONE:
        if (isspace(ch)) {
          next_char();
        } else if (ch == '"') {
          parse_mode = MODE_STRING;
          word_pos = pos;
          next_char();
          strbuf.clear();
        } else {
          wordbuf_i = 0;
          word_pos = pos;

          if (isalpha(ch)) {
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
        if (ch != '.' && !isdigit(ch)) {
          wordbuf[wordbuf_i++] = '\0';

          if (num_is_float) {
            char *str_end;
            double v = strtod(wordbuf, &str_end);
            if (str_end == wordbuf) { // conversion failed
              if (error)
                *error = parse_error_s {
                  word_pos,
                  "could not parse number literal"
                };
              
              return false;
            } else {
              tokens.push_back(token_s::make_float(v, word_pos));
              parse_mode = MODE_NONE;
            }
          } else {
            char *str_end;
            int32_t v = (int32_t)strtol(wordbuf, &str_end, 10);
            if (str_end == wordbuf) { // conversion failed
              if (error)
                *error = parse_error_s {
                  word_pos,
                  "could not parse number literal"
                };
              
              return false;
            } else {
              tokens.push_back(token_s::make_integer(v, word_pos));
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
        if (!isalnum(ch)) {
          wordbuf[wordbuf_i++] = '\0';

          token_keyword_e kw;
          if (identify_keyword(wordbuf, kw)) {
            tokens.push_back(token_s::make_keyword(kw, word_pos));
          } else {
            tokens.push_back(token_s::make_identifier(wordbuf, word_pos));
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

        token_symbol_e symbol = identify_symbol(wordbuf);
        if (symbol == SYMBOL_INVALID) {
          if (tmp_symbol == SYMBOL_INVALID) {
            if (error)
                *error = parse_error_s {
                  word_pos,
                  std::string("invalid symbol ") + wordbuf
                };
              
            return false;
          }

          tokens.push_back(token_s::make_symbol(tmp_symbol, word_pos));
          parse_mode = MODE_NONE;
        } else {
          tmp_symbol = symbol;
          next_char();
        }

        break;
      }

      case MODE_STRING:
        if (ch == '"') {
          tokens.push_back(token_s::make_string(strbuf, word_pos));
          parse_mode = MODE_NONE;
          next_char();
        } else {
          strbuf.push_back(ch);
        }

        break;
    }
  }

  return true;
}

int main(int argc, const char *argv[]) {
  if (argc == 1) {
    std::cerr << "expected filename as first argument\n";
    return 2;
  }

  std::ifstream stream(argv[1]);
  if (!stream.is_open()) {
    std::cerr << "could not open file " << argv[1] << "\n";
  }

  std::vector<token_s> tokens;
  parse_error_s error;
  if (!parse_tokens(stream, tokens, &error)) {
    std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
  } else {
    for (auto &tok : tokens) {
      switch (tok.type) {
        case TOKEN_FLOAT:
          std::cout << "(FLT) ";
          std::cout << tok.number;
          break;

        case TOKEN_INTEGER:
          std::cout << "(INT) ";
          std::cout << tok.integer;
          break;

        case TOKEN_KEYWORD:
          std::cout << "(KYW) ";
          std::cout << keyword_to_str(tok.keyword);
          break;

        case TOKEN_IDENTIFIER:
          std::cout << "(IDN) ";
          std::cout << tok.str;
          break;
        
        case TOKEN_SYMBOL:
          std::cout << "(SYM) ";
          std::cout << symbol_to_str(tok.symbol);
          break;

        case TOKEN_STRING:
          std::cout << "(STR) ";
          std::cout << tok.str;
          break;
      }

      std::cout << "\n";
    }
  }

  return 0;
}
