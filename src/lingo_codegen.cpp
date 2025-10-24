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

struct var_scope {
    enum scope_class {
        SCOPE_GLOBAL,
        SCOPE_PROPERTY,
        SCOPE_LOCAL
    };

    std::unordered_set<std::string> globals;
    std::unordered_set<std::string> properties;
    std::unordered_set<std::string> locals;
    std::unordered_set<std::string> handlers; // stores script-scope handlers
    std::unordered_set<std::string> lua_locals;
    var_scope *parent_scope;

    bool has_var(const std::string &id, scope_class &classif) const {
        {
            auto it = globals.find(id);
            if (it != globals.end()) {
                classif = SCOPE_GLOBAL;
                return true;
            }
        }

        {
            auto it = properties.find(id);
            if (it != properties.end()) {
                classif = SCOPE_PROPERTY;
                return true;
            }
        }

        {
            auto it = locals.find(id);
            if (it != locals.end()) {
                classif = SCOPE_LOCAL;
                return true;
            }
        }

        if (parent_scope)
            return parent_scope->has_var(id, classif);
        else
            return false;
    }

    bool has_handler(const std::string &id) const {
        if (handlers.find(id) != handlers.end())
            return true;

        if (parent_scope)
            return parent_scope->has_handler(id);
        else
            return false;
    }

    inline void ensure_lua_local(const std::string &name,
                                 std::ostream &ostream) {
        if (lua_locals.find(name) == lua_locals.end()) {
            lua_locals.insert(name);
            ostream << "local " << name << "\n";
        }
    }
};

struct expr_gen_ctx {
    var_scope &scope;
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

            case '\\':
                ostream << "\\\\";
                break;
            
            default:
                ostream.put(src_ch);
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
    
    if (ctx.scope.has_handler(name)) {
        ostream << "script." << name;
        return true;
    }

    return false;
}

static void generate_expr(std::unique_ptr<ast::ast_expr> &expr,
                          std::ostream &ostream, expr_gen_ctx &ctx,
                          bool assign = false) {
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
            }

            break;
        }

        case ast::EXPR_IDENTIFIER: {
            auto data = static_cast<ast::ast_expr_identifier*>(expr.get());
            var_scope::scope_class classif;
            if (!assign && !ctx.scope.has_var(data->identifier, classif)) {
                throw gen_exception(
                    data->pos,
                    std::string("use of undeclared variable \"") + data->identifier + "\"");
            }

            ostream << LOCAL_VAR_PREFIX;
            ostream << data->identifier;
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
                        generate_expr(data->left, ostream, ctx);
                        ostream << " == ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_NEQ:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " ~= ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_GT:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " > ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_LT:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " < ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_GE:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " >= ";
                        generate_expr(data->right, ostream, ctx);
                        break;

                    case ast::EXPR_BINOP_LE:
                        generate_expr(data->left, ostream, ctx);
                        ostream << " <= ";
                        generate_expr(data->right, ostream, ctx);
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

static void generate_func(std::ostream &stream,
                          const ast::ast_handler_definition &handler,
                          var_scope &parent_scope) {
    var_scope scope;
    scope.parent_scope = &parent_scope;

    stream << "function script." << handler.name << "(";

    // write and register parameter names
    for (auto it = handler.params.begin(); it != handler.params.end(); ++it) {
        if (it != handler.params.begin())
            stream << ", ";
        
        std::string lua_name = LOCAL_VAR_PREFIX + *it;
        scope.locals.insert(*it);
        scope.lua_locals.insert(lua_name);

        stream << lua_name;
    }

    stream << ")\n";

    std::stringstream body_contents;
    std::stringstream tmp_stream;

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
        tmp_stream.clear();
        expr_gen_ctx expr_ctx { scope, body_contents };

        switch (stm->type) {
            case ast::STATEMENT_ASSIGN: {
                auto assign = static_cast<ast::ast_statement_assign*>(stm.get());

                // if this is a variable assignment, declare local variable if
                // it was not already declared
                if (assign->lvalue->type == ast::EXPR_IDENTIFIER) {
                    auto lvalue = static_cast<ast::ast_expr_identifier*>(assign->lvalue.get());
                    std::string real_var = LOCAL_VAR_PREFIX + lvalue->identifier;

                    var_scope::scope_class classif;
                    if (!scope.has_var(lvalue->identifier, classif)) {
                        // register lingo and lua name of local
                        scope.locals.insert(lvalue->identifier);
                        scope.lua_locals.insert(real_var);

                        // insert local declaration at the top of the lua
                        // function
                        stream << "local ";
                        stream << real_var;
                        stream << "\n";
                    }
                }

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

                    case INDEX_SPLIT_STATIC:
                        // local _tmp0 = target()
                        // _tmp0.idx = _tmp0.idx .. expr()
                        expr_ctx.scope.ensure_lua_local("_tmp0", stream);
                        tmp_stream << "_tmp0 = " << l << "\n";
                        tmp_stream << "_tmp0." << r << " = ";

                        if (data->before) {
                            generate_expr(data->expr, tmp_stream, expr_ctx);
                            tmp_stream << " .. _tmp0." << r;
                        } else {
                            tmp_stream << " _tmp0." << r << " .. ";
                            generate_expr(data->expr, tmp_stream, expr_ctx);
                        }

                        break;

                    case INDEX_SPLIT_DYNAMIC:
                        // local _tmp0 = target()
                        // local _tmp1 = index()
                        // _tmp0[_tmp1] = _tmp0[_tmp1] .. expr()
                        expr_ctx.scope.ensure_lua_local("_tmp0", stream);
                        expr_ctx.scope.ensure_lua_local("_tmp1", stream);
                        tmp_stream << "_tmp0 = " << l << " ";
                        tmp_stream << "_tmp1 = " << r << "\n";

                        if (data->before) {
                            tmp_stream << "_tmp0[_tmp1] = ";
                            generate_expr(data->expr, tmp_stream, expr_ctx);
                            tmp_stream << " .. _tmp0[_tmp1]";
                        } else {
                            tmp_stream << "_tmp0[_tmp1] = _tmp0[_tmp1] .. ";
                            generate_expr(data->expr, tmp_stream, expr_ctx);
                        }

                        break;
                }

                body_contents << tmp_stream.rdbuf() << "\n";
                break;
            }

            default:
                throw new gen_exception(stm->pos, "unknown statement type");
        }
    }

    if (body_contents.rdbuf()->in_avail()) {
        stream << body_contents.rdbuf();
    }

    stream << "end\n\n";
}

static void generate_script(const ast::ast_root &root, std::ostream &stream) {
    var_scope script_scope;
    script_scope.parent_scope = nullptr;

    // first, catalog all handlers defined in script
    for (auto &gdecl : root) {
        if (gdecl->type == ast::STATEMENT_DEFINE_HANDLER) {
            auto decl = static_cast<ast::ast_handler_definition*>(gdecl.get());
            script_scope.handlers.insert(decl->name);
        }
    }

    // then perform code generation
    stream << "local script = {}\n";
    stream << "local globals = lingo.globals\n";
    stream << "local lruntime = lingo.runtime\n";
    stream << "local land = lruntime.logical_and\n";
    stream << "local lor = lruntime.logical_or\n";
    stream << "local lnot = lruntime.logical_not\n";
    stream << "local tostring = lruntime.to_string\n";
    stream << "\n";

    for (auto &gdecl : root) {
        switch (gdecl->type) {
            case ast::STATEMENT_DECLARE_GLOBAL: {
                auto decl = static_cast<ast::ast_global_declaration*>(gdecl.get());
                for (auto &id : decl->identifiers) {
                    script_scope.globals.insert(id);
                }
                break;
            }

            case ast::STATEMENT_DECLARE_PROPERTY: {
                auto decl = static_cast<ast::ast_property_declaration*>(gdecl.get());
                for (auto &id : decl->identifiers) {
                    script_scope.properties.insert(id);
                }
                break;
            }

            case ast::STATEMENT_DEFINE_HANDLER: {
                auto decl = static_cast<ast::ast_handler_definition*>(gdecl.get());
                generate_func(stream, *decl, script_scope);
                break;
            }

            default:
                throw gen_exception(gdecl->pos, "internal error");
        }
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