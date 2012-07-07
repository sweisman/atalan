#include "language.h"

void ExpectExpression(Var * result);
void ParseEnumItems(Type * type, UInt16 column_count);
void ParseAssign(InstrOp mode, VarSubmode submode, Type * to_type);
Var * ParseVariable();

Type * ParseType();
Type * ParseSubtype();

Bool PARSE_INLINE;

void ParseArgList(VarSubmode mode, Type * to_type)
/*
Purpose:
	Parse block with list of arguments.
	  [">" | "<"] assign
	Arguments are added to current context with submode SUBMODE_ARG_*.

	This method is used when parsing procedure or macro argument declaration or structure declaration.
*/
{
	VarSubmode submode = SUBMODE_EMPTY;
	Var * var, * adr;
	Bool out_part = false;

 	EnterBlockWithStop(TOKEN_EQUAL);			// TOKEN_EQUAL

	while (TOK != TOKEN_ERROR && !NextIs(TOKEN_BLOCK_END)) {

		if (!out_part && NextIs(TOKEN_RIGHT_ARROW)) {
			out_part = true;
		}

		submode = mode;

		if (out_part) {
			submode = SUBMODE_ARG_OUT;
		} else {

			if (NextIs(TOKEN_LOWER)) {
				submode = SUBMODE_ARG_IN;
			}
			if (NextIs(TOKEN_HIGHER)) {
				submode = SUBMODE_ARG_OUT;
			}
		}

		// Variables preceded by @ define local variables used in the procedure.
		if (NextIs(TOKEN_ADR)) {
			adr = ParseVariable();
			if (TOK) {
				var = VarAllocScopeTmp(to_type->owner, INSTR_VAR, adr->type);
				var->adr  = adr;
				NextIs(TOKEN_EOL);
				continue;
			}
		}

		if (TOK == TOKEN_ID) {
			ParseAssign(INSTR_VAR, submode, to_type);
			NextIs(TOKEN_COMMA);
			NextIs(TOKEN_EOL);
		} else {
			SyntaxError("Expected variable name");
		}
	}
}


Type * ParseIntType()
{
	Type * type = TUNDEFINED;
	Var * var;
	
	ExpectExpression(NULL);
	if (TOK) {
		var = BufPop();

		// If we parsed rule argument
		if (var->mode == INSTR_ELEMENT || VarIsRuleArg(var)) {
			type = TypeAlloc(TYPE_VAR);
			type->var = var;
			goto done;
		// Integer type may be defined using predefined type definition or be defined as type of other variable
		} else if (var->mode == INSTR_TYPE || var->mode == INSTR_VAR) {			
			type = var->type;
			if (type->variant != TYPE_INT) {
				SyntaxError("Expected integer type");
			}
			goto done;
		} else if (var->mode == INSTR_CONST) {
			type = TypeAlloc(TYPE_INT);
			type->range.min = var->n;
		} else {
			//TODO: If simple integer variable, use it as type range
			SyntaxError("expected constant expression");
		}

		if (TOK == TOKEN_DOTDOT) {
			NextToken();
			ExpectExpression(NULL);
			if (TOK) {
				var = BufPop();
				if (var->mode == INSTR_CONST) {
					type->range.max = var->n;
				} else {
					SyntaxError("expected constant expression");
				}
			}
		} else {
			type->range.max = type->range.min;
			type->range.min = 0;
		}

		if (type->range.min > type->range.max) {
			SyntaxError("range minimum bigger than maximum");
		}
	}

/*
	if (TOK == TOKEN_INT) {
		type = TypeAlloc(TYPE_INT);
		type->range.min = LEX.n;
		NextToken();
		if (TOK == TOKEN_DOTDOT) {
			NextToken();
			if (TOK == TOKEN_INT) {
				type->range.max = LEX.n;
				NextToken();
			}
		} else {
			type->range.max = type->range.min;
			type->range.min = 0;
		}

		if (type->range.min > type->range.max) {
			SyntaxError("range minimum bigger than maximum");
		}
	// Sme variable
	} else if (TOK == TOKEN_ID) {
		var = VarFind2(NAME);
		if (var != NULL) {
			if (var->mode == INSTR_CONST) {
				if (var->type->variant == TYPE_INT) {
					type = TypeAlloc(TYPE_INT);
					type->range.min = 0;
					type->range.max = var->n;
				} else {
					SyntaxError("Expected integer constant");
				}
			} else {
				type = var->type;
			}
			NextToken();
		} else {
			SyntaxError("$unknown variable");
		}
	} else {
		SyntaxError("Expected definition of integer type");
	}
*/
done:
	return type;
}

void ParseEnumStruct(Type * type)
{
	Var * var;
	UInt16 column_count = 0;

 	EnterBlockWithStop(TOKEN_EQUAL);			// TOKEN_EQUAL

	while (TOK != TOKEN_ERROR && !NextIs(TOKEN_BLOCK_END)) {

		if (TOK == TOKEN_ID) {
			ParseAssign(INSTR_VAR, SUBMODE_EMPTY, type);
			NextIs(TOKEN_COMMA);
			NextIs(TOKEN_EOL);
		} else {
			SyntaxError("Expected variable name");
		}
	}

	// Convert parsed fields to constant arrays
	FOR_EACH_LOCAL(type->owner, var)
		var->type = TypeArray(type, var->type);		
//		var->type->step = TypeSize(var->type->element);
		var->mode = INSTR_CONST;
		column_count++;
	NEXT_LOCAL

	NextIs(TOKEN_HORIZ_RULE);

	ParseEnumItems(type, column_count);
}

Type * ParseType3()
{
	Type * type = NULL, * variant_type = NULL;
	Type * elmt, * t;
	Var * var;

	//# "type" restrict_type
	if (NextIs(TOKEN_TYPE2)) {
		variant_type = ParseType2(INSTR_VAR);
		type = TypeType(variant_type);
	//# "enum" ["struct"]
	} else if (NextIs(TOKEN_ENUM)) {
		type = TypeAlloc(TYPE_INT);
		type->range.flexible = true;
		type->is_enum        = true;
		if (NextIs(TOKEN_STRUCT)) {
			ParseEnumStruct(type);
		} /*else if (TOK == TOKEN_INT) {
			goto range;
		}*/
//		goto const_list;

	//# "proc" args
	} else if (NextIs(TOKEN_PROC)) {
		type = TypeAlloc(TYPE_PROC);
		ParseArgList(SUBMODE_ARG_IN, type);
		if (TOK) {
			ProcTypeFinalize(type);
		}
	//# "macro" args
	} else if (NextIs(TOKEN_MACRO)) {

		type = TypeAlloc(TYPE_MACRO);
		ParseArgList(SUBMODE_ARG_IN, type);

	// Struct
	} else if (NextIs(TOKEN_STRUCT)) {
		type = TypeAlloc(TYPE_STRUCT);
		ParseArgList(SUBMODE_EMPTY, type);

	// String
	} else if (NextIs(TOKEN_STRING_TYPE)) {
		type = TypeAlloc(TYPE_STRING);

	// Array
	} else if (NextIs(TOKEN_ARRAY)) {		
		type = TypeAlloc(TYPE_ARRAY);
		t = NULL;

		if (TOK == TOKEN_OPEN_P) {
			EnterBlockWithStop(TOKEN_EQUAL);
			while (TOK != TOKEN_ERROR && !NextIs(TOKEN_BLOCK_END)) {
				elmt = ParseIntType();
				if (type->index == NULL) {
					type->index = elmt;
				} else if (t != NULL) {
					t->right = TypeTuple(t->right, elmt);
					t = t->right;
				} else {
					t = TypeTuple(type->index, elmt);
					type->index = t;
				}
				NextIs(TOKEN_COMMA);
			};
		}
		
		// If no dimension has been defined, use flexible array.
		// This is possible only for constants now.

		if (TOK) {
			if (type->index == NULL) {
				elmt = TypeAlloc(TYPE_INT);
				elmt->range.flexible = true;
				elmt->range.min = 0;
				type->index = elmt;
			}
		}

		// Element STEP may be defined
		if (TOK) {
			if (NextIs(TOKEN_STEP)) {
				ExpectExpression(NULL);
				if (TOK) {
					var = STACK[0];
					if (VarIsIntConst(var)) {
						type->step = var->n;
					} else {
						SyntaxError("Expected integer constant");
					}
				}
			}
		}

		if (TOK) {
			if (NextIs(TOKEN_OF)) {
				type->element = ParseSubtype();
			} else {
				type->element = TypeByte();
			}
		}

		if (TOK) {
			if (type->step == 0) {
				type->step = TypeSize(type->element);
			}
		}

	} else if (NextIs(TOKEN_ADR2)) {
		elmt = NULL;
		if (NextIs(TOKEN_OF)) {
			elmt = ParseSubtype();
		}
		type = TypeAdrOf(elmt);
	}
	return type;
}

Type * ParseType2(InstrOp mode)
/*
Purpose:
	Parse: <int> [".." <int>] | <var> | proc <VarList>
Input:
	mode	Type of variable for which we parse.
*/
{
	
	Var * var;
	Type * type = NULL, * variant_type = NULL;
	long last_n = 0;
	Bool id_required;
	Var * min, * max;

next:
	type = ParseType3();
	if (!TOK) return NULL;

	if (type == NULL) {
		ParseExpressionType(TypeType(NULL));
		if (TOK && TOP != 0) {
			min = BufPop();
			max = NULL;
			if (NextIs(TOKEN_DOTDOT)) {
				ExpectExpression(NULL);
				if (TOK) {
					max = BufPop();
				}
			}

			type = NULL;
			if (max == NULL) {
				var = min;
				if (var->mode == INSTR_TYPE) {
					type = var->type;
				} else if (var->mode == INSTR_VAR && var->type->variant == TYPE_TYPE) {
					type = var->type_value;
				}

				// This is directly type
				if (type != NULL) {
					// For integer type, constants may be defined
					if (type->variant == TYPE_INT) goto const_list;
					goto done;
				}
				max = var;		
			}

			if (VarIsIntConst(min) && VarIsIntConst(max)) {
				type = TypeAllocRange(min, max);

				if (type->range.min > type->range.max) {
					SyntaxError("range minimum bigger than maximum");
				}

			} else {
				SyntaxError("expected constant expression");
			}
		}
	}

const_list:
	// Parse type specific constants
	// There can be list of constants specified in block.
	// First thing in the block must be an identifier, so we try to open the block with this in mind.
	// We try to parse constants only for integer types (in future, we may try other numberic or string types)

	if (type != NULL && type->variant == TYPE_INT) {
		if (TOK != TOKEN_OR) {
			EnterBlockWithStop(TOKEN_VOID);
		
			id_required = false;

			while (TOK != TOKEN_ERROR && !NextIs(TOKEN_BLOCK_END)) {

				while(NextIs(TOKEN_EOL));

				if (TOK == TOKEN_ID || (TOK >= TOKEN_KEYWORD && TOK <= TOKEN_LAST_KEYWORD)) {
					var = VarAllocScope(NO_SCOPE, INSTR_CONST, NAME, 0);
					NextToken();
					if (NextIs(TOKEN_EQUAL)) {
						SyntaxError("Unexpected equal");
					}

					if (/*NextIs(TOKEN_EQUAL) ||*/ NextIs(TOKEN_COLON)) {
						// Parse const expression
						if (TOK == TOKEN_INT) {
							last_n = LEX.n;
							NextToken();
						} else {
							SyntaxError("expected integer value");
						}
					} else {
						last_n++;
					}
					var->n = last_n;
					var->value_nonempty = true;

					if (type->owner != SCOPE) {
						type = TypeDerive(type);
					}

					TypeAddConst(type, var);

				} else {
					if (id_required) {
						SyntaxError("expected constant identifier");
					} else {
						ExitBlock();
						break;
					}
				}
				id_required = false;
				// One code may be ended either by comma or by new line
				if (NextIs(TOKEN_COMMA)) id_required = true;
				NextIs(TOKEN_EOL);
			}
		}
	}
done:
	if (TOK) {
		if (variant_type != NULL) {
			variant_type->right = type;
			type = variant_type;
		}

		if (NextIs(TOKEN_OR)) {
			variant_type = TypeAlloc(TYPE_VARIANT);
			variant_type->left = type;
			goto next;
		}
	}
	return type;
}

Type * ParseType()
{
	PARSE_INLINE = false;
	return ParseType2(INSTR_VAR);
}

Type * ParseTypeInline() 
/*
Syntax: "+" full_type | "(" full_type ")" | normal_type |  identifier | int ".." exp | "-" int ".." exp
*/{
	Type * type = NULL;
	Var * var;
	
	PARSE_INLINE = true;

	if (TOK == TOKEN_OPEN_P || TOK == TOKEN_PLUS) {
		type = ParseType2(INSTR_TYPE);
	} else {
		type = ParseType3();
		if (!TOK) return NULL;
		if (type != NULL) return type;

		if (ParseArg(&var)) {
			type = TypeAlloc(TYPE_VAR);
			type->var = var;
		} else if (TOK == TOKEN_ID) {
			var = ParseVariable();
			if (TOK) {
				if (var->mode == INSTR_TYPE) {
					type = var->type;
				} else if (var->mode == INSTR_VAR && var->type->variant == TYPE_TYPE) {
					type = var->type_value;
				}
			}
		} else if (TOK == TOKEN_INT || TOK == TOKEN_MINUS) {
			type = TypeAlloc(TYPE_INT);
			ParseExpression(NULL);
			var = BufPop();
			if (var->mode == INSTR_CONST) {
				type->range.min = var->n;
			} else {
				SyntaxError("expected constant expression");
			}

			if (NextIs(TOKEN_DOTDOT)) {
				ExpectExpression(NULL);
				if (TOK) {
					var = BufPop();
					if (var->mode == INSTR_CONST) {
						type->range.max = var->n;
					} else {
						SyntaxError("expected constant expression");
					}
				}
			} else {
				type->range.max = type->range.min;
//				type->range.min = 0;
			}
		}
	}
	return type;
}

Type * ParseSubtype()
{
	if (PARSE_INLINE) {
		return ParseTypeInline();
	} else {
		return ParseType();
	}
}


/*
	} else if (TOK == TOKEN_ID) {
		var = ParseVariable();
		if (TOK != TOKEN_ERROR) {
			if (var->type->variant == TYPE_TYPE) {
				type = var->type_value;
			} else {
				if (mode == INSTR_TYPE) {
					type = TypeDerive(var->type);
					// For integer type, constants may be defined
					if (type->variant == TYPE_INT) goto const_list;
				} else {
					if (var->mode != INSTR_TYPE) {
						Print("xxx");
					}
					type = var->type;
				}
			}
		}
//	}	//TODO: We may solve the integer type as default, when there was no other type involved

//	} else if (TOK == TOKEN_INT || TOK == TOKEN_MINUS) {
*/