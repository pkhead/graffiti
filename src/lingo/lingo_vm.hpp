#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace lingo {
    namespace vm {
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