#pragma once
#include <string>
#include <cstdint>
#include <istream>
#include <vector>
#include <utility>
#include <memory>
#include <cassert>

namespace lingo {
    struct pos_info {
        int line; // 1-indexed
        int column; // 1-indexed
    };

    struct parse_error {
        pos_info pos;
        std::string errmsg;
    };

    namespace ast {
        // tokens
        enum token_type : uint8_t {
            TOKEN_KEYWORD,
            TOKEN_SYMBOL,
            TOKEN_FLOAT,
            TOKEN_INTEGER,
            TOKEN_WORD,
            TOKEN_STRING,
            TOKEN_SYMBOL_LITERAL,
            TOKEN_LINE_END
        };

        enum token_keyword : uint8_t {
            KEYWORD_ON,
            KEYWORD_ELSE,
            KEYWORD_THEN,

            KEYWORD_AND,
            KEYWORD_OR,
            KEYWORD_NOT,
            KEYWORD_MOD,
        };

        enum token_word_id : uint8_t {
            WORD_ID_RETURN,
            WORD_ID_END,
            WORD_ID_EXIT,
            WORD_ID_NEXT,
            WORD_ID_IF,
            WORD_ID_REPEAT,
            WORD_ID_WITH,
            WORD_ID_TO,
            WORD_ID_DOWN,
            WORD_ID_WHILE,
            WORD_ID_CASE,
            WORD_ID_OTHERWISE,
            WORD_ID_THE,
            WORD_ID_OF,
            WORD_ID_IN,
            WORD_ID_PUT,
            WORD_ID_AFTER,
            WORD_ID_BEFORE,

            WORD_ID_TYPE,
            WORD_ID_NUMBER,
            WORD_ID_INTEGER,
            WORD_ID_STRING,
            WORD_ID_POINT,
            WORD_ID_RECT,
            WORD_ID_IMAGE,

            WORD_ID_GLOBAL,
            WORD_ID_PROPERTY,

            WORD_ID_UNKNOWN = UINT8_MAX
        };

        enum token_symbol : uint8_t {
            SYMBOL_COMMA, // ,
            SYMBOL_PERIOD, // .

            SYMBOL_MINUS, // -
            SYMBOL_PLUS, // +
            SYMBOL_SLASH, // /
            SYMBOL_STAR, // *
            SYMBOL_AMPERSAND, // &
            SYMBOL_POUND, // #
            SYMBOL_RANGE, // ..
            
            SYMBOL_LPAREN, // (
            SYMBOL_RPAREN, // )
            SYMBOL_LBRACKET, // [
            SYMBOL_RBRACKET, // ]
            SYMBOL_LBRACE, // {
            SYMBOL_RBRACE, // }
            SYMBOL_COLON, // :

            SYMBOL_EQUAL, // = (both assignment and comparison)
            SYMBOL_NEQUAL, // <>
            SYMBOL_LT, // <
            SYMBOL_GT, // >
            SYMBOL_LE, // <=
            SYMBOL_GE, // >=

            SYMBOL_DOUBLE_AMPERSAND, // && (concatenates with space)
            SYMBOL_COMMENT, // --
            SYMBOL_LINE_CONT, // \

            SYMBOL_INVALID = UINT8_MAX,
        };

        struct token {
            token_type type;
            pos_info pos;

            std::string str;
            union {
                token_keyword keyword;
                token_symbol symbol;
                token_word_id word_id;
                double number;
                int32_t integer;
            };

            static token make_keyword(token_keyword v, const pos_info &pos);
            static token make_integer(int32_t v, const pos_info &pos);
            static token make_symbol(token_symbol v, const pos_info &pos);
            static token make_float(double v, const pos_info &pos);
            static token make_word(const std::string &v, const pos_info &pos);
            static token make_word(token_word_id word_id, const pos_info &pos);
            static token make_string(const std::string &v, const pos_info &pos);
            static token make_symbol_literal(const std::string &v, const pos_info &pos);
            static token make_line_end(const pos_info &pos);

            constexpr bool is_keyword(token_keyword v) const noexcept {
                return type == TOKEN_KEYWORD && keyword == v;
            }

            constexpr bool is_word(token_word_id v) const noexcept {
                return type == TOKEN_WORD && word_id == v;
            }

            constexpr bool is_symbol(token_symbol v) const noexcept {
                return type == TOKEN_SYMBOL && symbol == v;
            }

            constexpr bool is_a(token_type t) const noexcept {
                return type == t;
            }
        };

        constexpr const char* token_type_str(token_type type) {
            switch (type) {
                case TOKEN_KEYWORD:
                    return "keyword";
            
                case TOKEN_SYMBOL:
                    return "symbol";
            
                case TOKEN_FLOAT:
                    return "float";

                case TOKEN_INTEGER:
                    return "integer";
            
                case TOKEN_WORD:
                    return "word";
            
                case TOKEN_STRING:
                    return "string";
                
                case TOKEN_SYMBOL_LITERAL:
                    return "symbol-literal";
            
                case TOKEN_LINE_END:
                    return "newline";

                default: return "???";
            }
        }

        bool parse_tokens(std::istream &stream, std::vector<token> &tokens,
                          parse_error *error);

        const char* keyword_to_str(token_keyword keyword);
        const char* symbol_to_str(token_symbol symbol);
        const char* word_id_to_str(token_word_id word_id);
        std::string token_to_str(const token &tok);

        // AST expressions
        enum ast_expr_type : uint8_t {
            EXPR_BINOP, // X ? Y
            EXPR_UNOP, // ? X
            EXPR_THE, // the X
            EXPR_LITERAL,
            EXPR_LIST,
            EXPR_PROP_LIST,
            EXPR_IDENTIFIER,
            EXPR_DOT, // X.Y
            EXPR_INDEX, // X[Y] or X[A..B]
            EXPR_CALL, // X(...)
        };

        enum ast_binop : uint8_t {
            EXPR_BINOP_ADD, // X + Y
            EXPR_BINOP_SUB, // X - Y
            EXPR_BINOP_MUL, // X * Y
            EXPR_BINOP_DIV, // X / Y
            EXPR_BINOP_MOD, // X mod Y

            EXPR_BINOP_AND, // X and Y
            EXPR_BINOP_OR, // X or Y

            EXPR_BINOP_LT, // X < Y
            EXPR_BINOP_GT, // X > Y
            EXPR_BINOP_LE, // X <= Y
            EXPR_BINOP_GE, // X >= Y
            EXPR_BINOP_EQ, // X = Y
            EXPR_BINOP_NEQ, // X <> Y

            EXPR_BINOP_CONCAT, // X & Y
            EXPR_BINOP_CONCAT_WITH_SPACE, // X && Y
        };

        enum ast_unop : uint8_t {
            EXPR_UNOP_NEG, // -X
            EXPR_UNOP_NOT // not X
        };

        enum ast_the_id : uint8_t {
            EXPR_THE_MOVIE_PATH,
            EXPR_THE_FRAME,
            EXPR_THE_DIR_SEPARATOR,
            EXPR_THE_MILLISECONDS,
            EXPR_THE_RANDOM_SEED,
            EXPR_THE_PLATFORM
        };

        enum ast_literal_type : uint8_t {
            EXPR_LITERAL_FLOAT,
            EXPR_LITERAL_INTEGER,
            EXPR_LITERAL_STRING,
            EXPR_LITERAL_VOID,
            EXPR_LITERAL_SYMBOL
        };

        enum ast_scope : uint8_t {
            SCOPE_PROPERTY, // highest precedence
            SCOPE_GLOBAL,
            SCOPE_LOCAL,
        };

        struct ast_expr {
            ast_expr_type type;
            pos_info pos;
        };

        struct ast_expr_binop : public ast_expr {
            inline ast_expr_binop() { type = EXPR_BINOP; }

            std::unique_ptr<ast_expr> left;
            std::unique_ptr<ast_expr> right;
            ast_binop op;
        };

        struct ast_expr_unop : public ast_expr {
            inline ast_expr_unop() { type = EXPR_UNOP; }

            std::unique_ptr<ast_expr> expr;
            ast_unop op;
        };

        struct ast_expr_the : public ast_expr {
            inline ast_expr_the() { type = EXPR_THE; }

            ast_the_id identifier;
        };

        struct ast_expr_literal : public ast_expr {
            inline ast_expr_literal() { type = EXPR_LITERAL; }

            ast_literal_type literal_type;
            std::string str;
            union {
                int32_t intv;
                double floatv;
            };

            static inline ast_expr_literal make_int(pos_info pos, int32_t v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_INTEGER;
                ret.intv = v;
                return ret;
            }

            static inline ast_expr_literal make_float(pos_info pos, double v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_FLOAT;
                ret.floatv = v;
                return ret;
            }

            static inline ast_expr_literal make_string(pos_info pos,
                                                       const std::string &v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_STRING;
                ret.str = v;
                return ret;
            }

            static inline ast_expr_literal make_symbol(pos_info pos,
                                                       const std::string &v) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_SYMBOL;
                ret.str = v;
                return ret;
            }

            static inline ast_expr_literal make_void(pos_info pos) {
                ast_expr_literal ret;
                ret.pos = pos;
                ret.literal_type = EXPR_LITERAL_VOID;
                return ret;
            }
        };

        struct ast_expr_identifier : public ast_expr {
            inline ast_expr_identifier() { type = EXPR_IDENTIFIER; }

            std::string identifier;
            ast_scope scope;
        };

        struct ast_expr_list : public ast_expr {
            inline ast_expr_list() { type = EXPR_LIST; }

            std::vector<std::unique_ptr<ast_expr>> items;
        };

        struct ast_expr_prop_list : public ast_expr {
            inline ast_expr_prop_list() { type = EXPR_PROP_LIST; }

            std::vector<std::pair<std::unique_ptr<ast_expr>,
                                  std::unique_ptr<ast_expr>>> pairs;
        };

        struct ast_expr_dot : public ast_expr {
            inline ast_expr_dot() { type = EXPR_DOT; }

            std::unique_ptr<ast_expr> expr;
            std::string index;
        };

        struct ast_expr_index : public ast_expr {
            inline ast_expr_index() { type = EXPR_INDEX; }

            std::unique_ptr<ast_expr> expr;
            std::unique_ptr<ast_expr> index_from;
            std::unique_ptr<ast_expr> index_to; // nullptr if not a range
        };

        struct ast_expr_call : public ast_expr {
            inline ast_expr_call() { type = EXPR_CALL; }

            std::unique_ptr<ast_expr> method;
            std::vector<std::unique_ptr<ast_expr>> arguments;
        };

        // AST statements
        enum ast_statement_type : uint8_t {
            // handler statements
            STATEMENT_RETURN,
            STATEMENT_ASSIGN,
            STATEMENT_EXPR,
            STATEMENT_IF,
            STATEMENT_REPEAT_WHILE,
            STATEMENT_REPEAT_TO,
            STATEMENT_REPEAT_IN,
            STATEMENT_EXIT_REPEAT, // aka break
            STATEMENT_NEXT_REPEAT, // aka continue
            STATEMENT_PUT,
            STATEMENT_PUT_ON,
            STATEMENT_CASE
        };

        struct ast_statement {
            ast_statement_type type;
            pos_info pos;
        };

        struct ast_statement_return : public ast_statement {
            inline ast_statement_return() { type = STATEMENT_RETURN; }

            std::unique_ptr<ast_expr> expr;
        };

        struct ast_statement_assign : public ast_statement {
            inline ast_statement_assign() { type = STATEMENT_ASSIGN; }

            std::unique_ptr<ast_expr> lvalue;
            std::unique_ptr<ast_expr> rvalue;
        };

        struct ast_statement_expr : public ast_statement {
            inline ast_statement_expr() { type = STATEMENT_EXPR; }

            std::unique_ptr<ast_expr> expr;
        };

        struct ast_if_branch {
            std::unique_ptr<ast_expr> condition;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_if : public ast_statement {
            inline ast_statement_if() { type = STATEMENT_IF; }

            // the first branch is the if branch, then the rest are the
            // else-if branches
            std::vector<std::unique_ptr<ast_if_branch>> branches;
            bool has_else = false;
            std::vector<std::unique_ptr<ast_statement>> else_branch;
        };

        struct ast_statement_repeat_while : public ast_statement {
            inline ast_statement_repeat_while()
                { type = STATEMENT_REPEAT_WHILE; }

            std::unique_ptr<ast_expr> condition;
            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_repeat_to : public ast_statement {
            inline ast_statement_repeat_to() { type = STATEMENT_REPEAT_TO; }

            std::unique_ptr<ast_expr> iterator;
            std::unique_ptr<ast_expr> init;
            std::unique_ptr<ast_expr> to;
            bool down;

            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_repeat_in : public ast_statement {
            inline ast_statement_repeat_in() { type = STATEMENT_REPEAT_IN; }

            std::unique_ptr<ast_expr> iterator;
            std::unique_ptr<ast_expr> iterable;

            std::vector<std::unique_ptr<ast_statement>> body;
        };

        struct ast_statement_exit_repeat : public ast_statement {
            inline ast_statement_exit_repeat() { type = STATEMENT_EXIT_REPEAT; }
        };

        struct ast_statement_next_repeat : public ast_statement {
            inline ast_statement_next_repeat() { type = STATEMENT_NEXT_REPEAT; }
        };

        struct ast_statement_put : public ast_statement {
            inline ast_statement_put() { type = STATEMENT_PUT; }

            std::unique_ptr<ast_expr> expr;
        };

        struct ast_statement_put_on : public ast_statement {
            inline ast_statement_put_on() { type = STATEMENT_PUT_ON; }

            std::unique_ptr<ast_expr> expr;
            std::unique_ptr<ast_expr> target; // after/before X syntax
            bool before; // true if before, false if after
        };

        struct ast_case_clause {
            std::vector<std::unique_ptr<ast_expr>> literal;
            std::vector<std::unique_ptr<ast_statement>> branch;
        };

        struct ast_statement_case : public ast_statement {
            inline ast_statement_case() { type = STATEMENT_CASE; }

            std::unique_ptr<ast_expr> expr;
            std::vector<std::unique_ptr<ast_case_clause>> clauses;

            bool has_otherwise;
            std::vector<std::unique_ptr<ast_statement>> otherwise_clause;
        };

        // AST root
        struct ast_handler_decl {
            pos_info pos;

            std::string name;
            std::vector<std::string> params;
            std::vector<std::unique_ptr<ast_statement>> body;
            std::vector<std::string> locals;
        };

        struct ast_root {
            std::vector<std::string> properties;
            std::vector<std::unique_ptr<ast_handler_decl>> handlers;
        };

        bool parse_ast(const std::vector<token> &tokens, ast_root &root,
                       parse_error *error);
    } // namespace ast

    namespace bc {
        enum opcode : uint8_t {
            OP_RET,     // .          Return from the function. Value will be
                        //            popped from the stack to serve as the
                        //            return value.
            OP_POP,     // .          Pop the value on the top of the stack.
            OP_DUP,     // .          Duplicate the value at the top of the
                        //            stack.
            OP_LOADVOID,// .          Push a void value onto the stack.
            OP_LOADI0,  // .          Load integer 0 (FALSE) onto the stack.
            OP_LOADI1,  // .          Load integer 1 (TRUE) onto the stack.
            OP_LOADC,   // [u16]      Push a literal from the constant list onto
                        //            the stack.
            OP_LOADL,   // [u16]      Push the value of a local onto the stack.
            OP_LOADL0,  // .          Push the value of local #0 (me) onto the
                        //            stack.
            OP_LOADG,   // [u16]      Push the value of the global name onto the
                        //            stack.
            OP_STOREL,  // [u16]      Store the value on the top of the stack
                        //            into the given local variable index.
            OP_STOREG,  // [u16]      Store the value on the top of the stack
                        //            into a global of the given name.
            OP_UNM,     // [u8]       Unary negation.
            OP_ADD,     // .          Pop two values, and push their sum.
            OP_SUB,     // .          Pop two values, and push their difference.
            OP_MUL,     // .          Pop two values, and push their product.
            OP_DIV,     // .          Pop two values, and push their quotient.
                        //            Will perform integer division if either A
                        //            or B is an integer.
            OP_MOD,     // .          Pop two values, push result of A mod B.
            OP_EQ,      // .          Pop 2, push 1 if A == B, 0 if not.
            OP_LT,      // .          Pop 2, push 1 if A < B, 0 if not.
            OP_GT,      // .          Pop 2, push 1 if A > B, 0 if not.
            OP_LTE,     // .          Pop 2, push 1 if A <= B, 0 if not.
            OP_GTE,     // .          Pop 2, push 1 if A >= B, 0 if not.
            OP_AND,     // .          Pop 2, compute the logical AND of A and B.
            OP_OR,      // .          Pop 2, compute the logical OR of A and B.
            OP_NOT,     // .          Pop 1, compute the logical NOT Of A
            OP_CONCAT,  // .          Pop 2, push string concatenation of the
                        //            two values.
            OP_CONCATSP,// .          Pop 2, push string concatenation of the
                        //            two values, separated by a space.
            OP_JMP,     // [i16]      Relative unconditional jump.
            OP_BT,      // [i16]      Jump to given relative instruction index
                        //            if popped value equals 1.
            OP_CALL,    // [u16] [u8] Call global message handler by the given
                        //            string literal (#1), with n (#2)
                        //            arguments. That number of arguments will
                        //            be popped to the stack, bottom-to-top.
                        //            Return value will be pushed to the stack.
            OP_OCALL,   // [u16] [u8] Invoke message, of name #1 (string
                        //            literal), with (#2) integer literal
                        //            argument count, on an object. The value
                        //            first pushed to the stack is the pertinent
                        //            object. The rest of the stack values will
                        //            be arguments sent to the function call.
                        //            Return value will be pushed to the stack.
            OP_OIDXG,   // .          pop: index then object. push result of o[i].
            OP_OIDXS,   // .          pop: index, object, then value. perform
                        //            o[i] = v.
            OP_OIDXK,   // .          pop: index, key (string), then object.
                        //            push result of o.k[i].
            OP_OIDXKR,  // .          pop: index B (integer), index A (integer),
                        //            key (string), object. push result of
                        //            o.k[a..b].
            OP_THE,     // [u8]       Push the "the" value.
            OP_NEWLLIST,// [u16]      Push a newly constructed empty linear
                        //            list with a given number of pre-allocated.
                        //            elements.
            OP_NEWPLIST,//            Push a newly constructed empty property
                        //            list.
            OP_CASE,    // [u16]      Pop value from stack. Use that as the
                        //            test expression. Parameter #1 is the jump
                        //            table identifier.
        }; // enum opcode

        // extra notes on object indices:
        // - O.k will be emitted as
        //      LOADL local O
        //      LOADC #k
        //      OIDXG
        // - O[k] will be emitted as
        //      LOADL local O
        //      LOADL local k
        //      OIDXG
        // - O.foo.bar[3] will be emitted as
        //      LOADL local O
        //      LOADC #foo
        //      OIDXG
        //      PUSHC #bar
        //      PUSHC 3
        //      OIDXK

        typedef uint32_t instr;

        enum vtype : uint8_t {
            TYPE_VOID,
            TYPE_INT, // int32_t
            TYPE_FLOAT, // double
            TYPE_STRING, // ref
            TYPE_SYMBOL, // ref
            TYPE_LLIST, // linear list, ref
            TYPE_PLIST, // property list, ref
            TYPE_POINT, // ref
            TYPE_QUAD, // ref
        }; // enum type

        // this is a header struct - subsequent characters directly follow
        // afterwards in memory. will be nul-terminated, meaning that there will
        // always be at least one character of data. size field does not account
        // for this nul byte.
        struct chunk_const_str {
            size_t size;
            char first;

            inline bool equal(const chunk_const_str *other) const {
                if (size != other->size) return false;
                return !memcmp(&first, &other->first, size + 1);
            }

            inline bool equal(const char *str, size_t len) const {
                if (size != len) return false;
                return !memcmp(&first, str, len + 1);
            }

            inline bool equal(const char *str) const {
                return equal(str, strlen(str));
            }
        };

        struct chunk_const {
        private:
            template <typename T>
            static constexpr vtype vtype_of() {
                if constexpr (std::is_same<const T&, const int32_t&>())
                    return TYPE_INT;
                else if constexpr (std::is_same<const T&, const double&>())
                    return TYPE_FLOAT;
                else if constexpr (std::is_same<const T&, const std::string&>())
                    return TYPE_STRING;
                else
                    static_assert(false, "unimplemented/invalid type_enum_of");
            }

        public:
            vtype type;
            union {
                int32_t i32;
                double f64;
                chunk_const_str *str; // shared by variant
            };

            chunk_const() : type(TYPE_VOID), i32(0) { }
            chunk_const(int32_t v) : type(TYPE_INT), i32(v) { }
            chunk_const(double v) : type(TYPE_FLOAT), f64(v) { }
            chunk_const(chunk_const_str *str) : type(TYPE_STRING), str(str) { }
        }; // struct variant;

        struct jtable_bucket {
            uint16_t item_count;
            uint16_t *items;
            int16_t jump_offset;

            inline ~jtable_bucket() {
                delete[] items;
            }
        }; // struct jtable_entry

        struct jtable {
            uint16_t count;
            jtable_bucket *buckets;

            inline ~jtable() {
                delete[] buckets;
            }
        }; // struct jtable

        struct chunk_line_info {
            uint32_t line;
            uint32_t instr_index;
        };
        
        /*
        ? - means variable-width
            strings are nul-terminated

        struct chunk {
            chunk_header header;
            instr instrs[?];
            byte str_pool[?][?];
            chunk_const consts[?]; (references str_pool)
            char file_name[?];
            const char *arg_names[?]; (references str_pool)
            chunk_line_info line_info[?] (references str_pool)
            char name[?];
        };
        */

        struct chunk_header {
            uint8_t nargs; // can be zero. me will be automatically inserted
                           // if so.
            uint16_t nlocals;
            uint16_t nconsts;
            uint32_t ninstr;
            uint32_t line_info_count;

            // these are offsets from the start of the chunk header
            const bc::instr *instrs;
            const char *name;
            const chunk_const *consts;
            const chunk_const_str *string_pool;
            const char *file_name;
            const char *arg_names;
            const chunk_line_info *line_info;
            
            // variant *consts;
            // std::string *strings;

            // std::string *dbg_arg_names;
            // std::string dbg_file_name;
            // unsigned int dbg_line_count;
            // dbg_line_info *dbg_lines;
        };

        template <typename Ta, typename Tb>
        constexpr Tb* base_offset(const Ta *base, Tb *offset) {
            uintptr_t ptr = ((uintptr_t)base + (uintptr_t)offset);
            assert(ptr % alignof(Tb) == 0);
            return (Tb *)ptr;
        }

        bool generate_bytecode(const ast::ast_root &root,
                               std::vector<std::vector<uint8_t>> &chunk_list,
                               parse_error *error);
    } // namespace bc

    bool compile_bytecode(std::istream &istream,
                          std::vector<std::vector<uint8_t>> &chunk_list,
                          parse_error *error);
    // bool compile_luajit_text(std::istream &istream, std::ostream &ostream,
    //                          parse_error *error,
    //                          extra_gen_params *params = nullptr);
} // namespace lingo
