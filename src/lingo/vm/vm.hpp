#pragma once
#include "../lang/lingo.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// data structures
namespace lingo::vm {
    class gc_object {
    public:
        enum otype : uint8_t {
            OTYPE_STRING
        };

    protected:
        otype obj_type;
        gc_object(otype obj_type) : obj_type(obj_type) { }
    };

    class string : public gc_object {
    protected:
        size_t _length;
        char *_chars; // always null-terminated
    
    public:
        inline string(const char *str, size_t len)
        : gc_object(OTYPE_STRING), _length(len) {
            _chars = new char[len + 1];
            memcpy(_chars, str, len);
            _chars[len] = '\0';
        }

        inline string(size_t len) : gc_object(OTYPE_STRING) {
            _length = len;
            _chars = new char[len + 1];
            memset(_chars, 0, (len + 1) * sizeof(char));
        }

        inline string(const char *str) : string(str, strlen(str)) { }

        inline string(const std::string &str)
            : string(str.c_str(), str.length()) { };

        inline string(const string &str)
            : string(str._chars, str._length) { };
        
        inline string(string &&src) : gc_object(OTYPE_STRING) {
            _chars = src._chars;
            _length = src._length;
            src._chars = nullptr;
            src._length = 0;
        }

        inline ~string() {
            delete[] _chars;
        }

        inline char* data() const { return _chars; }
        inline size_t length() const { return _length; }

        inline bool operator==(const string &other) const {
            return _length == other._length &&
                    !memcmp(_chars, other._chars, _length);
        }

        inline std::string to_cpp_string() const {
            return std::string(_chars, _length);
        }
    };

    struct variant {
        bc::vtype type;
        union {
            int32_t i32;
            double f64;
            gc_object *ref;
        };

        variant() : type(bc::TYPE_VOID), i32(0) { }
    }; // struct variant;
} // namespace lingo::vm

template<>
struct std::hash<lingo::vm::string> {
    std::size_t operator()(const lingo::vm::string &k) const {
        const char *dat = k.data();
        size_t count = k.length();
        if (count > 32) count = 32;

        size_t res = *(dat++);
        size_t fac = 53;
        for (size_t i = 1; i < count; ++i) {
            res += ((size_t) *(dat++)) * fac;
        }

        return res;
    }
};

// runner class
namespace lingo::vm {
    template <typename T>
    constexpr bc::vtype vtype_of() {
        if constexpr (std::is_same<const T&, const int32_t&>())
            return bc::TYPE_INT;
        else if constexpr (std::is_same<const T&, const double&>())
            return bc::TYPE_FLOAT;
        else if constexpr (std::is_same<const T&, const string*&>())
            return bc::TYPE_STRING;
        else
            static_assert(false, "unimplemented/invalid type_enum_of");
    }

    class runner {
    public:
        struct call_info {
            const bc::chunk_header *chunk;
            const bc::instr *ip;
            variant *stack_base;
        };
    private:
        variant _stack[256];
        variant *_stack_top;

        call_info _cstack[256];
        call_info *_cstack_top;

        std::unordered_map<string, string*> _symbol_intern;

        string* stringify(const variant *variant);
    public:
        runner();
        runner(const runner&) = delete;
        runner(runner&&) = delete;
        ~runner();

        bool run(const bc::chunk_header *chunk);
    };
} // namespace lingo::vm