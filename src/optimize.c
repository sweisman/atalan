/*

Code optimalization routines

(c) 2010 Rudolf Kudla 
Licensed under the MIT license: http://www.opensource.org/licenses/mit-license.php

*/

#include "language.h"

#define PEEPHOLE_SIZE 2
extern Bool VERBOSE;
extern Var * VARS;		// global variables
extern Var   ROOT_PROC;


// flag is set to 1, if the variable is live

void VarMark(Var * var, Bool state)
/*
Purpose:
	Mark variable as used or dead.

	Reference to array using variable may not be marked as dead or alive, as 
	it may in fact reference other variable than we think in case the variable is changes.
*/
{
	Var * var2;
	if (var == NULL) return;

	// If this is array access variable, mark indices as live (used)

	if (var->mode == MODE_ELEMENT) {

		// Reference uses the array variable
		if (var->submode == SUBMODE_REF) {
			VarMark(var->adr, 1);
		}
			
		VarMark(var->var, 1);

		// Array references with variable indexes are always live

		if (var->var->mode != MODE_CONST) state = 1;
	} else {
		if (var->adr != NULL) {
			if (var->adr->mode == MODE_VAR) {
				VarMark(var->adr, state);
			}
		}
	}

	var->flags = state;

	FOR_EACH_VAR(var2)
		if (var2->mode == MODE_ELEMENT) {
			if (var2->adr == var) {
				var2->flags = state;
			}
		}
	NEXT_VAR
}

void VarMarkLive(Var * var)
{
	VarMark(var, 1);
}

void VarMarkDead(Var * var)
{
	VarMark(var, 0);
}


// 0 dead
// 1 live
// 2 undecided

void MarkBlockAsUnprocessed(InstrBlock * block)
{
	InstrBlock * nb;
	for(nb = block; nb != NULL; nb = nb->next) {
		nb->processed = false;
	}
}

UInt8 VarIsLiveInBlock(Var * proc, InstrBlock * block, Var * var)
/*
Purpose:
	Test, if variable is live after specified block.
*/
{
	Instr * i;
	UInt8 res1, res2;
	if (block == NULL) return 0;
	if (block->processed) return 2;

	block->processed = true;

	res1 = res2 = 2;

	for (i = block->first; i != NULL; i = i->next) {
		if (i->arg1 == var || i->arg2 == var) { res1 = 1; goto done; }
		if (i->result == var) { res1 = 0; goto done;}
		if (i->op == INSTR_LET_ADR) {
			if (VarIsArrayElement(i->arg1)) {
				if (var->mode == MODE_ELEMENT) {
					if (var->adr == i->arg1->adr) { res1 = 1; goto done; }
				}
			}
		}
	}

	// If block ends and the variable is one of results, it is live

	if (block->next == NULL && FlagOn(var->submode, SUBMODE_ARG_OUT) && var->scope == proc) return 1;

	// We haven't encountered the variable, let's try blocks following this block

	res1 = VarIsLiveInBlock(proc, block->to, var);
	if (res1 != 1) {
		res2 = VarIsLiveInBlock(proc, block->cond_to, var);
	}

	if (res1 == 1 || res2 == 1) { res1 = 1; goto done; }
	if (res1 == 0 && res2 == 0) res1 = 0;
done:
//	block->processed = false;
	return res1;
}

void MarkProcLive(Var * proc)
{
	Var * var;

	var = VarFirstLocal(proc);
	while(var != NULL) {
		if (FlagOff(var->submode, SUBMODE_ARG_IN | SUBMODE_ARG_OUT)) {
			VarMarkDead(var);
		} else {
			if (FlagOn(var->submode, SUBMODE_ARG_IN)) VarMarkLive(var);
			if (FlagOn(var->submode, SUBMODE_ARG_OUT)) VarMarkDead(var);
		}
		var = VarNextLocal(proc, var);
	}
}

Bool VarIsOut(Var * var)
{
	if (var == NULL) return false;
	if (FlagOn(var->submode, SUBMODE_OUT)) return true;
	if (var->mode == MODE_ELEMENT) {
		return VarIsOut(var->adr);
	}
	return false;
}

Bool OptimizeLive(Var * proc)
{
	Bool modified = false;
	InstrBlock * blk;
	Instr * i;
	Var * var, * result;
	UInt32 n = 0, blk_n = 0;
	InstrOp op;

	if (VERBOSE) {
		printf("------ optimize live ------\n");
		PrintProc(proc);
	}

	for(blk = proc->instr; blk != NULL; blk = blk->next) {
		
		if (VERBOSE) {
			n += blk_n;
			blk_n = 0;
			for(i = blk->last; i != NULL; i = i->prev) blk_n++;
			n += blk_n;
		}

		// At the beginning, all variables are dead (except procedure output variables for tail blocks)

		FOR_EACH_VAR(var)

			if (StrEqual(var->name, "s")) {
				printf("");
			}

			if (blk->to == NULL && var->submode == SUBMODE_ARG_OUT) {
				var->flags = 1;		// procedure output arguments are live in last block
			} else if (blk->to == NULL && !VarIsOut(var)) {
				var->flags = 0;
			} else if (VarIsOut(var)) {
				var->flags = 1;		// out variables are always live, procedure output arguments are line in last block
			} else {
				var->flags = 0;
				MarkBlockAsUnprocessed(proc->instr);
				if (VarIsLiveInBlock(proc, blk->to, var) == 1 || VarIsLiveInBlock(proc, blk->cond_to, var) == 1) {
					var->flags = 1;
				}
			}
		NEXT_VAR

		// Skip the data part (data etc.)
		// At the end of main procedure (but possibly in others too) there is a set of constant data initialization
		// and allocation.
		// There are label instructions, to which it will not be jumped, but that are used to reference the data. (data labels)
		// These labels would cause marking all variables as live, so we skip that part of code.
/*
		for(i = blk->last; i != NULL; i = i->prev, n--) {
			op = i->op;
			if (op != INSTR_INCLUDE && op != INSTR_LINE && op != INSTR_LABEL && op != INSTR_DATA && op != INSTR_PTR && op != INSTR_ALLOC 
				&& op != INSTR_FILE && op != INSTR_ALIGN && op != INSTR_ARRAY_INDEX) break;
		}
*/
		for(i = blk->last; i != NULL; i = i->prev, n--) {

			op = i->op;
			if (op == INSTR_LINE) continue;

			// End of basic block, all variables are marked as live
	/*
			if (IS_INSTR_JUMP(i->op) || i->op == INSTR_LABEL || i->op == INSTR_CALL || i->op == INSTR_FORMAT || i->op == INSTR_PRINT) {

				// TODO: If this label is not target of any jump instruction (it is data label), it may be removed

				FOR_EACH_VAR(var)
					var->flags = 1;
				NEXT_VAR
			}
	*/
			// Mark arguments as live (used)
			// We must first mark the arguments, as we do not want the instructions like add x, x, 1 to be removed (x is argument)

			result = i->result;
			if (result != NULL) {
				if (op != INSTR_LABEL && op != INSTR_CALL && op != INSTR_PRINT && op != INSTR_FORMAT) {
					if (result->flags == 0 && !VarIsLabel(result) && !VarIsArray(result) && FlagOff(result->submode, SUBMODE_REF)) {
						if (VERBOSE) {
							printf("removed dead %ld:", n); InstrPrint(i);
						}
						i = InstrDelete(blk, i);
						modified = true;
						if (i == NULL) break;		// we may have removed last instruction in the block
						continue;
					}
				}
			}

			// Mark result as dead

			if (result != NULL) {
				// For reference, the adr is marked live, reference is not marked dead, as we may not know, what adr there is
				if (FlagOn(result->submode, SUBMODE_REF)) {
					VarMarkLive(result->adr);
				} else if (!VarIsOut(result)) {
					VarMarkDead(result);
				}
				// Array indexes are marked live (even if value itself is marked dead)
				if (result->mode == MODE_ELEMENT) {
					VarMarkLive(result->var);
				}
			}


			if (op == INSTR_CALL) {
				MarkProcLive(i->result);
			} else {
				// Mark arguments as live (used)
				VarMarkLive(i->arg1);
				VarMarkLive(i->arg2);
			}
		}
	}
	return modified;
}


void VarIncRead(Var * var)
{
	if (var != NULL) {
		var->read++;
		if (var->write == 0) {
			// variable should not be marked as uninitialized, if there has been label or jump or call
			var->flags |= VarUninitialized;
		}
		if (var->mode == MODE_ELEMENT) {
			var->adr->read++;
			var->var->read++;
		}
	}
}

void VarIncWrite(Var * var)
{
	if (var != NULL) {
		var->write++;
		if (var->mode == MODE_ELEMENT) {
			var->adr->write++;
			var->var->read++;
		}
	}
}

void InstrVarUse(InstrBlock * code, InstrBlock * end)
{
	Instr * i;
	Var * result;
	InstrBlock * blk;
	
	for(blk = code; blk != end; blk = blk->next) {
		for(i = blk->first; i != NULL; i = i->next) {

			if (i->op == INSTR_LINE) continue;
			if (i->op == INSTR_CALL) continue;		// Calls are used to compute call chains and there are other rules of computation

			// Writes are registered as last to correctly detect uninitialized variable access
			VarIncRead(i->arg1);
			VarIncRead(i->arg2);

			result = i->result;
			VarIncWrite(result);

			// In instructions like op X, X, ? or op X, ?, X, X is induction variable

			if (result != NULL) {
				if (result == i->arg1 || result == i->arg2) {
					result->flags |= VarLoop;
				}
			}
		}
	}
}

void InstrVarLoopDependent(InstrBlock * code, InstrBlock * end)
/*
	Compute dependency of variables on loop variables.
*/
{
	Instr * i;
	Var * result;
	UInt8 flags;

	InstrBlock * blk;
	
	for(blk = code; blk != end; blk = blk->next) {
		for(i = blk->first; i != NULL; i = i->next) {

			if (i->op == INSTR_LINE) continue;

			flags = 0;
			result = i->result;
			if (result != NULL) {
				if (i->op == INSTR_LET) {
					result->flags &= ~(VarLoopDependent|VarLoop);
					result->flags |= i->arg1->flags & (VarLoopDependent|VarLoop);
				} else {
					// Exclude self dependency
					if (i->arg1 != NULL && i->arg1 != result) flags |= i->arg1->flags;
					if (i->arg2 != NULL && i->arg2 != result) flags |= i->arg2->flags;
					if (FlagOn(flags, VarLoop | VarLoopDependent)) {
						result->flags |= VarLoopDependent;
					}
				}
			}
		}
	}
}

void VarUse()
{
	Var * var;

	VarResetUse();

	FOR_EACH_VAR(var)
		if (var->type != NULL && var->type->variant == TYPE_PROC) {
			if (var->instr != NULL) {
				InstrVarUse(var->instr, NULL);
			}
		}
	NEXT_VAR

	InstrVarUse(ROOT_PROC.instr, NULL);
}

//TODO: Replace variable management (keep array of those variables and reuse them)

Int16 VarTestReplace(Var ** p_var, Var * from, Var * to)
{
	Var * var, * var2;
	Int16 n = 0;
	Int16 n2, n3;
	Var * v2, * v3;

	var = *p_var;
	if (var == from) {
		*p_var = to;
		n++;
	} else {
		//TODO: We should probably alloc new element variable here
		//      because this one may be used somewhere else
		if (var != NULL) {
			if (var->mode == MODE_ELEMENT) {
				v2 = var->adr;
				v3 = var->var;
				n2 = VarTestReplace(&v2, from, to);
				n3 = VarTestReplace(&v3, from, to);

				if (n2 > 0 || n3 > 0) {
					var2 = MemAllocStruct(Var);
					memcpy(var2, var, sizeof(Var));
					var2->adr = v2;
					var2->var = v3;
					var2->next = NULL;

					n += n2 + n3;
					*p_var = var2;
				}
			}
		}
	}
	return n;
}

Int16 VarReplace(Var ** p_var, Var * from, Var * to)
{
	Var * var;
	Int16 n = 0;

	var = *p_var;
	if (var == from) {
		*p_var = to;
		n++;
	} else {
		//TODO: We should probably alloc new element variable here
		//      because this one may be used somewhere else
		if (var != NULL) {
			if (var->mode == MODE_ELEMENT) {
				n += VarReplace(&var->adr, from, to);
				n += VarReplace(&var->var, from, to);
			}
		}
	}
	return n;
}

UInt32 VarByteSize(Var * var)
/*
Purpose:
	Return size of variable in bytes.
*/
{
	Int32 lrange;

	Type * type;
	if (var != NULL) {
		type = var->type;
		if (var->mode == MODE_ELEMENT) {
			return 1;		//TODO: Compute size in a better way
		}

		if (type != NULL) {
			switch(type->variant) {
				case TYPE_INT:
					// Some integer variables may have ranges starting higher than 0.
					// (Like 100..300).
					// Byte size of such integers must be computed as if their min range was 0.

					lrange = type->range.min;
					if (lrange > 0) lrange = 0;
					return (type->range.max - lrange) / 256 + 1;
				default: break;
			}
		}
	}
	return 0;
}

Bool ArgNeedsSpill(Var * arg, Var * var)
{
	Bool spill = false;
	if (arg != NULL) {
		if (arg->mode == MODE_ELEMENT) {
			if (arg->adr == var->adr && FlagOn(arg->submode, SUBMODE_REF)) {
				spill = true;
			}			
		}
	}
	return spill;
}

Bool InstrSpill(Instr * i, Var * var)
{
	Bool spill = false;

	if (i->op == INSTR_PRINT || i->op == INSTR_FORMAT) return true;

	if (var->mode == MODE_ELEMENT) {

		spill = ArgNeedsSpill(i->result, var) 
			 || ArgNeedsSpill(i->arg1, var) 
			 || ArgNeedsSpill(i->arg2, var);

		if (i->arg1 == var->adr || i->arg2 == var->adr) {
			spill = true;
		}
	}
	return spill;
}

Bool VarUsesVar(Var * var, Var * test_var)
{
	Bool uses = false;
	if (var != NULL) {
		if (var->mode == MODE_ELEMENT) {
			uses = VarUsesVar(var->var, test_var) || VarUsesVar(var->adr, test_var);
		} else {
			uses = (var == test_var);
		}
	}
	return uses;	
}

Bool InstrUsesVar(Instr * i, Var * var)
{
	if (i == NULL || i->op == INSTR_LINE) return false;

	return VarUsesVar(i->result, var) 
		|| VarUsesVar(i->arg1, var)
		|| VarUsesVar(i->arg2, var);
}

Var * FindMostUsedVar()
/*
Purpose:
	Find most used variable in loop.
*/
{
	UInt32 max_cnt, cnt;
	Var * top_var, * var;

	max_cnt = 0; top_var = NULL;

	FOR_EACH_VAR(var)
		//TODO: Exclude registers, in or out variables
		//Exclude:
		//   - labels
		//   - variables with specified address (typically registers)
		//   - constants
		//   - arrays (we may optimize array element access though)
		//   - variables whose size is bigger than register

		cnt = var->read + var->write;
		if (cnt > 0) {

//			if (VERBOSE) {
//				printf("Var: "); PrintVar(var);
//			}

			if (var->mode == MODE_ELEMENT) {
				if (FlagOn(var->adr->submode, SUBMODE_IN | SUBMODE_OUT | SUBMODE_REG)) continue;
				if (var->var->mode != MODE_CONST) continue;
//				continue;
				// If array index is loop dependent, do not attempt to replace it with register
//				if (FlagOn(var->var->flags, VarLoopDependent)) continue;
			}

			if (FlagOff(var->submode, SUBMODE_IN | SUBMODE_OUT | SUBMODE_REG | SUBMODE_REF) 
//			 && (var->mode != MODE_ELEMENT || FlagOff(var->adr->submode, SUBMODE_IN | SUBMODE_OUT | SUBMODE_REG))
			 && var->mode != MODE_CONST 
			 && var->type != NULL && var->type->variant != TYPE_PROC
			 && !VarIsLabel(var) 
			 && !VarIsArray(var)
			 && FlagOff(var->flags, VarLoopDependent)
			) {
				if (cnt > max_cnt) {
					max_cnt = cnt;
					top_var = var;
				}
			}
		}
	NEXT_VAR

	return top_var;
}

/*
void NumberLabels(InstrBlock * blk)
{
	Instr * i;
	UInt32 n;
	Var * lab;

	for(n = 1, i = blk->first; i != NULL; i = i->next, n++) {
		if (i->op == INSTR_LABEL) {
			lab = i->result;
			lab->seq_no = n;
		}
	}
}
*/

UInt32 InstrVarUseCount(Instr * from, Instr * to, Var * var)
/*
Purpose:
	Count number of uses of the variable in the specified block of instructions.
*/
{
	Instr * i;
	UInt32 cnt = 0;
	for(i = from; i != to; i = i->next) {
		if (InstrUsesVar(i, var)) cnt++;
	}
	return cnt;
}

UInt32 NumberBlocks(InstrBlock * block)
{
	UInt32 seq_no;
	InstrBlock * nb;

	seq_no = 1;
	for(nb = block; nb != NULL; nb = nb->next) {
		nb->seq_no = seq_no++;
	}
	return seq_no;
}

Int32 UsageQuotient(InstrBlock * header, InstrBlock * end, Var * top_var, Var * reg, Bool * p_init)
//===== Compute usage quotient (q)
//      The bigger the value, the more suitable the register is
//      0 means no gain, >0 means using the register would lead to worser code
{
	Var * prev_var;
	Int32 q, q1;
	InstrBlock * blk, * exit;
	Instr * i2, ti;
	UInt32 n2;
	Bool spill;

	exit = end->next;

	*p_init = false;

	q = 0;
	reg->current_val = top_var;	// we expect initialization by top_var before loop
	prev_var = NULL;			// previous variable contained in the register
								// this variable must be loaded, when instruction using the register is encountered
								// (if it is not top_var)

	// Compute usage coeficient
	for(blk = header; blk != exit; blk = blk->next) {
		for(i2 = blk->first, n2 = 0; i2 != NULL; i2 = i2->next, n2++) {

			if (i2->op == INSTR_LINE) continue;

			// Call to subroutine destroys all registers, there will be spill
			if (i2->op == INSTR_CALL) {
				q = 1;
				goto done;
			}
			// If there is jump except last instruction
			if (IS_INSTR_JUMP(i2->op) && (i2 != blk->last || blk != end)) {
				*p_init = true;
			}

			if (i2->op == INSTR_LET) {
				if (i2->result == top_var && i2->arg1 == reg) {
					// Register currently contains the replaced variable
					if (reg->current_val == top_var) {
						q -= 3;
						continue;
					}
				}

				if (i2->result == reg && i2->arg1 == top_var) {
					if (reg->current_val == top_var) {
						q -= 3;
						continue;
					} else {
						prev_var = reg->current_val;
						reg->current_val = i2->arg1;
						continue;
					}
				}
			}

			// If current instruction uses the register, and it is
			// we need to save the register and load some other

			if (InstrUsesVar(i2, reg) && reg->current_val != top_var) {
				if (prev_var != NULL) {
					q += 3;
				}
			}

			// register value becomes unknown (has different value than top_val)
			if (i2->result == reg) {
				reg = NULL;
			}

			memcpy(&ti, i2, sizeof(Instr));

			q1 = VarTestReplace(&ti.result, top_var, reg);
			q1 += VarTestReplace(&ti.arg1, top_var, reg);
			q1 += VarTestReplace(&ti.arg2, top_var, reg);

			// If the instruction was changed (it used top_var),
			// test, whether we are able to compile it (some register/adress mode combinations must not be available)

			if (q1 != 0) {
				if (EmitRule(&ti) == NULL) {
//					if (VERBOSE) {
//						printf("     %ld: invalid code\n", n2);
//					}
					q = 1;		// do not use this register, as invalid code would get generated
					goto done;
				}
				// If there is currently not the value of top_var in replaced register,
				// we would have to load it
				if (reg->current_val != top_var && prev_var != NULL) {
//					if (VERBOSE) {
//						printf("     %ld: load\n", n2);
//					}
					q += 3;
				}
				q -= q1;
//				if (VERBOSE) {
//					printf("     %ld: usage\n", n2);
//				}
			}

			// Will it be necessary to spill?
			// We use the variable (array) that is stored to register

			spill = InstrSpill(i2, top_var);
			if (spill) {
				q += 4;
//				if (VERBOSE) {
//					printf("     %ld: spill\n", n2);
//				}

			}
		} // instr
	} // blk
	// Value of register is not known at the end of loop, we need to load it before first use
	if (reg == NULL) {
		q += 3;
	}
done:
	return q;
}

void OptimizeLoop(Var * proc, InstrBlock * header, InstrBlock * end)
/*
1. Find loop (starting with inner loops)
   - Every jump to label preceeding the jump (backjump) forms a label
   - In nested labels, we encounter the backjump first
				<code1>
			l1@
				<code2>
			l2@
				<code3>
				if.. l2@
				<code4>
				if.. l3@
				<code5>

2. Select variable to put to register
   - Most used variable should be used
   - Some variables are already moved to index register (this is considered use too)

3. Compute cost of moving the variable to register

*/
{
	Instr * i2;
//	Instr * ls, * le;		// loop init start and end instruction 
	Var * top_var, * reg, * top_reg;
	UInt16 r, regi;
	UInt32 var_size;
	Int32 q, top_q;
	Bool init;
	InstrBlock * blk;
//	UInt32 lbl_cnt, lbl_loop_cnt;
	InstrBlock * exit;

	exit = end->next;

	VarResetUse();
	InstrVarUse(header, exit);
	InstrVarLoopDependent(header, end);

	// When processing, we assign var to register
	for(regi = 1; regi < REG_CNT; regi++) REG[regi]->var = NULL;

	if (header->seq_no == 41) {
		printf("");
	}

	while(top_var = FindMostUsedVar()) {

		if (VERBOSE) {
			printf("Most user var: "); PrintVar(top_var);
		}

		top_var->read = top_var->write = 0;
		var_size = VarByteSize(top_var);

		//====== Select the best register for the given variable
		//      let %A,%A   => index -3
		//      use of register instead of variable -1
		//      spill +3

		top_q = 0; top_reg = NULL;

		for(regi = 1; regi < REG_CNT; regi++) {

			reg = REG[regi];
			if (reg->type->range.max == 1) continue;			// exclude flag registers
			if (var_size != VarByteSize(reg)) continue;			// exclude registers with different size
			if (FlagOn(reg->submode, SUBMODE_OUT)) continue;	// out registers can not be used to replace variables
			if (reg->var != NULL) continue;

			if (VERBOSE) {
				printf("  Testing register: "); 
				PrintVar(reg);
			}

			q = UsageQuotient(header, end, top_var, reg, &init);

//			if (VERBOSE) {
//				printf("     Quotient: %d\n", q);
//			}

			if (q < top_q) {
				top_q = q;
				top_reg = reg;
			}
		}

		if (top_reg == NULL) continue;

		if (VERBOSE) {
			printf("Var: "); PrintVar(top_var);
			printf("Register: "); PrintVar(top_reg);
		}


		//TODO: If there is Let reg = var and var is not top_var, we need to spill store

		//=== Replace the use of registers
				
		// Generate instruction initializing the register used to replace the variable
		// before the start of the loop.
		// We only do this, if the variable is not initialized inside the loop.
		if (FlagOn(top_var->flags, VarUninitialized) || init) {
			// TODO: Create new block for initialization
			blk = header->from;		// TODO: We may have come from multiple places
			i2 = blk->last;

			// Loops with condition at the beginning may start with jump to condition
			// We need to insert the initialization code before this jump.

			if (i2->op == INSTR_GOTO) {
				i2 = i2->prev;
			} else {
				i2 = NULL;
			}

			InstrInsert(blk, i2, INSTR_LET, top_reg, top_var, NULL);
			top_reg->var = top_var;		// to prevent using the register in subsequent steps
		}
		r = 0;
		
		for(blk = header; blk != exit; blk = blk->next) {
			for(i2 = blk->first; i2 != NULL; i2 = i2->next) {
				if (i2->op == INSTR_LINE) continue;

				if (InstrSpill(i2, top_var)) {
					InstrInsert(blk, i2, INSTR_LET, top_var, top_reg, NULL);
				}
				r += VarReplace(&i2->result, top_var, top_reg);
				VarReplace(&i2->arg1, top_var, top_reg);
				VarReplace(&i2->arg2, top_var, top_reg);

				if (i2->op == INSTR_LET && i2->result == i2->arg1) {
					i2 = InstrDelete(blk, i2);
					if (i2 == NULL) break;
					continue;
				}

			}
		}

		// If we replaced some destination by the register, store the register to destination
		if (r > 0) {
			// There may be exit label as part of the loop
			// We need to spill after it
			//TODO: Check, that the label belongs to the loop and not to someone else)
			//      All jumps to the label go from the loop
/*
			while(i2 != NULL && i2->op == INSTR_LABEL) {
				lbl_cnt = InstrVarUseCount(blk->first, NULL, i2->result);
				lbl_loop_cnt = InstrVarUseCount(ls, le, i2->result);						
				if (lbl_loop_cnt > 0) {
					if (lbl_cnt - lbl_loop_cnt > 1) {
						InternalError("label used both by loop interior and exterior");
					}
					i2 = i2->next;
				} else {
					// This is not a loop label
					break;
				}
			}
*/
			if (exit == NULL) {
				exit = MemAllocStruct(InstrBlock);
				end->next = exit;
				end->to   = exit;
			}

			//TODO: Create new block if necessary (there may be NULL)
			//      We may test, if variable is live
			InstrInsert(exit, exit->first, INSTR_LET, top_var, top_reg, NULL);
		}

		if (FlagOn(top_var->flags, VarLoopDependent)) {
			reg->flags |= VarLoopDependent;
		}

		//Reset the usage count of this variable, so next variable will be selected
//		if (VERBOSE) {
//			NumberLabels(blk);
//		}
	}
}

void OptimizeLoops(Var * proc)
/*
Purpose:
	Find loops in flow graph and call optimization for it.
*/
{
	InstrBlock * nb, * header;

	NumberBlocks(proc->instr);

	if (VERBOSE) {
		printf("========== Optimize loops ==============\n");
		PrintProc(proc);
	}

	// Test for each block, if it is end of an loop

	for(nb = proc->instr; nb != NULL; nb = nb->next) {
		if (nb->cond_to != NULL) {
			if (nb->cond_to->seq_no <= nb->seq_no) {
				nb->cond_to->loop_end = nb;
			}
		}

		if (nb->to != NULL) {
			if (nb->to->seq_no <= nb->seq_no) {
				nb->to->loop_end = nb;
			}
		}
	}

	for(nb = proc->instr; nb != NULL; nb = nb->next) {
		header = NULL;
		if (nb->cond_to != NULL && nb->cond_to->loop_end == nb) header = nb->cond_to;
		if (nb->to != NULL && nb->to->loop_end == nb) header = nb->to;

		if (header != NULL) {
			if (VERBOSE) {
				printf("*** Loop %d..%d\n", header->seq_no, nb->seq_no);
			}
			OptimizeLoop(proc, header, nb);
		}

	}
}

void OptimizeCombined(Var * proc)
{
	Bool modified;
	do {
//		modified = OptimizePeephole(proc);
		modified = OptimizeLive(proc);
		modified |= OptimizeValues(proc);
	} while(modified);
}

void ProcOptimize(Var * proc)
{
	if (VERBOSE) {
		printf("************************************** before blocks\n");
		PrintProc(proc);
	}

	if (StrEqual(proc->name, "changeDirection")) {
		printf("");
	}

	GenerateBasicBlocks(proc->instr);
	if (VERBOSE) {
		printf("**************************************\n");
		PrintProc(proc);
	}
	OptimizeCombined(proc);
	OptimizeLoops(proc);
	OptimizeCombined(proc);
}
