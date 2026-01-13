#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace lingo {
    namespace vm {
        enum opcode : uint8_t {
            OP_RET,     // .          Return from the function. Value will be
                        //            popped from the stack to serve as the
                        //            return value.
            OP_POP,     // .          Pop the value on the top of the stack.
            OP_DUP,     // [u16]      Copy the value at stack index #(#1) to the
                        //            top of the stack.
            OP_LOADV,   // .          Push a void value onto the stack.
            OP_LOADC,   // [u16]      Push a literal from the constant list onto
                        //            the stack.
            OP_LOADL,   // [u16]      Push the value of a local onto the stack.
            OP_LOADG,   // [u16]      Push the value of the global name onto the
                        //            stack.
            OP_STOREL,  // [u16]      Store the value on the top of the stack
                        //            into the given local variable index.
            OP_STOREG,  // [u16]      Store the value on the top of the stack
                        //            into a global of the given name.
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
            OP_OIDXS,   // .          pop: value, index, then object. perform
                        //            o[i] = v.
            OP_OIDXK,   // .          pop: index, key (string), then object.
                        //            push result of o.k[i].
            OP_OIDXKR,  // .          pop: index B (integer), index A (integer),
                        //            key (string), object. push result of
                        //            o.k[a..b].
            OP_THE,     // [u8]       Push the "the" value.
            OP_NEWLLIST,// [u8]       Push a newly constructed empty linear
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

        class string {
        protected:
            size_t _length;
            char *_chars; // always null-terminated
        
        public:
            string(const char *str);
            string(const std::string &str);
            string(const string &str);
            string(string &&str);
            ~string();

            inline size_t length() const { return _length; }
        };

        template <typename T>
        using ref = std::shared_ptr<T>;

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

        template <typename T>
        constexpr vtype vtype_of() {
            if constexpr (std::is_same<T, int32_t>())
                return TYPE_INT;
            else if constexpr (std::is_same<T, double>())
                return TYPE_FLOAT;
            else if constexpr (std::is_same<T, ref<string>>())
                return TYPE_STRING;
            else
                static_assert(false, "unimplemented/invalid type_enum_of");
        }

        struct variant {
            vtype type;
            union {
                int32_t i32;
                double f64;
                ref<string> str; // shared by variant
            };

            variant() : type(TYPE_VOID), i32(0) { }

            ~variant() {
                if (type == TYPE_STRING || type == TYPE_SYMBOL) {
                    str.~ref<string>();
                }
            }

            template <typename T>
            void set(T v) {
                constexpr vtype new_type = vtype_of<T>();
                bool old_is_str = type == TYPE_STRING || type == TYPE_SYMBOL;
                bool new_is_str = new_type == TYPE_STRING || new_type == TYPE_SYMBOL;

                if (type != new_type) {
                    type = new_type;

                    if (old_is_str)
                        str.~ref<string>();

                    if (new_is_str) {
                        new (&str) ref<string>(std::move(v));
                        return;
                    }
                } else {
                    if (new_is_str) {
                        str = std::move(v);
                        return;
                    }
                }

                switch (new_type) {
                    case TYPE_INT:
                        i32 = v;
                        break;

                    case TYPE_FLOAT:
                        f64 = v;
                        break;

                    default: break;
                }
            }
        }; // struct variant;

        struct jtable_bucket {
            uint16_t item_count;
            variant *items;
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

        struct dbg_line_info {
            unsigned int line;
            unsigned int instr_index;
        };

        struct function {
            std::string name;
            uint8_t nargs; // can be zero. me will be automatically inserted
                           // if so.
            uint16_t nlocals;

            uint32_t ninstr;
            instr *instrs;
            
            variant *consts;
            std::string *strings;

            std::string *dbg_arg_names;
            std::string dbg_file_name;
            unsigned int dbg_line_count;
            dbg_line_info *dbg_lines;

            inline ~function() {
                delete[] instrs;
                delete[] consts;
                delete[] strings;
                delete[] dbg_arg_names;
                delete[] dbg_lines;
            }
        };

        struct script {
            std::vector<function> funcs;
        };

        // value/local stack will be kept globally and shared across
        // stack frames. i guess.
        struct stack_frame {
            const function *func;
            uint32_t pc;
            size_t stack_base;
            size_t local_base;
        };
    } // namespace vm
}