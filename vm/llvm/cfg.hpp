#include <list>
#include "vmmethod.hpp"

namespace rubinius {
namespace jit {
  class CFGBlock {
  public:
    typedef std::list<CFGBlock*> List;

  private:
    int start_ip_;
    int end_ip_;
    List children_;
    bool loop_;
    bool detached_;
    CFGBlock* exception_handler_;

  public:
    CFGBlock(int start, bool loop=false)
      : start_ip_(start)
      , end_ip_(0)
      , loop_(loop)
      , detached_(true)
      , exception_handler_(0)
    {}

    int start_ip() {
      return start_ip_;
    }

    void set_end_ip(int ip) {
      end_ip_ = ip;
    }

    void add_child(CFGBlock* block) {
      if(block->detached_p()) {
        block->attached();
        children_.push_back(block);
      }
    }

    void attached() {
      detached_ = false;
    }

    bool detached_p() {
      return detached_;
    }

    bool loop_p() {
      return loop_;
    }

    CFGBlock* exception_handler() {
      return exception_handler_;
    }

    void set_exception_handler(CFGBlock* blk) {
      exception_handler_ = blk;
    }
  };

  class CFGCalculator {
  public:
    typedef std::map<opcode, CFGBlock*> Blocks;

  private:
    Blocks blocks_;
    CFGBlock* root_;
    CFGBlock* current_;
    opcode* stream_;
    size_t stream_size_;

  public:
    CFGCalculator(opcode* stream, size_t size)
      : root_(0)
      , current_(0)
      , stream_(stream)
      , stream_size_(size)
    {}

    CFGCalculator(VMMethod* vmm)
      : root_(0)
      , current_(0)
      , stream_(vmm->opcodes)
      , stream_size_(vmm->total)
    {}

    ~CFGCalculator() {
      for(Blocks::iterator i = blocks_.begin();
          i != blocks_.end();
          i++) {
        delete i->second;
      }
    }

    CFGBlock* root() {
      return root_;
    }

    CFGBlock* find_block(int ip) {
      Blocks::iterator i = blocks_.find(ip);
      if(i == blocks_.end()) return 0;
      return i->second;
    }

    CFGBlock* add_block(int ip, bool loop=false) {
      CFGBlock* blk = find_block(ip);
      if(blk) return blk;

      blk = new CFGBlock(ip, loop);

      // Inherit the current exception handler
      blk->set_exception_handler(current_->exception_handler());

      blocks_[ip] = blk;
      return blk;
    }

    void find_backward_gotos() {
      VMMethod::Iterator iter(stream_, stream_size_);

      while(!iter.end()) {
        switch(iter.op()) {
        case InstructionSequence::insn_goto:
        case InstructionSequence::insn_goto_if_true:
        case InstructionSequence::insn_goto_if_false:
          if(iter.operand1() < iter.position()) {
            if(!find_block(iter.operand1())) {
              CFGBlock* blk = new CFGBlock(iter.operand1(), true);
              blocks_[iter.operand1()] = blk;
            }
          }
          break;
        }

        iter.inc();
      }
    }

    void close_current(VMMethod::Iterator& iter, CFGBlock* next) {
      current_->set_end_ip(iter.position());
      current_->add_child(next);
      current_ = next;
    }

    CFGBlock* start_new_block(VMMethod::Iterator& iter) {
      if(!iter.last_instruction()) {
        CFGBlock* blk = add_block(iter.next_position());
        close_current(iter, blk);
        return blk;
      }

      return 0;
    }

    void build() {
      find_backward_gotos();

      // Construct the root block specially.
      root_ = new CFGBlock(0);
      blocks_[0] = root_;

      current_ = root_;

      VMMethod::Iterator iter(stream_, stream_size_);
      for(;;) {
        if(CFGBlock* next_block = find_block(iter.position())) {
          if(next_block->loop_p()) {
            // The handler wasn't setup originally, so we have to set it now.
            next_block->set_exception_handler(current_->exception_handler());

            close_current(iter, next_block);
          } else {
            current_ = next_block;
          }
        }

        switch(iter.op()) {
        case InstructionSequence::insn_goto:
        case InstructionSequence::insn_goto_if_true:
        case InstructionSequence::insn_goto_if_false:
          if(iter.operand1() > iter.position()) {
            current_->add_child(add_block(iter.operand1()));
            start_new_block(iter);
          } else {
#ifndef NDEBUG
            CFGBlock* loop_header = find_block(iter.operand1());
            assert(loop_header);
            assert(loop_header->exception_handler() == current_->exception_handler());
#endif
          }
          break;

        case InstructionSequence::insn_setup_unwind: {
          assert(iter.operand1() > iter.position());
          CFGBlock* handler = add_block(iter.operand1());
          current_->add_child(handler);

          CFGBlock* body = start_new_block(iter);
          assert(body); // make sure it's not at the end.

          body->set_exception_handler(handler);
          break;
        }
        case InstructionSequence::insn_pop_unwind: {
          assert(current_->exception_handler());
          CFGBlock* cont = start_new_block(iter);
          CFGBlock* current_handler = cont->exception_handler();
          assert(current_handler);

          // Effectively pop the current handler by setting the
          // blocks handler (and thus all blocks after it) to the current
          // handlers handler.
          cont->set_exception_handler(current_handler->exception_handler());
          break;
        }

        case InstructionSequence::insn_ensure_return:
        case InstructionSequence::insn_raise_exc:
        case InstructionSequence::insn_raise_return:
        case InstructionSequence::insn_raise_break:
        case InstructionSequence::insn_reraise:
        case InstructionSequence::insn_ret:
          start_new_block(iter);
          break;
        }

        if(!iter.advance()) break;
      }

      current_->set_end_ip(iter.position());
    }
  };
}}