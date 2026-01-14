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

static constexpr uintptr_t aligned(size_t alignment, uintptr_t addr) {
    return (addr + alignment - 1) & ~(alignment -1);
}

#define GENERIC_GET_LITERAL(consts, vtype, field, v)                           \
    for (auto it = (consts).begin(); it != (consts).end(); ++it) {             \
        auto &c = *it;                                                         \
        if (c.type == (vtype) && c.field == (v)) {                             \
            return (uint16_t) std::distance(consts.begin(), it);               \
        }                                                                      \
    }                                                                          \
                                                                               \
    consts.push_back(bc::chunk_const(v));                                      \
    return (uint16_t) (consts.size() - 1)                                      \

class gen_handler_scope {
private:
    uint16_t next_local_idx = 0;

    uintptr_t _alloc_string(const char *v, size_t len) {
        uintptr_t string_ptr_idx = string_pool.size();
        string_pool.insert(string_pool.end(), (char*)&len, (char*)(&len + 1));
        string_pool.insert(string_pool.end(), v, v + (len + 1)); // also copy null terminator
        size_t next_addr = aligned(alignof(bc::chunk_const_str), string_pool.size());
        string_pool.insert(string_pool.end(), next_addr - string_pool.size(), '\0');

        return string_ptr_idx;
    }

    uint16_t _get_strbased_const(bc::vtype type, const char *v, size_t len) {
        for (auto it = chunk_consts.begin(); it != chunk_consts.end(); ++it) {
            auto &c = *it;
            if (c.type == type && bc::base_offset(string_pool.data(), c.str)->equal(v, len)) {
                return (uint16_t) std::distance(chunk_consts.begin(), it);
            }
        }

        auto alloc_str = (bc::chunk_const_str *)_alloc_string(v, len);
        bc::chunk_const new_const = bc::chunk_const(alloc_str);
        new_const.type = type;
        chunk_consts.push_back(std::move(new_const));
        return (uint16_t) (chunk_consts.size() - 1);
    }
    
public:
    gen_script_scope &script_scope;
    std::vector<char> string_pool;
    std::vector<bc::instr> instrs;
    std::vector<bc::chunk_const> chunk_consts;
    std::vector<std::pair<uint32_t, uint32_t>> line_info;
    std::vector<uintptr_t> local_name_refs;

    std::unordered_map<std::string, int> local_indices;

    gen_handler_scope(gen_script_scope &script_scope)
        : script_scope(script_scope)
        { }

    inline uint16_t local_count() const { return next_local_idx; }
    
    uint16_t get_literal(int32_t v) {
        GENERIC_GET_LITERAL(chunk_consts, bc::TYPE_INT, i32, v);
    }

    uint16_t get_literal(double v) {
        GENERIC_GET_LITERAL(chunk_consts, bc::TYPE_FLOAT, f64, v);
    }

    uint16_t get_literal(const char *v, size_t len) {
        return _get_strbased_const(bc::TYPE_STRING, v, len);
    }

    uint16_t get_literal(const std::string &v) {
        return _get_strbased_const(bc::TYPE_STRING, v.c_str(), v.size());
    }
    
    uint16_t get_symbol(const char *v, size_t len) {
        return _get_strbased_const(bc::TYPE_SYMBOL, v, len);
    }

    uint16_t get_symbol(const char *v) {
        return _get_strbased_const(bc::TYPE_SYMBOL, v, strlen(v));
    }

    uint16_t get_symbol(const std::string &v) {
        return _get_strbased_const(bc::TYPE_SYMBOL, v.c_str(), v.size());
    }

    inline uint16_t register_local(const std::string &name) {
        local_indices[name] = next_local_idx;
        local_name_refs.push_back(_alloc_string(name.c_str(), name.size()));
        return next_local_idx++;
    }

    uint16_t get_local_index(const std::string &name) const {
        const auto &it = local_indices.find(name);
        if (it == local_indices.end()) {
            assert(false && "get_local_index: name not found");
            return UINT16_MAX;
        }

        return (uint16_t) it->second;
    }
};

struct expr_gen_ctx {
    gen_handler_scope &scope;
};

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

#define INSTR(op) (bc::instr)(op)
#define INSTR_16(op, a) (bc::instr)((uint8_t)(op) | ((uint16_t)(a) << 8))
#define INSTR_8(op, a) (bc::instr)((uint8_t)(op) | ((uint8_t)(a) << 8))
#define INSTR_16_8(op, a, b) (bc::instr)((uint8_t)(op) | ((uint16_t)(a) << 8) | ((uint8_t)(b) << 24))

static void generate_expr(std::unique_ptr<ast::ast_expr> &expr,
                          expr_gen_ctx &ctx, bool assignment = false) {
    gen_handler_scope &scope = ctx.scope;

    switch (expr->type) {
        case ast::EXPR_LITERAL: {
            auto data = static_cast<ast::ast_expr_literal*>(expr.get());

            switch (data->literal_type) {
                case ast::EXPR_LITERAL_FLOAT:
                    scope.instrs.push_back(INSTR_16(
                        bc::OP_LOADC,
                        scope.get_literal(data->floatv)));
                    break;

                case ast::EXPR_LITERAL_INTEGER:
                    switch (data->intv) {
                        case 0:
                            scope.instrs.push_back(INSTR(bc::OP_LOADI0));
                            break;

                        case 1:
                            scope.instrs.push_back(INSTR(bc::OP_LOADI1));
                            break;

                        default:
                            scope.instrs.push_back(INSTR_16(
                                bc::OP_LOADC,
                                scope.get_literal(data->intv)
                            ));
                            break;
                    }
                    break;

                case ast::EXPR_LITERAL_STRING:
                    scope.instrs.push_back(INSTR_16(
                        bc::OP_LOADC,
                        scope.get_literal(data->str)));
                    break;

                case ast::EXPR_LITERAL_SYMBOL:
                    scope.instrs.push_back(INSTR_16(
                        bc::OP_LOADC,
                        scope.get_symbol(data->str)));
                    break;
                
                case ast::EXPR_LITERAL_VOID:
                    scope.instrs.push_back(INSTR(bc::OP_LOADVOID));
                    break;
            }

            break;
        }

        case ast::EXPR_IDENTIFIER: {
            auto data = static_cast<ast::ast_expr_identifier*>(expr.get());

            switch (data->scope) {
                case ast::SCOPE_LOCAL:
                    if (assignment) {
                        scope.instrs.push_back(INSTR_16(
                            bc::OP_STOREL,
                            scope.get_local_index(data->identifier)));
                    } else {
                        scope.instrs.push_back(INSTR_16(
                            bc::OP_LOADL,
                            scope.get_local_index(data->identifier)));
                    }

                    break;

                case ast::SCOPE_GLOBAL:
                    if (assignment) {
                        scope.instrs.push_back(INSTR_16(
                            bc::OP_STOREG,
                            scope.get_symbol(data->identifier)));
                    } else {
                        scope.instrs.push_back(INSTR_16(
                            bc::OP_LOADG,
                            scope.get_symbol(data->identifier)));
                    }

                    break;

                case ast::SCOPE_PROPERTY:
                    if (assignment) {
                        scope.instrs.push_back(INSTR(bc::OP_LOADL0));
                        scope.instrs.push_back(INSTR_16(
                            bc::OP_LOADC,
                            scope.get_symbol(data->identifier)));
                        scope.instrs.push_back(INSTR(bc::OP_OIDXS));
                    } else {
                        scope.instrs.push_back(INSTR(bc::OP_LOADL0));
                        scope.instrs.push_back(INSTR_16(
                            bc::OP_LOADC,
                            scope.get_symbol(data->identifier)));
                        scope.instrs.push_back(INSTR(bc::OP_OIDXG));
                    }

                    break;
            }

            break;
        }

        case ast::EXPR_THE: {
            auto data = static_cast<ast::ast_expr_the*>(expr.get());
            scope.instrs.push_back(INSTR_8(bc::OP_THE, data->identifier));
            break;
        }

        case ast::EXPR_LIST: {
            auto data = static_cast<ast::ast_expr_list*>(expr.get());
            scope.instrs.push_back(INSTR_16(bc::OP_NEWLLIST, data->items.size()));

            uint16_t add_str_idx = scope.get_symbol("add");
            
            for (auto &elem : data->items) {
                scope.instrs.push_back(INSTR(bc::OP_DUP));
                generate_expr(elem, ctx);
                scope.instrs.push_back(INSTR_16_8(bc::OP_OCALL, add_str_idx, 1));
                scope.instrs.push_back(INSTR(bc::OP_POP));
            }

            break;
        }

        case ast::EXPR_PROP_LIST: {
            auto data = static_cast<ast::ast_expr_prop_list*>(expr.get());
            (void)data;

            assert(false && "TODO: ast::EXPR_PROP_LIST");

            // ostream << "lingo.propList(";
            // bool write_comma = false;
            // for (auto &pair : data->pairs) {
            //     if (write_comma)
            //         ostream << ", ";

            //     generate_expr(pair.first, ostream, ctx);
            //     ostream << ",";
            //     generate_expr(pair.second, ostream, ctx);
                
            //     write_comma = true;
            // }
            // ostream << ")";

            break;
        }

        case ast::EXPR_BINOP: {
            auto data = static_cast<ast::ast_expr_binop*>(expr.get());

            generate_expr(data->left, ctx);
            generate_expr(data->right, ctx);

            switch (data->op) {
                case ast::EXPR_BINOP_AND:
                    scope.instrs.push_back(INSTR(bc::OP_AND));
                    break;

                case ast::EXPR_BINOP_OR:
                    scope.instrs.push_back(INSTR(bc::OP_OR));
                    break;

                case ast::EXPR_BINOP_ADD:
                    scope.instrs.push_back(INSTR(bc::OP_ADD));
                    break;

                case ast::EXPR_BINOP_SUB:
                    scope.instrs.push_back(INSTR(bc::OP_SUB));
                    break;

                case ast::EXPR_BINOP_MUL:
                    scope.instrs.push_back(INSTR(bc::OP_MUL));
                    break;

                case ast::EXPR_BINOP_DIV:
                    scope.instrs.push_back(INSTR(bc::OP_DIV));
                    break;

                case ast::EXPR_BINOP_MOD:
                    scope.instrs.push_back(INSTR(bc::OP_MOD));
                    break;

                case ast::EXPR_BINOP_CONCAT:
                    scope.instrs.push_back(INSTR(bc::OP_CONCAT));
                    break;

                case ast::EXPR_BINOP_CONCAT_WITH_SPACE:
                    scope.instrs.push_back(INSTR(bc::OP_CONCATSP));
                    break;
                
                case ast::EXPR_BINOP_EQ:
                    scope.instrs.push_back(INSTR(bc::OP_EQ));
                    break;

                case ast::EXPR_BINOP_NEQ:
                    scope.instrs.push_back(INSTR(bc::OP_EQ));
                    scope.instrs.push_back(INSTR(bc::OP_NOT));
                    break;

                case ast::EXPR_BINOP_GT:
                    scope.instrs.push_back(INSTR(bc::OP_GT));
                    break;

                case ast::EXPR_BINOP_LT:
                    scope.instrs.push_back(INSTR(bc::OP_LT));
                    break;

                case ast::EXPR_BINOP_GE:
                    scope.instrs.push_back(INSTR(bc::OP_GTE));
                    break;

                case ast::EXPR_BINOP_LE:
                    scope.instrs.push_back(INSTR(bc::OP_LTE));
                    break;
                    
                default: assert(false); break;
            }

            break;
        }

        case ast::EXPR_UNOP: {
            auto data = static_cast<ast::ast_expr_unop*>(expr.get());

            generate_expr(data->expr, ctx);

            switch (data->op) {
                case ast::EXPR_UNOP_NEG:
                    scope.instrs.push_back(INSTR(bc::OP_UNM));
                    break;

                case ast::EXPR_UNOP_NOT:
                    scope.instrs.push_back(INSTR(bc::OP_NOT));
                    break;
            }

            break;
        }

        case ast::EXPR_CALL: {
            assert(false && "EXPR_CALL not implemented");
            // auto data = static_cast<ast::ast_expr_call*>(expr.get());
            // bool first_comma;

            // if (data->method->type == ast::EXPR_DOT) {
            //     auto handler_ref =
            //         static_cast<ast::ast_expr_dot*>(data->method.get());
                
            //     generate_expr(handler_ref->expr, ostream, ctx);
            //     ostream << ":" << handler_ref->index << "(";
            //     first_comma = false;
            // } else {
            //     if (data->method->type != ast::EXPR_IDENTIFIER) {
            //         throw gen_exception(data->pos, "reference to handler must come from direct identifier or dot index");
            //     }

            //     auto handler_id =
            //         static_cast<ast::ast_expr_identifier*>(data->method.get());
            //     const std::string &name = handler_id->identifier;
                
            //     // handler name not found in script, dynamic dispatch
            //     if (!get_handler_ref(name, ostream, ctx)) {
            //         ostream << "call_handler(";
            //         write_escaped_str(name, ostream);
            //         first_comma = true;
            //     } else {
            //         ostream << "(";
            //         first_comma = false;
            //     }
            // }

            // for (auto &arg_expr : data->arguments) {
            //     if (first_comma)
            //         ostream << ", ";
            //     first_comma = true;

            //     generate_expr(arg_expr, ostream, ctx);
            // }

            // ostream << ")";
            break;
        }

        case ast::EXPR_DOT: {
            assert(false && "EXPR_DOT not implemented");
            // auto data = static_cast<ast::ast_expr_dot*>(expr.get());

            // ostream << "(";
            // generate_expr(data->expr, ostream, ctx);
            // ostream << ")." << data->index;
            break;
        }

        case ast::EXPR_INDEX: {
            assert(false && "EXPR_INDEX not implemented");
            // auto data = static_cast<ast::ast_expr_index*>(expr.get());

            // if (data->index_to) {
            //     ostream << "(lruntime.range(";
            //     generate_expr(data->expr, ostream, ctx);
            //     ostream << ", ";
            //     generate_expr(data->index_from, ostream, ctx);
            //     ostream << ", ";
            //     generate_expr(data->index_to, ostream, ctx);
            //     ostream << "))";
            // } else {
            //     ostream << "(";
            //     generate_expr(data->expr, ostream, ctx);
            //     ostream << ")[";
            //     generate_expr(data->index_from, ostream, ctx);
            //     ostream << "]";
            // }

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

// static index_split_result object_index_split(
//     const std::unique_ptr<ast::ast_expr> &expr, expr_gen_ctx &ctx,
//     std::string &left, std::string &right
// ) {
//     if (expr->type == ast::EXPR_INDEX) {
//         auto data = static_cast<ast::ast_expr_index*>(expr.get());
//         if (data->index_to) {
//             throw gen_exception(expr->pos, "internal: object_index_split with index range is unsupported");
//         }
        
//         std::stringstream ls;
//         generate_expr(data->expr, ls, ctx);

//         std::stringstream rs;
//         generate_expr(data->index_from, rs, ctx);

//         left = ls.str();
//         right = rs.str();
//         return INDEX_SPLIT_DYNAMIC;
//     }

//     if (expr->type == ast::EXPR_DOT) {
//         auto data = static_cast<ast::ast_expr_dot*>(expr.get());
        
//         std::stringstream ls;
//         generate_expr(data->expr, ls, ctx);

//         left = ls.str();
//         right = data->index;
//         return INDEX_SPLIT_STATIC;
//     }

//     return INDEX_SPLIT_INVALID;
// }

static void generate_statement(const std::unique_ptr<ast::ast_statement> &stm,
                               gen_handler_scope &scope) {
    expr_gen_ctx expr_ctx { scope };

    // usage:
    // {
    //   auto tmp = scope.create_temp_var();
    //   auto chk = cond_check(cond_expr, tmp);
    //   _tmp_stream << "if " << chk << " then"
    // }

    switch (stm->type) {
        case ast::STATEMENT_EXPR: {
            auto data = static_cast<ast::ast_statement_expr*>(stm.get());

            generate_expr(data->expr, expr_ctx);
            scope.instrs.push_back(INSTR(bc::OP_POP));

            // if (data->expr->type == ast::EXPR_CALL) {
            //     generate_expr(data->expr, tmp_stream, expr_ctx);
            //     tmp_stream << "\n";    
            // } else {
            //     tmp_stream << "_ = ";
            //     generate_expr(data->expr, tmp_stream, expr_ctx);
            //     tmp_stream << " _ = nil\n";
            // }

            // body_contents << tmp_stream.rdbuf();
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

            generate_expr(assign->rvalue, expr_ctx);
            generate_expr(assign->lvalue, expr_ctx, true);

            break;
        }

        case ast::STATEMENT_RETURN: {
            auto data = static_cast<ast::ast_statement_return*>(stm.get());

            if (data->expr)
                generate_expr(data->expr, expr_ctx);
            else
                scope.instrs.push_back(INSTR(bc::OP_LOADVOID));

            scope.instrs.push_back(INSTR(bc::OP_RET));
            break;
        }

        case ast::STATEMENT_PUT: {
            auto data = static_cast<ast::ast_statement_put*>(stm.get());
            generate_expr(data->expr, expr_ctx);
            scope.instrs.push_back(INSTR(bc::OP_PUT));
            break;
        }

        case ast::STATEMENT_PUT_ON: {
            auto data = static_cast<ast::ast_statement_put_on*>(stm.get());
            (void)data;
            assert(false && "PUT_ON unimplemented");

            // std::string l, r;

            // switch (object_index_split(data->target, expr_ctx, l, r)) {
            //     case INDEX_SPLIT_INVALID: {
            //         std::unique_ptr<ast::ast_expr> *expr_left;
            //         std::unique_ptr<ast::ast_expr> *expr_right;

            //         if (data->before) {
            //             expr_left = &data->expr;
            //             expr_right = &data->target;
            //         } else {
            //             expr_left = &data->target;
            //             expr_right = &data->expr;
            //         }

            //         generate_expr(data->target, tmp_stream, expr_ctx);
            //         tmp_stream << " = ";
            //         generate_expr(*expr_left, tmp_stream, expr_ctx);
            //         tmp_stream << " .. ";
            //         generate_expr(*expr_right, tmp_stream, expr_ctx);
            //         break;
            //     }

            //     case INDEX_SPLIT_STATIC: {
            //         // local _tmp0 = target()
            //         // _tmp0.idx = _tmp0.idx .. expr()
            //         auto tmp0 = expr_ctx.scope.create_temp_var(func_stream);
            //         tmp_stream << tmp0.name << " = " << l << "\n";
            //         tmp_stream << tmp0.name << "." << r << " = ";

            //         if (data->before) {
            //             generate_expr(data->expr, tmp_stream, expr_ctx);
            //             tmp_stream << " .. " << tmp0.name << "." << r;
            //         } else {
            //             tmp_stream << " " << tmp0.name << "." << r << " .. ";
            //             generate_expr(data->expr, tmp_stream, expr_ctx);
            //         }

            //         break;
            //     }

            //     case INDEX_SPLIT_DYNAMIC: {
            //         // local _tmp0 = target()
            //         // local _tmp1 = index()
            //         // _tmp0[_tmp1] = _tmp0[_tmp1] .. expr()
            //         auto tmp0 = expr_ctx.scope.create_temp_var(func_stream);
            //         auto tmp1 = expr_ctx.scope.create_temp_var(func_stream);
            //         tmp_stream << tmp0.name << " = " << l << " ";
            //         tmp_stream << tmp1.name << " = " << r << "\n";

            //         if (data->before) {
            //             tmp_stream << tmp0.name << "[" << tmp1.name << "] = ";
            //             generate_expr(data->expr, tmp_stream, expr_ctx);
            //             tmp_stream << " .. " << tmp0.name << "[" << tmp1.name << "]";
            //         } else {
            //             tmp_stream << tmp0.name << "[" << tmp1.name << "] = "
            //                 << tmp0.name << "[" << tmp1.name << "] .. ";
            //             generate_expr(data->expr, tmp_stream, expr_ctx);
            //         }

            //         break;
            //     }
            // }

            // body_contents << tmp_stream.rdbuf() << "\n";
            break;
        }

        case ast::STATEMENT_EXIT_REPEAT: {
            // body_contents << "break\n";
            assert(false && "EXIT_REPEAT unimplemented");
            break;
        }

        case ast::STATEMENT_NEXT_REPEAT: {
            // body_contents << "goto ::nextrepeat::\n";
            assert(false && "NEXT_REPEAT unimplemented");
            break;
        }

        case ast::STATEMENT_IF: {
            assert(false && "IF unimplemented");
            // auto data = static_cast<ast::ast_statement_if*>(stm.get());

            // size_t i = 0;
            // for (auto it = data->branches.begin(); it != data->branches.end(); ++it, ++i) {
            //     auto &branch = *it;

            //     if (i > 0) {
            //         tmp_stream << "else\n";
            //     }

            //     {
            //         // insert runtime check if value is a integer or void type
            //         auto tmp = expr_ctx.scope.create_temp_var(func_stream);
            //         auto check = cond_check(branch->condition, tmp);
            //         // create the Real branch
            //         tmp_stream << "if " << check << " then\n";
            //     }

            //     for (const auto &child_stm : branch->body) {
            //         generate_statement(child_stm, func_stream, tmp_stream, scope);
            //     }
            // }

            // if (data->has_else) {
            //     tmp_stream << "else\n";
            //     for (const auto &child_stm : data->else_branch) {
            //         generate_statement(child_stm, func_stream, tmp_stream, scope);
            //     }
            // }

            // for (i = 0; i < data->branches.size(); ++i) {
            //     tmp_stream << "end ";
            // }

            // tmp_stream << "\n";
            // body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_REPEAT_WHILE: {
            assert(false && "REPEAT_WHILE unimplemented");
            // auto data = static_cast<ast::ast_statement_repeat_while*>(stm.get());

            // tmp_stream << "while true do\n";
            // {
            //     auto tmp = expr_ctx.scope.create_temp_var(func_stream);
            //     auto check = cond_check(data->condition, tmp);
            //     // create the Real branch
            //     tmp_stream << "if not (" << check << ") then break end\n";
            // }

            // for (auto &child_stm : data->body) {
            //     generate_statement(child_stm, func_stream, tmp_stream, scope);
            // }

            // tmp_stream << "::nextrepeat::\nend\n";
            // body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_REPEAT_TO: {
            assert(false && "REPEAT_TO unimplemented");
            // auto data = static_cast<ast::ast_statement_repeat_to*>(stm.get());

            // generate_expr(data->iterator, tmp_stream, expr_ctx);
            // tmp_stream << " = ";
            // generate_expr(data->init, tmp_stream, expr_ctx);

            // tmp_stream << "\nwhile ";
            // generate_expr(data->iterator, tmp_stream, expr_ctx);

            // if (data->down) {
            //     tmp_stream << " >= ";
            // } else {
            //     tmp_stream << " <= ";
            // }

            // generate_expr(data->to, tmp_stream, expr_ctx);

            // tmp_stream << " do\n";

            // for (auto &child_stm : data->body) {
            //     generate_statement(child_stm, func_stream, tmp_stream, scope);
            // }

            // tmp_stream << "::nextrepeat::\n";

            // tmp_stream << LINECTL("O");
            // generate_expr(data->iterator, tmp_stream, expr_ctx);
            // tmp_stream << " = ";
            // generate_expr(data->iterator, tmp_stream, expr_ctx);
            // tmp_stream << LINECTL("I");

            // if (data->down) {
            //     tmp_stream << " - 1";
            // } else {
            //     tmp_stream << " + 1";
            // }

            // tmp_stream << "\nend\n";
            // body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_REPEAT_IN: {
            assert(false && "REPEAT_IN unimplemented");
            // auto data = static_cast<ast::ast_statement_repeat_in*>(stm.get());

            // auto tmp = scope.create_temp_var(func_stream);
            // tmp_stream << tmp.name << " = ";
            // generate_expr(data->iterable, tmp_stream, expr_ctx);

            // tmp_stream << "\nfor i=1, #" << tmp.name << " do\n";
            // generate_expr(data->iterator, tmp_stream, expr_ctx);
            // tmp_stream << " = " << tmp.name << "[i]\n";

            // for (auto &child_stm : data->body) {
            //     generate_statement(child_stm, func_stream, tmp_stream, scope);
            // }

            // tmp_stream << "::exitrepeat::\nend\n";
            // tmp_stream << tmp.name << " = nil\n";
            // // TODO: should use do blocks for temp variables instead of making
            // // them always active

            // body_contents << tmp_stream.rdbuf();
            break;
        }

        case ast::STATEMENT_CASE: {
            assert(false && "CASE unimplemented");
            // auto data = static_cast<ast::ast_statement_case*>(stm.get());

            // tmp_stream << "do\n";
            // tmp_stream << "local case = ";
            // generate_expr(data->expr, tmp_stream, expr_ctx);
            // tmp_stream << "\n";

            // // if case has only an otherwise clause, run otherwise clause
            // // unconditionally
            // if (data->clauses.empty() && data->has_otherwise) {
            //     for (auto &child_stm : data->otherwise_clause) {
            //         generate_statement(child_stm, func_stream, tmp_stream, scope);
            //     }
            // } else {
            //     bool is_first = true;
            //     for (auto &clause : data->clauses) {
            //         if (is_first)
            //             tmp_stream << "if ";
            //         else
            //             tmp_stream << "elseif ";

            //         bool insert_or = false;
            //         for (auto &check : clause->literal) {
            //             if (insert_or) {
            //                 tmp_stream << " or ";
            //             }

            //             tmp_stream << "(case == ";
            //             generate_expr(check, tmp_stream, expr_ctx);
            //             tmp_stream << ")";

            //             insert_or = true;
            //         }
            //         tmp_stream << " then\n";

            //         for (auto &child_stm : clause->branch) {
            //             generate_statement(child_stm, func_stream, tmp_stream, scope);
            //         }

            //         is_first = false;
            //     }

            //     if (data->has_otherwise) {
            //         tmp_stream << "else\n";

            //         for (auto &child_stm : data->otherwise_clause) {
            //             generate_statement(child_stm, func_stream, tmp_stream, scope);
            //         }
            //     }

            //     tmp_stream << "end\n";
            // }

            // tmp_stream << "end\n";
            // body_contents << tmp_stream.rdbuf();
            break;
        }

        default:
            throw gen_exception(stm->pos, "unknown statement type");
    }
}

static void generate_chunk(std::vector<uint8_t> &out,
                           const ast::ast_handler_decl &handler,
                           gen_script_scope &script_scope) {
    gen_handler_scope scope(script_scope);

    bc::chunk_header chunk_header {};

    if (handler.params.size() > UINT8_MAX)
        throw gen_exception(handler.pos, "parameter count exceeded max of 255");

    if (handler.locals.size() > UINT16_MAX)
        throw gen_exception(handler.pos, "local count exceeded max of 65535");

    chunk_header.nargs = (uint8_t) handler.params.size();

    // write and register parameter names
    // note: the me argument must always be present in order for property
    // variables to work correctly when the first argument (me) is not present
    // in the lingo code.
    for (auto it = handler.params.begin(); it != handler.params.end(); ++it) {
        scope.register_local(*it);
    }

    if (handler.params.size() == 0) {
        scope.register_local("me (implicit)");
        ++chunk_header.nargs;
    }

    for (auto &local_name : handler.locals) {
        scope.register_local(local_name);
    }

    for (auto &stm : handler.body) {
        generate_statement(stm, scope);
    }

    scope.instrs.push_back(INSTR(bc::OP_LOADVOID));
    scope.instrs.push_back(INSTR(bc::OP_RET));

    // TODO: call finalize_jumps here(?)

    if (scope.instrs.size() > UINT32_MAX)
        throw gen_exception(handler.pos, "too many instructions");

    if (scope.chunk_consts.size() > UINT16_MAX)
        throw gen_exception(handler.pos, "too many unique constants");

    chunk_header.nconsts = (uint16_t) scope.chunk_consts.size();
    chunk_header.ninstr = (uint32_t) scope.instrs.size();
    chunk_header.nlocals = (uint16_t) handler.locals.size();

    uintptr_t out_end = sizeof(chunk_header);

    uintptr_t instr_loc = aligned(alignof(bc::instr), out_end);
    size_t instr_size = sizeof(bc::instr) * scope.instrs.size();
    out_end = instr_loc + instr_size;

    uintptr_t const_loc = aligned(alignof(bc::chunk_const), out_end);
    size_t const_size = sizeof(bc::chunk_const) * scope.chunk_consts.size();
    out_end = const_loc + const_size;

    uintptr_t strpool_loc = aligned(alignof(bc::chunk_const_str), out_end);
    size_t strpool_size = scope.string_pool.size();
    out_end = strpool_loc + strpool_size;

    // array of bc::chunk_const_str*
    uintptr_t lname_loc = aligned(alignof(uintptr_t), out_end);
    size_t lname_size = scope.local_name_refs.size() * sizeof(uintptr_t);
    out_end = lname_loc + lname_size;

    chunk_header.instrs = (bc::instr *)instr_loc;
    chunk_header.consts = (bc::chunk_const *)const_loc;
    chunk_header.string_pool = (bc::chunk_const_str *)strpool_loc;
    chunk_header.local_names = (const bc::chunk_const_str **)lname_loc;
    
    out.resize(out_end);
    memcpy(out.data(), &chunk_header, sizeof(chunk_header));
    memcpy(out.data() + instr_loc, scope.instrs.data(), instr_size);
    memcpy(out.data() + const_loc, scope.chunk_consts.data(), const_size);
    memcpy(out.data() + strpool_loc, scope.string_pool.data(), strpool_size);
    memcpy(out.data() + lname_loc, scope.local_name_refs.data(), lname_size);

    // if (body_contents.rdbuf()->in_avail()) {
    //     stream << body_contents.rdbuf();
    // }

    // stream << "end\n\n";
}

static void generate_script(const ast::ast_root &root,
                            std::vector<std::vector<uint8_t>> &chunk_list) {
    gen_script_scope script_scope;

    // first, put all handlers defined in script into scope
    for (auto &decl : root.handlers) {
        script_scope.handlers.insert(decl->name);
    }

    // then perform code generation
    // stream << "local symbol = lingo.symbol\n";
    // stream << "local globals = lingo.globals\n";
    // stream << "local lruntime = lingo.runtime\n";
    // stream << "local land = lruntime.logical_and\n";
    // stream << "local lor = lruntime.logical_or\n";
    // stream << "local lnot = lruntime.logical_not\n";
    // stream << "local tostring = lruntime.to_string\n";
    // stream << "local btoi = lruntime.bool_to_int\n";
    // stream << "\n";
    // stream << "local script = {}\n";

    // stream << "script._props = {";
    // for (auto it = root.properties.begin(); it != root.properties.end(); ++it) {
    //     if (it != root.properties.begin()) {
    //         stream << ", ";
    //     }

    //     write_escaped_str(*it, stream);
    // }
    // stream << "}\n";
    // stream << "\n";

    for (auto &decl : root.handlers) {
        std::vector<uint8_t> out;
        generate_chunk(out, *decl, script_scope);
        out.shrink_to_fit();
        chunk_list.push_back(out);
    }

    // stream << "return script\n";
}

bool bc::generate_bytecode(const ast::ast_root &root,
                           std::vector<std::vector<uint8_t>> &chunk_list,
                           parse_error *error) {
    try {
        generate_script(root, chunk_list);
    } catch (gen_exception except) {
        if (error) {
            *error = parse_error { except.pos, except.msg };
        }

        return false;
    }

    return true;
}









bool lingo::compile_bytecode(std::istream &istream,
                             std::vector<std::vector<uint8_t>> &chunk_list,
                             parse_error *error) {
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

    if (!lingo::bc::generate_bytecode(script_tree, chunk_list, &err)) {
        if (error) *error = err;
        return false;
    }

    return true;
}

enum usage_hint {
    HINT_NONE,
    HINT_LOCAL,
    HINT_CONST,
    HINT_THE
};

static int eval_hint(char *buf, size_t bufsz, const bc::chunk_header *chunk,
                     int value, usage_hint hint) {
    if (hint == HINT_NONE) return 0;
    if (hint == HINT_CONST) {
        assert(chunk->consts);
        assert(chunk->string_pool);

        const bc::chunk_const *c =
            &bc::base_offset(chunk, chunk->consts)[value];
        const bc::chunk_const_str *str_pool =
            bc::base_offset(chunk, chunk->string_pool);
        
        switch (c->type) {
            case bc::TYPE_INT:
                return snprintf(buf, bufsz, "%i", c->i32);

            case bc::TYPE_FLOAT:
                return snprintf(buf, bufsz, "%f", c->f64);

            case bc::TYPE_STRING: {
                const bc::chunk_const_str *str =
                    bc::base_offset(str_pool, c->str);
                return snprintf(buf, bufsz, "\"%s\"", &str->first);
            }

            case bc::TYPE_SYMBOL: {
                const bc::chunk_const_str *str =
                    bc::base_offset(str_pool, c->str);
                return snprintf(buf, bufsz, "#%s", &str->first);
            }

            default:
                return snprintf(buf, bufsz, "???");
        }
    }

    if (hint == HINT_LOCAL && chunk->local_names) {
        assert(chunk->string_pool);
        const bc::chunk_const_str *str_pool =
            bc::base_offset(chunk, chunk->string_pool);

        const bc::chunk_const_str **names =
            bc::base_offset(chunk, chunk->local_names);

        const bc::chunk_const_str *str = bc::base_offset(str_pool, names[value]);
        return snprintf(buf, bufsz, "%s", &str->first);
    }

    return 0;
}

// fucking evil goto but i don't care
void bc::instr_disasm(const chunk_header *chunk, instr instruction, char *buf,
                      size_t bufsz) {
    #define OP(o) case OP_##o: opcode = #o; goto decode_none
    #define OP_U16(o, h) case OP_##o: opcode = #o; hint_a = h; goto decode_u16
    #define OP_U16_U8(o, a, b) case OP_##o: opcode = #o; hint_a = a; hint_b = b; goto decode_u16_u8
    #define OP_I16(o, a) case OP_##o: opcode = #o; hint_a = a; goto decode_i16
    #define OP_I16_U8(o, a, b) case OP_##o: opcode = #o; hint_a = a; hint_b = b; goto decode_i16_u8
    #define OP_U8(o, a) case OP_##o: opcode = #o; hint_a = a; goto decode_u8
    #define WRITE(f, ...) do {\
        offset = (size_t) (f)(buf, bufsz, __VA_ARGS__); \
        if (offset >= bufsz) return; \
        buf += offset; bufsz -= offset; \
    } while (false)

    const char *opcode;
    usage_hint hint_a = HINT_NONE;
    usage_hint hint_b = HINT_NONE;

    uint16_t u16;
    int16_t i16;
    uint8_t u8[2];
    size_t offset = 0;

    int operand_a, operand_b;

    switch (instruction & 0xFF) {
        OP(RET);
        OP(POP);
        OP(DUP);
        OP(LOADVOID);
        OP(LOADI0);
        OP(LOADI1);
        OP_U16(LOADC, HINT_CONST);
        OP_U16(LOADL, HINT_LOCAL);
        OP(LOADL0);
        OP_U16(LOADG, HINT_CONST);
        OP_U16(STOREL, HINT_LOCAL);
        OP_U16(STOREG, HINT_CONST);
        OP(UNM);
        OP(ADD);
        OP(SUB);
        OP(MUL);
        OP(DIV);
        OP(MOD);
        OP(EQ);
        OP(LT);
        OP(GT);
        OP(LTE);
        OP(GTE);
        OP(AND);
        OP(OR);
        OP(NOT);
        OP(CONCAT);
        OP(CONCATSP);
        OP_I16(JMP, HINT_NONE);
        OP_I16(BRT, HINT_NONE);
        OP_I16(BRF, HINT_NONE);
        OP_U16_U8(CALL, HINT_CONST, HINT_NONE);
        OP_U16_U8(OCALL, HINT_CONST, HINT_NONE);
        OP(OIDXG);
        OP(OIDXS);
        OP(OIDXK);
        OP(OIDXKR);
        OP_U8(THE, HINT_THE);
        OP_U16(NEWLLIST, HINT_NONE);
        OP(NEWPLIST);
        OP_U16(CASE, HINT_NONE);
        OP(PUT);

        default:
            snprintf(buf, bufsz, "??");
            return;
    }

    decode_none:
        WRITE(snprintf, "%s", opcode);
        return;

    decode_u8:
        bc::instr_decode(instruction, &u8[0]);
        operand_a = (int)u8[0];
        WRITE(snprintf, "%-12s %i", opcode, operand_a);
        goto hint_a;

    decode_u16:
        bc::instr_decode(instruction, &u16);
        operand_a = (int)u16;
        WRITE(snprintf, "%-12s %i", opcode, operand_a);
        goto hint_a;

    decode_i16:
        bc::instr_decode(instruction, &i16);
        operand_a = (int)i16;
        WRITE(snprintf, "%-12s %i", opcode, operand_a);
        goto hint_a;
        
    decode_u16_u8:
        bc::instr_decode(instruction, &u16, &u8[0]);
        operand_a = (int)u16;
        operand_b = (int)u8[0];
        WRITE(snprintf, "%-12s %i %i", opcode, operand_a, operand_b);
        goto hint_ab;

    hint_a:
        if (chunk && hint_a != HINT_NONE) {
            WRITE(snprintf, " ; ");
            WRITE(eval_hint, chunk, operand_a, hint_a);
        }
        return;

    hint_ab:
        if (chunk && (hint_a != HINT_NONE || hint_b != HINT_NONE)) {
            WRITE(snprintf, " ; ");

            if (hint_a != HINT_NONE) {
                WRITE(eval_hint, chunk, operand_a, hint_a);
            }

            WRITE(snprintf, ", ");

            if (hint_b != HINT_NONE) {
                WRITE(eval_hint, chunk, operand_b, hint_b);
            }
        }
        return;

    // decode_i16_u8:
    //     bc::instr_decode(instruction, &i16, &u8[0]);
    //     offset += snprintf(buf, bufsz, "%-12s %i %i", opcode, (int)i16, (int)u8[0]);
    //     return;
}