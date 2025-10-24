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

template <unsigned int Lv = 0>
static std::unique_ptr<ast_expr> parse_expression(token_reader &reader,
                                                  bool assignment = false) {
    static_assert(Lv <= 7, "invalid precedence level");

    // (1) comparison/equality
    if constexpr (Lv == 0) {
        std::unique_ptr<ast_expr> left = parse_expression<Lv+1>(reader);

        const token *tok = &reader.peek();
        while ((tok->is_symbol(SYMBOL_EQUAL) && !assignment) ||
              tok->is_symbol(SYMBOL_NEQUAL) || tok->is_symbol(SYMBOL_GT) ||
              tok->is_symbol(SYMBOL_LT) || tok->is_symbol(SYMBOL_GE) ||
              tok->is_symbol(SYMBOL_LE))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right = parse_expression<Lv+1>(reader);
            
            auto tmp = std::make_unique<ast_expr_binop>();
            tmp->pos = op.pos;
            tmp->left = std::move(left);
            tmp->right = std::move(right);
            switch (op.symbol) {
                case SYMBOL_EQUAL:
                    tmp->op = EXPR_BINOP_EQ;
                    break;

                case SYMBOL_NEQUAL:
                    tmp->op = EXPR_BINOP_NEQ;
                    break;

                case SYMBOL_GT:
                    tmp->op = EXPR_BINOP_GT;
                    break;

                case SYMBOL_LT:
                    tmp->op = EXPR_BINOP_LT;
                    break;

                case SYMBOL_GE:
                    tmp->op = EXPR_BINOP_GE;
                    break;

                case SYMBOL_LE:
                    tmp->op = EXPR_BINOP_LE;
                    break;

                default: throw parse_exception(op.pos, "error parsing Lv0");
            }

            left = std::move(tmp);
        }

        return left;
    }

    // (2) concatenation
    if constexpr (Lv == 1) {
        std::unique_ptr<ast_expr> left = parse_expression<Lv+1>(reader);

        const token *tok = &reader.peek();
        while (tok->is_symbol(lingo::ast::SYMBOL_AMPERSAND) ||
              tok->is_symbol(lingo::ast::SYMBOL_DOUBLE_AMPERSAND))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right = parse_expression<Lv+1>(reader);
            
            auto tmp = std::make_unique<ast_expr_binop>();
            tmp->pos = op.pos;
            tmp->left = std::move(left);
            tmp->right = std::move(right);
            switch (op.symbol) {
                case SYMBOL_AMPERSAND:
                    tmp->op = EXPR_BINOP_CONCAT;
                    break;

                case SYMBOL_DOUBLE_AMPERSAND:
                    tmp->op = EXPR_BINOP_CONCAT_WITH_SPACE;
                    break;

                default: throw parse_exception(op.pos, "error parsing Lv1");
            }

            left = std::move(tmp);
            tok = &reader.peek();
        }

        return left;
    }

    // (3) add/sub
    if constexpr (Lv == 2) {
        std::unique_ptr<ast_expr> left = parse_expression<Lv+1>(reader);

        const token *tok = &reader.peek();
        while (tok->is_symbol(lingo::ast::SYMBOL_PLUS) ||
              tok->is_symbol(lingo::ast::SYMBOL_MINUS))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right = parse_expression<Lv+1>(reader);
            
            auto tmp = std::make_unique<ast_expr_binop>();
            tmp->pos = op.pos;
            tmp->left = std::move(left);
            tmp->right = std::move(right);
            switch (op.symbol) {
                case SYMBOL_PLUS:
                    tmp->op = EXPR_BINOP_ADD;
                    break;

                case SYMBOL_MINUS:
                    tmp->op = EXPR_BINOP_SUB;
                    break;

                default: throw parse_exception(op.pos, "error parsing Lv2");
            }

            left = std::move(tmp);
            tok = &reader.peek();
        }

        return left;
    }

    // (4) mul/div/modulo, binary boolean logic
    if constexpr (Lv == 3) {
        std::unique_ptr<ast_expr> left = parse_expression<Lv+1>(reader);

        const token *tok = &reader.peek();
        while (tok->is_symbol(SYMBOL_STAR) || tok->is_symbol(SYMBOL_SLASH) ||
               tok->is_keyword(KEYWORD_MOD) || tok->is_keyword(KEYWORD_AND) ||
               tok->is_keyword(KEYWORD_OR))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right = parse_expression<Lv+1>(reader);
            
            auto tmp = std::make_unique<ast_expr_binop>();
            tmp->pos = op.pos;
            tmp->left = std::move(left);
            tmp->right = std::move(right);

            if (op.is_a(TOKEN_SYMBOL)) {
                switch (op.symbol) {
                    case SYMBOL_STAR:
                        tmp->op = EXPR_BINOP_MUL;
                        break;

                    case SYMBOL_SLASH:
                        tmp->op = EXPR_BINOP_DIV;
                        break;

                    default: throw parse_exception(op.pos, "error parsing Lv3");
                }
            } else if (op.is_a(TOKEN_KEYWORD)) {
                switch (op.keyword) {
                    case KEYWORD_MOD:
                        tmp->op = EXPR_BINOP_MOD;
                        break;

                    case KEYWORD_AND:
                        tmp->op = EXPR_BINOP_AND;
                        break;

                    case KEYWORD_OR:
                        tmp->op = EXPR_BINOP_OR;
                        break;

                    default: throw parse_exception(op.pos, "error parsing Lv3");
                }
            } else {
                throw parse_exception(op.pos, "error parsing Lv3");
            }

            left = std::move(tmp);
            tok = &reader.peek();
        }

        return left;
    }

    // (5) unaries
    if constexpr (Lv == 4) {
        const token *tok = &reader.peek();

        if (tok->is_symbol(SYMBOL_MINUS)) {
            reader.pop();

            // if next token is a number literal, instead of returning a
            // UnOp(UNOP_NEG, Literal(X)), return a Literal(-X)
            const token &lit = reader.peek(1);
            bool is_float = lit.is_a(TOKEN_FLOAT);
            bool is_int = lit.is_a(TOKEN_INTEGER);

            if (is_float || is_int) {
                auto ret = std::make_unique<ast_expr_literal>();
                ret->pos = tok->pos;

                if (is_float) {
                    ret->literal_type = EXPR_LITERAL_FLOAT;
                    ret->floatv = lit.number;
                } else if (is_int) {
                    ret->literal_type = EXPR_LITERAL_INTEGER;
                    ret->intv = lit.integer;
                }

                return ret;
            }

            auto ret = std::make_unique<ast_expr_unop>();
            ret->pos = tok->pos;
            ret->op = EXPR_UNOP_NEG;
            ret->expr = parse_expression<Lv+1>(reader);
            return ret;
        }

        if (tok->is_keyword(KEYWORD_NOT)) {
            reader.pop();
            auto ret = std::make_unique<ast_expr_unop>();
            ret->pos = tok->pos;
            ret->op = EXPR_UNOP_NOT;
            ret->expr = parse_expression<Lv+1>(reader);
            return ret;
        }
        
        return parse_expression<Lv+1>(reader);
    }

    // function calls
    if constexpr (Lv == 5) {
        auto expr = parse_expression<Lv+1>(reader);

        while (reader.peek().is_symbol(SYMBOL_LPAREN)) {
            pos_info pos = reader.pop().pos;

            std::vector<std::unique_ptr<ast::ast_expr>> args;
            while (!reader.peek().is_symbol(SYMBOL_RPAREN)) {
                args.push_back(parse_expression<0>(reader));

                // pop off optional comma
                if (reader.peek().is_symbol(SYMBOL_COMMA)) {
                    reader.pop();
                }
            }

            reader.pop(); // pop off rparen

            auto call = std::make_unique<ast_expr_call>();
            call->pos = pos;
            call->method = std::move(expr);
            call->arguments = std::move(args);
            expr = std::move(call);
        }

        return expr;
    }

    // dot index, array index
    if constexpr (Lv == 6) {
        auto expr = parse_expression<Lv+1>(reader);

        while (true) {
            const token *op = &reader.peek();
            if (!(op->is_symbol(SYMBOL_PERIOD) ||
                  op->is_symbol(SYMBOL_LBRACKET)))
                break;
            
            reader.pop();
            
            if (op->is_symbol(SYMBOL_PERIOD)) {
                const token *id = &reader.pop();
                tok_expect(*id, TOKEN_IDENTIFIER);

                auto left = std::make_unique<ast_expr_dot>();
                left->pos = op->pos;
                left->expr = std::move(expr);
                left->index = id->str;

                expr = std::move(left);
            } else if (op->is_symbol(SYMBOL_LBRACKET)) {
                // TODO: index ranges
                auto inner = parse_expression<0>(reader);

                const token &term = reader.pop();
                if (!term.is_symbol(SYMBOL_RBRACKET)) {
                    throw parse_exception(
                        term.pos,
                        std::string("expected symbol ']', got ") + token_to_str(term)
                    );
                }

                auto left = std::make_unique<ast_expr_index>();
                left->pos = op->pos;
                left->expr = std::move(expr);
                left->index_from = std::move(inner);
                left->index_to = nullptr;

                expr = std::move(left);
            }
        }
        
        return expr;
    }

    // groups and literals
    if constexpr (Lv == 7) {
        const token &tok = reader.pop();

        if (tok.is_symbol(SYMBOL_LPAREN)) {
            auto expr = parse_expression<0>(reader);

            const token &term = reader.pop();
            if (!term.is_symbol(SYMBOL_RPAREN)) {
                throw parse_exception(
                    term.pos,
                    std::string("expected symbol ')', got ") + token_to_str(term));
            }

            return expr;
        }

        if (tok.is_keyword(KEYWORD_THE)) {
            const token &id = reader.pop();
            tok_expect(id, TOKEN_IDENTIFIER);

            auto ret = std::make_unique<ast_expr_the>();
            ret->pos = tok.pos;

            if (id.str == "moviepath") {
                ret->identifier = EXPR_THE_MOVIE_PATH;
            } else if (id.str == "dirseparator") {
                ret->identifier = EXPR_THE_DIR_SEPARATOR;
            } else if (id.str == "randomseed") {
                ret->identifier = EXPR_THE_RANDOM_SEED;
            } else {
                throw parse_exception(
                    id.pos,
                    std::string("invalid 'the' identifier ") + id.str);
            }

            return ret;
        }

        if (tok.is_a(TOKEN_IDENTIFIER)) {
            // TODO: built-in constants (not done rn because QUOTE = 3 is a valid statement)
            // TODO: phrases
            
            auto ret = std::make_unique<ast_expr_identifier>();
            ret->pos = tok.pos;
            ret->identifier = tok.str;
            return ret;
        }

        if (tok.is_a(TOKEN_FLOAT)) {
            auto ret = std::make_unique<ast_expr_literal>();
            ret->pos = tok.pos;
            ret->literal_type = EXPR_LITERAL_FLOAT;
            ret->floatv = tok.number;
            return ret;
        }

        if (tok.is_a(TOKEN_INTEGER)) {
            auto ret = std::make_unique<ast_expr_literal>();
            ret->pos = tok.pos;
            ret->literal_type = EXPR_LITERAL_INTEGER;
            ret->intv = tok.integer;
            return ret;
        }

        if (tok.is_a(TOKEN_STRING)) {
            auto ret = std::make_unique<ast_expr_literal>();
            ret->pos = tok.pos;
            ret->literal_type = EXPR_LITERAL_STRING;
            ret->str = tok.str;
            return ret;
        }

        throw parse_exception(
            tok.pos,
            std::string("unexpected ") + token_to_str(tok));
    }
}

static std::unique_ptr<ast_statement> parse_statement(token_reader &reader) {
    const token *tok = &reader.peek();
    pos_info line_pos = tok->pos;

    // TODO: global declarations in handlers

    // return statement
    if (tok->is_keyword(KEYWORD_RETURN)) {
        reader.pop();

        std::unique_ptr<ast_expr> return_expr;
        if (!reader.peek().is_a(TOKEN_LINE_END)) {
            return_expr = parse_expression(reader);
        }
        tok_expect(reader.pop(), TOKEN_LINE_END);

        auto stm = std::make_unique<ast_statement_return>();
        stm->pos = line_pos;
        stm->expr = std::move(return_expr);
        return stm;
    
    } else if (tok->is_keyword(KEYWORD_PUT)) {
        reader.pop();

        auto expr = parse_expression(reader);
        std::unique_ptr<ast_expr> source_str;

        int append_mode = 0;
        if (reader.peek().is_keyword(KEYWORD_AFTER)) {
            append_mode = 1;
            reader.pop();
            source_str = parse_expression(reader);
        } else if (reader.peek().is_keyword(KEYWORD_BEFORE)) {
            append_mode = 2;
            reader.pop();
            source_str = parse_expression(reader);
        }

        tok_expect(reader.pop(), TOKEN_LINE_END);

        // print
        if (append_mode == 0) {
            auto stm = std::make_unique<ast_statement_put>();
            stm->pos = line_pos;
            stm->expr = std::move(expr);
            return stm;
        }

        // append to end of string
        if (append_mode == 1) {
            auto stm = std::make_unique<ast_statement_put_on>();
            stm->pos = line_pos;
            stm->expr = std::move(expr);
            stm->target = std::move(source_str);
            stm->before = false;
            return stm;
        }

        // append to start of string
        if (append_mode == 2) {
            auto stm = std::make_unique<ast_statement_put_on>();
            stm->pos = line_pos;
            stm->expr = std::move(expr);
            stm->target = std::move(source_str);
            stm->before = true;
            return stm;
        }

        throw parse_exception(line_pos, "internal error");

    // assignment or invocation
    } else {
        auto expr = parse_expression(reader, true);

        // assignment
        if (reader.peek().is_symbol(SYMBOL_EQUAL)) {
            reader.pop();
            auto value_expr = parse_expression(reader);
            tok_expect(reader.pop(), TOKEN_LINE_END);

            auto stm = std::make_unique<ast_statement_assign>();
            stm->lvalue = std::move(expr);
            stm->rvalue = std::move(value_expr);
            stm->pos = line_pos;
            return stm;

        // handler invocation
        } else {
            throw parse_exception(tok->pos, "handler invocation statement not impl");
        }
    }
    
    // return nullptr;
}

static std::unique_ptr<ast_statement> parse_script_decl(token_reader &reader) {
    const token *tok = &reader.pop();
    pos_info stm_pos = tok->pos;

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
            decl->pos = stm_pos;
            decl->identifiers = ids;
            return std::move(decl);
        } else if (decl_prop) {
            std::unique_ptr<ast_property_declaration> decl =
                std::make_unique<ast_property_declaration>();
            decl->pos = stm_pos;
            decl->identifiers = ids;
            return std::move(decl);
        }

    // method handler header
    } else if (tok->is_keyword(KEYWORD_ON)) {
        tok = &reader.pop();
        tok_expect(*tok, TOKEN_IDENTIFIER);

        auto func = std::make_unique<ast_handler_definition>();
        func->name = tok->str;
        func->pos = stm_pos;

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
                if (paren && tok->is_symbol(SYMBOL_RPAREN)) {
                    break;
                }

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
            // if (paren) {
            //     tok = &reader.pop();
            //     tok_expect(*tok, TOKEN_LINE_END);
            // }
        }

        // pop line end
        tok_expect(reader.pop(), TOKEN_LINE_END);

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

    throw parse_exception(
        tok->pos,
        std::string("unexpected ") + token_type_str(tok->type));
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