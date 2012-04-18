#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "monetdb_config.h"

#include "../../modules/atoms/mtime.h"
#include "sql.h"
#include "dvf.h"
//#include "../../optimizer/opt_mergetable.h"
#include "mal_interpreter.h"

#define NUM_RET_MOUNT 4

int get_column_num(str schema_name, str table_name, str column_name);
int get_column_type(str schema_name, str table_name, int column_num);
// static int OPTmergetableImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p);

int get_column_type(str schema_name, str table_name, int column_num)
{
	if(strcmp(schema_name, "mseed") != 0 || strcmp(table_name, "data") != 0)
		return -1;
	
	switch(column_num)
	{
		case 0:
			return TYPE_str;
		case 1:
			return TYPE_int;
		case 2:
			return TYPE_timestamp;
		case 3:
			return TYPE_int;
		default:
			return -1;
	}
}

int get_column_num(str schema_name, str table_name, str column_name)
{
	if(strcmp(schema_name, "mseed") != 0 || strcmp(table_name, "data") != 0)
		return -1;
	
	if(strcmp(column_name, "file_location") == 0)
		return 0;
	else if(strcmp(column_name, "seq_no") == 0)
		return 1;
	else if(strcmp(column_name, "sample_time") == 0)
		return 2;
	else if(strcmp(column_name, "sample_value") == 0)
		return 3;
	else
		return -1;
}

str plan_modifier(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	str data_table_identifier = "data";
	str mountRef = putName("mount", 5);
	str miniseedRef = putName("miniseed", 8);
// 	str dvfRef = putName("dvf", 3);
// 	str planmodifierRef = putName("plan_modifier", 13);
	
	InstrPtr *old = NULL, *mounts = NULL, q = NULL, r = NULL, s = NULL, o = NULL;
	int i, limit, slimit, actions = 0;
	int num_fl = 0;
	BUN b1 = 0, b2 = 0;
	
	BAT *BAT_fl = NULL; //BAT for file_locations
	
	bit after_first_data_bind = FALSE;
	
	str *schema_name = (str*) getArgReference(stk,pci,1); //arg 1: schema_name
	int bat_fl = *(int*) getArgReference(stk,pci,2); //arg 2: bat of file_locations
	
	BATiter fli;
	
	/* check for logical error: mb must never be NULL */
	assert (mb != NULL);
	
	cntxt = cntxt; //to escape 'unused' parameter error.
	stk = stk; //to escape 'unused' parameter error.
	pci = pci; //to escape 'unused' parameter error.
	
	if ((BAT_fl = BATdescriptor(bat_fl)) == NULL)
		throw(MAL, "dvf.plan_modifier", RUNTIME_OBJECT_MISSING);
	
	/* check tail type */
	if (BAT_fl->ttype != TYPE_str) 
	{
		throw(MAL, "dvf.plan_modifier",
		      "tail-type of input BAT must be TYPE_str");
	}
	
	num_fl = BAT_fl->U->count;
	
	// TODO when num_fl is 0.
	
	/* prepare to keep the potential mount instructions */
	mounts = (InstrPtr*)GDKmalloc(num_fl*sizeof(InstrPtr));
	if(mounts == NULL)
		throw(MAL, "dvf.plan_modifier", MAL_MALLOC_FAIL);
	
	/* save the old stage of the MAL block */
	old = mb->stmt;
	limit= mb->stop;
	slimit = mb->ssize;
	
	/* initialize the statement list. Notice, the symbol table remains intact */
	if (newMalBlkStmt(mb, mb->ssize) < 0)
		return 0;
	
	/* iterate over the instructions of the input MAL program, skip the dvf.plan_modifier itself. */
	for (i = 0; i < limit; i++) 
	{
		InstrPtr p = old[i];
		
		/* check for
		 * v6 := sql.bind(..., schema_name, data_table_name, ..., ...);
		 */
		if(getModuleId(p) == sqlRef && 
			getFunctionId(p) == bindRef &&
			p->argc == 6 &&
			p->retc == 1 &&
			strcmp(getVarConstant(mb, getArg(p, 2)).val.sval, *schema_name) == 0 &&
			strstr(getVarConstant(mb, getArg(p, 3)).val.sval, data_table_identifier) != NULL)
		{
			int which_mount = 0;
			int which_column = 0;
			if(!after_first_data_bind)
			{
				int which_fl = 0;
				after_first_data_bind = TRUE;
				// push mount instructions
				/* create BAT iterator */
				fli = bat_iterator(BAT_fl);
				
				/* advice on sequential scan */
				BATaccessBegin(BAT_fl, USE_TAIL, MMAP_SEQUENTIAL);
				
				BATloop(BAT_fl, b1, b2) 
				{
					int a = 0, type;
					/* get tail value */
					str t = (str) BUNtail(fli, b1);
					
					//create mount instruction
					q = newInstruction(mb, ASSIGNsymbol);
					//q->argc = NUM_RET_MOUNT + 1;
					//q->retc = NUM_RET_MOUNT;
					setModuleId(q, miniseedRef);
					setFunctionId(q, mountRef);
					for(; a < NUM_RET_MOUNT; a++)
					{
						type = get_column_type(*schema_name, getVarConstant(mb, getArg(p, 3)).val.sval, a);
						if(type < 0)
							throw(MAL, "dvf.get_column_num", "is not defined yet for schema: %s and table: %s and column: %s.", *schema_name, getVarConstant(mb, getArg(p, 3)).val.sval, getVarConstant(mb, getArg(p, 4)).val.sval);
						q = pushReturn(mb, q, newTmpVariable(mb, newBatType(TYPE_oid, type)));
					}
					
					q = pushStr(mb, q, t);
					
					VALcopy(&stk->stk[q->argv[NUM_RET_MOUNT]], &getVarConstant(mb, getArg(q, NUM_RET_MOUNT)));
					
					mounts[which_fl] = q;
					which_fl++;
					// push the new instruction
					pushInstruction(mb, q);
					actions++;
				}
				
				BATaccessEnd(BAT_fl, USE_TAIL, MMAP_SEQUENTIAL);
				
				/* check for logical error */
				assert(which_fl == num_fl);
				
			}
			
			// new mat.new for the column being binded
			r = newInstruction(mb, ASSIGNsymbol);
			//r->argc = num_fl + 1;
			//r->retc = 1;
			setModuleId(r, matRef);
			setFunctionId(r, newRef);
			r = pushReturn(mb, r, newTmpVariable(mb, TYPE_any)); // push tmp var to pass to markH.
			which_column = get_column_num(*schema_name, getVarConstant(mb, getArg(p, 3)).val.sval, 
						      getVarConstant(mb, getArg(p, 4)).val.sval);
			if(which_column < 0)
				throw(MAL, "dvf.get_column_num", "is not defined yet for schema: %s and table: %s and column: %s.", 
				      *schema_name, getVarConstant(mb, getArg(p, 3)).val.sval, getVarConstant(mb, getArg(p, 4)).val.sval);
			for(; which_mount < num_fl; which_mount++)
			{
				r = pushArgument(mb, r, getArg(mounts[which_mount], which_column));
			}
			
			// push the new instruction
			pushInstruction(mb, r);
			actions++;
			
			// arrange oids of return val of mat.new
			s = newInstruction(mb, ASSIGNsymbol);
			setModuleId(s, algebraRef);
			setFunctionId(s, markHRef);
			s = pushReturn(mb, s, getArg(p, 0)); // push the ret of sql.bind as ret of (mat.new + algebra.markH)
			s = pushArgument(mb, s, getArg(r, 0));
			s = pushOid(mb, s, 0);
			
			// push the new instruction
			pushInstruction(mb, s);
			actions++;
			
		}
		else
		{
			// push instruction
			pushInstruction(mb, old[i]);
			if (p->token == ENDsymbol) break;
		}
	}
	/* We would like to retain everything from the ENDsymbol
	 * up to the end of the plan, because after the ENDsymbol
	 * the remaining optimizer steps are stored.
	 */
	for(i++; i<limit; i++)
		if (old[i])
			pushInstruction(mb, old[i]);
	
	/*
	 * *optimizer.inline();optimizer.remap();optimizer.evaluate();optimizer.costModel();optimizer.coercions();optimizer.emptySet();optimizer.aliases(); optimizer.mergetable();optimizer.deadcode();optimizer.commonTerms();optimizer.groups();optimizer.joinPath();optimizer.reorder();optimizer.deadcode();optimizer.reduce();optimizer.history();optimizer.multiplex();optimizer.accumulators();optimizer.garbageCollector();
	 */
	
	o = newFcnCall(mb, "optimizer", "inline");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "remap");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "evaluate");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "costModel");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "coercions");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "emptySet");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "aliases");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
// 	o = newFcnCall(mb, "optimizer", "mergetable");
// 	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "deadcode");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "commonTerms");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "groups");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "joinPath");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
// 	o = newFcnCall(mb, "optimizer", "reorder");
// 	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "deadcode");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
// 	o = newFcnCall(mb, "optimizer", "reduce");
// 	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "history");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "multiplex");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "accumulators");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	o = newFcnCall(mb, "optimizer", "garbageCollector");
	typeChecker(cntxt->fdout, cntxt->nspace, mb, o, FALSE);
	optimizeMALBlock(cntxt, mb);
	//o = o;
	
// 	chkProgram(cntxt->fdout, cntxt->nspace, mb);
// 	printFunction(cntxt->fdout,mb, 0, LIST_MAL_EXPLAIN);
		
	/* any remaining MAL instruction records are removed */
	for(; i<slimit; i++)
		if (old[i])
			freeInstruction(old[i]);
		
	GDKfree(old);
		
	/* for statistics we print if/how many patches have been made */
	DEBUGoptimizers
	printf("#dvf.plan_modifier: %d actions\n", actions);
	
	chkTypes(cntxt->fdout, cntxt->nspace, mb, FALSE);
	chkFlow(cntxt->fdout, mb);
	chkDeclarations(cntxt->fdout, mb);
	
	chkProgram(cntxt->fdout, cntxt->nspace, mb);
	printFunction(cntxt->fdout,mb, 0, LIST_MAL_EXPLAIN);
	
// 	actions = OPTmergetableImplementation(cntxt, mb, stk, pci);
// 	printf("#mergetable after dvf.plan_modifier: %d actions\n", actions);
	
// 	malGarbageCollector(mb);
	
	return MAL_SUCCEED;
}

