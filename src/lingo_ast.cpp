#include "lingo.hpp"
#include <sstream>
#include <stdexcept>
#include <cassert>

using namespace lingo;
using namespace lingo::ast;

class parse_exception : public std::runtime_error {
public:
    pos_info pos;
    std::string msg;

    parse_exception(pos_info pos, const std::string &what = "")
        : pos(pos), msg(what), std::runtime_error(what) { } // TODO: add pos info to error
};


class token_reader {
private:
    size_t index;
    const std::vector<token> &tokens;

public:
    token_reader(const std::vector<token> &tokens) : tokens(tokens), index(0)
    { }

    const bool eof() const {
        return index >= tokens.size();
    }

    const token& pop() {
        if (eof())
            throw parse_exception(tokens.back().pos, "Unexpected EOF");

        return tokens[index++];
    }

    const token& peek() {
        if (eof())
            throw parse_exception(tokens.back().pos, "Unexpected EOF");

        return tokens[index];
    }

    const token& peek(int offset) {
        if (index + offset >= tokens.size())
            throw parse_exception(tokens.back().pos, "Unexpected EOF");

        return tokens[index + offset];
    }
};

static std::string type_errorstr(token_type desired, token_type got) {
    std::stringstream buf;
    buf << "expected ";
    buf << token_type_str(desired);
    buf << ", got ";
    buf << token_type_str(got);
    buf << " instead";
    return buf.str();
}

static inline void tok_expect(const token &tok, token_type type) {
    if (tok.type != type) {
        throw parse_exception(tok.pos, type_errorstr(type, tok.type));
    }
}

static std::unique_ptr<ast_statement> parse_statement(token_reader &reader) {
    while (!reader.pop().is_a(TOKEN_LINE_END));
    return nullptr;
}

static std::unique_ptr<ast_statement> parse_script_decl(token_reader &reader) {
    const token *tok = &reader.pop();
    int line = tok->pos.line;

    // global or property declarations
    bool decl_global = tok->is_keyword(KEYWORD_GLOBAL);
    bool decl_prop = tok->is_keyword(KEYWORD_PROPERTY);
    if (decl_global || decl_prop) {
        std::vector<std::string> ids;
        while (true) {
            tok = &reader.pop();
            tok_expect(*tok, TOKEN_IDENTIFIER);
            ids.push_back(tok->str);

            if (reader.eof() || !reader.peek().is_symbol(SYMBOL_COMMA)) {
                // pop newline token
                if (!reader.eof()) {
                    tok_expect(reader.pop(), TOKEN_LINE_END);
                }

                break;
            }

            // pop comma
            reader.pop();
        }

        if (decl_global) {
            std::unique_ptr<ast_global_declaration> decl =
                std::make_unique<ast_global_declaration>();
            decl->line = line;
            decl->identifiers = ids;
            return std::move(decl);
        } else if (decl_prop) {
            std::unique_ptr<ast_property_declaration> decl =
                std::make_unique<ast_property_declaration>();
            decl->line = line;
            decl->identifiers = ids;
            return std::move(decl);
        }

    // method handler header
    } else if (tok->is_keyword(KEYWORD_ON)) {
        tok = &reader.pop();
        tok_expect(*tok, TOKEN_IDENTIFIER);

        auto func = std::make_unique<ast_handler_definition>();
        func->name = tok->str;
        func->line = line;

        if (!reader.eof() && !reader.peek().is_a(TOKEN_LINE_END)) {
            // read parameters
            // first, see if parameter list is parenthesized or not
            bool paren = false;
            if (reader.peek().is_symbol(SYMBOL_LPAREN)) {
                paren = true;
                reader.pop(); 
            }

            // then read parameters
            while (true) {
                // read param name
                tok = &reader.pop();
                tok_expect(*tok, TOKEN_IDENTIFIER);
                func->params.push_back(tok->str);

                // check eol or comma
                tok = &reader.pop();
                if (tok->is_symbol(SYMBOL_COMMA)) {
                    continue;
                } else if (paren ? tok->is_symbol(SYMBOL_RPAREN) : tok->is_a(TOKEN_LINE_END)) {
                    break;
                } else {
                    throw parse_exception(
                        tok->pos,
                        std::string("unexpected ") + token_type_str(tok->type));
                }
            }

            // pop line end off with parenthesized list
            if (paren) {
                tok = &reader.pop();
                tok_expect(*tok, TOKEN_LINE_END);
            }
        }

        // pop line end
        reader.pop();

        // read statements
        while (!reader.peek().is_keyword(KEYWORD_END)) {
            auto stm = parse_statement(reader);
            if (!stm) continue;

            func->body.push_back(std::move(stm));
        }

        // pop end keyword
        reader.pop();

        // pop line end
        tok_expect(reader.pop(), TOKEN_LINE_END);

        return std::move(func);
    }

    throw parse_exception(tok->pos, std::string("unexpected ") + token_type_str(tok->type));
    return nullptr;
}

bool lingo::ast::parse_ast(const std::vector<token> &tokens, ast_root &root,
                           parse_error *error) {
    token_reader reader(tokens);

    try {
        while (!reader.eof()) {
            auto decl = parse_script_decl(reader);
            assert(decl);

            root.push_back(std::move(decl));
        }
    } catch (parse_exception except) {
        if (error) {
            *error = parse_error { except.pos, except.msg };
        }

        return false;
    }
    
    return true;
}