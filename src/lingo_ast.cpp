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

struct script_scope {
    std::set<std::string> properties;
    std::set<std::string> globals;

    bool has_var(const std::string &name, ast_scope *scope) const {
        if (properties.find(name) != properties.end()) {
            *scope = SCOPE_PROPERTY;
            return true;
        }

        if (globals.find(name) != globals.end()) {
            *scope = SCOPE_GLOBAL;
            return true;
        }

        return false;
    }
};

struct handler_scope {
    std::set<std::string> globals;
    std::set<std::string> locals;
    std::set<std::string> params;
    script_scope *parent_scope;

    bool has_var(const std::string &name, ast_scope *scope) const {
        ast_scope parent_var_scope = SCOPE_LOCAL;
        bool parent_has_var = false;
        if (parent_scope) {
            parent_has_var = parent_scope->has_var(name, &parent_var_scope);
        }

        // properties always take highest precedence
        if (parent_has_var && parent_var_scope == SCOPE_PROPERTY) {
            if (scope) *scope = parent_var_scope;
            return true;
        }

        if (locals.find(name) != locals.end()) {
            if (scope) *scope = SCOPE_LOCAL;
            return true;
        }

        if (params.find(name) != params.end()) {
            if (scope) *scope = SCOPE_LOCAL;
            return true;
        }

        if (globals.find(name) != globals.end()) {
            if (scope) *scope = SCOPE_GLOBAL;
            return true;
        }

        if (parent_has_var && scope)
            *scope = parent_var_scope;

        return parent_has_var;
    }
};

struct parse_ctx {
    handler_scope &scope;

    constexpr parse_ctx(handler_scope &scope) noexcept : scope(scope)
    { }
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

static inline void tok_expect(const token &tok, token_word_id word_id) {
    if (tok.type != TOKEN_WORD || tok.word_id != word_id) {
        std::stringstream buf;
        buf << "expected '";
        buf << word_id_to_str(word_id);
        buf << "', got ";
        buf << token_to_str(tok);
        buf << " instead";
        throw parse_exception(tok.pos, buf.str());
    }
}

static inline void tok_expect(const token &tok, token_keyword kw) {
    if (tok.type != TOKEN_KEYWORD || tok.keyword != kw) {
        std::stringstream buf;
        buf << "expected '";
        buf << keyword_to_str(kw);
        buf << "', got ";
        buf << token_to_str(tok);
        buf << " instead";
        throw parse_exception(tok.pos, buf.str());
    }
}

static inline void tok_expect(const token &tok, token_symbol sym) {
    if (tok.type != TOKEN_SYMBOL || tok.symbol != sym) {
        std::stringstream buf;
        buf << "expected symbol '";
        buf << symbol_to_str(sym);
        buf << "', got ";
        buf << token_to_str(tok);
        buf << " instead";
        throw parse_exception(tok.pos, buf.str());
    }
}

template <unsigned int Lv = 0>
static std::unique_ptr<ast_expr>
parse_expression(token_reader &reader, parse_ctx &ctx,
                 bool assignment = false) {
    static_assert(Lv <= 7, "invalid precedence level");

    // (1) comparison/equality
    if constexpr (Lv == 0) {
        std::unique_ptr<ast_expr> left =
            parse_expression<Lv+1>(reader, ctx, assignment);

        const token *tok = &reader.peek();
        while ((tok->is_symbol(SYMBOL_EQUAL) && !assignment) ||
              tok->is_symbol(SYMBOL_NEQUAL) || tok->is_symbol(SYMBOL_GT) ||
              tok->is_symbol(SYMBOL_LT) || tok->is_symbol(SYMBOL_GE) ||
              tok->is_symbol(SYMBOL_LE))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right =
                parse_expression<Lv+1>(reader, ctx);
            
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
            tok = &reader.peek();
        }

        return left;
    }

    // (2) concatenation
    if constexpr (Lv == 1) {
        std::unique_ptr<ast_expr> left =
            parse_expression<Lv+1>(reader, ctx);

        const token *tok = &reader.peek();
        while (tok->is_symbol(lingo::ast::SYMBOL_AMPERSAND) ||
              tok->is_symbol(lingo::ast::SYMBOL_DOUBLE_AMPERSAND))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right =
                parse_expression<Lv+1>(reader, ctx);
            
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
        std::unique_ptr<ast_expr> left =
            parse_expression<Lv+1>(reader, ctx);

        const token *tok = &reader.peek();
        while (tok->is_symbol(lingo::ast::SYMBOL_PLUS) ||
              tok->is_symbol(lingo::ast::SYMBOL_MINUS))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right =
                parse_expression<Lv+1>(reader, ctx);
            
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
        std::unique_ptr<ast_expr> left =
            parse_expression<Lv+1>(reader, ctx);

        const token *tok = &reader.peek();
        while (tok->is_symbol(SYMBOL_STAR) || tok->is_symbol(SYMBOL_SLASH) ||
               tok->is_keyword(KEYWORD_MOD) || tok->is_keyword(KEYWORD_AND) ||
               tok->is_keyword(KEYWORD_OR))
        {
            const token &op = reader.pop();
            std::unique_ptr<ast_expr> right =
                parse_expression<Lv+1>(reader, ctx);
            
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
            ret->expr = parse_expression<Lv+1>(reader, ctx);
            return ret;
        }

        if (tok->is_keyword(KEYWORD_NOT)) {
            reader.pop();
            auto ret = std::make_unique<ast_expr_unop>();
            ret->pos = tok->pos;
            ret->op = EXPR_UNOP_NOT;
            ret->expr = parse_expression<Lv+1>(reader, ctx);
            return ret;
        }
        
        return parse_expression<Lv+1>(reader, ctx);
    }

    // dot index, array index, function call
    if constexpr (Lv == 5) {
        auto expr = parse_expression<Lv+1>(reader, ctx);

        while (true) {
            const token *op = &reader.peek();

            // func call
            if (op->is_symbol(SYMBOL_LPAREN)) {
                pos_info pos = reader.pop().pos;

                std::vector<std::unique_ptr<ast::ast_expr>> args;
                int argn = 0;
                while (!reader.peek().is_symbol(SYMBOL_RPAREN)) {
                    args.push_back(parse_expression<0>(reader, ctx));

                    // pop off optional comma
                    const token &tok = reader.peek();
                    if (argn > 0) {
                        if (!tok.is_symbol(SYMBOL_RPAREN)) {
                            tok_expect(tok, SYMBOL_COMMA);
                            reader.pop();
                        }
                    } else if (reader.peek().is_symbol(SYMBOL_COMMA)) {
                        reader.pop();
                    }

                    ++argn;
                }

                reader.pop(); // pop off rparen

                auto call = std::make_unique<ast_expr_call>();
                call->pos = pos;
                call->method = std::move(expr);
                call->arguments = std::move(args);
                expr = std::move(call);

            // dot or array index
            } else if (op->is_symbol(SYMBOL_PERIOD) ||
                       op->is_symbol(SYMBOL_LBRACKET)) {            
                reader.pop();
                
                if (op->is_symbol(SYMBOL_PERIOD)) {
                    const token *id = &reader.pop();
                    tok_expect(*id, TOKEN_WORD);

                    auto left = std::make_unique<ast_expr_dot>();
                    left->pos = op->pos;
                    left->expr = std::move(expr);
                    left->index = id->str;

                    expr = std::move(left);
                } else if (op->is_symbol(SYMBOL_LBRACKET)) {
                    // TODO: index ranges
                    auto inner = parse_expression<0>(reader, ctx);

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
            } else {
                break;
            }
        }
        
        return expr;
    }

    // groups and literals
    if constexpr (Lv == 6) {
        const token &tok = reader.pop();

        if (tok.is_symbol(SYMBOL_LPAREN)) {
            auto expr = parse_expression<0>(reader, ctx);

            const token &term = reader.pop();
            if (!term.is_symbol(SYMBOL_RPAREN)) {
                throw parse_exception(
                    term.pos,
                    std::string("expected symbol ')', got ") + token_to_str(term));
            }

            return expr;
        }

        if (tok.is_word(WORD_ID_THE)) {
            const token &id = reader.pop();
            tok_expect(id, TOKEN_WORD);

            auto ret = std::make_unique<ast_expr_the>();
            ret->pos = tok.pos;

            if (id.str == "moviepath") {
                ret->identifier = EXPR_THE_MOVIE_PATH;
            } else if (id.str == "frame") {
                ret->identifier = EXPR_THE_FRAME;
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

        if (tok.is_a(TOKEN_WORD)) {
            // TODO: built-in constants (not done rn because QUOTE = 3 is a valid statement)
            #define MAKE_INT(v) std::make_unique<ast_expr_literal>(ast_expr_literal::make_int(tok.pos, v))
            #define MAKE_FLOAT(v) std::make_unique<ast_expr_literal>(ast_expr_literal::make_float(tok.pos, v))
            #define MAKE_STRING(v) std::make_unique<ast_expr_literal>(ast_expr_literal::make_string(tok.pos, v))
            #define MAKE_VOID() std::make_unique<ast_expr_literal>(ast_expr_literal::make_void(tok.pos))

            if (tok.str == "true") {
                return MAKE_INT(1);
            }
            else if (tok.str == "false") {
                return MAKE_INT(0);
            }
            else if (tok.str == "pi") {
                return MAKE_FLOAT(3.14159265358979323846);
            }
            else if (tok.str == "quote") {
                return MAKE_STRING("\"");
            }
            else if (tok.str == "empty") {
                return MAKE_STRING("");
            }
            else if (tok.str == "enter") {
                return MAKE_STRING("\x03"); // wtf is this character??
            }
            else if (tok.str == "return") {
                return MAKE_STRING("\r");
            }
            else if (tok.str == "space") {
                return MAKE_STRING(" ");
            }
            else if (tok.str == "tab") {
                return MAKE_STRING("\t");
            }
            else if (tok.str == "backspace") {
                return MAKE_STRING("\b");
            }
            else if (tok.str == "void") {
                return MAKE_VOID();
            }
            // TODO: phrases

            // throw on use of undeclared identifier
            // but don't do so if the identifier is immediately followed by an
            // rparen, because dynamic dispatch for function call.
            // kind of a hack but whatever.
            ast_scope var_scope;
            bool func_call = !reader.eof() && reader.peek().is_symbol(SYMBOL_LPAREN);
            if (!ctx.scope.has_var(tok.str, &var_scope) &&
                !func_call) {
                throw parse_exception(tok
                    .pos,
                    "use of undeclared variable '" + tok.str + "'");
            }

            auto ret = std::make_unique<ast_expr_identifier>();
            ret->pos = tok.pos;
            ret->identifier = tok.str;
            ret->scope = func_call ? SCOPE_LOCAL : var_scope;

            return ret;

            #undef MAKE_INT
            #undef MAKE_FLOAT
            #undef MAKE_STRING
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

// a statement which is formatted like this
//   <ident> [arg1 [, arg2 [, arg3 ...]]]
// will call handler <ident> with the given args.
// it's very evil.
inline static bool check_handler_invocation_statement(token_reader &reader) {
    if (!reader.peek().is_a(TOKEN_WORD)) return false;

    const token &next_tok = reader.peek(1);
    return next_tok.is_a(TOKEN_LINE_END)    ||
            next_tok.is_a(TOKEN_WORD)        ||
            next_tok.is_a(TOKEN_STRING)      ||
            next_tok.is_a(TOKEN_FLOAT)       ||
            next_tok.is_a(TOKEN_INTEGER)     ||
            next_tok.is_symbol(SYMBOL_POUND);
}

static std::unique_ptr<ast_statement>
parse_statement(token_reader &reader, handler_scope &scope) {
    const token *tok = &reader.peek();
    pos_info line_pos = tok->pos;
    parse_ctx ctx(scope);

    // TODO: global declarations in handlers

    // variable assignment
    if (reader.peek( ).is_a(TOKEN_WORD) &&
        reader.peek(1).is_symbol(SYMBOL_EQUAL))
    {
        // pop identifier
        const auto &id_tok = reader.pop();
        // pop equals
        reader.pop();

        auto value_expr = parse_expression(reader, ctx);
        tok_expect(reader.pop(), TOKEN_LINE_END);

        std::string var_name = id_tok.str;

        // if variable does not exist, then declare a new local variable
        ast_scope var_scope;
        if (!scope.has_var(var_name, &var_scope)) {
            var_scope = SCOPE_LOCAL;
            scope.locals.insert(var_name);
        }

        // create statement node
        auto stm = std::make_unique<ast_statement_assign>();
        auto id_expr = std::make_unique<ast_expr_identifier>();
        id_expr->pos = id_tok.pos;
        id_expr->identifier = var_name;
        id_expr->scope = var_scope;

        stm->lvalue = std::move(id_expr);
        stm->rvalue = std::move(value_expr);
        stm->pos = line_pos;

        return stm;
    
    // return statement
    } else if (tok->is_word(WORD_ID_RETURN)) {
        reader.pop();

        std::unique_ptr<ast_expr> return_expr;
        if (!reader.peek().is_a(TOKEN_LINE_END)) {
            return_expr = parse_expression(reader, ctx);
        }
        tok_expect(reader.pop(), TOKEN_LINE_END);

        auto stm = std::make_unique<ast_statement_return>();
        stm->pos = line_pos;
        stm->expr = std::move(return_expr);
        return stm;
    
    // put statement
    } else if (tok->is_word(WORD_ID_PUT)) {
        reader.pop();

        auto expr = parse_expression(reader, ctx);
        std::unique_ptr<ast_expr> source_str;

        int append_mode = 0;
        if (reader.peek().is_word(WORD_ID_AFTER)) {
            append_mode = 1;
            reader.pop();
            source_str = parse_expression(reader, ctx);
        } else if (reader.peek().is_word(WORD_ID_BEFORE)) {
            append_mode = 2;
            reader.pop();
            source_str = parse_expression(reader, ctx);
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
    
    // if statement
    } else if (tok->is_word(WORD_ID_IF)) {
        reader.pop();

        auto if_stm = std::make_unique<ast_statement_if>();
        if_stm->pos = line_pos;
        bool else_allowed = false;

        while (true) {
            std::unique_ptr<ast_expr> cond_expr;
            std::vector<std::unique_ptr<ast_statement>> body;

            // check if this is an else if or a terminating else
            bool is_else = false;
            if (else_allowed) {
                if (reader.peek().is_word(WORD_ID_IF)) {
                    reader.pop();
                } else {
                    is_else = true;
                }
            }

            // parse condition if this is not the else branch
            if (!is_else) {
                cond_expr = parse_expression(reader, ctx);

                // expect then keyword
                if (!reader.peek().is_keyword(KEYWORD_THEN)) {
                    throw parse_exception(
                        reader.peek().pos,
                        "expected keyword 'then', got " + token_to_str(reader.peek()));
                }

                reader.pop(); // pop then keyword
            }

            // if not eol, then if statement shoule be of this form:
            //   if <cond> then <statement>
            // if eol is present, then it should be of this form:
            //    if <cond> then
            //      <block>
            //    (end if) | (else if) | (else)
            if (reader.peek().is_a(TOKEN_LINE_END)) {
                reader.pop();
                
                while (true) {
                    tok = &reader.peek();
                    if (tok->is_word(WORD_ID_END) || tok->is_keyword(KEYWORD_ELSE))
                        break;
                    
                    body.push_back(parse_statement(reader, scope));
                }

                // end if found, stop and commit
                if (is_else) {
                    if_stm->has_else = true;
                    if_stm->else_branch = std::move(body);
                } else {
                    auto branch = std::make_unique<ast_if_branch>();
                    branch->condition = std::move(cond_expr);
                    branch->body = std::move(body);
                    if_stm->branches.push_back(std::move(branch));
                }

                // check if it is an "end if" or an "else"
                // if it is an "end if", terminate if processing.
                // otherwise, processing may continue.
                tok = &reader.peek();
                if (is_else) {
                    tok_expect(*tok, WORD_ID_END);
                }

                if (tok->is_word(WORD_ID_END)) {
                    reader.pop();
                    if (!reader.pop().is_word(WORD_ID_IF)) {
                        throw parse_exception(tok->pos, "expected end if");
                    }

                    tok_expect(reader.pop(), TOKEN_LINE_END);
                    break;
                } else {
                    tok_expect(reader.pop(), KEYWORD_ELSE);
                    else_allowed = true;
                }
            } else {
                if (is_else) {
                    if_stm->has_else = true;
                    if_stm->else_branch.push_back(parse_statement(reader, scope));
                } else {
                    auto branch = std::make_unique<ast_if_branch>();
                    branch->condition = std::move(cond_expr);
                    branch->body.push_back(parse_statement(reader, scope));
                    if_stm->branches.push_back(std::move(branch));
                }
                
                // tok_expect(reader.pop(), TOKEN_LINE_END);
                if (!reader.peek().is_keyword(KEYWORD_ELSE))
                    break;

                reader.pop();
                else_allowed = true;
            }
        }

        return if_stm;

    // repeat statement
    } else if (tok->is_word(WORD_ID_REPEAT)) {
        reader.pop();

        tok = &reader.pop();

        auto read_repeat_body = [&]() {
            std::vector<std::unique_ptr<ast_statement>> stms;
            while (!reader.peek().is_word(WORD_ID_END)) {
                stms.push_back(parse_statement(reader, scope));
            }

            // pop end keyword
            tok = &reader.pop();
            if (!reader.pop().is_word(WORD_ID_REPEAT)) {
                throw parse_exception(tok->pos, "expected end repeat");
            }

            tok_expect(reader.pop(), TOKEN_LINE_END);
            return stms;
        };

        if (tok->is_word(WORD_ID_WITH)) {
            const token *id_tok = &reader.pop();
            tok_expect(*id_tok, TOKEN_WORD);

            ast_scope id_scope = SCOPE_LOCAL;
            if (!scope.has_var(id_tok->str, &id_scope)) {
                scope.locals.insert(id_tok->str);
                id_scope = SCOPE_LOCAL;
            }

            auto id_expr = std::make_unique<ast_expr_identifier>();
            id_expr->pos = id_tok->pos;
            id_expr->identifier = id_tok->str;
            id_expr->scope = id_scope;

            // numeric for
            //   repeat with <var> = <init> to <stop> then
            // or
            //   repeat with <var> = <init> down to <stop> then
            tok = &reader.pop();
            if (tok->is_symbol(SYMBOL_EQUAL)) {
                auto init_expr = parse_expression(reader, ctx);

                bool down = false;
                if (reader.peek().is_word(WORD_ID_DOWN)) {
                    reader.pop();
                    down = true;
                }

                tok_expect(reader.pop(), WORD_ID_TO);
                auto stop_expr = parse_expression(reader, ctx);

                // discard the rest of the line [sic]
                while (!reader.pop().is_a(TOKEN_LINE_END));

                auto repeat_stm = std::make_unique<ast_statement_repeat_to>();
                repeat_stm->body = read_repeat_body();
                repeat_stm->pos = line_pos;
                repeat_stm->iterator = std::move(id_expr);
                repeat_stm->init = std::move(init_expr);
                repeat_stm->to = std::move(stop_expr);
                repeat_stm->down = down;
                return repeat_stm;
            }

            // iterable object
            else if (tok->is_word(WORD_ID_IN)) {
                auto iterable = parse_expression(reader, ctx);

                // discard the rest of the line [sic]
                while (!reader.pop().is_a(TOKEN_LINE_END));

                auto repeat_stm = std::make_unique<ast_statement_repeat_in>();
                repeat_stm->body = read_repeat_body();
                repeat_stm->pos = line_pos;
                repeat_stm->iterator = std::move(id_expr);
                repeat_stm->iterable = std::move(iterable);
                return repeat_stm;
            }

            else {
                throw parse_exception(
                    tok->pos,
                    "expected '=' or 'in', got " + token_to_str(*tok));
            }
        }
        else if (tok->is_word(WORD_ID_WHILE)) {
            auto cond_expr = parse_expression(reader, ctx);

            // discard the rest of the line [sic]
            while (!reader.pop().is_a(TOKEN_LINE_END));

            auto repeat_stm = std::make_unique<ast_statement_repeat_while>();
            repeat_stm->body = read_repeat_body();
            repeat_stm->pos = line_pos;
            repeat_stm->condition = std::move(cond_expr);
            return repeat_stm;
        } else {
            throw parse_exception(
                tok->pos,
                "expected 'while' or 'with', got " + token_to_str(*tok));
        }

    // a statement which is formatted like this
    //   <ident> [arg1 [, arg2 [, arg3 ...]]]
    // will call handler <ident> with the given args.
    // it's very evil.
    } else if (check_handler_invocation_statement(reader)) {
        const token &id_tok = reader.pop();
        tok_expect(id_tok, TOKEN_WORD);

        auto ident_expr = std::make_unique<ast_expr_identifier>();
        ident_expr->pos = id_tok.pos;
        ident_expr->scope = SCOPE_LOCAL;
        ident_expr->identifier = id_tok.str;

        auto call_expr = std::make_unique<ast_expr_call>();
        call_expr->pos = line_pos;
        call_expr->method = std::move(ident_expr);

        // parse arguments
        int argn = 0;
        while (!reader.peek().is_a(TOKEN_LINE_END)) {
            auto arg_expr = parse_expression(reader, ctx);

            // only the first comma is optional [sic]
            // seriously what the fuck. like why. how.
            tok = &reader.peek();
            if (argn > 0) {
                if (!tok->is_a(TOKEN_LINE_END)) {
                    tok_expect(*tok, SYMBOL_COMMA);
                    reader.pop();
                } 
            } else if (tok->is_symbol(SYMBOL_COMMA)) {
                reader.pop();
            }

            call_expr->arguments.push_back(std::move(arg_expr));
            ++argn;
        }

        reader.pop(); // pop newline

        // create and return expression statement
        auto stm = std::make_unique<ast_statement_expr>();
        stm->pos = line_pos;
        stm->expr = std::move(call_expr);
        return std::move(stm);
    
    // expression assignment or evaluation
    } else {
        auto expr = parse_expression(reader, ctx, true);

        // assignment
        if (reader.peek().is_symbol(SYMBOL_EQUAL)) {
            reader.pop();
            auto value_expr = parse_expression(reader, ctx);
            tok_expect(reader.pop(), TOKEN_LINE_END);

            auto stm = std::make_unique<ast_statement_assign>();
            stm->lvalue = std::move(expr);
            stm->rvalue = std::move(value_expr);
            stm->pos = line_pos;

            return stm;

        // expression evaluation
        } else {
            tok_expect(reader.pop(), TOKEN_LINE_END);

            auto stm = std::make_unique<ast_statement_expr>();
            stm->pos = line_pos;
            stm->expr = std::move(expr);
            return stm;
        }
    }
    
    // return nullptr;
}

static std::unique_ptr<ast_handler_decl>
parse_script_decl(token_reader &reader, script_scope &scope) {
    const token *tok = &reader.pop();
    pos_info stm_pos = tok->pos;

    // global or property declarations
    bool decl_global = tok->is_word(WORD_ID_GLOBAL);
    bool decl_prop = tok->is_word(WORD_ID_PROPERTY);
    if (decl_global || decl_prop) {
        std::vector<std::string> ids;
        while (true) {
            tok = &reader.pop();
            tok_expect(*tok, TOKEN_WORD);

            std::set<std::string> *set;
            const char *err_prefix;
            if (decl_global) {
                set = &scope.globals;
                err_prefix = "global '";
            } else  {
                assert(decl_prop);
                set = &scope.properties;
                err_prefix = "property '";
            }

            if (set->find(tok->str) != set->end()) {
                throw parse_exception(
                    tok->pos,
                    std::string(err_prefix) + tok->str + "' already declared");
            }
            
            ids.push_back(tok->str);
            set->insert(tok->str);

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

        return nullptr;

    // method handler header
    } else if (tok->is_keyword(KEYWORD_ON)) {
        tok = &reader.pop();
        tok_expect(*tok, TOKEN_WORD);

        auto func = std::make_unique<ast_handler_decl>();
        func->name = tok->str;
        func->pos = stm_pos;

        handler_scope handler_scope;
        handler_scope.parent_scope = &scope;

        // first, see if parameter list is parenthesized or not
        bool paren = false;
        if (reader.peek().is_symbol(SYMBOL_LPAREN)) {
            paren = true;
            reader.pop(); 
        }

        // read parameters
        while (true) {
            tok = &reader.pop();

            // check if param list has ended
            if (paren ? tok->is_symbol(SYMBOL_RPAREN) : tok->is_a(TOKEN_LINE_END))
                break;

            // read param name
            tok_expect(*tok, TOKEN_WORD);

            if (handler_scope.params.find(tok->str) != handler_scope.params.end()) {
                throw parse_exception(
                    tok->pos,
                    std::string("parameter '") + tok->str + "' already declared");
            }

            func->params.push_back(tok->str);
            handler_scope.params.insert(tok->str);

            // pop comma
            if (reader.peek().is_symbol(SYMBOL_COMMA)) {
                reader.pop();
            }
        }

        // pop line end off with parenthesized list
        if (paren) {
            tok = &reader.pop();
            tok_expect(*tok, TOKEN_LINE_END);
        }

        // pop line end
        // tok_expect(reader.pop(), TOKEN_LINE_END);

        // read statements
        while (!reader.peek().is_word(WORD_ID_END)) {
            auto stm = parse_statement(reader, handler_scope);
            if (!stm) continue;

            func->body.push_back(std::move(stm));
        }

        // pop end keyword
        reader.pop();

        // pop line end
        tok_expect(reader.pop(), TOKEN_LINE_END);

        for (auto &local_name : handler_scope.locals) {
            func->locals.push_back(local_name);
        }

        return std::move(func);
    }

    throw parse_exception(
        tok->pos,
        std::string("unexpected ") + token_type_str(tok->type));
}

bool lingo::ast::parse_ast(const std::vector<token> &tokens, ast_root &root,
                           parse_error *error) {
    try {
        token_reader reader(tokens);
        script_scope scope;

        while (!reader.eof()) {
            auto handler = parse_script_decl(reader, scope);
            if (handler) {
                root.handlers.push_back(std::move(handler));
            }
        }

        for (auto &prop_name : scope.properties) {
            root.properties.push_back(prop_name);
        }
    } catch (parse_exception except) {
        if (error) {
            *error = parse_error { except.pos, except.msg };
        }

        return false;
    }
    
    return true;
}