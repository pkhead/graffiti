#include "lingo.hpp"
#include <cassert>
#include <sstream>
#include <memory>
#include <unordered_set>
#include <unordered_map>

using namespace lingo;

static constexpr char ESC = '\x1b';

class gen_exception : public std::runtime_error {
public:
    pos_info pos;
    std::string msg;

    gen_exception(pos_info pos, const std::string &what = "")
        : std::runtime_error(what), pos(pos), msg(what) { } // TODO: add pos info to error
};

class gen_script_scope {
public:
    std::unordered_set<std::string> handlers; // stores script-scope handlers

    bool has_handler(const std::string &id) const {
        if (handlers.find(id) != handlers.end())
            return true;

        return false;
    }
};

class gen_handler_scope {
private:
    int tmpvar_index = 0;

public:
    class tmpvar_handle {
    private:
        gen_handler_scope &scope;

    public:
        std::string name;

        tmpvar_handle(gen_handler_scope &scope, std::ostream &code_stream)
            : scope(scope), name("_tmp" + std::to_string(scope.tmpvar_index++))
        {
            scope.ensure_lua_local(name, code_stream);
        }

        tmpvar_handle(const tmpvar_handle&) = delete;
        tmpvar_handle& operator=(const tmpvar_handle&) = delete;
        tmpvar_handle(tmpvar_handle&& src)
            : scope(src.scope), name(std::move(src.name))
            { }
        tmpvar_handle& operator=(const tmpvar_handle&& src) {
            assert(&scope == &src.scope);
            name = std::move(src.name);
            return *this;
        }

        ~tmpvar_handle() {
            --scope.tmpvar_index;
        }
    };

    gen_script_scope &script_scope;
    std::unordered_set<std::string> lua_locals;

    gen_handler_scope(gen_script_scope &script_scope)
        : script_scope(script_scope)
        { }

    inline void ensure_lua_local(const std::string &name,
                                 std::ostream &ostream) {
        if (lua_locals.find(name) == lua_locals.end()) {
            lua_locals.insert(name);
            ostream << "local " << name << "\n";
        }
    }

    tmpvar_handle create_temp_var(std::ostream &ostream) {
        return tmpvar_handle(*this, ostream);
    }
};

struct expr_gen_ctx {
    gen_handler_scope &scope;
    std::ostream &decl_stream;
};

static constexpr const char *LOCAL_VAR_PREFIX = "LN_";

static void write_escaped_str(const std::string &str, std::ostream &ostream) {
    ostream.put('"');

    for (char src_ch : str) {
        switch (src_ch) {
            case '"':
                ostream << "\\\"";
                break;

            case '\n':
                ostream << "\\n";
                break;

            case '\t':
                ostream << "\\t";
                break;

            case '\r':
                ostream << "\\r";
                break;
            
            case '\b':
                ostream << "\\b";
                break;

            case '\\':
                ostream << "\\\\";
                break;
            
            default:
                if (src_ch < 32 || src_ch >= 126) {
                    ostream << "\\";
                    ostream << (unsigned int)src_ch;
                } else {
                    ostream.put(src_ch);
                }
                
                break;
        }
    }

    ostream.put('"');
}

static inline bool is_literal_str(const ast::ast_expr *expr, const char **str) {
    if (expr->type != ast::EXPR_LITERAL) return false;
    const auto *data = static_cast<const ast::ast_expr_literal*>(expr);
    if (data->literal_type != ast::EXPR_LITERAL_STRING) return false;

    if (str) *str = data->str.c_str();
    return true;
}

static bool get_handler_ref(const std::string &name, std::ostream &ostream,
                            expr_gen_ctx &ctx) {
    static const std::unordered_map<std::string, std::string> fmap = {
        { "abs", "math.abs" },
        { "atan", "math.atan" },
        { "cos", "math.cos" },
        { "exp", "math.exp" },
        { "log", "math.log" },
        { "sin", "math.sin" },
        { "sqrt", "math.sqrt" },

        { "string", "tostring" },

        { "rect", "lingo.rect" },
        { "point", "lingo.point" },
        { "member", "member" },
        { "sprite", "sprite" },
        { "float", "lruntime.to_float" },
    };
    
    const auto &it = fmap.find(name);
    if (it != fmap.end()) {
        ostream << it->second;
        return true;
    }
    
    if (ctx.scope.script_scope.has_handler(name)) {
        ostream << "script." << name;
        return true;
    }

    return false;
}

static void generate_expr(std::unique_ptr<ast::ast_expr> &expr,
                          std::ostream &ostream, expr_gen_ctx &ctx) {
    ostream << SET_LINE(expr->pos.line);

    switch (expr->type) {
        case ast::EXPR_LITERAL: {
            auto data = static_cast<ast::ast_expr_literal*>(expr.get());

            switch (data->literal_type) {
                case ast::EXPR_LITERAL_FLOAT:
                    char b[256];
                    snprintf(b, 256, "%.16f", data->floatv);
                    ostream << b;
                    break;

                case ast::EXPR_LITERAL_INTEGER:
                    ostream << data->intv;
                    break;

                case ast::EXPR_LITERAL_STRING:
                    write_escaped_str(data->str, ostream);
                    break;

                case ast::EXPR_LITERAL_SYMBOL:
                    ostream << "symbol(";
                    write_escaped_str(data->str, ostream);
                    ostream << ")";
                    break;
                
                case ast::EXPR_LITERAL_VOID:
                    ostream << "null";
                    break;
            }

            break;
        }

        case ast::EXPR_IDENTIFIER: {
            auto data = static_cast<ast::ast_expr_identifier*>(expr.get());

            switch (data->scope) {
                case ast::SCOPE_LOCAL:
                    ostream << LOCAL_VAR_PREFIX;
                    ostream << data->identifier;
                    break;

                case ast::SCOPE_GLOBAL:
                    ostream << "globals.";
                    ostream << data->identifier;
                    break;

                case ast::SCOPE_PROPERTY:
                    ostream << "self.";
                    ostream << data->identifier;
                    break;
            }

            break;
        }

        case ast::EXPR_THE: {
            auto data = static_cast<ast::ast_expr_the*>(expr.get());

            switch (data->identifier) {
                case ast::EXPR_THE_FRAME:
                    ostream << "(_movie.frame)";
                    break;

                case ast::EXPR_THE_MOVIE_PATH:
                    ostream << "(_movie.path)";
                    break;

                case ast::EXPR_THE_DIR_SEPARATOR:
                    #ifdef _WIN32
                    ostream << "\"\\\\\"";
                    #else
                    ostream << "\"/\"";
                    #endif
                    break;

                case ast::EXPR_THE_RANDOM_SEED:
                    throw gen_exception(data->pos, "the randomseed not implemented");
                    break;
            }

            break;
        }

        case ast::EXPR_LIST: {
            auto data = static_cast<ast::ast_expr_list*>(expr.get());

            ostream << "lingo.list(";
            bool write_comma = false;
            for (auto &elem : data->items) {
                if (write_comma)
                    ostream << ", ";

                generate_expr(elem, ostream, ctx);
                write_comma = true;
            }
            ostream << ")";

            break;
        }

        case ast::EXPR_PROP_LIST: {
            auto data = static_cast<ast::ast_expr_prop_list*>(expr.get());

            ostream << "lingo.propList(";
            bool write_comma = false;
            for (auto &pair : data->pairs) {
                if (write_comma)
                    ostream << ", ";

                generate_expr(pair.first, ostream, ctx);
                ostream << ",";
                generate_expr(pair.second, ostream, ctx);
                
                write_comma = true;
            }
            ostream << ")";

            break;
        }

        case ast::EXPR_BINOP: {
            auto data = static_cast<ast::ast_expr_binop*>(expr.get());

            if (data->op == ast::EXPR_BINOP_AND ||
                data->op == ast::EXPR_BINOP_OR)
            {
                if (data->op == ast::EXPR_BINOP_AND) {
                    ostream << "land";
                } else if (data->op == ast::EXPR_BINOP_OR) {
                    ostream << "lor";
                }

                ostream << "(";
                generate_expr(data->left, ostream, ctx);
                ostream << ", ";
                generate_expr(data->right, ostream, ctx);
                ostream << ")";
            } else {
                ostream << "(";

                switch (data->op) {
                    case ast::EXPR_BINOP_ADD:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " + ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_SUB:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " - ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_MUL:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " * ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_DIV:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " / ";
                        generate_expr(data->right, ostream, ctx);
                        break;
                    
                    case ast::EXPR_BINOP_MOD:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " % ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_CONCAT:
                        if (is_literal_str(data->left.get(), nullptr)) {
                            generate_expr(data->left, ostream, ctx);
                        } else {
                            ostream << "tostring(";
                            generate_expr(data->left, ostream, ctx);
                            ostream << ")";
                        }

                        ostream << " .. ";

                        if (is_literal_str(data->right.get(), nullptr)) {
                            generate_expr(data->right, ostream, ctx);
                        } else {
                            ostream << "tostring(";
                            generate_expr(data->right, ostream, ctx);
                            ostream << ")";
                        }

                        break;
                    
                    case ast::EXPR_BINOP_CONCAT_WITH_SPACE:
                    if (is_literal_str(data->left.get(), nullptr)) {
                            generate_expr(data->left, ostream, ctx);
                        } else {
                            ostream << "tostring(";
                            generate_expr(data->left, ostream, ctx);
                            ostream << ")";
                        }

                        ostream << " ..\" \".. ";

                        if (is_literal_str(data->right.get(), nullptr)) {
                            generate_expr(data->right, ostream, ctx);
                        } else {
                            ostream << "tostring(";
                            generate_expr(data->right, ostream, ctx);
                            ostream << ")";
                        }

                        break;

                    case ast::EXPR_BINOP_EQ:
                        ostream << "btoi(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << " == ";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;

                    case ast::EXPR_BINOP_NEQ:
                        ostream << "btoi(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << " ~= ";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;

                    case ast::EXPR_BINOP_GT:
                        ostream << "btoi(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << " > ";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;

                    case ast::EXPR_BINOP_LT:
                        ostream << "btoi(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << " < ";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;

                    case ast::EXPR_BINOP_GE:
                        ostream << "btoi(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << " >= ";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;

                    case ast::EXPR_BINOP_LE:
                        ostream << "btoi(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << " <= ";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;

                    default:
                        assert(false);
                }

                ostream << ")";
            }

            break;
        }

        case ast::EXPR_UNOP: {
            auto data = static_cast<ast::ast_expr_unop*>(expr.get());

            switch (data->op) {
                case ast::EXPR_UNOP_NEG:
                    ostream << "(";
                    ostream << "-";
                    generate_expr(data->expr, ostream, ctx);
                    ostream << ")";
                    break;

                case ast::EXPR_UNOP_NOT:
                    ostream << "lnot(";
                    generate_expr(data->expr, ostream, ctx);
                    ostream << ")";
                    break;
            }

            break;
        }

        case ast::EXPR_CALL: {
            auto data = static_cast<ast::ast_expr_call*>(expr.get());
            bool first_comma;

            if (data->method->type == ast::EXPR_DOT) {
                auto handler_ref =
                    static_cast<ast::ast_expr_dot*>(data->method.get());
                
                generate_expr(handler_ref->expr, ostream, ctx);
                ostream << ":" << handler_ref->index << "(";
                first_comma = false;
            } else {
                if (data->method->type != ast::EXPR_IDENTIFIER) {
                    throw gen_exception(data->pos, "reference to handler must come from direct identifier or dot index");
                }

                auto handler_id =
                    static_cast<ast::ast_expr_identifier*>(data->method.get());
                const std::string &name = handler_id->identifier;
                
                // handler name not found in script, dynamic dispatch
                if (!get_handler_ref(name, ostream, ctx)) {
                    ostream << "call_handler(";
                    write_escaped_str(name, ostream);
                    first_comma = true;
                } else {
                    ostream << "(";
                    first_comma = false;
                }
            }

            for (auto &arg_expr : data->arguments) {
                if (first_comma)
                    ostream << ", ";
                first_comma = true;

                generate_expr(arg_expr, ostream, ctx);
            }

            ostream << ")";
            break;
        }

        case ast::EXPR_DOT: {
            auto data = static_cast<ast::ast_expr_dot*>(expr.get());

            ostream << "(";
            generate_expr(data->expr, ostream, ctx);
            ostream << ")." << data->index;
            break;
        }

        case ast::EXPR_INDEX: {
            auto data = static_cast<ast::ast_expr_index*>(expr.get());

            if (data->index_to) {
                ostream << "(lruntime.range(";
                generate_expr(data->expr, ostream, ctx);
                ostream << ", ";
                generate_expr(data->index_from, ostream, ctx);
                ostream << ", ";
                generate_expr(data->index_to, ostream, ctx);
                ostream << "))";
            } else {
                ostream << "(";
                generate_expr(data->expr, ostream, ctx);
                ostream << ")[";
                generate_expr(data->index_from, ostream, ctx);
                ostream << "]";
            }

            break;
        }

        default:
            throw gen_exception(expr->pos, "unimplemented expr type");
    }
}

enum index_split_result {
    INDEX_SPLIT_INVALID,
    INDEX_SPLIT_STATIC,
    INDEX_SPLIT_DYNAMIC,
};

static index_split_result object_index_split(
    const std::unique_ptr<ast::ast_expr> &expr, expr_gen_ctx &ctx,
    std::string &left, std::string &right
) {
    if (expr->type == ast::EXPR_INDEX) {
        auto data = static_cast<ast::ast_expr_index*>(expr.get());
        if (data->index_to) {
            throw gen_exception(expr->pos, "internal: object_index_split with index range is unsupported");
        }
        
        std::stringstream ls;
        generate_expr(data->expr, ls, ctx);

        std::stringstream rs;
        generate_expr(data->index_from, rs, ctx);

        left = ls.str();
        right = rs.str();
        return INDEX_SPLIT_DYNAMIC;
    }

    if (expr->type == ast::EXPR_DOT) {
        auto data = static_cast<ast::ast_expr_dot*>(expr.get());
        
        std::stringstream ls;
        generate_expr(data->expr, ls, ctx);

        left = ls.str();
        right = data->index;
        return INDEX_SPLIT_STATIC;
    }

    return INDEX_SPLIT_INVALID;
}

static void generate_statement(const std::unique_ptr<ast::ast_statement> &stm,
                               std::ostream &func_stream,
                               std::ostream &body_contents,
                               gen_handler_scope &scope) {
    std::stringstream tmp_stream;
    expr_gen_ctx expr_ctx { scope, body_contents };

    // usage:
    // {
    //   auto tmp = scope.create_temp_var();
    //   auto chk = cond_check(cond_expr, tmp);
    //   _tmp_stream << "if " << chk << " then"
    // }
    auto cond_check = [&](std::unique_ptr<ast::ast_expr> &cond,
                          gen_handler_scope::tmpvar_handle &tmp) {
        // insert runtime check if value is a integer or void type
        tmp_stream << tmp.name << " = ";
        generate_expr(cond, tmp_stream, expr_ctx);
        tmp_stream << "\n";
        tmp_stream << "if " << tmp.name << " ~= nil and "
            << "(type(" << tmp.name << ") ~= \"number\" or "
            << "math.floor(" << tmp.name << ") ~= " << tmp.name << ") then\n"
            << "error(\"expected integer or void, got \" .. type(" << tmp.name << "))\n"
            << "end\n";
        
        return tmp.name + " ~= 0 and " + tmp.name + " ~= nil";
    };

    body_contents << SET_LINE(stm->pos.line);

    switch (stm->type) {
        case ast::STATEMENT_EXPR: {
            auto data = static_cast<ast::ast_statement_expr*>(stm.get());

            if (data->expr->type == ast::EXPR_CALL) {
                generate_expr(data->expr, tmp_stream, expr_ctx);
                tmp_stream << "\n";    
            } else {
                tmp_stream << "_ = ";
                generate_expr(data->expr, tmp_stream, expr_ctx);
                tmp_stream << " _ = nil\n";
            }

            body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_ASSIGN: {
            auto assign = static_cast<ast::ast_statement_assign*>(stm.get());

            // if this is a variable assignment, declare local variable if
            // it was not already declared
            // if (assign->lvalue->type == ast::EXPR_IDENTIFIER) {
            //     auto lvalue = static_cast<ast::ast_expr_identifier*>(assign->lvalue.get());
            //     std::string real_var = LOCAL_VAR_PREFIX + lvalue->identifier;

            //     var_scope::scope_class classif;
            //     if (!scope.has_var(lvalue->identifier, classif)) {
            //         // register lingo and lua name of local
            //         scope.locals.insert(lvalue->identifier);
            //         scope.lua_locals.insert(real_var);

            //         // insert local declaration at the top of the lua
            //         // function
            //         func_stream << "local ";
            //         func_stream << real_var;
            //         func_stream << "\n";
            //     }
            // }

            generate_expr(assign->lvalue, tmp_stream, expr_ctx);
            tmp_stream << " = ";
            generate_expr(assign->rvalue, tmp_stream, expr_ctx);

            body_contents << tmp_stream.rdbuf() << "\n";
            break;
        }

        case ast::STATEMENT_RETURN: {
            auto data = static_cast<ast::ast_statement_return*>(stm.get());

            tmp_stream << "return ";
            if (data->expr)
                generate_expr(data->expr, tmp_stream, expr_ctx);

            body_contents << tmp_stream.rdbuf() << "\n";
            break;
        }

        case ast::STATEMENT_PUT: {
            auto data = static_cast<ast::ast_statement_put*>(stm.get());
            tmp_stream << "print(";
            generate_expr(data->expr, tmp_stream, expr_ctx);
            tmp_stream << ")";

            body_contents << tmp_stream.rdbuf() << "\n";
            break;
        }

        case ast::STATEMENT_PUT_ON: {
            auto data = static_cast<ast::ast_statement_put_on*>(stm.get());

            std::string l, r;

            switch (object_index_split(data->target, expr_ctx, l, r)) {
                case INDEX_SPLIT_INVALID: {
                    std::unique_ptr<ast::ast_expr> *expr_left;
                    std::unique_ptr<ast::ast_expr> *expr_right;

                    if (data->before) {
                        expr_left = &data->expr;
                        expr_right = &data->target;
                    } else {
                        expr_left = &data->target;
                        expr_right = &data->expr;
                    }

                    generate_expr(data->target, tmp_stream, expr_ctx);
                    tmp_stream << " = ";
                    generate_expr(*expr_left, tmp_stream, expr_ctx);
                    tmp_stream << " .. ";
                    generate_expr(*expr_right, tmp_stream, expr_ctx);
                    break;
                }

                case INDEX_SPLIT_STATIC: {
                    // local _tmp0 = target()
                    // _tmp0.idx = _tmp0.idx .. expr()
                    auto tmp0 = expr_ctx.scope.create_temp_var(func_stream);
                    tmp_stream << tmp0.name << " = " << l << "\n";
                    tmp_stream << tmp0.name << "." << r << " = ";

                    if (data->before) {
                        generate_expr(data->expr, tmp_stream, expr_ctx);
                        tmp_stream << " .. " << tmp0.name << "." << r;
                    } else {
                        tmp_stream << " " << tmp0.name << "." << r << " .. ";
                        generate_expr(data->expr, tmp_stream, expr_ctx);
                    }

                    break;
                }

                case INDEX_SPLIT_DYNAMIC: {
                    // local _tmp0 = target()
                    // local _tmp1 = index()
                    // _tmp0[_tmp1] = _tmp0[_tmp1] .. expr()
                    auto tmp0 = expr_ctx.scope.create_temp_var(func_stream);
                    auto tmp1 = expr_ctx.scope.create_temp_var(func_stream);
                    tmp_stream << tmp0.name << " = " << l << " ";
                    tmp_stream << tmp1.name << " = " << r << "\n";

                    if (data->before) {
                        tmp_stream << tmp0.name << "[" << tmp1.name << "] = ";
                        generate_expr(data->expr, tmp_stream, expr_ctx);
                        tmp_stream << " .. " << tmp0.name << "[" << tmp1.name << "]";
                    } else {
                        tmp_stream << tmp0.name << "[" << tmp1.name << "] = "
                            << tmp0.name << "[" << tmp1.name << "] .. ";
                        generate_expr(data->expr, tmp_stream, expr_ctx);
                    }

                    break;
                }
            }

            body_contents << tmp_stream.rdbuf() << "\n";
            break;
        }

        case ast::STATEMENT_EXIT_REPEAT: {
            body_contents << "break\n";
            break;
        }

        case ast::STATEMENT_NEXT_REPEAT: {
            body_contents << "goto ::nextrepeat::\n";
            break;
        }

        case ast::STATEMENT_IF: {
            auto data = static_cast<ast::ast_statement_if*>(stm.get());

            size_t i = 0;
            for (auto it = data->branches.begin(); it != data->branches.end(); ++it, ++i) {
                auto &branch = *it;

                if (i > 0) {
                    tmp_stream << "else\n";
                }

                {
                    // insert runtime check if value is a integer or void type
                    auto tmp = expr_ctx.scope.create_temp_var(func_stream);
                    auto check = cond_check(branch->condition, tmp);
                    // create the Real branch
                    tmp_stream << "if " << check << " then\n";
                }

                for (const auto &child_stm : branch->body) {
                    generate_statement(child_stm, func_stream, tmp_stream, scope);
                }
            }

            if (data->has_else) {
                tmp_stream << "else\n";
                for (const auto &child_stm : data->else_branch) {
                    generate_statement(child_stm, func_stream, tmp_stream, scope);
                }
            }

            for (i = 0; i < data->branches.size(); ++i) {
                tmp_stream << "end ";
            }

            tmp_stream << "\n";
            body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_REPEAT_WHILE: {
            auto data = static_cast<ast::ast_statement_repeat_while*>(stm.get());

            tmp_stream << "while true do\n";
            {
                auto tmp = expr_ctx.scope.create_temp_var(func_stream);
                auto check = cond_check(data->condition, tmp);
                // create the Real branch
                tmp_stream << "if not (" << check << ") then break end\n";
            }

            for (auto &child_stm : data->body) {
                generate_statement(child_stm, func_stream, tmp_stream, scope);
            }

            tmp_stream << "::nextrepeat::\nend\n";
            body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_REPEAT_TO: {
            auto data = static_cast<ast::ast_statement_repeat_to*>(stm.get());

            generate_expr(data->iterator, tmp_stream, expr_ctx);
            tmp_stream << " = ";
            generate_expr(data->init, tmp_stream, expr_ctx);

            tmp_stream << "\nwhile ";
            generate_expr(data->iterator, tmp_stream, expr_ctx);

            if (data->down) {
                tmp_stream << " >= ";
            } else {
                tmp_stream << " <= ";
            }

            generate_expr(data->to, tmp_stream, expr_ctx);

            tmp_stream << " do\n";

            for (auto &child_stm : data->body) {
                generate_statement(child_stm, func_stream, tmp_stream, scope);
            }

            tmp_stream << "::nextrepeat::\n";

            tmp_stream << LINECTL("O");
            generate_expr(data->iterator, tmp_stream, expr_ctx);
            tmp_stream << " = ";
            generate_expr(data->iterator, tmp_stream, expr_ctx);
            tmp_stream << LINECTL("I");

            if (data->down) {
                tmp_stream << " - 1";
            } else {
                tmp_stream << " + 1";
            }

            tmp_stream << "\nend\n";
            body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_REPEAT_IN: {
            auto data = static_cast<ast::ast_statement_repeat_in*>(stm.get());

            auto tmp = scope.create_temp_var(func_stream);
            tmp_stream << tmp.name << " = ";
            generate_expr(data->iterable, tmp_stream, expr_ctx);

            tmp_stream << "\nfor i=1, #" << tmp.name << " do\n";
            generate_expr(data->iterator, tmp_stream, expr_ctx);
            tmp_stream << " = " << tmp.name << "[i]\n";

            for (auto &child_stm : data->body) {
                generate_statement(child_stm, func_stream, tmp_stream, scope);
            }

            tmp_stream << "::exitrepeat::\nend\n";
            tmp_stream << tmp.name << " = nil\n";
            // TODO: should use do blocks for temp variables instead of making
            // them always active

            body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_CASE: {
            auto data = static_cast<ast::ast_statement_case*>(stm.get());

            tmp_stream << "do\n";
            tmp_stream << "local case = ";
            generate_expr(data->expr, tmp_stream, expr_ctx);
            tmp_stream << "\n";

            // if case has only an otherwise clause, run otherwise clause
            // unconditionally
            if (data->clauses.empty() && data->has_otherwise) {
                for (auto &child_stm : data->otherwise_clause) {
                    generate_statement(child_stm, func_stream, tmp_stream, scope);
                }
            } else {
                bool is_first = true;
                for (auto &clause : data->clauses) {
                    if (is_first)
                        tmp_stream << "if ";
                    else
                        tmp_stream << "elseif ";

                    bool insert_or = false;
                    for (auto &check : clause->literal) {
                        if (insert_or) {
                            tmp_stream << " or ";
                        }

                        tmp_stream << "(case == ";
                        generate_expr(check, tmp_stream, expr_ctx);
                        tmp_stream << ")";

                        insert_or = true;
                    }
                    tmp_stream << " then\n";

                    for (auto &child_stm : clause->branch) {
                        generate_statement(child_stm, func_stream, tmp_stream, scope);
                    }

                    is_first = false;
                }

                if (data->has_otherwise) {
                    tmp_stream << "else\n";

                    for (auto &child_stm : data->otherwise_clause) {
                        generate_statement(child_stm, func_stream, tmp_stream, scope);
                    }
                }

                tmp_stream << "end\n";
            }

            tmp_stream << "end\n";
            body_contents << tmp_stream.rdbuf();
            break;
        }

        default:
            throw new gen_exception(stm->pos, "unknown statement type");
    }
}

static void generate_func(std::ostream &stream,
                          const ast::ast_handler_decl &handler,
                          gen_script_scope &script_scope) {
    gen_handler_scope scope(script_scope);

    stream << SET_LINE(handler.pos.line);
    stream << "function script." << handler.name << "(self";

    // write and register parameter names
    // note: the self argument must always be present in order for property
    // variables to work correctly when the first argument (me) is not present
    // in the lingo code.
    {
        int idx = 0;
        for (auto it = handler.params.begin(); it != handler.params.end(); ++it, ++idx) {
            std::string lua_name = LOCAL_VAR_PREFIX + *it;
            scope.lua_locals.insert(lua_name);

            if (idx > 0) {
                stream << ", ";
                stream << lua_name;
            }
        }
    }

    stream << ")\n";

    stream << "local _\n";

    if (handler.params.size() > 0) {
        stream << "local " << LOCAL_VAR_PREFIX << handler.params.front();
        stream << " = self\n";
    }

    for (auto &local_name : handler.locals) {
        scope.ensure_lua_local(std::string(LOCAL_VAR_PREFIX) + local_name, stream);
    }

    std::stringstream body_contents;

    // convert lua booleans in parameters to integers, in the case that this
    // handler was called directly from lua
    for (auto &name : handler.params) {
        std::string lua_name = LOCAL_VAR_PREFIX + name;
        stream << "if " << lua_name << " == true then\n";
        stream << "\t" << lua_name << " = 1\n";
        stream << "elseif " << lua_name << " == false then\n";
        stream << "\t" << lua_name << " = 0\n";
        stream << "end\n";
    }

    for (auto &stm : handler.body) {
        generate_statement(stm, stream, body_contents, scope);
    }

    if (body_contents.rdbuf()->in_avail()) {
        stream << body_contents.rdbuf();
    }

    stream << "end\n\n";
}

static void generate_script(const ast::ast_root &root, std::ostream &stream) {
    gen_script_scope script_scope;

    // first, put all handlers defined in script into scope
    for (auto &decl : root.handlers) {
        script_scope.handlers.insert(decl->name);
    }

    // then perform code generation
    stream << "local symbol = lingo.symbol\n";
    stream << "local globals = lingo.globals\n";
    stream << "local lruntime = lingo.runtime\n";
    stream << "local land = lruntime.logical_and\n";
    stream << "local lor = lruntime.logical_or\n";
    stream << "local lnot = lruntime.logical_not\n";
    stream << "local tostring = lruntime.to_string\n";
    stream << "local btoi = lruntime.bool_to_int\n";
    stream << "\n";
    stream << "local script = {}\n";

    stream << "script._props = {";
    for (auto it = root.properties.begin(); it != root.properties.end(); ++it) {
        if (it != root.properties.begin()) {
            stream << ", ";
        }

        write_escaped_str(*it, stream);
    }
    stream << "}\n";
    stream << "\n";

    for (auto &decl : root.handlers) {
        generate_func(stream, *decl, script_scope);
    }

    stream << "return script\n";
}

bool codegen::generate_luajit_text(const ast::ast_root &root,
                                   std::ostream &stream, parse_error *error,
                                   extra_gen_params *params) {
    lua_writer lwriter(stream);
    // _dbg_last_line = 1;
    if (params)
        lwriter.set_line_intercept(!params->no_line_numbers);
    
    try {
        generate_script(root, lwriter);
    } catch (gen_exception except) {
        if (error) {
            *error = parse_error { except.pos, except.msg };
        }

        return false;
    }

    lwriter.flush();
    return true;
}









bool lingo::compile_luajit_text(std::istream &istream, std::ostream &ostream,
                                parse_error *error, extra_gen_params *params) {
    std::vector<lingo::ast::token> tokens;
    lingo::parse_error err;
    if (!lingo::ast::parse_tokens(istream, tokens, &err)) {
        if (error) *error = err;
        return false;
    }

    // for (auto &tok : tokens) {
    //     switch (tok.type) {
    //         case lingo::ast::TOKEN_FLOAT:
    //             std::cout << "(FLT) ";
    //             std::cout << tok.number;
    //             break;

    //         case lingo::ast::TOKEN_INTEGER:
    //             std::cout << "(INT) ";
    //             std::cout << tok.integer;
    //             break;

    //         case lingo::ast::TOKEN_KEYWORD:
    //             std::cout << "(KYW) ";
    //             std::cout << lingo::ast::keyword_to_str(tok.keyword);
    //             break;

    //         case lingo::ast::TOKEN_WORD:
    //             std::cout << "(WRD) ";
    //             std::cout << tok.str;
    //             break;
            
    //         case lingo::ast::TOKEN_SYMBOL:
    //             std::cout << "(SYM) ";
    //             std::cout << lingo::ast::symbol_to_str(tok.symbol);
    //             break;

    //         case lingo::ast::TOKEN_STRING:
    //             std::cout << "(STR) ";
    //             std::cout << tok.str;
    //             break;

    //         case lingo::ast::TOKEN_SYMBOL_LITERAL:
    //             std::cout << "(SYL) ";
    //             std::cout << tok.str;
    //             break;
            
    //         case lingo::ast::TOKEN_LINE_END:
    //             std::cout << "(NXT) ";
    //             break;
    //     }

    //     std::cout << "\n";
    // }

    lingo::ast::ast_root script_tree;
    if (!lingo::ast::parse_ast(tokens, script_tree, &err)) {
        if (error) *error = err;
        return false;
    }

    if (!lingo::codegen::generate_luajit_text(script_tree, ostream, &err, params)) {
        if (error) *error = err;
        return false;
    }

    return true;
}