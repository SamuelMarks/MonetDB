/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 1997 - July 2008 CWI, August 2008 - 2020 MonetDB B.V.
 *
 *
 * Push some Projection structures into others to avoid materialization.
 * Watch out, operations may produce columns with different OID sequences.
 * This optimizer should be ran after the opt_candidates
 */

#include "monetdb_config.h"
#include "opt_pushproject.h"
#include "mal_interpreter.h"	/* for showErrors() */


str
OPTpushprojectImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
        int i, limit, slimit, actions =0;
        InstrPtr p=0, q=0, *old= mb->stmt;
        int *projects=0;
        char buf[256];
        lng usec = GDKusec();
        str msg= MAL_SUCCEED;

        (void) cntxt;
        (void) pci;
        (void) stk;             /* to fool compilers */

        if ( mb->inlineProp )
                return MAL_SUCCEED;

        projects = GDKzalloc(2 * mb->vtop * sizeof(int));
        if (projects == NULL)
                return MAL_SUCCEED;

        limit = mb->stop;
        slimit = mb->ssize;
        if (newMalBlkStmt(mb, mb->ssize) < 0) {
                msg= createException(MAL,"optimizer.deadcode", SQLSTATE(HY013) MAL_MALLOC_FAIL);
                goto wrapup;
        }

        // Consolidate the actual need for variables
        for (i = 0; i < limit; i++) {
                p = old[i];
                if( p == 0)
                        continue; //left behind by others?

                if ( getModuleId(p) == algebraRef && getFunctionId(p) == projectionRef && p->argc ==3 && isVarCList(mb, getArg(p,1)) && isaBatType(getArgType(mb, p, 2))){
			// keep projection , probably restrict to pure projections with candidate argument
			projects[getArg(p,0)] = i;
		}
		if ( getModuleId(p) == algebraRef && getFunctionId(p) == projectionpathRef && projects[getArg(p, p->argc-1)] ){
			q = old[projects[getArg(p, p->argc-1)]];
			getArg(p, p->argc-1) = getArg(q,1);
			p= pushArgument(mb, p, getArg(q,2));
			actions++;
		}
		/*
		 [ 64428,   X_347:bat[:lng] := algebra.projection(C_335:bat[:oid], X_264=:bat[:lng]); ]
			[ 239922,   X_358]:bat[:lng] := batcalc.-(100:lng, X_347=:bat[:lng], nil:BAT);    ]

			Into
			 239922,   X_358:bat[:lng] := batcalc.-(100:lng, X_264:bat[:lng], C_335:bat[:oid]);    ]
		*/
		if ( getModuleId(p) == batcalcRef && p->argc == 3 && projects[getArg(p, p->argc-1)] ){
			// keep it simple for now
			if( getFunctionId(p)[0] == '-' ||
			    getFunctionId(p)[0] == '+' ||
			    getFunctionId(p)[0] == '/' ||
			    getFunctionId(p)[0] == '*' ||
			    getFunctionId(p)[0] == '%' ){
				// Inject the projection arguments
				if( projects[getArg(p, p->argc-1)]){
					q = old[projects[getArg(p, p->argc-1)]];
					getArg(p, p->argc-1) = getArg(q,1);
					p= pushArgument(mb, p, getArg(q,2));
					actions++;
				}
			}
		}
		if ( getModuleId(p) == batcalcRef && p->argc == 4 ){
			// keep it simple for now
			if( getFunctionId(p)[0] == '-' ||
			    getFunctionId(p)[0] == '+' ||
			    getFunctionId(p)[0] == '/' ||
			    getFunctionId(p)[0] == '*' ||
			    getFunctionId(p)[0] == '%' ){
				// Inject the projection arguments
				if( projects[getArg(p, p->retc)]){
					q = old[projects[getArg(p, p->retc)]];
					getArg(p, p->retc) = getArg(q,2);
					getArg(p, p->argc-1) = getArg(q,1);
					actions++;
				}
				if( projects[getArg(p, p->retc + 1)]){
					q = old[projects[getArg(p, p->retc + 1)]];
					getArg(p, p->retc +1 ) = getArg(q,2);
					getArg(p, p->argc-1) = getArg(q,1);
					actions++;
				}
			}
		}
		/* same for the first argument */
		if ( getModuleId(p) == batcalcRef && p->argc == 5 && (projects[getArg(p, p->retc)] || projects[getArg(p, p->retc+1)]) ){
			// keep it simple for now
			if( getFunctionId(p)[0] == '-' ||
			    getFunctionId(p)[0] == '+' ||
			    getFunctionId(p)[0] == '/' ||
			    getFunctionId(p)[0] == '*' ||
			    getFunctionId(p)[0] == '%' ){
				// Inject the projection arguments
				if( projects[getArg(p, p->retc)]){
					q = old[projects[getArg(p, p->retc)]];
					getArg(p, p->retc) = getArg(q,2);
					getArg(p, p->retc+2) = getArg(q,1);
					actions++;
				}
				if( projects[getArg(p, p->retc+1)]){
					q = old[projects[getArg(p, p->retc+1)]];
					getArg(p, p->retc+1) = getArg(q,2);
					getArg(p, p->retc+3) = getArg(q,1);
					actions++;
				}
			}
		}
		/*  [ 33941,   X_357:bat[:str] := algebra.projection(C_333:bat[:oid], X_303bat[:str]); ]
			[ 250871,   (X_414:bat[:oid], C_415=[4]:bat[:oid]) := group.subgroupdone(X_357:bat[:str], X_386:bat[:oid]);  ]
			 
			INTO
			[ 250871,   (X_405:bat[:oid], C_406=[4]:bat[:oid]) := group.subgroupdone(X_303:bat[:str],C_333:bat[:oid],  X_386:bat[:oid]);
			Incorrect. oids change in the projection

		if ( getModuleId(p) == groupRef && getFunctionId(p) == subgroupdoneRef && projects[getArg(p, p->retc)] ){
			q = old[projects[getArg(p, p->retc)]];
			getArg(p,p->retc) = getArg(q,2);
			setArgument(mb, p, p->retc +1, getArg(q,1));
			actions++;
		}
		*/
		/* Raises error during startup  in Mtest
		 * Case Q1.2
			[ 33941,  X_343:bat[:str] := algebra.projection(C_415=[:bat[:oid], X_357=[:bat[:str]);
			[ 19,   "X_417:bat[:str] := algebra.projection(C_394=]:bat[:oid], X_343=:bat[:str]);"   ]

			INTO
			X_420:bat[:str] := algebra.projectionpath(C_394=]:bat[:oid], C_415=[:bat[:oid], X_357=[:bat[:str])
			Incorrect, the OIDs in C_394 are not aligned with X_357. They get reassigned
		
                if ( getModuleId(p) == algebraRef && getFunctionId(p) == projectionRef && projects[getArg(p, p->argc-1)] ){
			// Inject the projection arguments
			q = old[projects[getArg(p, p->argc-1)]];
			getArg(p, p->retc) = getArg(q,1);
			setFunctionId(p, projectionpathRef);
			p= setArgument(mb, p, p->retc+1, getArg(q,2));
			actions++;
		}
		*/
                pushInstruction(mb, p);
        }
	for(; i<slimit; i++)
	if( old[i])
		freeInstruction(old[i]);
	/* Defense line against incorrect plans */
	if( actions > 0){
		msg = chkTypes(cntxt->usermodule, mb, FALSE);
		if (!msg)
			msg = chkFlow(mb);
		if (!msg)
			msg = chkDeclarations(mb);
	}
    /* keep all actions taken as a post block comment */
        usec = GDKusec()- usec;
    snprintf(buf,256,"%-20s actions=%2d time=" LLFMT " usec","pushproject",actions, usec);
    newComment(mb,buf);
    if( actions > 0)
	addtoMalBlkHistory(mb);

wrapup:
        if(old) GDKfree(old);
        if(projects) GDKfree(projects);
        return msg;
}
