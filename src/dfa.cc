#include "dfa.h"

namespace regen {

DFA::DFA(const ExprInfo &expr_info, std::size_t limit):
    expr_info_(expr_info), complete_(false), minimum_(false), olevel_(Regen::Options::O0)
#ifdef REGEN_ENABLE_XBYAK
    , xgen_(NULL)
#endif
{
  complete_ = Construct(limit);
}

DFA::DFA(const NFA &nfa, std::size_t limit):
    complete_(false), minimum_(false), olevel_(Regen::Options::O0)
#ifdef REGEN_ENABLE_XBYAK
    , xgen_(NULL)
#endif
{
  complete_ = Construct(nfa, limit);
}

bool DFA::ContainAcceptState(const Subset &states) const
{
  for (Subset::iterator iter = states.begin(); iter != states.end(); ++iter) {
    if ((*iter)->type() == Expr::kEOP) {
      return true;
    }
  }
  return false;
}

void DFA::ExpandStates(Subset* states, bool begline, bool endline) const
{
  std::set<Operator*> intersections;
  std::set<Operator*> exclusives;
  std::map<std::size_t, Operator*> exclusives_;
entry:
  for (Subset::iterator iter = states->begin(); iter != states->end(); ++iter) {
    switch ((*iter)->type()) {
      case Expr::kOperator: {
        Operator *op = static_cast<Operator*>(*iter);
        switch (op->optype()) {
          case Operator::kIntersection:
            if (intersections.find(op) == intersections.end()) {
              intersections.insert(op);
              if (intersections.find(op->pair()) != intersections.end()) {
                std::size_t presize = states->size();
                states->insert(op->follow().begin(), op->follow().end());
                if (presize < states->size()) goto entry;
              }
            }
            break;
          case Operator::kXOR:
            if (exclusives.find(op) == exclusives.end()) {
              exclusives.insert(op);
              std::map<std::size_t, Operator*>::iterator iter = exclusives_.find(op->id());
              if (iter != exclusives_.end()) {
                exclusives_.erase(iter);
              } else {
                exclusives_[op->id()] = op;
              }
            }
            break;
          default:
            break;
        }
        break;
      }
      case Expr::kAnchor: {
        Anchor * an = static_cast<Anchor*>(*iter);
        switch (an->atype()) {
          case Anchor::kBegLine:
            if (begline) {
              std::size_t presize = states->size();
              states->insert(an->follow().begin(), an->follow().end());
              if (presize < states->size()) goto entry;
            }
            break;
          case Anchor::kEndLine:
            if (endline) {
              std::size_t presize = states->size();
              states->insert(an->follow().begin(), an->follow().end());
              if (presize < states->size()) goto entry;
            }
          default:
            break;
        }
        break;
      }
      default:
        break;
    }
  }

  std::size_t presize = states->size();
  for (std::map<std::size_t, Operator*>::iterator iter = exclusives_.begin();
       iter != exclusives_.end(); ++iter) {
    Operator *op = static_cast<Operator*>(iter->second);
    if (exclusives.find(op->pair()) == exclusives.end()) {
      states->insert(op->follow().begin(), op->follow().end());
    }
    if (presize < states->size()) goto entry;
  }
}

void DFA::FillTransition(StateExpr* state, std::vector<Subset>* transition) const
{
  if (state->non_greedy()) MakeNonGreedy(state);
  switch (state->type()) {
    case Expr::kLiteral: {
      Literal *lit = static_cast<Literal*>(state);
      unsigned char index = lit->literal();
      if (index == flag_.delimiter() && !flag_.one_line()) break;
      (*transition)[index].insert(lit->follow().begin(), lit->follow().end());
      break;
    }
    case Expr::kCharClass: {
      CharClass *cc = static_cast<CharClass*>(state);
      for (std::size_t c = 0; c < 256; c++) {
        if (c == flag_.delimiter() && !flag_.one_line()) continue;
        if (cc->Match(c)) {
          (*transition)[c].insert(cc->follow().begin(), cc->follow().end());
        }
      }
      break;
    }
    case Expr::kDot: {
      Dot *dot = static_cast<Dot*>(state);
      for (std::size_t c = 0; c < 256; c++) {
        if (c == flag_.delimiter() && !flag_.one_line()
            && !dot->match_delimiter()) continue;
        (*transition)[c].insert(dot->follow().begin(), dot->follow().end());
      }
      break;
    }
    case Expr::kAnchor:
      if (!flag_.one_line()) {
      Anchor* an = static_cast<Anchor*>(state);
      (*transition)[flag_.delimiter()].insert(an->follow().begin(), an->follow().end());
      }
      break;
    default: break;
  }
}

void DFA::MakeNonGreedy(StateExpr* state) const {
  if (state->complete_non_greedy()) return;
  Subset follow_;

  for (Subset::iterator iter = state->follow().begin(); iter != state->follow().end(); ++iter) {
    StateExpr* next = *iter;
    if (!next->non_greedy() && next->type() != Expr::kEOP) {
      if (state->root_non_greedy()) {
        if (next->near_root_non_greedy_pair() != NULL) {
          follow_.insert(next->near_root_non_greedy_pair());
        } else {
          StateExpr* next_ = static_cast<StateExpr*>(next->Clone(&pool_));
          next_->set_non_greedy(true);
          next_->follow() = next->follow();
          next->set_near_root_non_greedy_pair(next_);
          next_->set_near_root_non_greedy_pair(next);
          follow_.insert(next_);
        }
      } else {
        if (next->non_greedy_pair() != NULL) {
          follow_.insert(next->non_greedy_pair());
        } else {
          StateExpr* next_ = static_cast<StateExpr*>(next->Clone(&pool_));
          next_->set_non_greedy(true);
          next_->follow() = next->follow();
          next->set_non_greedy_pair(next_);
          next_->set_non_greedy_pair(next);
          follow_.insert(next_);
        }
      }
    } else {
      follow_.insert(next);
    }
  }
  state->follow() = follow_;
  state->set_complete_non_greedy(true);
}

void DFA::TrimNonGreedy(Subset* states) const {
  Subset trim;
  for (Subset::iterator iter = states->begin(); iter != states->end(); ++iter) {
    if ((*iter)->non_greedy()) trim.insert(*iter);
  }
  for (Subset::iterator iter = trim.begin(); iter != trim.end(); ++iter) {
    StateExpr* state = *iter;
    if (state->non_greedy_pair() != NULL) states->insert(state->non_greedy_pair());
    states->erase(state);
  }
}

bool DFA::Construct(std::size_t limit)
{
  if (expr_info_.expr_root == NULL) return false;
  
  std::queue<Subset> queue;
  std::vector<Subset> transition(256);

  state_t dfa_id = 0;
  bool limit_over = false, begline = true;
  Subset states = expr_info_.expr_root->first();

  ExpandStates(&states, begline);
  if (ContainAcceptState(states)) TrimNonGreedy(&states);
  queue.push(states);
  
  nfa_map_[dfa_id] = states;
  dfa_map_[states] = dfa_id++;

  while (!queue.empty()) {
    states = queue.front();
    queue.pop();

    std::fill(transition.begin(), transition.end(), Subset());
    for (Subset::iterator iter = states.begin(); iter != states.end(); ++iter) {
      FillTransition(*iter, &transition);
    }

    State &state = get_new_state();
    Transition &trans = transition_[state.id];
    state.accept = ContainAcceptState(states);

    if (!flag_.suffix_match() && flag_.shortest_match()) {
      /* Leftmost-Shortest matching
         if current state is accepted
         then no more transitions are needed.
       */
      if (state.accept) {
        trans.fill(REJECT);
        state.dst_states.insert(REJECT);
        begline = false;
        continue;
      }
    }

    // fill transitions of current state
    for (std::size_t c = 0; c < 256; c++) {
      Subset& next = transition[c];

      if (next.empty()) {
        trans[c] = REJECT;
        state.dst_states.insert(REJECT);
        continue;
      }

      ExpandStates(&next);
      if (ContainAcceptState(next)) TrimNonGreedy(&next);
      
      if (dfa_map_.find(next) == dfa_map_.end()) {
        if (dfa_id < limit) {
          nfa_map_[dfa_id] = next;
          dfa_map_[next] = dfa_id++;
          queue.push(next); 
        } else {
          limit_over = true;
          continue;
        }
      }
      trans[c] = dfa_map_[next];
      state.dst_states.insert(dfa_map_[next]);
    }
    begline = false;
  }

  if (limit_over) {
    return false;
  } else {
    Finalize();
    return true;
  }
}

bool DFA::Construct(const NFA &nfa, size_t limit)
{
  state_t dfa_id = 0;

  typedef std::set<NFA::state_t> Subset_;

  std::map<std::set<NFA::state_t>, state_t> dfa_map;
  std::queue<Subset_> queue;
  const Subset_ &start_states = nfa.start_states();

  dfa_map[start_states] = dfa_id++;
  queue.push(start_states);

  while (!queue.empty()) {
    Subset_ nfa_states = queue.front();
    queue.pop();
    std::vector<Subset_> transition(256);
    bool accept = false;

    for (Subset_::iterator iter = nfa_states.begin(); iter != nfa_states.end(); ++iter) {
      const NFA::State &state = nfa[*iter];
      for (std::size_t i = 0; i < 256; i++) {
        transition[i].insert(state.transition[i].begin(), state.transition[i].end());
      }
      accept |= state.accept;
    }

    State &state = get_new_state();
    Transition &trans = transition_[state.id];
    state.accept = accept;
    //Leftmost-Shortest matching
    if (!flag_.suffix_match() && flag_.shortest_match() && accept) {
      trans.fill(REJECT);
      state.dst_states.insert(REJECT);
      continue;
    }
    
    for (state_t i = 0; i < 256; i++) {
      Subset_ &next = transition[i];
      if (next.empty()) {
        trans[i] = REJECT;
        state.dst_states.insert(REJECT);
        continue;
      }
      if (dfa_map.find(next) == dfa_map.end()) {
        dfa_map[next] = dfa_id++;
        queue.push(next);
      }
      trans[i] = dfa_map[next];
      state.dst_states.insert(dfa_map[next]);
    }
  }

  Finalize();

  return true;
}

void DFA::Finalize()
{
  for (iterator state_iter = begin(); state_iter != end(); ++state_iter) {
    for (std::set<state_t>::iterator iter = state_iter->dst_states.begin();
         iter != state_iter->dst_states.end(); ++iter) {
      if (*iter != REJECT) {
        states_[*iter].src_states.insert(state_iter->id);
      }
    }
  }

  complete_ = true;
}

DFA::State& DFA::get_new_state() const
{
  transition_.resize(states_.size()+1);
  states_.resize(states_.size()+1);
  State &new_state = states_.back();
  new_state.transitions = &transition_;
  new_state.id = states_.size()-1;
  new_state.alter_transition.next1 = UNDEF;
  return new_state;
}

void DFA::state2label(state_t state, char* labelbuf) const
{
  if (state == REJECT) {
    strcpy(labelbuf, "reject");
  } else {
    assert(0 <= state && state <= (state_t)size());
    sprintf(labelbuf, "s%d", state);
  }
}

bool DFA::Minimize()
{
  if (!complete_) return false;
  if (minimum_) return true;
  
  std::vector<std::vector<bool> > distinction_table;
  distinction_table.resize(size()-1);
  for (state_t i = 0; i < size()-1; i++) {
    distinction_table[i].resize(size()-i-1);
    for (state_t j = i+1; j < size(); j++) {
      distinction_table[i][size()-j-1] = states_[i].accept != states_[j].accept;
    }
  }

  bool distinction_flag = true;
  while (distinction_flag) {
    distinction_flag = false;
    for (state_t i = 0; i < size()-1; i++) {
      for (state_t j = i+1; j < size(); j++) {
        if (!distinction_table[i][size()-j-1]) {
          for (std::size_t input = 0; input < 256; input++) {
            state_t n1, n2;
            n1 = transition_[i][input];
            n2 = transition_[j][input];
            if (n1 != n2) {
              if (n1 > n2) std::swap(n1, n2);
              if ((n1 == REJECT || n2 == REJECT) ||
                  distinction_table[n1][size()-n2-1]) {
                distinction_flag = true;
                distinction_table[i][size()-j-1] = true;
                break;
              }
            }
          }
        }
      }
    }
  }
  
  std::map<state_t, state_t> swap_map;
  for (state_t i = 0; i < size()-1; i++) {
    for (state_t j = i+1; j < size(); j++) {
      if (swap_map.find(j) == swap_map.end()) {
        if (!distinction_table[i][size()-j-1]) {
          swap_map[j] = i;
        }
      }
    }
  }

  if (swap_map.empty()) {
    minimum_ = true;
    return true;
  }

  size_t minimum_size = size() - swap_map.size();
  std::vector<state_t> replace_map(size());
  for (state_t s = 0, d = 0; s < size(); s++) {
    if (swap_map.find(s) == swap_map.end()) {
      replace_map[s] = d++;
      if (s != replace_map[s]) {
        transition_[replace_map[s]] = transition_[s];
        states_[replace_map[s]] = states_[s];
        states_[replace_map[s]].id = replace_map[s];
      }
    } else {
      replace_map[s] = replace_map[swap_map[s]];
    }
  }

  std::set<state_t> tmp_set;
  for (iterator state_iter = begin(); state_iter->id < minimum_size; ++state_iter) {
    State &state = *state_iter;
    for (std::size_t input = 0; input < 256; input++) {
      state_t n = state[input];
      if (n != REJECT) {
        state[input] = replace_map[n];
      }
    }
    tmp_set.clear();
    for (std::set<state_t>::iterator iter = state.dst_states.begin(); iter != state.dst_states.end(); ++iter) {
      if (*iter != REJECT) tmp_set.insert(replace_map[*iter]);
      else tmp_set.insert(REJECT);
    }
    state.dst_states = tmp_set;
    tmp_set.clear();
    for (std::set<state_t>::iterator iter = state.src_states.begin(); iter != state.src_states.end(); ++iter) {
      tmp_set.insert(replace_map[*iter]);
    }
    state.src_states = tmp_set;
  }

  transition_.resize(minimum_size);
  states_.resize(minimum_size);

  minimum_ = true;
  return true;
}

void DFA::Complementify()
{
  state_t reject = REJECT;
  for (iterator state_iter = begin(); state_iter != end(); ++state_iter) {
    State &state = *state_iter;
    if (state.id != reject) {
      state.accept = !state.accept;
    }
    bool to_reject = false;
    for (std::size_t i = 0; i < 256; i++) {
      if (state[i] == REJECT) {
        if (reject == REJECT) {
          State &reject_state = get_new_state();
          reject = reject_state.id;
          for (std::size_t j = 0; j < 256; j++) {
            reject_state[j] = reject;
          }
          reject_state.dst_states.insert(reject);
          reject_state.accept = true;
        }
        to_reject = true;
        state[i] = reject;
      }
    }
    if (to_reject) {
      state.dst_states.insert(reject);
      states_[reject].src_states.insert(state.id);
    }
  }
}

#if REGEN_ENABLE_XBYAK
JITCompiler::JITCompiler(const DFA &dfa, std::size_t state_code_size = 64):
    /* code segment for state transition.
     *   each states code was 16byte alligned.
     *                        ~~
     *   padding for 4kb allign between code and data
     *                        ~~
     * data segment for transition table
     *                                                */
    CodeGenerator(code_segment_size(dfa.size()) + data_segment_size(dfa.size())),
    code_segment_size_(code_segment_size(dfa.size())),
    data_segment_size_(data_segment_size(dfa.size())),
    total_segment_size_(code_segment_size(dfa.size())+data_segment_size(dfa.size())), filter_entry_(NULL),
    reset_state_(DFA::UNDEF)
{
  states_addr_.resize(dfa.size());

  const uint8_t* code_addr_top = getCurr();
  const uint8_t** transition_table_ptr = (const uint8_t **)(code_addr_top + code_segment_size_);

#ifdef XBYAK32
  const Xbyak::Reg32& arg1(ecx);
  const Xbyak::Reg32& arg2(edx);
  const Xbyak::Reg32& arg3(ebx);
  const Xbyak::Reg32& tbl (ebp);
  const Xbyak::Reg32& tmp1(esi);
  const Xbyak::Reg32& tmp2(edi);  
  const Xbyak::Reg32& reg_a(eax);
  push(esi);
  push(edi);
  push(ebp);
  push(ebx);
  mov(arg1, ptr [esp + P_ + 4]);
  mov(arg2, ptr [esp + P_ + 8]);
  mov(arg3, ptr [esp + P_ + 12]);
#elif defined(XBYAK64_WIN)
  const Xbyak::Reg64& arg1(rcx);
  const Xbyak::Reg64& arg2(rdx);
  const Xbyak::Reg64& arg3(r8);
  const Xbyak::Reg64& tbl (r9);
  const Xbyak::Reg64& tmp1(r10);
  const Xbyak::Reg64& tmp2(r11);
  const Xbyak::Reg64& reg_a(rax);
#else
  const Xbyak::Reg64& arg1(rdi);
  const Xbyak::Reg64& arg2(rsi);
  const Xbyak::Reg64& arg3(rdx);
  const Xbyak::Reg64& tbl (r8);
  const Xbyak::Reg64& tmp1(r10);
  const Xbyak::Reg64& tmp2(r11);
  const Xbyak::Reg64& reg_a(rax);
#endif
  // setup enviroment on register
  const int sign = dfa.flag().reverse_match() ? -1 : 1;
  push(arg2);
  push(arg1);
  mov(tbl, (size_t)transition_table_ptr);
  mov(tmp2, 0);
  mov(arg2, ptr[arg1+sizeof(uint8_t*)]);
  mov(arg1, ptr[arg1]);

  mov(tmp1, (std::size_t)&(states_addr_[0]));
  jmp(ptr[tmp1+arg3*sizeof(uint8_t*)]);

  L("reject");
  const uint8_t *reject_state_addr = getCurr();
  mov(reg_a, DFA::REJECT); // return false

  L("return");
  pop(tmp1);
  mov(ptr[tmp1], arg1);
  pop(tmp1);
  test(tmp1, tmp1);
  je("finalize");
  mov(ptr[tmp1], tmp2);

  L("finalize");
#ifdef XBYAK32
  pop(ebx);
  pop(ebp);
  pop(edi);
  pop(esi);
#endif

  ret();
  
  align(16);

  if (dfa.flag().filtered_match()) {
    /* generate filter code conditionaly. */
    if (dfa.expr_info().key.longest_keyword().length() > 1) {
      /* regex has keyword (which will be contained acceptable string certainly).
         so, we can try to search keyword firstly. */
      if (mie::isAvaiableSSE42()) {
        /* fastest keyword search with SSE4.2. */
      } else {
        /* or simple quick search algorithm. */
      }
    } else if (dfa.expr_info().key.longest_keyword().length() > 1) {
      /* memchr */
      
    }
    else if (dfa.expr_info().involve.count() < 126 && dfa.expr_info().min_length > 2) {
      /* (cheap but effective) quick filter. */
      std::size_t len = dfa.expr_info().min_length;
      filter_entry_ = getCurr();
      filter_table_.resize(256);
      mov(reg_a, (size_t)&(filter_table_[0]));
      const uint8_t *quick_filter_main = getCurr();
      add(arg1, (len - 1) * sign);
      if (dfa.flag().reverse_match()) {
        cmp(arg2, arg1);
      } else { 
        cmp(arg1, arg2);
      }
      jge("reject", T_NEAR);
      movzx(tmp1, byte[arg1]);
      jmp(ptr[reg_a+tmp1*sizeof(uint8_t*)]);

      const uint8_t *quick_filter_end = getCurr();
      sub(arg1, (len - 1) * sign);
    
      for (std::size_t i = 0; i < 256; i++) {
        if (dfa.expr_info().involve[i]) {
          filter_table_[i] = quick_filter_end;
        } else {
          if (reset_state_ == DFA::UNDEF) reset_state_ = dfa[0][i];
          filter_table_[i] = quick_filter_main;
        }
      }

      char labelbuf[100];
      dfa.state2label(reset_state_, labelbuf);
      jmp(labelbuf, T_NEAR);
    
      align(16);
    }
  }
  
  char labelbuf[100];
  // state code generation, and indexing every states address.
  for (std::size_t i = 0; i < dfa.size(); i++) {
    dfa.state2label(i, labelbuf);
    L(labelbuf);
    states_addr_[i] = getCurr();
    if (dfa.IsAcceptState(i) && !dfa.flag().suffix_match()) {
      mov(tmp2, arg1);
      if (dfa.flag().shortest_match()) {
        mov(reg_a, i);
        jmp("return");
      }
    }
    // can transition without table lookup ?
    const DFA::AlterTrans &at = dfa[i].alter_transition;
    if (dfa.olevel() >= Regen::Options::O2 && at.next1 != DFA::UNDEF) {
      std::size_t state = i;
      std::size_t inline_level = dfa.olevel() == Regen::Options::O3 ? dfa[i].inline_level : 0;
      bool inlining = inline_level != 0;
      std::size_t transition_depth = -1;
      inLocalLabel();
      if (inlining) {
        lea(tmp1, ptr[arg1 + inline_level * sign]);
        if (dfa.flag().reverse_match()) {
          cmp(arg2, tmp1);
        } else {
          cmp(tmp1, arg2);
        }
        jge(".ret", T_NEAR);
      } else {
        cmp(arg1, arg2);
        je(".ret", T_NEAR);        
      }
   emit_transition:
      const DFA::AlterTrans &at = dfa.GetAlterTrans(state);
      bool jn_flag = false;
      transition_depth++;
      assert(at.next1 != DFA::UNDEF);
      dfa.state2label(at.next1, labelbuf);
      if (at.next1 != DFA::REJECT) {
        jn_flag = true;  
      }
      if (at.next2 == DFA::UNDEF) {
        if (!inlining) {
          add(arg1, sign);
          jmp(labelbuf, T_NEAR);
        } else if (transition_depth == inline_level) {
          add(arg1, transition_depth * sign);
          jmp(labelbuf, T_NEAR);
        } else {
          state = at.next1;
          goto emit_transition;
        }
      } else {
        if (!inlining) {
          movzx(tmp1, byte[arg1]);
          add(arg1, sign);
        } else if (transition_depth == inline_level) {
          movzx(tmp1, byte[arg1 + transition_depth * sign]);
          add(arg1, (transition_depth+1) * sign);
        } else {
          movzx(tmp1, byte[arg1 + transition_depth * sign]);
        }
        if (at.key.first == at.key.second) {
          cmp(tmp1, at.key.first);
          if (transition_depth == inline_level || !jn_flag) {
            je(labelbuf, T_NEAR);
          } else {
            jne("reject", T_NEAR);
          }
        } else {
          sub(tmp1, at.key.first);
          cmp(tmp1, at.key.second-at.key.first+1);
          if (transition_depth == inline_level || !jn_flag) {
            jc(labelbuf, T_NEAR);
          } else {
            jnc("reject", T_NEAR);
          }
        }
        dfa.state2label(at.next2, labelbuf);
        if (transition_depth == inline_level) {
          jmp(labelbuf, T_NEAR);
        } else {
          if (jn_flag) {
            state = at.next1;
            goto emit_transition;
          } else {
            state = at.next2;
            goto emit_transition;
          }
        }
      }
      if (inlining) {
        L(".ret");
        cmp(arg1, arg2);
        je("@f", T_NEAR);
        movzx(tmp1, byte[arg1]);
        add(arg1, sign);
        jmp(ptr[tbl+i*256*sizeof(uint8_t*)+tmp1*sizeof(uint8_t*)]);
        L("@@");
        mov(reg_a, i);
        jmp("return");
      } else {
        L(".ret");
        mov(reg_a, i);
        jmp("return");
      }
      outLocalLabel();
    } else {
      cmp(arg1, arg2);
      je("@f");
      movzx(tmp1, byte[arg1]);
      add(arg1, sign);
      jmp(ptr[tbl+i*256*sizeof(uint8_t*)+tmp1*sizeof(uint8_t*)]);
      L("@@");
      mov(reg_a, i);
      jmp("return");
    }
    align(16);
  }

  // backpatching (each states address)
  for (std::size_t i = 0; i < dfa.size(); i++) {
    const DFA::Transition &trans = dfa.GetTransition(i);
    for (int c = 0; c < 256; c++) {
      DFA::state_t next = trans[c];
      if (next == DFA::REJECT) {
        transition_table_ptr[i*256+c] = reject_state_addr;
      } else if (filter_entry_ != NULL && next == reset_state_) {
        transition_table_ptr[i*256+c] = filter_entry_;
      } else {
        transition_table_ptr[i*256+c] = states_addr_[next];
      }
    }
  }
}

bool DFA::EliminateBranch()
{
  for (iterator state_iter = begin(); state_iter != end(); ++state_iter) {
    State &state = *state_iter;
    state_t next1 = state[0], next2 = DFA::UNDEF;
    unsigned int begin = 0, end = 256;
    int c;
    for (c = 1; c < 256 && next1 == state[c]; c++);
    if (c < 256) {
      next2 = next1;
      next1 = state[c];
      begin = c;
      for (++c; c < 256 && next1 == state[c]; c++);
    }
    if (c < 256) {
      end = --c;
      for (++c; c < 256 && next2 == state[c]; c++);
    }
    if (c < 256) {
      next1 = next2 = DFA::UNDEF;
    }
    state.alter_transition.next1 = next1;
    state.alter_transition.next2 = next2;
    state.alter_transition.key.first = begin;
    state.alter_transition.key.second = end;
  }

  return true;
}

bool DFA::Reduce()
{
  states_[0].src_states.insert(DFA::UNDEF);
  std::vector<bool> inlined(size());
  const std::size_t MAX_REDUCE = 10;

  for (iterator state_iter = begin(); state_iter != end(); ++state_iter) {
    // Pick inlining region (make degenerate graph).
    state_t state_id = state_iter->id;
    if (inlined[state_id]) continue;
    state_t current_id = state_id;
    for(;;) {
      State &current = states_[current_id];
      if (current.dst_states.size() > 2 ||
          current.dst_states.size() == 0) break;
      if (current.dst_states.size() == 2 &&
          current.dst_states.count(DFA::REJECT) == 0) break;
      if (current.dst_states.size() == 1 &&
          current.dst_states.count(DFA::REJECT) == 1) break;
      State &next = states_[*(current.dst_states.lower_bound(0))];
      if (next.alter_transition.next1 == DFA::UNDEF) break;
      if (next.src_states.size() != 1 ||
          next.accept) break;
      if (inlined[next.id]) break;
      inlined[next.id] = true;
      current_id = next.id;

      if(++(state_iter->inline_level) >= MAX_REDUCE) break;
    }
  }
  states_[0].src_states.erase(DFA::UNDEF);

  return true;
}

bool DFA::Compile(Regen::Options::CompileFlag olevel)
{
  if (!complete_) return false;
  if (olevel <= olevel_) return true;
  if (olevel >= Regen::Options::O2) {
    if (EliminateBranch()) {
      olevel_ = Regen::Options::O2;
    }
    if (olevel == Regen::Options::O3 && Reduce()) {
      olevel_ = Regen::Options::O3;
    }
  }
  xgen_ = new JITCompiler(*this);
  CompiledMatch = (state_t (*)(const unsigned char**, const unsigned char**, state_t))xgen_->getCode();
  if (olevel_ < Regen::Options::O1) olevel_ = Regen::Options::O1;
  return olevel == olevel_;
}
#else
bool DFA::EliminateBranch() { return false; }
bool DFA::Reduce() { return false; }
bool DFA::Compile(Regen::Options::CompileFlag) { return false; }
#endif

bool DFA::Match(const Regen::StringPiece &string, Regen::StringPiece *result) const
{
  if (!complete_) return OnTheFlyMatch(string, result);

  Regen::StringPiece string_(string);
  int sign = 1;
  const unsigned char* matchptr = NULL;
  if (flag_.reverse_match()) {
    sign = -1;
    string_.reverse();
  }
  state_t state = 0;
  bool accept = false;

  if (olevel_ >= Regen::Options::O1) {
    /* JITed matching */
    const unsigned char **arg1 = string_._udata();
    state = CompiledMatch(arg1, &matchptr, state);
  } else {
    if (result == NULL) {
      while (!string_.empty() && (state = transition_[state][*string_.udata()]) != DFA::REJECT) {
        string_.consume(sign);
      }
    } else {
      if (IsAcceptState(state)) matchptr = string_.udata();
      while (!string_.empty() && (state = transition_[state][*string_.udata()]) != DFA::REJECT) {
        if (IsAcceptState(state)) matchptr = string_.udata();
        string_.consume(sign);
      }
    }
  }

  accept = IsAcceptState(state);
  if (!accept && state != REJECT && string_.empty()) {
    Subset endstates = nfa_map_[state];
    ExpandStates(&endstates, string.empty(), true);
    accept = ContainAcceptState(endstates);
  }
  if (result == NULL) {
    return accept;
  } else {
    if (flag_.suffix_match() && accept) {
      if (flag_.reverse_match()) {
        result->set_begin(string.begin());
      } else {
        result->set_end(string.end());
      }
      return true;
    } else {
      if (accept |= matchptr != NULL) {
        if (flag_.reverse_match()) {
          result->set_ubegin(matchptr+1);
        } else {
          result->set_uend(matchptr);
        }
      }
      return accept;
    }
  }
}

bool DFA::OnTheFlyMatch(const Regen::StringPiece& string, Regen::StringPiece* result) const
{
  if (empty()) {
    Subset states = expr_info_.expr_root->first();
    ExpandStates(&states, true);
    bool accept = ContainAcceptState(states);
    State& s = get_new_state();
    dfa_map_[states] = s.id;
    nfa_map_[s.id] = states;
    s.accept = accept;
  }

  int dir = 1;  
  const unsigned char* str = string.ubegin();
  const unsigned char* end = string.uend();
  if (flag_.reverse_match()) {
    dir = -1; str--, end--;
    std::swap(str, end);
  }
  
  state_t state = 0, next = UNDEF;
  
  while (str != end) {
    next = transition_[state][*str];
    if (next >= UNDEF) {
      if (next == REJECT) return false;
      do { // do matching with on-the-fly construction.
        Subset& states = nfa_map_[state];
        Subset nexts;

        for (Subset::iterator iter = states.begin(); iter != states.end(); ++iter) {
          if ((*iter)->Match(*str)) {
            nexts.insert((*iter)->follow().begin(), (*iter)->follow().end());
          }
        }
        ExpandStates(&nexts);

        if (nexts.empty()) {
          next = transition_[state][*str] = REJECT;
        } else if (dfa_map_.find(nexts) == dfa_map_.end()) {
          bool accept = ContainAcceptState(nexts);
          State& s = get_new_state();
          dfa_map_[nexts] = s.id;
          nfa_map_[s.id] = nexts;
          s.accept = accept;
          next = transition_[state][*str] = s.id;
        } else {
          next = dfa_map_[nexts];
        }
        str += dir;
        state = next;
      } while (str != end && transition_[state][*str] == UNDEF);
    } else {
      str += dir;
      state = next;
    }
  }

  if (IsAcceptState(state)) return true;
  if (str == end && state != REJECT) {
    Subset endstates = nfa_map_[state];
    ExpandStates(&endstates, str == string.ubegin(), true);
    return ContainAcceptState(endstates);
  }
  return false;
}

} // namespace regen
