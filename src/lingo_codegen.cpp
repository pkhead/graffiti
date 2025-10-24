#include "lingo.hpp"
#include <memory>
#include <unordered_set>

using namespace lingo;

class gen_exception : public std::runtime_error {
public:
    int line;
    std::string msg;

    gen_exception(int line, const std::string &what = "")
        : line(line), msg(what), std::runtime_error(what) { } // TODO: add pos info to error
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
    std::shared_ptr<var_scope> parent_scope;

    bool get_var(std::string id, scope_class &classif) const {
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
            if (it != properties.end()) {
                classif = SCOPE_LOCAL;
                return true;
            }
        }

        return false;
    }
};

static void generate_func(std::ostream &stream,
                          const ast::ast_handler_definition &handler) {
    stream << "function script." << handler.name << "(";

    for (auto it = handler.params.begin(); it != handler.params.end(); ++it) {
        if (it != handler.params.begin())
            stream << ", ";
        
        stream << *it;
    }

    stream << ")\n";
    stream << "end\n\n";
}

static void generate_script(const ast::ast_root &root, std::ostream &stream) {
    auto script_scope = std::make_shared<var_scope>();

    // first, catalog all handlers defined in script
    for (auto &gdecl : root) {
        if (gdecl->type == ast::STATEMENT_DEFINE_HANDLER) {
            auto decl = static_cast<ast::ast_handler_definition*>(gdecl.get());
            script_scope->handlers.insert(decl->name);
        }
    }

    // then perform code generation
    stream << "local script = {}\n";
    stream << "local globals = lingo.globals\n";
    stream << "local callhandler = lingo.call\n";
    stream << "\n";

    for (auto &gdecl : root) {
        switch (gdecl->type) {
            case ast::STATEMENT_DECLARE_GLOBAL: {
                auto decl = static_cast<ast::ast_global_declaration*>(gdecl.get());
                for (auto &id : decl->identifiers) {
                    script_scope->globals.insert(id);
                }
                break;
            }

            case ast::STATEMENT_DECLARE_PROPERTY: {
                auto decl = static_cast<ast::ast_property_declaration*>(gdecl.get());
                for (auto &id : decl->identifiers) {
                    script_scope->properties.insert(id);
                }
                break;
            }

            case ast::STATEMENT_DEFINE_HANDLER: {
                auto decl = static_cast<ast::ast_handler_definition*>(gdecl.get());
                generate_func(stream, *decl);
                break;
            }

            default:
                throw gen_exception(gdecl->line, "internal error");
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
            *error = parse_error { pos_info { except.line, 0 }, except.msg };
        }

        return false;
    }

    return true;
}