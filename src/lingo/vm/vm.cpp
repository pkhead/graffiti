#include "vm.hpp"
#include <iostream>
using namespace lingo;

vm::runner::runner() {
    _stack_top = _stack;
    _cstack_top = nullptr;
}

vm::runner::~runner() {}

vm::string* vm::runner::stringify(const variant *variant) {
    switch (variant->type) {
        case bc::TYPE_VOID:
            return new vm::string("<Void>");
        
        case bc::TYPE_INT:
            return new vm::string(std::to_string(variant->i32));

        case bc::TYPE_FLOAT:
            return new vm::string(std::to_string(variant->f64));

        case bc::TYPE_STRING:
            return static_cast<vm::string*>(variant->ref);

        case bc::TYPE_SYMBOL: {
            vm::string *src = static_cast<vm::string*>(variant->ref);
            vm::string *out = new vm::string(src->length() + 1);
            out->data()[0] = '#';
            memcpy(out->data() + 1, src->data(), src->length());
            return out;
        }

        case bc::TYPE_LLIST:
        case bc::TYPE_PLIST:
        case bc::TYPE_POINT:
        case bc::TYPE_QUAD: {
            char buf[64];
            snprintf(buf, 64, "<%p>", (void*)variant->ref);
            return new vm::string(buf);
        }

        default:
            assert(false);
            return nullptr;
    }
}

// TODO: memory allocation creates memory leak, since I have not yet implemented
// a garbage collector.
bool vm::runner::run(const bc::chunk_header *start_chunk) {
    _cstack_top = _cstack;
    _cstack_top->chunk = start_chunk;
    _cstack_top->ip = bc::base_offset(start_chunk, start_chunk->instrs);

    uint16_t u16_a, u16_b;
    int16_t i16_a, i16_b;
    uint8_t u8_a, u8_b;

    (void)u16_a, (void)u16_b;
    (void)i16_a, (void)i16_b;
    (void)u8_a,  (void)u8_b;

    const bc::chunk_header *chunk = start_chunk;
    const bc::chunk_const *const_pool = bc::base_offset(chunk, chunk->consts);
    const bc::chunk_const_str *string_pool = bc::base_offset(chunk, chunk->string_pool);
    const bc::instr *ip = _cstack_top->ip;

    while (_cstack_top) {
        bc::instr istr = *(ip++);
        switch (istr & 0xFF) {
            case bc::OP_RET:
                if (_cstack == _cstack_top) {
                    _cstack_top = nullptr;
                    break;
                }

                --_cstack_top;
                chunk = start_chunk;
                const_pool = bc::base_offset(chunk, chunk->consts);
                string_pool = bc::base_offset(chunk, chunk->string_pool);
                ip = _cstack_top->ip;
            
            case bc::OP_POP:
                --_stack_top;
                break;
            
            case bc::OP_DUP:
                *(_stack_top++) = *(_stack_top - 1);
                break;

            case bc::OP_LOADVOID:
                _stack_top->type = bc::TYPE_VOID;
                ++_stack_top;
                break;

            case bc::OP_LOADI0:
                _stack_top->type = bc::TYPE_INT;
                _stack_top->i32 = 0;
                ++_stack_top;
                break;

            case bc::OP_LOADI1:
                _stack_top->type = bc::TYPE_INT;
                _stack_top->i32 = 1;
                ++_stack_top;
                break;

            case bc::OP_LOADC:
                bc::instr_decode(istr, &u16_a);
                switch (const_pool[u16_a].type) {
                    case bc::TYPE_VOID:
                        _stack_top->type = bc::TYPE_VOID;
                        ++_stack_top;
                        break;

                    case bc::TYPE_INT:
                        _stack_top->type = bc::TYPE_INT;
                        _stack_top->i32 = const_pool[u16_a].i32;
                        ++_stack_top;
                        break;

                    case bc::TYPE_FLOAT:
                        _stack_top->type = bc::TYPE_FLOAT;
                        _stack_top->f64 = const_pool[u16_a].f64;
                        ++_stack_top;
                        break;

                    case bc::TYPE_STRING: {
                        const bc::chunk_const_str *str =
                            bc::base_offset(string_pool, const_pool[u16_a].str);
                        _stack_top->type = bc::TYPE_STRING;
                        _stack_top->ref = new string(&str->first, str->size);
                        ++_stack_top;
                        break;
                    }

                    case bc::TYPE_SYMBOL: {
                        const bc::chunk_const_str *cstr =
                            bc::base_offset(string_pool, const_pool[u16_a].str);

                        string temp_str(&cstr->first, cstr->size);
                        string *gc_str;

                        auto it = _symbol_intern.find(temp_str);
                        if (it == _symbol_intern.end()) {
                            gc_str = new string(temp_str);
                            _symbol_intern.emplace(std::move(temp_str), gc_str);
                        } else {
                            gc_str = it->second;
                        }

                        _stack_top->type = bc::TYPE_SYMBOL;
                        _stack_top->ref = gc_str;
                        ++_stack_top;
                        break;
                    }

                    default:
                        assert(false);
                        break;
                }                
                break;
            
            case bc::OP_LOADL:
                bc::instr_decode(istr, &u16_a);
                *(_stack_top++) = _cstack_top->stack_base[u16_a];
                break;

            case bc::OP_LOADL0:
                *(_stack_top++) = *_cstack_top->stack_base;
                break;

            // case bc::OP_LOADG:
            //     std::cerr << "OP_LOADG unimplemented\n";
            //     return 1;
            //     break;

            case bc::OP_STOREL:
                bc::instr_decode(istr, &u16_a);
                _cstack_top->stack_base[u16_a] = *(--_stack_top);
                break;

            // case bc::OP_STOREG:
            //     std::cerr << "OP_STOREG unimplemented\n";
            //     return 1;
            //     break;

            case bc::OP_UNM: {
                variant *const v = _stack_top - 1;
                switch (v->type) {
                    case bc::TYPE_INT:
                        v->i32 = -v->i32;
                        break;

                    case bc::TYPE_FLOAT:
                        v->f64 = -v->f64;
                        break;

                    default:
                        std::cerr << "unm invalid operand";
                        return 1;
                }
                break;
            }

            case bc::OP_ADD: {
                variant *const a = _stack_top - 2;
                variant *const b = _stack_top - 1;
                variant result;

                if (a->type == bc::TYPE_INT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = (double)a->i32 + b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_INT;
                        result.i32 = a->i32 + b->i32;
                    } else {
                        std::cerr << "add invalid operand types";
                        return 1;
                    }
                } else if (a->type == bc::TYPE_FLOAT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 + b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 + (double)b->i32;
                    } else {
                        std::cerr << "add invalid operand types";
                        return 1;
                    }
                }

                _stack_top -= 1;
                *(_stack_top - 1) = result;
                break;
            }

            case bc::OP_SUB: {
                variant *const a = _stack_top - 2;
                variant *const b = _stack_top - 1;
                variant result;

                if (a->type == bc::TYPE_INT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = (double)a->i32 - b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_INT;
                        result.i32 = a->i32 - b->i32;
                    } else {
                        std::cerr << "add invalid operand types";
                        return 1;
                    }
                } else if (a->type == bc::TYPE_FLOAT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 - b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 - (double)b->i32;
                    } else {
                        std::cerr << "sub invalid operand types";
                        return 1;
                    }
                }

                _stack_top -= 1;
                *(_stack_top - 1) = result;
                break;
            }

            case bc::OP_MUL: {
                variant *const a = _stack_top - 2;
                variant *const b = _stack_top - 1;
                variant result;

                if (a->type == bc::TYPE_INT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = (double)a->i32 * b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_INT;
                        result.i32 = a->i32 * b->i32;
                    } else {
                        std::cerr << "mul invalid operand types";
                        return 1;
                    }
                } else if (a->type == bc::TYPE_FLOAT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 * b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 * (double)b->i32;
                    } else {
                        std::cerr << "mul invalid operand types";
                        return 1;
                    }
                }

                _stack_top -= 1;
                *(_stack_top - 1) = result;
                break;
            }

            case bc::OP_DIV: {
                variant *const a = _stack_top - 2;
                variant *const b = _stack_top - 1;
                variant result;

                if (a->type == bc::TYPE_INT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = (double)a->i32 / b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_INT;
                        result.i32 = a->i32 / b->i32;
                    } else {
                        std::cerr << "div invalid operand types";
                        return 1;
                    }
                } else if (a->type == bc::TYPE_FLOAT) {
                    if (b->type == bc::TYPE_FLOAT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 / b->f64;
                    } else if (b->type == bc::TYPE_INT) {
                        result.type = bc::TYPE_FLOAT;
                        result.f64 = a->f64 / (double)b->i32;
                    } else {
                        std::cerr << "div invalid operand types";
                        return 1;
                    }
                }

                _stack_top -= 1;
                *(_stack_top - 1) = result;
                break;
            }

            case bc::OP_EQ: {
                variant *a = _stack_top - 2;
                variant *b = _stack_top - 1;
                bool res = false;

                if (b->type < a->type) {
                    variant *const tmp = a;
                    a = b;
                    b = tmp;
                }

                if (a->type == bc::TYPE_VOID) {
                    res = b->type == bc::TYPE_VOID;
                }
                else if (a->type == bc::TYPE_INT) {
                    if (b->type == bc::TYPE_INT) {
                        res = a->i32 == b->i32;
                    } else if (b->type == bc::TYPE_FLOAT) {
                        res = (double)a->i32 == b->f64;
                    } else if (b->type == bc::TYPE_STRING) {
                        vm::string *str_b = static_cast<vm::string*>(b->ref);

                        // determine if string describes a real or an integer
                        bool is_real = false;
                        for (size_t i = 0; i < str_b->length(); ++i) {
                            if (str_b->data()[i] == '.') {
                                is_real = true;
                                break;
                            }
                        }

                        if (is_real) {
                            res = (double)a->i32 == std::stod(str_b->to_cpp_string());
                        } else {
                            res = a->i32 == std::stoi(str_b->to_cpp_string());
                        }
                    }
                }
                else if (a->type == bc::TYPE_FLOAT) {
                    if (b->type == bc::TYPE_STRING) {
                        vm::string *str_b = static_cast<vm::string*>(b->ref);
                        res = a->f64 == std::stod(str_b->to_cpp_string());
                    }
                }
                else if (a->type == bc::TYPE_STRING) {
                    if (b->type == bc::TYPE_STRING || b->type == bc::TYPE_SYMBOL) {
                        vm::string *str_a = static_cast<vm::string*>(a->ref);
                        vm::string *str_b = static_cast<vm::string*>(b->ref);
                        res = *str_a == *str_b;
                    }
                }
                else if (a->type == bc::TYPE_SYMBOL) {
                    if (b->type == bc::TYPE_SYMBOL) {
                        res = a->ref == b->ref;
                    }
                }
                else {
                    res = false;
                }

                --_stack_top;
                (_stack_top - 1)->type = bc::TYPE_INT;
                (_stack_top - 1)->i32 = res;
                break;
            }

            case bc::OP_NOT: {
                variant *v = _stack_top - 1;

                if (v->type != bc::TYPE_INT) {
                    // instead of throwing an error, it returns FALSE??
                    v->type = bc::TYPE_INT;
                    v->i32 = 0;
                } else {
                    v->i32 = !v->i32;
                }
                
                break;
            }

            case bc::OP_PUT: {
                vm::string *str = stringify(_stack_top - 1);
                --_stack_top;
                std::cout << str->data() << "\n";
                break;
            }

            case bc::OP_JMP:
                bc::instr_decode(istr, &i16_a);
                ip += i16_a - 1;
                break;

            case bc::OP_BRF: {
                bc::instr_decode(istr, &i16_a);

                const variant *v = _stack_top - 1;

                if (v->type != bc::TYPE_INT && v->type != bc::TYPE_VOID) {
                    std::cerr << "error: expected integer";
                    return 1;
                }

                if ((v->type == bc::TYPE_INT && v->i32 == 0) ||
                    v->type == bc::TYPE_VOID
                ) {
                    ip += i16_a - 1;
                }

                break;
            }

            case bc::OP_BRT: {
                bc::instr_decode(istr, &i16_a);

                const variant *v = _stack_top - 1;

                if (v->type != bc::TYPE_INT && v->type != bc::TYPE_VOID) {
                    std::cerr << "error: expected integer";
                    return 1;
                }

                if (v->type == bc::TYPE_INT && v->i32 != 0) {
                    ip += i16_a - 1;
                }
                
                break;
            }

            default:
                std::cerr << "unimplemented opcode " << (istr & 0xFF);
                return 1;
        }
    }

    return 0;
}