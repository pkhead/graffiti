#include <lua.hpp>
#include <iostream>
#include <istream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "lingo/lingo.hpp"

constexpr const char* MT_LIST = "linear list";
constexpr const char* MT_PROP_LIST = "property list";

static int lua_load_lingo(lua_State *L, std::istream &istream, const char *chunk_name) {
    std::stringstream ostream;
    lingo::parse_error error;
    if (!lingo::compile_luajit_text(istream, ostream, &error)) {
        static char buf[256];
        const char *fmt;
        const char *nm = chunk_name;
        if (chunk_name[0] == '@' || chunk_name[0] == '=') {
            fmt = "%s:%i: %s";
            ++nm;
        } else if (strlen(chunk_name) >= 48) {
            fmt = "[string \"%.45s...\"]:%i: %s";
        } else {
            fmt = "[string \"%s\"]:%i: %s";
        }

        snprintf(buf, 256, fmt, nm, error.pos.line, error.errmsg.c_str());
        lua_pushstring(L, buf);
        return LUA_ERRSYNTAX;
    } else {
        std::string lua_code = ostream.str();
        return luaL_loadbuffer(L, lua_code.c_str(), lua_code.size(), chunk_name);
    }
}

class luawrap_State {
public:
    lua_State *ptr;
    inline constexpr luawrap_State(lua_State *ptr) noexcept : ptr(ptr)
    { }

    operator lua_State*() const { return ptr; }

    inline ~luawrap_State() {
        lua_close(ptr);
    }
};

struct lua_linear_list {
    int tref;
    int length;

    lua_linear_list(lua_State *L) {
        lua_newtable(L);
        tref = luaL_ref(L, LUA_REGISTRYINDEX);
        length = 0;
    }

    void release(lua_State *L) {
        luaL_unref(L, LUA_REGISTRYINDEX, tref);
        tref = 0;
        length = 0;
    }

    inline constexpr void push_table(lua_State *L) const {
        lua_rawgeti(L, LUA_REGISTRYINDEX, tref);
    }
};

static void set_up_globals(lua_State *L) {
    luaL_dostring(L, R"lua(
        lingo = {}
        lingo.globals = {}
        lingo.runtime = {}
        local lruntime = lingo.runtime

        local builtin_handlers = {
            readinput = function()
                return io.read("*l")
            end
        }
        
        function call_handler(hname, ...)
            local h = builtin_handlers[hname]
            if h then
                return h(...)
            else
                error(("unknown handler '%s'"):format(hname), 2)
            end
        end

        function lruntime.to_string(n)
            if n == nil then
                return ""
            else
                return tostring(n)
            end
        end

        function lruntime.logical_and(a, b)
            if (a ~= 0 and a ~= nil) and (b ~= 0 and b ~= nil) then
                return 1
            else
                return 0
            end
        end

        function lruntime.logical_or(a, b)
            if (a ~= 0 and a ~= nil) or (b ~= 0 and b ~= nil) then
                return 1
            else
                return 0
            end
        end

        function lruntime.logical_not(a)
            if not (a ~= 0 and a ~= nil) then
                return 1
            else
                return 0
            end
        end

        function lruntime.bool_to_int(v)
            if v then
                return 1
            else
                return 0
            end
        end

        -- TODO: holes can probably exist in lingo lists so probably should use
        -- either a userdata, or store length alongside table ... in which case
        -- i should use a userdata anyway so that client code can't pairs
        -- through the thing.
        local lingo_list_mt = {}
        lingo_list_mt.funcs = {
            add = function(self, v)
                rawset(self, #self+1, v)
            end
        }

        function lingo_list_mt.__index(t,k)
            local f = lingo_list_mt.funcs[k]
            if f then
                return f
            end

            if k == "length" then
                return #t
            end
            
            error("key does not exist", 2)
        end

        function lingo_list_mt.__newindex(t,k,v)
            error("key does not exist", 2)
        end

        lingo_list_mt.__metatable = "The metatable is locked"

        function lingo.list(...)
            return setmetatable({...}, lingo_list_mt)
        end
    )lua");

    // set up linear list metatable
    luaL_newmetatable(L, MT_LIST);

    // __gc
    lua_pushcfunction(L, [](lua_State *L) -> int {
        lua_linear_list *list = (lua_linear_list*)luaL_checkudata(L, 1, MT_LIST);
        list->release(L);
        list->~lua_linear_list();
        return 0;
    });
    lua_setfield(L, -2, "__gc");

    // __index
    lua_pushcfunction(L, [](lua_State *L) {
        lua_linear_list *list = (lua_linear_list*)luaL_checkudata(L, 1, MT_LIST);

        if (lua_isnumber(L, 2)) {
            int n = (int)luaL_checkinteger(L, 2) - 1;
            if (n < 0 || n >= list->length) {
                return luaL_error(L, "index out of range");
            }

            list->push_table(L);
            lua_rawgeti(L, -1, n);
            return 1;
        }

        const char *k = luaL_checkstring(L, 2);

        if (!strcmp(k, "length")) {
            lua_pushnumber(L, (lua_Number)list->length);
            return 1;
        }

        if (!strcmp(k, "add")) {
            lua_pushcfunction(L, [](lua_State *L) -> {
                lua_linear_list *list = (lua_linear_list*)luaL_checkudata(L, 1, MT_LIST);
                list->push_table(L);
                lua_pushvalue(L, 2);
                lua_rawseti(L, -2)
            });
            return 1;
        }
        return 0;
    });

    lua_pushstring(L, "The metatable is locked.");
    lua_setfield(L, -2, "__metatable");

    // lingo.list
    lua_pushcfunction(L, [](lua_State *L) -> int {
        int argn = lua_gettop(L);

        lua_linear_list *list = (lua_linear_list*)lua_newuserdata(L, sizeof(lua_linear_list));
        new(&list) lua_linear_list(L);
        luaL_getmetatable(L, MT_LIST);
        lua_setmetatable(L, -2);

        list->push_table(L);
        for (int i = 1; i <= argn; ++i) {
            lua_pushvalue(L, i);
            lua_rawseti(L, -2, i);
        }
        lua_pop(L, 1); // pop table

        return 1;
    });
}

static int lua_panic(lua_State *L) {
    throw std::runtime_error(lua_tostring(L, -1));
    return 0;
}

int lingo_compiler_test(int argc, const char *argv[]) {
    if (argc < 3) {
        std::cerr << "error: invalid arguments\nexpected format: evillingo [input] [output]\n";
        return 2;
    }

    int file_index = 0;
    const char *files[] = {nullptr, nullptr};
    bool no_line_numbers = false;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        
        if (!strcmp(arg, "--no-line-numbers")) {
            no_line_numbers = true;
        }
        else {
            if (file_index >= 2) {
                std::cerr << "no more files please";
                return 2;
            }

            files[file_index++] = arg;
        }
    }

    bool use_cin = !strcmp(files[0], "-");
    bool use_cout = !strcmp(files[1], "-");

    std::unique_ptr<std::istream> istream;
    if (use_cin) {
        istream = std::make_unique<std::istream>(std::cin.rdbuf());
    } else {
        auto fstream = new std::ifstream(files[0]);
        if (!fstream->is_open()) {
            std::cerr << "could not open file " << files[0] << "\n";
            delete fstream;
        }

        istream = std::unique_ptr<std::istream>(fstream);
    }

    std::unique_ptr<std::ostream> ostream;
    if (use_cout) {
        ostream = std::make_unique<std::ostream>(std::cout.rdbuf());
    } else {
        auto fstream = new std::ofstream(files[1]);
        if (!fstream->is_open()) {
            std::cerr << "could not open file " << files[1] << "\n";
            delete fstream;
        }

        ostream = std::unique_ptr<std::ostream>(fstream);
    }

    lingo::parse_error error;
    lingo::extra_gen_params params;
    params.no_line_numbers = no_line_numbers;

    if (!lingo::compile_luajit_text(*istream, *ostream, &error, &params)) {
        std::cerr << "error " << error.pos.line << ":" << error.pos.column << ": " << error.errmsg << "\n";
        return 1;
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

    //         case lingo::ast::TOKEN_IDENTIFIER:
    //             std::cout << "(IDN) ";
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
            
    //         case lingo::ast::TOKEN_LINE_END:
    //             std::cout << "(NXT) ";
    //             break;
    //     }

    //     std::cout << "\n";
    // }

    // auto thing = static_cast<lingo::ast::ast_handler_definition*>(statements[0].get());
    // std::cout << statements.size() << "\n";
    // std::cout << thing->params.size() << "\n";

    return 0;
}

int main(int argc, const char *argv[]) {
    if (argc > 1) {
        return lingo_compiler_test(argc, argv);
    }

    luawrap_State L(lua_open());
    lua_atpanic(L, lua_panic);
    luaL_openlibs(L);
    set_up_globals(L);
    
    {
        constexpr const char *FILE_NAME = "input.ls";
        std::ifstream f(FILE_NAME);
        if (!f.is_open()) {
            std::cerr << "could not open " << FILE_NAME << "\n";
            return 1;
        }

        std::string chunk_name = std::string("@") + FILE_NAME;
        if (lua_load_lingo(L, f, chunk_name.c_str()) != LUA_OK) {
            std::cerr << lua_tostring(L, -1);
            return 1;
        }
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        std::cerr << lua_tostring(L, -1);
        return 1;
    }

    lua_getfield(L, -1, "main");
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        std::cerr << lua_tostring(L, -1);
        return 1;
    }

    return 0;
}