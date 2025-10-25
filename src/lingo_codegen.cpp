#include "lingo.hpp"
#include <cassert>
#include <sstream>
#include <memory>
#include <unordered_set>

using namespace lingo;

class gen_exception : public std::runtime_error {
public:
    pos_info pos;
    std::string msg;

    gen_exception(pos_info pos, const std::string &what = "")
        : pos(pos), msg(what), std::runtime_error(what) { } // TODO: add pos info to error
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
        tmpvar_handle operator=(const tmpvar_handle&) = delete;
        tmpvar_handle(tmpvar_handle&& src)
            : scope(src.scope), name(std::move(src.name))
            { }
        tmpvar_handle operator=(const tmpvar_handle&& src) {
            assert(&scope == &src.scope);
            name = std::move(src.name);
        }

        ~tmpvar_handle() {
            --scope.tmpvar_index;
        }
    };

public:
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

static bool get_handler_ref(const std::string &name, std::ostream &ostream,
                            expr_gen_ctx &ctx) {
    static constexpr const char *MATH_LIBRARY_FUNCS[] = {
        "abs", "atan", "cos", "exp", "log", "sin", "sqrt",
    };

    for (size_t i = 0; i < sizeof(MATH_LIBRARY_FUNCS)/sizeof(*MATH_LIBRARY_FUNCS); ++i) {
        if (name == MATH_LIBRARY_FUNCS[i]) {
            ostream << "math." << name;
            return true;
        }
    }
    
    if (ctx.scope.script_scope.has_handler(name)) {
        ostream << "script." << name;
        return true;
    }

    return false;
}

static void generate_expr(std::unique_ptr<ast::ast_expr> &expr,
                          std::ostream &ostream, expr_gen_ctx &ctx) {
    switch (expr->type) {
        case ast::EXPR_LITERAL: {
            auto data = static_cast<ast::ast_expr_literal*>(expr.get());

            switch (data->literal_type) {
                case ast::EXPR_LITERAL_FLOAT:
                    ostream << data->floatv;
                    break;

                case ast::EXPR_LITERAL_INTEGER:
                    ostream << data->intv;
                    break;

                case ast::EXPR_LITERAL_STRING:
                    write_escaped_str(data->str, ostream);
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
                        ostream << "tostring(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << ") .. tostring(";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
                        break;
                    
                    case ast::EXPR_BINOP_CONCAT_WITH_SPACE:
                        ostream << "tostring(";
                        generate_expr(data->left, ostream, ctx);
                        ostream << ") ..\" \".. tostring(";
                        generate_expr(data->right, ostream, ctx);
                        ostream << ")";
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

            if (data->method->type != ast::EXPR_IDENTIFIER) {
                throw gen_exception(data->pos, "expected identifier for handler name");
            }

            auto handler_id =
                static_cast<ast::ast_expr_identifier*>(data->method.get());
            const std::string &name = handler_id->identifier;
            
            // handler name not found in script, dynamic dispatch
            bool first_comma;
            if (!get_handler_ref(name, ostream, ctx)) {
                ostream << "call_handler(";
                write_escaped_str(name, ostream);
                first_comma = true;
            } else {
                ostream << "(";
                first_comma = false;
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
            ostream << "." << data->index << ")";
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
                ostream << "((";
                generate_expr(data->expr, ostream, ctx);
                ostream << ")[";
                generate_expr(data->index_from, ostream, ctx);
                ostream << "])";
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

    switch (stm->type) {
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

        case ast::STATEMENT_IF: {
            auto data = static_cast<ast::ast_statement_if*>(stm.get());

            {
                auto tmp = expr_ctx.scope.create_temp_var(func_stream);
                tmp_stream << tmp.name << " = ";
                generate_expr(data->condition, tmp_stream, expr_ctx);
                tmp_stream << "\nif " << tmp.name << " ~= nil and "
                    << "type(" << tmp.name << ") ~= \"number\" or "
                    << "floor(" << tmp.name << ") ~= " << tmp.name << " then\n"
                    << "\terror(\"expected integer or void, got \" .. type(" << tmp.name << "))\n"
                    << "end\nif " << tmp.name << " ~= 0 and " << tmp.name << " ~= nil then\n";
            }

            for (const auto &child_stm : data->body) {
                generate_statement(child_stm, func_stream, tmp_stream, scope);
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
        stream << "else if " << lua_name << " == false then\n";
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

bool codegen::generate_lua51(const ast::ast_root &root, std::ostream &stream,
                             parse_error *error) {
    try {
        generate_script(root, stream);
    } catch (gen_exception except) {
        if (error) {
            *error = parse_error { except.pos, except.msg };
        }

        return false;
    }

    return true;
}